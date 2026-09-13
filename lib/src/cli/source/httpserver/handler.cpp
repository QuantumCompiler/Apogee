#include "httpserver/handler.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>
#include <utility>

#include "agentloop/content.h"
#include "agentloop/loop.h"
#include "agentloop/rag.h"
#include "agentloop/retriever.h"
#include "harness/errors.h"
#include "harness/roles.h"
#include "httpserver/sse_reporter.h"
#include "httpserver/sse_writer.h"
#include "logger/operational.h"

namespace apogee::httpserver {
namespace {

/// The sentinel a client sends as `session_id` to have one minted. A session
/// is asked for, never implied -- a request without one is stateless, which is
/// exactly what a stock OpenAI client expects.
constexpr std::string_view kNewSession = "new";
constexpr std::string_view kSessionHeader = "X-Apogee-Session-Id";
constexpr std::string_view kToolsHeader = "X-Apogee-Tools-Used";
constexpr std::string_view kBackendError = "backend_error";
constexpr int kUnprocessable = 400;
constexpr int kNotFound = 404;
constexpr int kUnavailable = 503;
constexpr int kBadGateway = 502;

/// A problem with the request, answered in the OpenAI error shape.
struct HttpError {
    int status = kUnprocessable;
    std::string message;
    std::string type{kInvalidRequestError};
    /// Extra fields beside `message` and `type` -- the session id on a 404.
    nlohmann::json extra = nlohmann::json::object();
};

[[nodiscard]] nlohmann::json error_json(const HttpError& error) {
    nlohmann::json body = error_body(error.message, error.type);
    for (const auto& [key, value] : error.extra.items()) {
        body["error"][key] = value;
    }
    return body;
}

[[nodiscard]] HttpResponse to_response(const HttpError& error) {
    return json_response(error.status, error_json(error));
}

[[nodiscard]] std::int64_t now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

[[nodiscard]] std::string fresh_id(std::string_view prefix) {
    static thread_local std::mt19937_64 engine{std::random_device{}()};
    std::ostringstream out;
    out << prefix << std::hex << std::setw(16) << std::setfill('0') << engine();
    return out.str();
}

[[nodiscard]] nlohmann::json parse_object(const std::string& body) {
    const nlohmann::json parsed = nlohmann::json::parse(body, nullptr, false);
    if (parsed.is_discarded()) {
        throw HttpError{.message = "the request body is not valid JSON"};
    }
    if (!parsed.is_object()) {
        throw HttpError{.message = "the request body must be a JSON object"};
    }
    return parsed;
}

[[nodiscard]] std::string optional_string(const nlohmann::json& in, const char* key) {
    const auto it = in.find(key);
    if (it == in.end() || it->is_null()) {
        return {};
    }
    if (!it->is_string()) {
        throw HttpError{.message = std::string{key} + " must be a string"};
    }
    return it->get<std::string>();
}

[[nodiscard]] bool optional_bool(const nlohmann::json& in, const char* key) {
    const auto it = in.find(key);
    if (it == in.end() || it->is_null()) {
        return false;
    }
    if (!it->is_boolean()) {
        throw HttpError{.message = std::string{key} + " must be true or false"};
    }
    return it->get<bool>();
}

[[nodiscard]] std::optional<double> optional_number(const nlohmann::json& in, const char* key) {
    const auto it = in.find(key);
    if (it == in.end() || it->is_null()) {
        return std::nullopt;
    }
    if (!it->is_number()) {
        throw HttpError{.message = std::string{key} + " must be a number"};
    }
    return it->get<double>();
}

[[nodiscard]] std::optional<std::int64_t> optional_integer(const nlohmann::json& in,
                                                           const char* key) {
    const auto it = in.find(key);
    if (it == in.end() || it->is_null()) {
        return std::nullopt;
    }
    if (!it->is_number_integer()) {
        throw HttpError{.message = std::string{key} + " must be an integer"};
    }
    return it->get<std::int64_t>();
}

[[nodiscard]] std::string join(const std::vector<std::string>& items, std::string_view separator) {
    std::string out;
    for (const std::string& item : items) {
        out += out.empty() ? "" : std::string{separator};
        out += item;
    }
    return out;
}

[[nodiscard]] std::string last_user_text(const std::vector<harness::ChatMessage>& messages) {
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
        if (it->role == harness::Role::User) {
            return it->content.plain_text();
        }
    }
    return {};
}

/// The `system` shorthand: prepended as a system message, or merged in front
/// of an existing leading one. Equivalent to sending it as `messages[0]`.
void apply_system_shorthand(std::vector<harness::ChatMessage>& messages,
                            const std::string& system) {
    if (system.empty()) {
        return;
    }
    if (!messages.empty() && messages.front().role == harness::Role::System) {
        messages.front().content =
            harness::MessageContent{system + "\n\n" + messages.front().content.plain_text()};
        return;
    }
    messages.insert(messages.begin(), harness::ChatMessage::system(system));
}

[[nodiscard]] nlohmann::json usage_json(const harness::Usage& usage) {
    return nlohmann::json{{"prompt_tokens", usage.prompt_tokens},
                          {"completion_tokens", usage.completion_tokens},
                          {"total_tokens", usage.total_tokens()}};
}

[[nodiscard]] harness::StatusEvent simple_event(harness::StatusEvent::Type type,
                                                harness::StatusEvent::Phase phase,
                                                std::string name = {}) {
    harness::StatusEvent event;
    event.type = type;
    event.phase = phase;
    event.name = std::move(name);
    return event;
}

}  // namespace

std::optional<ToolMode> tool_mode_from_string(std::string_view name) noexcept {
    if (name.empty() || name == "all") {
        return ToolMode::All;
    }
    if (name == "none") {
        return ToolMode::None;
    }
    return std::nullopt;
}

std::string_view openai_finish_reason(harness::FinishReason reason) noexcept {
    switch (reason) {
        case harness::FinishReason::Length:
            return "length";
        case harness::FinishReason::ContentFilter:
            return "content_filter";
        case harness::FinishReason::Stop:
        case harness::FinishReason::ToolCalls:
        case harness::FinishReason::Cancelled:
        case harness::FinishReason::Other:
            return "stop";
    }
    return "stop";
}

bool is_vendor_cli(harness::BackendType type) noexcept {
    switch (type) {
        case harness::BackendType::ClaudeCli:
        case harness::BackendType::CodexCli:
        case harness::BackendType::GeminiCli:
        case harness::BackendType::OllamaCli:
            return true;
        case harness::BackendType::Anthropic:
        case harness::BackendType::OpenAI:
        case harness::BackendType::Google:
        case harness::BackendType::LlamaCpp:
        case harness::BackendType::Mock:
            return false;
    }
    return false;
}

// ---------------------------------------------------------------------------
// The shapes a chat turn moves through
// ---------------------------------------------------------------------------

/// A chat request, parsed and validated.
struct Handler::ChatCall {
    std::string model;
    std::vector<harness::ChatMessage> messages;
    bool stream = false;
    std::optional<double> temperature;
    std::optional<std::int64_t> max_tokens;
    std::string system;
    ToolMode tool_mode = ToolMode::All;
    bool events = false;
    std::string session_id;
    /// From the query string; empty defers to the server-wide setting.
    std::string retriever;
    std::string rerank;
};

/// Everything one turn needs, decided before any model is called.
struct Handler::TurnPlan {
    std::string backend;
    std::vector<harness::ChatMessage> incoming;
    std::optional<logger::Session> session;
    std::string retriever;
    std::string rerank;
    /// Whether SOMEONE asked for a retriever explicitly -- the query
    /// parameter or the server flag -- so an impossible ask is an error
    /// rather than a fallback.
    bool explicit_retriever = false;
    bool stream = false;
    bool events = false;
    ToolMode tool_mode = ToolMode::All;
    std::optional<double> temperature;
    std::optional<std::int64_t> max_tokens;
    StreamIdentity identity;
};

struct Handler::TurnOutcome {
    std::string answer;
    std::vector<std::string> tools_used;
    harness::Usage usage;
    harness::FinishReason finish_reason = harness::FinishReason::Stop;
    bool hit_iteration_limit = false;
    std::string session_id;
};

Handler::Handler(const harness::Harness& harness, HandlerOptions options,
                 const agent::ToolRegistry* tools)
    : harness_{&harness}, options_{std::move(options)}, tools_{tools} {}

// ---------------------------------------------------------------------------
// Backend resolution
// ---------------------------------------------------------------------------

std::string Handler::resolve_served(std::string_view model) const {
    const harness::Config& config = harness_->config();
    const auto served_suffix = [this]() {
        return options_.served.empty() ? std::string{}
                                       : " (serving: " + join(options_.served, ", ") + ")";
    };

    std::string key;
    if (model.empty()) {
        key = options_.default_backend;
        if (key.empty()) {
            throw HttpError{.message =
                                "no model named and no default configured -- pass "
                                "\"model\", or set models.default" +
                                served_suffix()};
        }
    } else {
        key = commands::configured_backend_key(config, model);
        if (key.empty()) {
            throw HttpError{.message =
                                "no backend named '" + std::string{model} + "'" + served_suffix()};
        }
    }

    if (const harness::BackendConfig* entry = config.find_backend(key);
        entry != nullptr && is_vendor_cli(entry->type)) {
        throw HttpError{.message = "backend '" + key +
                                   "' runs through a vendor CLI on a personal subscription "
                                   "and is not served over HTTP; serve dispatches to "
                                   "API-billing and local backends only" +
                                   served_suffix()};
    }

    const bool served = std::ranges::find(options_.served, key) != options_.served.end();
    if (!served) {
        if (const auto reason = options_.unavailable.find(key);
            reason != options_.unavailable.end()) {
            throw HttpError{.status = kUnavailable,
                            .message = "backend '" + key + "' is configured but unavailable -- " +
                                       reason->second,
                            .type = std::string{kBackendUnavailable}};
        }
        throw HttpError{.message = "backend '" + key + "' is not served" + served_suffix()};
    }
    return key;
}

// ---------------------------------------------------------------------------
// POST /v1/chat/completions
// ---------------------------------------------------------------------------

namespace {

[[nodiscard]] std::vector<harness::ChatMessage> parse_messages(const nlohmann::json& body) {
    const auto messages = body.find("messages");
    if (messages == body.end() || !messages->is_array() || messages->empty()) {
        throw HttpError{.message = "messages is required and must be a non-empty array"};
    }
    try {
        return messages->get<std::vector<harness::ChatMessage>>();
    } catch (const harness::InvalidRequestError& e) {
        throw HttpError{.message = e.what()};
    } catch (const nlohmann::json::exception& e) {
        throw HttpError{.message = std::string{"messages: "} + e.what()};
    }
}

}  // namespace

Handler::TurnPlan Handler::plan_turn(const ChatCall& call) {
    TurnPlan plan;
    plan.backend = resolve_served(call.model);
    plan.stream = call.stream;
    plan.events = call.events;
    plan.tool_mode = call.tool_mode;

    const harness::Config& config = harness_->config();
    plan.temperature = call.temperature.has_value()
                           ? call.temperature
                           : commands::resolve_temperature(std::nullopt, config, plan.backend);
    plan.max_tokens = call.max_tokens.has_value()
                          ? call.max_tokens
                          : commands::resolve_max_tokens(std::nullopt, config, plan.backend);

    // The query parameter beats the server flag; both beat the collection's
    // pin -- inside the one resolver, which is handed the raw spelling.
    plan.retriever = call.retriever.empty() ? options_.retriever : call.retriever;
    plan.rerank = call.rerank.empty() ? options_.rerank : call.rerank;
    plan.explicit_retriever = !plan.retriever.empty();

    if (call.session_id == kNewSession) {
        logger::InferenceParams params;
        params.temperature = plan.temperature;
        params.max_tokens = plan.max_tokens;
        params.system_prompt = call.system;
        plan.session = sessions_.get(sessions_.create(plan.backend, params));
    } else if (!call.session_id.empty()) {
        plan.session = sessions_.get(call.session_id);
        if (!plan.session.has_value()) {
            throw HttpError{.status = kNotFound,
                            .message = "session not found",
                            .type = std::string{kSessionNotFound},
                            .extra = nlohmann::json{{"session_id", call.session_id}}};
        }
    }

    plan.incoming = call.messages;
    // A session created with a system prompt already carries it; the
    // shorthand applies to the incoming messages of a stateless request, and
    // to a continued session's new messages only when it is given again.
    if (!plan.session.has_value() || call.session_id != kNewSession) {
        apply_system_shorthand(plan.incoming, call.system);
    }

    plan.identity.id = fresh_id("chatcmpl-");
    plan.identity.model = plan.backend;
    plan.identity.created = now_seconds();
    return plan;
}

Handler::TurnOutcome Handler::run_turn(TurnPlan& plan, agentloop::Reporter& reporter,
                                       SseReporter* sse,
                                       const harness::CancellationToken& cancellation) {
    // One turn at a time, from the first model call to the persisted result.
    const std::lock_guard<std::mutex> lock{turn_mutex_};
    const auto started = std::chrono::steady_clock::now();
    const harness::Config& config = harness_->config();

    TurnOutcome outcome;
    std::vector<harness::ChatMessage> history =
        plan.session.has_value() ? plan.session->messages : std::vector<harness::ChatMessage>{};

    // Context is measured against what is ABOUT TO BE SENT -- the history plus
    // this turn -- the same rule the chat surface earned the hard way.
    std::vector<harness::ChatMessage> prospective = history;
    prospective.insert(prospective.end(), plan.incoming.begin(), plan.incoming.end());
    const agentloop::ContextUsage usage =
        agentloop::measure_context(*harness_, prospective, plan.backend);

    if (usage.should_warn() && sse != nullptr) {
        harness::StatusEvent warning = simple_event(harness::StatusEvent::Type::ContextWarning,
                                                    harness::StatusEvent::Phase::Start,
                                                    usage.should_compact() ? "compact" : "warn");
        warning.used_tokens = usage.used_tokens;
        warning.context_size = usage.window;
        warning.usage_percent = usage.fraction();
        if (!usage.exact) {
            warning.detail = "estimated";
        }
        sse->emit_meta(warning);
    }
    if (plan.session.has_value() && usage.should_compact() && !history.empty()) {
        // Compacts the PRIOR history only: folding the message the client
        // just sent into a summary of the conversation so far would summarise
        // away the question being asked. A stateless request owns its own
        // messages and is never compacted -- only warned.
        history = agentloop::compact_history(*harness_, history, plan.backend, cancellation);
        ++plan.session->compactions;
    }

    for (const harness::ChatMessage& message : plan.incoming) {
        history.push_back(message);
    }
    const std::size_t base = history.size();

    agentloop::Options loop_options;
    loop_options.model = plan.backend;
    loop_options.temperature = plan.temperature;
    loop_options.max_tokens = plan.max_tokens;
    loop_options.stream_answer = plan.stream;
    loop_options.cancellation = cancellation;
    if (tools_ != nullptr && plan.tool_mode == ToolMode::All && !tools_->empty()) {
        loop_options.tools = tools_;
        // No AskFn, ever: nobody is attached to a served request, so the
        // loop's rule leaves ask_user out of the request entirely. And no
        // confirm function, so a destructive tool that asks resolves to deny.
    }

    if (!options_.rag_collection.empty()) {
        if (sse != nullptr) {
            sse->emit_meta(simple_event(harness::StatusEvent::Type::RagSearch,
                                        harness::StatusEvent::Phase::Start,
                                        options_.rag_collection));
        }
        const agentloop::RagResult rag = commands::retrieve_for_collection(
            *harness_, config, options_.rag_collection, last_user_text(plan.incoming),
            options_.rag_limit, plan.retriever, plan.rerank, cancellation);
        if (sse != nullptr) {
            sse->emit_meta(simple_event(harness::StatusEvent::Type::RagSearch,
                                        harness::StatusEvent::Phase::Done,
                                        options_.rag_collection));
        }
        // Explicitly asked for and impossible -- `?retriever=vector` with no
        // vectors -- is the client's request failing, not a fallback.
        if (!rag.error.empty() && plan.explicit_retriever) {
            throw HttpError{.message = rag.error};
        }
        if (rag.error.empty() && rag.chunks > 0) {
            // Spliced into the OUTGOING request only; never into the history
            // the session persists.
            loop_options.transient_prefix = rag.prefix;
        }
        if (sse != nullptr) {
            harness::StatusEvent result =
                simple_event(harness::StatusEvent::Type::RagResult,
                             harness::StatusEvent::Phase::Done, options_.rag_collection);
            harness::RAGMeta meta;
            meta.db = options_.rag_collection;
            meta.chunks_found = static_cast<int>(rag.chunks);
            meta.top_score = rag.top_score;
            meta.retriever = rag.retriever;
            meta.reranked = rag.reranked;
            result.rag = meta;
            result.detail = rag.error.empty() ? join(rag.notes, "; ") : rag.error;
            sse->emit_meta(result);
        }
    }

    const agentloop::RunResult result = agentloop::run(*harness_, history, loop_options, reporter);
    outcome.answer = result.answer;
    outcome.usage = result.usage;
    outcome.finish_reason = result.finish_reason;
    outcome.hit_iteration_limit = result.hit_iteration_limit;
    for (auto it = history.begin() + static_cast<std::ptrdiff_t>(base); it != history.end(); ++it) {
        for (const harness::ToolCall& call : it->tool_calls) {
            outcome.tools_used.push_back(call.name);
        }
    }

    if (plan.session.has_value()) {
        // The history the loop extended is the transcript: transient context
        // never landed in it, and thinking never reached it. What the client
        // received is what is saved.
        plan.session->messages = history;
        ++plan.session->turns;
        sessions_.commit(*plan.session);
        outcome.session_id = plan.session->chat_id;
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    logger::log(logger::Level::Info, "serve",
                "chat " + plan.backend +
                    (outcome.session_id.empty() ? "" : " session " + outcome.session_id) +
                    (outcome.tools_used.empty() ? "" : " tools " + join(outcome.tools_used, ",")) +
                    " " + std::to_string(elapsed.count()) + "ms");
    return outcome;
}

void Handler::stream_turn(TurnPlan& plan, const WriteFn& write) {
    const harness::CancellationToken cancellation = harness::CancellationToken::create();
    SseWriter writer{write, cancellation};

    const std::optional<harness::StatusEvent> status = harness_->model_status(plan.backend);
    const bool loading =
        status.has_value() && status->type == harness::StatusEvent::Type::ModelLoading;
    SseReporter reporter{
        writer, plan.identity,
        SseReporter::Options{.emit_events = plan.events, .model_loading_pending = loading}};
    if (loading) {
        reporter.emit_meta(simple_event(harness::StatusEvent::Type::ModelLoading,
                                        harness::StatusEvent::Phase::Start, plan.backend));
    }

    // The first chunk names the role, as the vendors' own streams do.
    (void)writer.send(chat_chunk(
        plan.identity, nlohmann::json{{"role", "assistant"}, {"content", ""}}, std::nullopt));

    try {
        const TurnOutcome outcome = run_turn(plan, reporter, &reporter, cancellation);

        if (plan.events) {
            harness::StatusEvent count = simple_event(harness::StatusEvent::Type::TokenCount,
                                                      harness::StatusEvent::Phase::Done);
            const std::int64_t tokens = outcome.usage.reported()
                                            ? outcome.usage.completion_tokens
                                            : agentloop::estimate_tokens(outcome.answer).tokens;
            count.tokens = tokens;
            if (!outcome.usage.reported()) {
                count.detail = "estimated";
            }
            const double seconds = std::chrono::duration<double>(reporter.answer_elapsed()).count();
            if (seconds > 0.0) {
                count.tokens_per_second = static_cast<double>(tokens) / seconds;
            }
            reporter.emit_meta(count);
        }

        nlohmann::json last = chat_chunk(plan.identity, nlohmann::json::object(),
                                         std::string{openai_finish_reason(outcome.finish_reason)});
        if (!outcome.session_id.empty()) {
            last["session_id"] = outcome.session_id;
        }
        if (!outcome.tools_used.empty()) {
            last["apogee_tool_calls"] = outcome.tools_used;
        }
        if (outcome.usage.reported()) {
            last["usage"] = usage_json(outcome.usage);
        }
        (void)writer.send(last);
    } catch (const HttpError& e) {
        (void)writer.send(error_json(e));
    } catch (const harness::CancelledError&) {
        // The client went away mid-turn. There is nobody to tell, but the
        // operator may want to know the slot was spent on nothing.
        logger::log(logger::Level::Info, "serve", "client disconnected mid-turn; turn cancelled");
    } catch (const harness::NoAvailableBackendError& e) {
        (void)writer.send(error_body(e.what(), kInvalidRequestError));
    } catch (const harness::HarnessError& e) {
        logger::log(logger::Level::Error, "serve", e.what());
        (void)writer.send(error_body(e.what(), kBackendError));
    } catch (const std::exception& e) {
        logger::log(logger::Level::Error, "serve", e.what());
        (void)writer.send(error_body(e.what(), kServerError));
    }
    writer.done();
}

HttpResponse Handler::chat_completions(const HttpRequest& request) {
    try {
        const nlohmann::json body = parse_object(request.body);

        ChatCall call;
        call.model = optional_string(body, "model");
        call.messages = parse_messages(body);
        if (const auto tools = body.find("tools");
            tools != body.end() && !tools->is_null() && !(tools->is_array() && tools->empty())) {
            throw HttpError{.message =
                                "client-side tools are not supported: the server runs its own tool "
                                "loop and returns only the final answer (tool_mode selects it)"};
        }
        call.stream = optional_bool(body, "stream");
        call.temperature = optional_number(body, "temperature");
        call.max_tokens = optional_integer(body, "max_tokens");
        call.system = optional_string(body, "system");
        const std::string mode = optional_string(body, "tool_mode");
        const std::optional<ToolMode> parsed_mode = tool_mode_from_string(mode);
        if (!parsed_mode.has_value()) {
            throw HttpError{.message =
                                "tool_mode: unknown value '" + mode + "' (accepted: all, none)"};
        }
        call.tool_mode = *parsed_mode;
        call.events = optional_bool(body, "apogee_events");
        call.session_id = optional_string(body, "session_id");

        call.retriever = request.query_value("retriever");
        if (!call.retriever.empty() && !agentloop::valid_retriever(call.retriever)) {
            throw HttpError{.message =
                                agentloop::retriever_values_message("retriever", call.retriever)};
        }
        call.rerank = request.query_value("rerank");

        TurnPlan plan = plan_turn(call);

        if (!plan.stream) {
            agentloop::NullReporter quiet;
            TurnOutcome outcome;
            try {
                outcome = run_turn(plan, quiet, nullptr, {});
            } catch (const harness::NoAvailableBackendError& e) {
                throw HttpError{.message = e.what()};
            } catch (const harness::HarnessError& e) {
                logger::log(logger::Level::Error, "serve", e.what());
                throw HttpError{
                    .status = kBadGateway, .message = e.what(), .type = std::string{kBackendError}};
            }

            nlohmann::json message{{"role", "assistant"}, {"content", outcome.answer}};
            nlohmann::json choice{
                {"index", 0},
                {"message", std::move(message)},
                {"finish_reason", std::string{openai_finish_reason(outcome.finish_reason)}}};
            nlohmann::json response{{"id", plan.identity.id},
                                    {"object", "chat.completion"},
                                    {"created", plan.identity.created},
                                    {"model", plan.identity.model},
                                    {"choices", nlohmann::json::array({std::move(choice)})}};
            if (outcome.usage.reported()) {
                // Absent when the provider reported nothing. A zero would be
                // a measurement nobody made.
                response["usage"] = usage_json(outcome.usage);
            }
            if (!outcome.session_id.empty()) {
                response["session_id"] = outcome.session_id;
            }
            if (!outcome.tools_used.empty()) {
                response["apogee_tool_calls"] = outcome.tools_used;
            }

            HttpResponse out = json_response(200, response);
            if (!outcome.session_id.empty()) {
                out.headers[std::string{kSessionHeader}] = outcome.session_id;
            }
            if (!outcome.tools_used.empty()) {
                out.headers[std::string{kToolsHeader}] = join(outcome.tools_used, ",");
            }
            return out;
        }

        HttpResponse out;
        out.status = 200;
        out.content_type = "text/event-stream";
        out.headers["Cache-Control"] = "no-cache";
        if (plan.session.has_value()) {
            // Known before the first byte, so it can ride a header. It rides
            // the final chunk too, for a client that only reads the body.
            out.headers[std::string{kSessionHeader}] = plan.session->chat_id;
        }
        out.stream = [this, plan](const WriteFn& write) mutable { stream_turn(plan, write); };
        return out;
    } catch (const HttpError& e) {
        return to_response(e);
    }
}

// ---------------------------------------------------------------------------
// POST /v1/completions
// ---------------------------------------------------------------------------

HttpResponse Handler::completions(const HttpRequest& request) {
    try {
        const nlohmann::json body = parse_object(request.body);
        const auto prompt_field = body.find("prompt");
        if (prompt_field == body.end() || prompt_field->is_null()) {
            throw HttpError{.message = "prompt is required"};
        }
        if (!prompt_field->is_string()) {
            throw HttpError{.message = "prompt must be a single string"};
        }
        const std::string prompt = prompt_field->get<std::string>();
        if (prompt.empty()) {
            throw HttpError{.message = "prompt is required"};
        }
        const std::string backend = resolve_served(optional_string(body, "model"));
        const bool stream = optional_bool(body, "stream");

        harness::ChatRequest chat_request;
        chat_request.model = backend;
        chat_request.messages = {harness::ChatMessage::user(prompt)};
        const harness::Config& config = harness_->config();
        const std::optional<double> temperature = optional_number(body, "temperature");
        chat_request.temperature =
            temperature.has_value() ? temperature
                                    : commands::resolve_temperature(std::nullopt, config, backend);
        const std::optional<std::int64_t> max_tokens = optional_integer(body, "max_tokens");
        chat_request.max_tokens = max_tokens.has_value()
                                      ? max_tokens
                                      : commands::resolve_max_tokens(std::nullopt, config, backend);

        StreamIdentity identity;
        identity.id = fresh_id("cmpl-");
        identity.model = backend;
        identity.created = now_seconds();

        if (!stream) {
            harness::ChatResponse response;
            try {
                const std::lock_guard<std::mutex> lock{turn_mutex_};
                response = harness_->chat(chat_request, {});
            } catch (const harness::NoAvailableBackendError& e) {
                throw HttpError{.message = e.what()};
            } catch (const harness::HarnessError& e) {
                throw HttpError{
                    .status = kBadGateway, .message = e.what(), .type = std::string{kBackendError}};
            }
            nlohmann::json choice{
                {"index", 0},
                {"text", response.message.content.plain_text()},
                {"finish_reason", std::string{openai_finish_reason(response.finish_reason)}}};
            nlohmann::json out{{"id", identity.id},
                               {"object", "text_completion"},
                               {"created", identity.created},
                               {"model", identity.model},
                               {"choices", nlohmann::json::array({std::move(choice)})}};
            if (response.usage.reported()) {
                out["usage"] = usage_json(response.usage);
            }
            return json_response(200, out);
        }

        HttpResponse out;
        out.status = 200;
        out.content_type = "text/event-stream";
        out.headers["Cache-Control"] = "no-cache";
        out.stream = [this, chat_request, identity](const WriteFn& write) {
            const harness::CancellationToken cancellation = harness::CancellationToken::create();
            SseWriter writer{write, cancellation};
            try {
                const std::lock_guard<std::mutex> lock{turn_mutex_};
                harness::StreamOptions stream_options;
                stream_options.cancellation = cancellation;
                stream_options.on_token = [&](std::string_view chunk) {
                    // Empty pieces are skipped, never treated as the end: the
                    // stream ends when the provider returns.
                    if (!chunk.empty()) {
                        (void)writer.send(completion_chunk(identity, chunk, std::nullopt));
                    }
                };
                const harness::ChatResponse response =
                    harness_->stream_chat(chat_request, stream_options);
                (void)writer.send(completion_chunk(
                    identity, "", std::string{openai_finish_reason(response.finish_reason)}));
            } catch (const harness::CancelledError&) {
                logger::log(logger::Level::Info, "serve",
                            "client disconnected mid-completion; turn cancelled");
            } catch (const harness::NoAvailableBackendError& e) {
                (void)writer.send(error_body(e.what(), kInvalidRequestError));
            } catch (const harness::HarnessError& e) {
                (void)writer.send(error_body(e.what(), kBackendError));
            } catch (const std::exception& e) {
                (void)writer.send(error_body(e.what(), kServerError));
            }
            writer.done();
        };
        return out;
    } catch (const HttpError& e) {
        return to_response(e);
    }
}

// ---------------------------------------------------------------------------
// GET /v1/models, /v1/model/status, /health
// ---------------------------------------------------------------------------

HttpResponse Handler::list_models(const HttpRequest& /*request*/) {
    nlohmann::json data = nlohmann::json::array();
    for (const harness::ModelInfo& info : harness_->list_all_models()) {
        if (std::ranges::find(options_.served, info.backend) == options_.served.end()) {
            continue;
        }
        data.push_back(nlohmann::json{{"id", info.id},
                                      {"object", "model"},
                                      {"created", 0},
                                      {"owned_by", info.provider},
                                      {"apogee_backend", info.backend},
                                      // An extension: which model answers when
                                      // a request names none.
                                      {"default", info.backend == options_.default_backend}});
    }
    return json_response(200, nlohmann::json{{"object", "list"}, {"data", std::move(data)}});
}

HttpResponse Handler::model_status(const HttpRequest& request) {
    if (request.has_query("backend")) {
        const std::string name = request.query_value("backend");
        const std::optional<harness::StatusEvent> status = harness_->model_status(name);
        if (!status.has_value()) {
            return error_response(
                kNotFound, "backend '" + name + "' is not served or does not report a load state",
                kNotFoundError);
        }
        nlohmann::json out = status_event_json(*status);
        out["backend"] = name;
        return json_response(200, out);
    }
    nlohmann::json backends = nlohmann::json::object();
    for (const std::string& name : options_.served) {
        if (const std::optional<harness::StatusEvent> status = harness_->model_status(name)) {
            backends[name] = status_event_json(*status);
        }
    }
    return json_response(200, nlohmann::json{{"backends", std::move(backends)}});
}

HttpResponse Handler::health(const HttpRequest& /*request*/) {
    return json_response(200, nlohmann::json{{"status", "ok"}});
}

// ---------------------------------------------------------------------------
// The session plane
// ---------------------------------------------------------------------------

HttpResponse Handler::list_sessions(const HttpRequest& /*request*/) {
    nlohmann::json data = nlohmann::json::array();
    for (const SessionSummary& summary : sessions_.list()) {
        data.push_back(nlohmann::json{{"session_id", summary.id},
                                      {"model", summary.backend},
                                      {"turn_count", summary.turns},
                                      {"last_active", format_utc(summary.last_active)}});
    }
    return json_response(200, nlohmann::json{{"object", "list"}, {"data", std::move(data)}});
}

HttpResponse Handler::get_session(const HttpRequest& /*request*/, std::string_view id) {
    const std::optional<logger::Session> session = sessions_.get(id);
    if (!session.has_value()) {
        const HttpError error{.status = kNotFound,
                              .message = "session not found",
                              .type = std::string{kSessionNotFound},
                              .extra = nlohmann::json{{"session_id", std::string{id}}}};
        return to_response(error);
    }
    return json_response(200, nlohmann::json{{"session_id", session->chat_id},
                                             {"model", session->backend},
                                             {"turn_count", session->turns},
                                             {"compactions", session->compactions},
                                             {"messages", session->messages}});
}

HttpResponse Handler::delete_session(const HttpRequest& /*request*/, std::string_view id) {
    if (!sessions_.erase(id)) {
        const HttpError error{.status = kNotFound,
                              .message = "session not found",
                              .type = std::string{kSessionNotFound},
                              .extra = nlohmann::json{{"session_id", std::string{id}}}};
        return to_response(error);
    }
    HttpResponse out;
    out.status = 204;
    out.content_type.clear();
    return out;
}

}  // namespace apogee::httpserver
