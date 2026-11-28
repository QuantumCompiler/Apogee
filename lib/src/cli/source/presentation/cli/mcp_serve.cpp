#include "cli/mcp_serve.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "agent/tool.h"
#include "agentloop/loop.h"
#include "agentloop/reporter.h"
#include "cli/check.h"
#include "cli/complete.h"
#include "cli/graph.h"
#include "cli/graph_artifacts.h"
#include "cli/helpers.h"
#include "cli/knowledge.h"
#include "cli/models.h"
#include "cli/permissions.h"
#include "cli/suite_residency.h"
#include "cli/train.h"
#include "contracts/config.h"
#include "contracts/errors.h"
#include "contracts/mcp_serve.h"
#include "contracts/paths.h"
#include "machine/json_reporter.h"
#include "operations/training_reads.h"
#include "tools/toolsets.h"
#include "version/version.h"

namespace apogee::commands {
namespace {

constexpr std::array<std::string_view, 11> kStatusReads{
    kModelsListRead,    kModelsStatusRead,  kTrainStatusRead,   kTrainVersionsRead,
    kCheckRead,         kListGraphsRead,    kGraphStatsRead,    kGraphReportRead,
    kKnowledgeListRead, kKnowledgeInfoRead, kKnowledgeQueryRead};

/// The name a host sees in `initialize`, unchanged since the server was
/// hidden: a host keys nothing on it, but nothing gains by moving it.
constexpr std::string_view kServerName = "apogee-tools";

/// An optional string argument: absent or null is empty, anything but a
/// string refused by name.
[[nodiscard]] std::string string_argument(const nlohmann::json& args, std::string_view key) {
    const auto found = args.find(std::string{key});
    if (found == args.end() || found->is_null()) {
        return {};
    }
    if (!found->is_string()) {
        throw std::invalid_argument(std::string{key} + " must be a string");
    }
    return found->get<std::string>();
}

/// An optional boolean argument, false when absent. A host's model may send
/// it as a string as readily as a boolean, so the usual spellings are read,
/// case aside; anything else is refused rather than guessed.
[[nodiscard]] bool flag_argument(const nlohmann::json& args, std::string_view key) {
    const auto found = args.find(std::string{key});
    if (found == args.end() || found->is_null()) {
        return false;
    }
    if (found->is_boolean()) {
        return found->get<bool>();
    }
    if (found->is_string()) {
        std::string text = found->get<std::string>();
        std::ranges::transform(text, text.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (text == "true" || text == "yes" || text == "1") {
            return true;
        }
        if (text == "false" || text == "no" || text == "0") {
            return false;
        }
    }
    throw std::invalid_argument(std::string{key} + " must be true or false");
}

[[nodiscard]] mcp::ToolCallResult answered(const nlohmann::json& document) {
    return mcp::ToolCallResult{document_text(document), false};
}

/// A read served as text (58b's `graph_report`): the command's stdout less
/// the newline that ends it, as a document is.
[[nodiscard]] mcp::ToolCallResult answered_text(std::string text) {
    if (text.ends_with('\n')) {
        text.pop_back();
    }
    return mcp::ToolCallResult{std::move(text), false};
}

/// A string argument the read cannot do without, refused by name with what
/// it is.
[[nodiscard]] std::string required_argument(const nlohmann::json& args, std::string_view key,
                                            std::string_view what) {
    std::string value = string_argument(args, key);
    if (value.empty()) {
        throw std::invalid_argument(std::string{key} + " is required: " + std::string{what});
    }
    return value;
}

[[nodiscard]] mcp::ToolCallResult refused(std::string reason) {
    return mcp::ToolCallResult{"Error: " + std::move(reason), true};
}

/// The config the command reads, at the path it resolves -- a config that
/// will not load throws with the command's own reason.
[[nodiscard]] harness::Config config_of(const RootContext& context,
                                        std::filesystem::path& config_path) {
    config_path = harness::resolve_config_path(context.config_path);
    return harness::load_config(config_path);
}

[[nodiscard]] nlohmann::json object_schema(nlohmann::json properties = nlohmann::json::object()) {
    return nlohmann::json{{"type", "object"}, {"properties", std::move(properties)}};
}

/// An optional whole-number argument: absent or null is none. A host's model
/// may send it as a string as readily as a number, so a string of digits is
/// read; anything else is refused by name rather than guessed. A whole number
/// past a 64-bit integer's range is past every bound, so it is read as the
/// range's end and the caller's bound refuses it in its own words -- range
/// first, since casting a double past that range is undefined.
[[nodiscard]] std::optional<std::int64_t> whole_argument(const nlohmann::json& args,
                                                         std::string_view key) {
    const auto found = args.find(std::string{key});
    if (found == args.end() || found->is_null()) {
        return std::nullopt;
    }
    constexpr std::int64_t most = std::numeric_limits<std::int64_t>::max();
    constexpr std::int64_t least = std::numeric_limits<std::int64_t>::min();
    if (found->is_number_unsigned()) {
        const auto value = found->get<std::uint64_t>();
        return value > static_cast<std::uint64_t>(most) ? most : static_cast<std::int64_t>(value);
    }
    if (found->is_number_integer()) {
        return found->get<std::int64_t>();
    }
    if (found->is_number_float()) {
        const double value = found->get<double>();
        // 2^63: the first double past the range; -2^63 is its least member.
        constexpr double past = 9223372036854775808.0;
        if (std::isfinite(value) && value == std::trunc(value)) {
            if (value >= past) {
                return most;
            }
            if (value < -past) {
                return least;
            }
            return static_cast<std::int64_t>(value);
        }
    }
    if (found->is_string()) {
        const std::string text = found->get<std::string>();
        if (!text.empty() && text.size() <= 18 &&
            std::ranges::all_of(text, [](unsigned char c) { return std::isdigit(c) != 0; })) {
            return std::stoll(text);
        }
    }
    throw std::invalid_argument(std::string{key} + " must be a whole number");
}

/// The backends' names, as a refusal lists them.
[[nodiscard]] std::string names_of(const std::vector<ServedBackend>& backends) {
    std::string out;
    for (const ServedBackend& backend : backends) {
        out += (out.empty() ? "" : ", ") + backend.name;
    }
    return out;
}

/// What a served call's run says, said on the server's stderr: the stream a
/// host captures, never the protocol's. Its notices and progress notes as
/// lines; the answer is the call's result, so it is not said here too.
class Narrator final : public agentloop::Reporter {
public:
    explicit Narrator(std::ostream& out) : out_{out} {}

    void on_notice(std::string_view line) override {
        say(line);
    }

    void on_progress(std::string_view line) override {
        say(line);
    }

    void on_tool_status(std::string_view line) override {
        say(line);
    }

    void say(std::string_view line) {
        out_ << "apogee mcp serve: " << line << "\n";
        out_.flush();
    }

private:
    std::ostream& out_;
};

}  // namespace

std::span<const std::string_view> status_read_names() noexcept {
    return kStatusReads;
}

std::vector<mcp::ServedRead> status_reads(const RootContext& context, ServedProviders providers) {
    std::vector<mcp::ServedRead> reads;

    reads.push_back(mcp::ServedRead{
        .name = std::string{kModelsListRead},
        .description =
            "Apogee's models: every configured backend and every model in its store, each with "
            "what it is and where it stands -- the document `apogee models list --output-format "
            "json` prints, byte for byte. Reads each stored model's header, so a large store "
            "takes a moment.",
        .input_schema = object_schema(
            {{"all",
              {{"type", "boolean"},
               {"description",
                "Also list the SafeTensors sets a conversion has already consumed, as models "
                "list --all does"}}}}),
        .answer =
            [&context](const nlohmann::json& args) {
                const bool all = flag_argument(args, "all");
                std::filesystem::path config_path;
                const harness::Config config = config_of(context, config_path);
                return answered(render_model_document(read_model_rows(config, config_path), all));
            },
    });

    reads.push_back(mcp::ServedRead{
        .name = std::string{kModelsStatusRead},
        .description =
            "Which backend each of Apogee's roles resolves to -- chat, the helpers, embedding -- "
            "and whether it is configured; under a suite, the suite's footprint too. The "
            "document `apogee models status --output-format json` prints, byte for byte.",
        .input_schema = object_schema(
            {{"suite",
              {{"type", "string"},
               {"description",
                "Resolve under this suite instead of models.default_suite, or off for none, as "
                "models status --suite does"}}}}),
        .answer =
            [&context](const nlohmann::json& args) {
                const std::string suite = string_argument(args, "suite");
                std::filesystem::path config_path;
                harness::Config config = config_of(context, config_path);
                if (!suite.empty()) {
                    if (const std::string refusal = select_suite(config, suite); !refusal.empty()) {
                        return refused("suite: " + refusal);
                    }
                }
                return answered(role_status_document(config, {}, machine_budget));
            },
    });

    reads.push_back(mcp::ServedRead{
        .name = std::string{kTrainStatusRead},
        .description =
            "Apogee's training at a glance: the cycle, the active pipeline, the newest runs and "
            "each fine-tuned backend's active version. The document `apogee train status "
            "--output-format json` prints, byte for byte.",
        .input_schema = object_schema(),
        .answer =
            [](const nlohmann::json&) { return answered(operations::training_status_document()); },
    });

    reads.push_back(mcp::ServedRead{
        .name = std::string{kTrainVersionsRead},
        .description =
            "The versions training has promoted, per backend, each with its run, its eval and "
            "which one is active -- every ledger, or one backend's. The document `apogee train "
            "versions --output-format json` prints, byte for byte.",
        .input_schema =
            object_schema({{"backend",
                            {{"type", "string"},
                             {"description", "One backend's ledger; every ledger when omitted"}}}}),
        .answer =
            [](const nlohmann::json& args) {
                std::string refusal;
                const std::optional<nlohmann::json> document =
                    train_versions_document(string_argument(args, "backend"), refusal);
                return document.has_value() ? answered(*document) : refused(refusal);
            },
    });

    reads.push_back(mcp::ServedRead{
        .name = std::string{kCheckRead},
        .description =
            "Apogee's install check: every row `apogee check` reports -- its section, status, "
            "detail and remedy -- and the counts. The document `apogee check --output-format "
            "json` prints, byte for byte. Nothing is repaired: `apogee check --fix` is the "
            "person's to run.",
        .input_schema = object_schema(),
        .answer =
            [&context](const nlohmann::json&) {
                return answered(
                    render_report_document(read_check_report(check_inputs(context)), std::nullopt));
            },
    });

    // ---- the graphs (58b) ------------------------------------------------
    reads.push_back(mcp::ServedRead{
        .name = std::string{kListGraphsRead},
        .description =
            "Apogee's knowledge graphs: every named graph the config names, then each collection "
            "holding a graph of its own, each with its kind (named or collection), what a named "
            "graph is over, whether it is built, its nodes and edges and its last build. The "
            "names graph_query and the other graph tools take as graph. The document `apogee "
            "graph list --output-format json` prints, byte for byte.",
        .input_schema = object_schema(),
        .answer =
            [&context](const nlohmann::json&) {
                try {
                    return answered(graph_list_document(context));
                } catch (const GraphRefusal& e) {
                    return refused(e.what());
                }
            },
    });

    reads.push_back(mcp::ServedRead{
        .name = std::string{kGraphStatsRead},
        .description =
            "One knowledge graph's shape: its nodes by type, its edges and where they came from "
            "(parsed from source, or asserted by a model), mentions, coverage, the code it "
            "parsed, its extractor, what is stale, its communities -- and a named graph's "
            "members. The document `apogee graph stats <graph> --output-format json` prints, "
            "byte for byte. A graph not built yet answers built: false.",
        .input_schema =
            nlohmann::json{
                {"type", "object"},
                {"properties",
                 {{"graph",
                   {{"type", "string"},
                    {"description", "A named graph or a collection, as list_graphs names them"}}}}},
                {"required", nlohmann::json::array({"graph"})}},
        .answer =
            [&context](const nlohmann::json& args) {
                const std::string graph = required_argument(
                    args, "graph", "a named graph or a collection, as list_graphs names them");
                try {
                    return answered(graph_stats_document(context, graph));
                } catch (const GraphRefusal& e) {
                    return refused(e.what());
                }
            },
    });

    reads.push_back(mcp::ServedRead{
        .name = std::string{kGraphReportRead},
        .description =
            "A knowledge graph's architecture summary in Markdown -- its communities, hubs, the "
            "origin mix, cross-collection links, decisions and orphans -- from the store, with "
            "no model call. The Markdown document `apogee graph report` prints, byte for byte.",
        .input_schema = object_schema(
            {{"graph",
              {{"type", "string"},
               {"description",
                "The graph: a named graph or a collection's own graph, as list_graphs names "
                "them. Omit when only one graph is built"}}},
             {"collection",
              {{"type", "string"},
               {"description",
                "Report on the graph covering this collection instead: a named graph listing "
                "it, else its own"}}}}),
        .answer =
            [&context](const nlohmann::json& args) {
                const std::string graph = string_argument(args, "graph");
                const std::string collection = string_argument(args, "collection");
                try {
                    return answered_text(graph_report_markdown(context, graph, collection));
                } catch (const GraphRefusal& e) {
                    return refused(e.what());
                }
            },
    });

    // ---- the knowledge records (58b) ---------------------------------------
    reads.push_back(mcp::ServedRead{
        .name = std::string{kKnowledgeListRead},
        .description =
            "The project's knowledge records -- each decision's why, what was chosen, whether it "
            "shipped -- newest first, kept to a branch or a discipline when asked. The "
            "document `apogee knowledge list --output-format json` prints, byte for byte.",
        .input_schema = object_schema(
            {{"status",
              {{"type", "string"},
               {"description",
                "Keep only this branch -- shipped, rejected or superseded -- as knowledge list "
                "--status does"}}},
             {"discipline",
              {{"type", "string"},
               {"description",
                "Keep only this discipline, as knowledge list --discipline does"}}}}),
        .answer =
            [&context](const nlohmann::json& args) {
                const std::string status = string_argument(args, "status");
                const std::string discipline = string_argument(args, "discipline");
                try {
                    return answered(knowledge_list_document(
                        read_knowledge_records(context, {}, status, discipline)));
                } catch (const KnowledgeRefusal& e) {
                    return refused(e.what());
                }
            },
    });

    reads.push_back(mcp::ServedRead{
        .name = std::string{kKnowledgeInfoRead},
        .description =
            "One knowledge record in full: its intent, decision, status, discipline, link, "
            "provenance and when it was captured. The document `apogee knowledge info <id> "
            "--output-format json` prints, byte for byte; the archived conversation is never "
            "served.",
        .input_schema =
            nlohmann::json{{"type", "object"},
                           {"properties",
                            {{"id",
                              {{"type", "string"},
                               {"description", "The record id, as knowledge_list names them"}}}}},
                           {"required", nlohmann::json::array({"id"})}},
        .answer =
            [&context](const nlohmann::json& args) {
                const std::string id =
                    required_argument(args, "id", "a record id, as knowledge_list names them");
                try {
                    return answered(knowledge_info_document(context, {}, id, false));
                } catch (const KnowledgeRefusal& e) {
                    return refused(e.what());
                }
            },
    });

    reads.push_back(mcp::ServedRead{
        .name = std::string{kKnowledgeQueryRead},
        .description =
            "Find the shipped knowledge records that best answer a question, each with its score "
            "on the retriever that ran, which the answer names, since their scales differ. The "
            "document `apogee knowledge query \"<question>\" --rerank off --output-format json` "
            "prints, byte for byte: a vector search embeds the question through the records' "
            "embedder, one small call that a paid embedder bills; a lexical one makes none, and "
            "no rerank judge is ever asked.",
        .input_schema =
            nlohmann::json{
                {"type", "object"},
                {"properties",
                 {{"question",
                   {{"type", "string"},
                    {"description", "What the records are asked, as knowledge query takes it"}}},
                  {"top_k",
                   {{"type", "integer"},
                    {"minimum", 1},
                    {"description",
                     "How many records to return (default 5), as knowledge query -n"}}}}},
                {"required", nlohmann::json::array({"question"})}},
        .answer =
            [&context, providers = std::move(providers)](const nlohmann::json& args) {
                const std::string question =
                    required_argument(args, "question", "what the records are asked");
                KnowledgeQuery query{.question = question};
                if (const std::optional<std::int64_t> top_k = whole_argument(args, "top_k");
                    top_k.has_value()) {
                    if (*top_k < 1) {
                        throw std::invalid_argument("top_k must be at least 1");
                    }
                    query.top_k = static_cast<int>(
                        std::min<std::int64_t>(*top_k, std::numeric_limits<int>::max()));
                }
                // Nothing spends but the question's embedding: never a judge.
                query.rerank = "off";
                try {
                    return answered(
                        knowledge_query_document(run_knowledge_query(context, query, providers)));
                } catch (const KnowledgeRefusal& e) {
                    return refused(e.what());
                }
            },
    });

    return reads;
}

CallableBackends complete_backends(const harness::Config& config, const MeteredProbe& metered) {
    CallableBackends out;
    for (const std::string& name : config.backend_names()) {
        const harness::BackendConfig& entry = *config.find_backend(name);
        if (std::ranges::find(entry.mcp_serve, harness::kServedComplete) == entry.mcp_serve.end()) {
            continue;
        }
        // The provider's word, never a type list; a null probe tells
        // nothing, and unknown is metered.
        const MeteredAnswer answer =
            metered ? metered(config, name)
                    : MeteredAnswer{.metered = true, .unknown = "nothing here can build it"};
        if (answer.metered || !answer.unknown.empty()) {
            out.left_out.push_back(harness::mcp_serve_ineligible(
                name, {std::string{harness::kServedComplete}}, answer.metered, answer.unknown));
            continue;
        }
        out.backends.push_back(ServedBackend{
            .name = name, .max_tokens = harness::served_max_tokens(entry.max_tokens)});
    }
    return out;
}

std::string complete_description(const std::vector<ServedBackend>& backends) {
    std::string named;
    for (const ServedBackend& backend : backends) {
        named += (named.empty() ? "" : ", ") + backend.name + " (at most " +
                 std::to_string(backend.max_tokens) + " tokens)";
    }
    return "Hand a prompt to one of Apogee's local models and answer with its reply. One call is "
           "the one-shot `apogee complete -m <backend> \"<prompt>\"` runs: the backend's own "
           "system prompt and sampling, no tools, no retrieval. backend may name: " +
           named +
           ". Each runs on this machine and is unmetered by its own provider's word, so a call "
           "spends this machine's time, never money. max_tokens asks for fewer tokens than the "
           "backend's bound, never more, and a reply that reaches its bound ends there. Offered "
           "because Apogee's config lists complete under these backends' mcp_serve.";
}

ServedModels::ServedModels(const harness::Config& config, std::filesystem::path config_path)
    : harness_{config}, config_path_{std::move(config_path)} {}

const backends::BuildResult& ServedModels::ready() {
    if (!built_.has_value()) {
        backends::BuildOptions options;
        options.config_path = config_path_;
        built_ = backends::build_providers(harness_, options);
    }
    return *built_;
}

mcp::ServedCall served_complete(const RootContext& context, ServedModels& models,
                                std::vector<ServedBackend> backends, std::ostream& narration) {
    std::int64_t widest = 0;
    nlohmann::json names = nlohmann::json::array();
    for (const ServedBackend& backend : backends) {
        widest = std::max(widest, backend.max_tokens);
        names.push_back(backend.name);
    }
    mcp::ServedCall call{
        .name = std::string{harness::kServedComplete},
        .description = complete_description(backends),
        .input_schema =
            {{"type", "object"},
             {"properties",
              {{"prompt",
                {{"type", "string"},
                 {"description", "What the model is asked, as apogee complete takes its prompt"}}},
               {"backend",
                {{"type", "string"},
                 {"enum", std::move(names)},
                 {"description", "The backend that answers: one of those this tool names"}}},
               {"max_tokens",
                {{"type", "integer"},
                 {"minimum", 1},
                 {"maximum", widest},
                 {"description",
                  "The most tokens the reply may run to, at most the backend's bound (the "
                  "bound when omitted)"}}}}},
             {"required", nlohmann::json::array({"prompt", "backend"})}},
        .answer = {},
    };
    call.answer = [&context, &models, backends = std::move(backends),
                   &narration](const nlohmann::json& args) -> mcp::ToolCallResult {
        const std::string prompt = string_argument(args, "prompt");
        if (prompt.empty()) {
            return refused("prompt is required: what the model is asked");
        }
        const std::string backend = string_argument(args, "backend");
        const auto served = std::ranges::find(backends, backend, &ServedBackend::name);
        if (served == backends.end()) {
            return refused(backend.empty()
                               ? "backend is required: one of " + names_of(backends)
                               : "'" + backend +
                                     "' is not a backend complete runs on here -- it may name " +
                                     names_of(backends));
        }
        const std::optional<std::int64_t> asked = whole_argument(args, "max_tokens");
        if (asked.has_value() && (*asked < 1 || *asked > served->max_tokens)) {
            return refused("max_tokens must be from 1 to " + std::to_string(served->max_tokens) +
                           ", " + backend + "'s bound");
        }
        const std::int64_t bound = asked.value_or(served->max_tokens);

        // Consent is the config's, read again at the call: a listing taken
        // back since the server started is no longer served.
        try {
            const harness::Config now =
                harness::load_config(harness::resolve_config_path(context.config_path));
            const harness::BackendConfig* entry = now.find_backend(backend);
            if (entry == nullptr || std::ranges::find(entry->mcp_serve, harness::kServedComplete) ==
                                        entry->mcp_serve.end()) {
                return refused("'" + backend +
                               "' no longer lists complete under mcp_serve -- the config "
                               "changed since this server started, and nothing was run");
            }
        } catch (const std::exception& e) {
            return refused(e.what());
        }

        // The providers `apogee complete` builds, built once for the
        // server's life; the run then refuses as the command does.
        const backends::BuildResult& built = models.ready();
        const harness::Harness& harness = models.harness();
        const harness::Config& config = harness.config();
        if (const std::string refusal = named_backend_refusal(config, built, backend);
            !refusal.empty()) {
            return refused(refusal);
        }
        if (const std::string refusal = validate_active_suite(config, built); !refusal.empty()) {
            return refused(refusal);
        }
        // The built provider's own word again: what would run is what was
        // asked about.
        if (harness.generation_is_metered(backend)) {
            return refused(harness::mcp_serve_ineligible(
                backend, {std::string{harness::kServedComplete}}, true, {}));
        }

        // The command's own core, the utility model off: no call but this
        // backend's.
        OneShot shot = one_shot(config, backend, prompt,
                                OneShotSettings{.max_tokens = bound, .utility = false});
        Narrator narrator{narration};
        narrator.say("complete on '" + backend + "', at most " + std::to_string(bound) + " tokens");
        agentloop::RunResult result;
        try {
            result = agentloop::run(harness, shot.messages, shot.options, narrator);
        } catch (const harness::CancelledError&) {
            narrator.say("complete on '" + backend + "' cancelled");
            return refused("cancelled");
        } catch (const harness::HarnessError& e) {
            narrator.say("complete on '" + backend + "' failed: " + e.what());
            return refused(e.what());
        }
        narrator.say("complete on '" + backend + "' answered" +
                     (result.finish_reason == harness::FinishReason::Length
                          ? ", stopped at its bound of " + std::to_string(bound) + " tokens"
                          : std::string{}));
        return mcp::ToolCallResult{result.answer, false};
    };
    return call;
}

int serve_mcp(const RootContext& context, std::istream& in, std::ostream& out, std::ostream& err) {
    // The native toolsets only, never the MCP registry: a config that
    // listed this very command as a server would otherwise spawn itself
    // without end.
    harness::Config config;
    std::filesystem::path config_path;
    try {
        config_path = harness::resolve_config_path(context.config_path);
        config = harness::load_config(config_path);
    } catch (const std::exception&) {
        // No config is fine: the defaults sandbox to the launch folder, and
        // each status read says the config's own reason when asked.
    }
    // One Harness for the server's life (54b), its providers built at the
    // first call that needs them: search_documents' embedder, complete.
    ServedModels models{config, config_path};
    agent::ToolRegistry registry;
    tools::ToolsetOptions options;
    options.config = &config;
    options.harness = &models.harness();
    options.fs_root = harness::expand_env(config.tools.fs_root);
    options.disabled = config.tools.disabled;
    tools::register_native_toolsets(registry, options);
    // search_documents resolves its retriever as chat's tool does -- the
    // collection's embedder asked of the providers (54b) -- so they are
    // built before it searches.
    // A served review fetch reaches only the websites the config lists
    // (57x): nobody is there to ask.
    mcp::Dispatch native = [&models, through = mcp::dispatch_through(
                                         registry, make_permission_checker(config, nullptr))](
                               std::string_view name, const nlohmann::json& args) {
        if (name == "search_documents") {
            (void)models.ready();
        }
        return through(name, args);
    };
    // knowledge_query embeds through the same providers (58b), asked only
    // when its question might need an embedder.
    ServedProviders providers = [&models]() -> const harness::Harness& {
        (void)models.ready();
        return models.harness();
    };
    mcp::ServedSet served = mcp::with_reads(mcp::read_only_tools(registry), std::move(native),
                                            status_reads(context, std::move(providers)));

    // The calls the config lists (54b), each backend asked its provider's
    // word; a listing the editor would have refused is said and left out.
    const CallableBackends callable =
        complete_backends(config, provider_metered_probe(config_path));
    for (const std::string& line : callable.left_out) {
        err << "apogee mcp serve: " << line << "; it is not served\n";
    }
    if (!callable.backends.empty()) {
        std::vector<mcp::ServedCall> calls;
        calls.push_back(served_complete(context, models, callable.backends, err));
        served = mcp::with_calls(std::move(served), std::move(calls));
    }
    err.flush();
    return mcp::serve_stdio(in, out, kServerName, std::string{version::semantic()}, served.tools,
                            served.dispatch);
}

}  // namespace apogee::commands
