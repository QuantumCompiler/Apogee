#include "cli/chat.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <filesystem>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>

#include "agentloop/content.h"
#include "agentloop/loop.h"
#include "agentloop/query_rewrite.h"
#include "agentloop/rag.h"
#include "agentloop/rerank.h"
#include "agentloop/retriever.h"
#include "agentloop/review_context.h"
#include "ansi/ansi.h"
#include "backends/factory.h"
#include "cli/chat_attachments.h"
#include "cli/chat_completer.h"
#include "cli/chat_history.h"
#include "cli/chat_recall.h"
#include "cli/config_suites.h"
#include "cli/embed.h"
#include "cli/helpers.h"
#include "cli/permissions.h"
#include "cli/suite_residency.h"
#include "contracts/config.h"
#include "contracts/errors.h"
#include "contracts/paths.h"
#include "harness/context_windows.h"
#include "harness/roles.h"
#include "knowledge/clerk.h"
#include "knowledge/record.h"
#include "logger/operational.h"
#include "machine/json_reporter.h"
#include "mcp/registry.h"
#include "modelstore/footprint.h"
#include "operations/knowledge_core.h"
#include "operations/suites.h"
#include "platform/platform.h"
#include "tools/git.h"
#include "views/ask_prompt.h"
#include "views/cli_reporter.h"
#include "views/input_gate.h"
#include "views/line_reader.h"
#include "views/terminal.h"

namespace apogee::commands {
namespace {

[[noreturn]] void fail_user(const std::string& message) {
    std::cerr << "apogee chat: " << message << "\n";
    throw CLI::RuntimeError(kUserError);
}

std::string trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' ||
                             text.back() == '\n')) {
        text.remove_suffix(1);
    }
    return std::string{text};
}

struct ChatFlags {
    std::string model;
    std::string system_prompt;
    std::vector<std::string> images;
    /// Files, folders and globs attached from the start (26d).
    std::vector<std::string> attach;
    std::string rag;
    int rag_limit = 4;
    /// Kept so an explicit `--rag ""` can be told from no flag at all.
    CLI::Option* rag_option = nullptr;
    std::string retriever;
    std::string rerank;
    CLI::Option* retriever_option = nullptr;
    CLI::Option* rerank_option = nullptr;
    double temperature = 0.0;
    std::int64_t max_tokens = 0;
    /// Whether the model thinks first, and for how long (26i).
    std::string think;
    /// Recall no earlier chats in this run (26l).
    bool no_recall = false;
    /// The session's answers, given at launch (26o).
    std::vector<std::string> allow;
    std::vector<std::string> deny;
    std::vector<std::string> allow_hosts;
    std::int64_t think_budget = 0;
    bool tools = false;
    bool search = false;
    bool no_color = false;
    bool raw = false;
    OutputFormat output_format = OutputFormat::Text;
    std::optional<InputFormat> input_format;
    bool verbose = false;
    std::string resume;
    bool cont = false;
    /// The suite the chat runs under (27d), or `off`; empty leaves it to what
    /// the chat last had, else the config's default suite.
    std::string suite;
    /// Load the suite's members up front, on the busy line (27e).
    bool warm = false;
    /// Run the suite even when its footprint is over the machine's budget.
    bool force = false;
    /// No busy line while the members load.
    bool quiet = false;
    /// The arbitrary-branch review: the git tools' defaults and a system
    /// note, from flags -- and re-pointed by `/branch` mid-session.
    std::string branch;
    std::string base;
    std::string remote = "origin";
    bool fetch = false;
    bool no_fetch = false;

    CLI::Option* temperature_option = nullptr;
    CLI::Option* max_tokens_option = nullptr;
    CLI::Option* think_budget_option = nullptr;
};

}  // namespace

std::optional<SlashCommand> parse_slash(std::string_view line) {
    if (line.empty() || line.front() != '/') {
        return std::nullopt;
    }
    const std::string_view rest = line.substr(1);
    const std::size_t space = rest.find_first_of(" \t");
    const std::string_view word = space == std::string_view::npos ? rest : rest.substr(0, space);

    // A command word contains no '/'. Otherwise `/usr/bin/env is a path` -- a
    // perfectly reasonable question -- would be swallowed as a command.
    if (word.empty() || word.find('/') != std::string_view::npos) {
        return std::nullopt;
    }

    SlashCommand command;
    command.name = std::string{word};
    if (space != std::string_view::npos) {
        command.argument = trim(rest.substr(space + 1));
    }
    return command;
}

namespace {

/// Everything one chat turn does, for every surface that drives one.
///
/// Extracted when machine mode landed. The terminal REPL and a JSONL-driven
/// child differ entirely in how they READ input -- a line editor with slash
/// commands versus one JSON object per line -- and not at all in what a turn
/// *is*: measure the context, compact or warn, append, run the loop, persist.
/// (Titling is shared the same way, as `BackgroundTitle` below, but it outlives
/// a turn: it runs while the next one is being typed.)
///
/// Duplicating that for the driver would have been the parity failure the
/// Reporter seam exists to prevent, one level up: context monitoring or the
/// per-turn save would have reached one surface and not the other, and nobody
/// would notice until a GUI user lost a conversation.
///
/// `notice` is the one genuinely surface-specific part. The terminal prints
/// warnings to its status line; machine mode sends them to **stderr**, because
/// stdout carries only protocol events and a context warning is a diagnostic
/// rather than a Reporter event. Inventing an event type for it would grow a
/// second vocabulary out of the first.
/// How a chat turn decides what to retrieve from.
///
/// The FLAG is fixed for the session; the config's `auto_rag` is read again
/// on every turn, from the file, so an edit mid-conversation takes effect on
/// the next question like every other config value. That is why this holds a
/// path rather than a loaded value.
struct RagSettings {
    bool flag_given = false;
    std::string flag_value;
    int limit = 4;
    std::filesystem::path config_path;
};

/// A backend's word on its saved state (26j), as a progress note: shown
/// under `--verbose`, like the prompt cache's own line.
[[nodiscard]] harness::StatusSink progress_sink(agentloop::Reporter& reporter) {
    return [&reporter](const harness::StatusEvent& event) {
        if (!event.detail.empty()) {
            reporter.on_progress(event.detail);
        }
    };
}

void run_chat_turn(const harness::Harness& harness, logger::Session& session,
                   const std::string& input, const agent::ToolRegistry* tools,
                   agentloop::ToolSelection* selection, const agentloop::AskFn& ask,
                   const ToolGate& gate, agentloop::Reporter& reporter,
                   const std::function<void(const std::string&)>& notice, const RagSettings& rag,
                   const std::string& review_note, ChatAttachments* attached, ChatRecall* recall) {
    const std::vector<harness::ChatMessage> incoming = build_messages({}, {}, input, {});
    // The turn's model calls besides the chat model's own, said where the
    // surface says them -- the terminal, inside the thinking block (26n).
    const agentloop::SideCallSink side = [&reporter](const agentloop::SideCall& call) {
        reporter.on_side_call(call);
    };

    // Context is measured against what is ABOUT TO BE SENT -- the saved history
    // plus this turn -- not the history alone. Measuring before appending means
    // the first turn always reads as empty, and a single large prompt never
    // trips the threshold it should.
    std::vector<harness::ChatMessage> prospective = session.messages;
    prospective.insert(prospective.end(), incoming.begin(), incoming.end());
    const agentloop::ContextUsage usage =
        agentloop::measure_context(harness, prospective, session.backend);

    if (usage.should_compact()) {
        // The utility model summarises when one is set, else the chat's own
        // backend, as before (26b).
        const std::string compactor =
            helper_backend(harness.config(), harness::ModelRole::Utility, session.backend);
        notice("context " + std::to_string(static_cast<int>(usage.fraction() * 100)) +
               "% full -- compacting" + (compactor == session.backend ? "" : " with " + compactor));
        // Compacts the PRIOR history only: folding the message the user just
        // typed into a summary of the conversation so far would summarise away
        // the question being asked.
        {
            const agentloop::SideCallScope said{side, "utility",
                                                "compacting the conversation with " + compactor};
            session.messages = agentloop::compact_history(harness, session.messages, compactor);
        }
        ++session.compactions;
        if (attached != nullptr) {
            attached->after_compaction();
        }
    } else if (usage.should_warn()) {
        notice("context " + std::to_string(static_cast<int>(usage.fraction() * 100)) + "% full" +
               (usage.exact ? "" : " (estimated)"));
    }

    for (const harness::ChatMessage& message : incoming) {
        session.messages.push_back(message);
    }

    agentloop::Options loop_options;
    loop_options.model = session.backend;
    loop_options.temperature = session.params.temperature;
    loop_options.max_tokens = session.params.max_tokens;
    // Whether the model thinks first (26i): `/think` or the flag, else the
    // backend's; `auto` asks the utility model, when one is set.
    loop_options.thinking = resolve_thinking(
        session.params.thinking, session.params.thinking_budget, harness.config(), session.backend);
    loop_options.thinking_judge = named_utility(harness.config());
    loop_options.stream_answer = true;
    // A large tool result is summarised by the utility model, when one is
    // set, before the chat model reads it (26b).
    loop_options.summary_model = named_utility(harness.config());
    if (tools != nullptr) {
        loop_options.tools = tools;
        // The tools each step offers, when there are many (26g): one
        // selection for the conversation, so a turn can keep the last one's.
        loop_options.tool_selection = selection;
        loop_options.ask = ask;
        loop_options.permission = gate.permission;
        loop_options.confirm = gate.confirm;
    }

    // Retrieval runs PER TURN, against what the user just asked -- which is the
    // reason the injected chunks must stay out of history. Turn one's context
    // left lying in the transcript would still be competing for attention on
    // turn five, against the chunks that actually answer the new question.
    //
    // Which collection: the flag for the whole session, else whatever
    // `auto_rag` says RIGHT NOW. The config is re-read here rather than at
    // startup on purpose, and a config that has become unreadable mid-session
    // costs this turn its auto_rag and says so -- never the turn itself.
    RagChoice rag_choice;
    std::optional<harness::Config> turn_config;
    try {
        turn_config = harness::load_config(rag.config_path);
        // The chat's suite, not the file's default: retrieval's embedder and
        // judge resolve under the suite the chat is running (27d).
        turn_config->models.default_suite = harness.config().models.default_suite;
    } catch (const harness::ConfigError& e) {
        if (!rag.flag_given) {
            notice(std::string{"auto_rag skipped -- config unreadable: "} + e.what());
        }
    }
    if (rag.flag_given) {
        rag_choice = choose_rag_collection(true, rag.flag_value, {});
    } else if (turn_config.has_value()) {
        rag_choice = choose_rag_collection(false, {}, turn_config->auto_rag);
    }
    // A follow-up is searched as a standalone question, restated by the
    // utility model when one is set, else the chat's own (26b) -- once, for
    // the attachments and the collection alike. The chat model is still
    // asked the question as the user wrote it.
    std::string query = input;
    if (rag_choice.active() || (attached != nullptr && attached->retrieves())) {
        const std::string rewriter =
            helper_backend(harness.config(), harness::ModelRole::Utility, session.backend);
        std::optional<agentloop::SideCallScope> said;
        if (agentloop::has_earlier_turn(session.messages)) {
            said.emplace(side, "utility",
                         "rewriting the follow-up into a search query with " + rewriter);
        }
        const agentloop::QueryRewrite rewrite =
            agentloop::rewrite_query(harness, rewriter, session.messages, input, {});
        said.reset();
        if (rewrite.rewritten) {
            reporter.on_progress("search query by " + rewriter + ": " + rewrite.query);
        } else if (!rewrite.note.empty()) {
            notice(rewrite.note);
        }
        query = rewrite.query;
        loop_options.selection_query = query;
    } else if (selection != nullptr && selection->active()) {
        // The tools are ranked for a follow-up restated to stand alone --
        // by the utility model only when one is set: a request to the chat
        // model itself every turn would cost more than the ranking saves.
        if (const std::string utility = named_utility(harness.config());
            !utility.empty() && agentloop::has_earlier_turn(session.messages)) {
            std::optional<agentloop::SideCallScope> said;
            said.emplace(side, "utility",
                         "restating the follow-up to rank the tools, with " + utility);
            const agentloop::QueryRewrite rewrite =
                agentloop::rewrite_query(harness, utility, session.messages, input, {});
            said.reset();
            if (rewrite.rewritten) {
                reporter.on_progress("tools ranked for, by " + utility + ": " + rewrite.query);
                loop_options.selection_query = rewrite.query;
            }
        }
    }
    const agentloop::TurnBudget budget =
        agentloop::turn_budget(harness, session.backend, session.params.max_tokens);

    // The chat's attachments first (26d): an inlined one rides the message
    // just sent, and the others' excerpts are retrieved -- ahead of a
    // collection's, which gets what they leave of the share.
    std::int64_t share_used = 0;
    if (attached != nullptr) {
        ChatAttachments::Turn turn =
            attached->for_turn(session.messages.size() - 1, query, budget, rag.limit, {}, side);
        loop_options.inline_attachments = std::move(turn.inlined);
        if (turn.retrieved.has_value()) {
            if (turn.retrieved->error.empty() && !turn.retrieved->prefix.empty()) {
                loop_options.transient_prefix = turn.retrieved->prefix;
                share_used = turn.retrieved->tokens;
            }
            notice(describe_attachment_retrieval(*turn.retrieved));
        }
    }
    if (rag_choice.active()) {
        // The session's retriever and rerank SETTINGS, read each turn so
        // /retriever and /rerank take effect on the next question and a
        // resumed session continues as it was last set.
        const harness::Config fallback;
        const harness::Config& config = turn_config.has_value() ? *turn_config : fallback;
        const agentloop::RagResult retrieved = retrieve_for_collection(
            harness, config, rag_choice.collection, query, rag.limit, session.retriever,
            session.rerank, {}, session.backend, budget, share_used, side);
        if (retrieved.error.empty() && !retrieved.prefix.empty()) {
            loop_options.transient_prefix.insert(loop_options.transient_prefix.end(),
                                                 retrieved.prefix.begin(), retrieved.prefix.end());
            share_used += retrieved.tokens;
        }
        notice(describe_retrieval(rag_choice, retrieved));
    }
    // What earlier chats established, within what the share has left (26l):
    // transient like the rest, and said, so the user can see why the model
    // knows it.
    if (recall != nullptr && recall->active()) {
        const Recalled recalled =
            recall->for_turn(query, budget, share_used,
                             rag_choice.active() ? rag_choice.collection : std::string{}, side);
        if (!recalled.prefix.empty()) {
            loop_options.transient_prefix.insert(loop_options.transient_prefix.end(),
                                                 recalled.prefix.begin(), recalled.prefix.end());
            reporter.on_recall(recalled.chats, recalled.decisions);
        }
    }
    if (!review_note.empty()) {
        // The review note rides the transient prefix, never the transcript:
        // `/branch` can change it between turns, and a resumed session gets
        // whatever ITS flags say rather than a stale note from last time.
        loop_options.transient_prefix.insert(loop_options.transient_prefix.begin(),
                                             harness::ChatMessage::system(review_note));
    }

    try {
        const agentloop::RunResult result =
            agentloop::run(harness, session.messages, loop_options, reporter);
        ++session.turns;
        if (recall != nullptr) {
            recall->after_turn();
        }
        if (attached != nullptr) {
            attached->after_turn(result.inline_dropped);
        }
        if (result.hit_iteration_limit) {
            notice("tool-call limit reached");
        }
    } catch (const harness::CancelledError&) {
        notice("cancelled");
    } catch (const harness::HarnessError& e) {
        logger::log(logger::Level::Error, "chat", e.what());
        notice(e.what());
    }

    // Persist after EVERY turn. A kill -9 mid-conversation must leave every
    // completed turn on disk, and that is a property of writing here rather
    // than at exit.
    logger::save(session);
}

/// `text` without the double quotes a path with spaces is typed in.
std::string unquoted(std::string_view text) {
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
        text = text.substr(1, text.size() - 2);
    } else if (text.starts_with('"')) {
        text.remove_prefix(1);  // a folder's quote left open by completion
    }
    return std::string{text};
}

/// Attaches each path `message` mentions with `@` that exists, exactly as
/// `/attach` would, and leaves the message as typed. A mention naming nothing
/// stays text -- people type `@` in prose -- and `note` says so (26d).
void attach_mentions(ChatAttachments& attached, std::string_view message,
                     const std::filesystem::path& working_directory,
                     const std::function<void(const std::string&)>& note) {
    const std::vector<std::string> already = attached.names();
    for (const std::string& mention : mentioned_paths(message)) {
        const std::optional<std::string> path = existing_mention(mention, working_directory);
        if (!path.has_value()) {
            note("@" + mention + ": no file or folder there -- left as text");
            continue;
        }
        if (std::ranges::find(already, *path) == already.end()) {
            (void)attached.attach(*path, working_directory);
        }
    }
}

/// The conversation's title, asked for once and off the prompt's path.
///
/// Auto-titling rides the first completed exchange, as a side request so it
/// never enters the conversation's own history. It used to run in line, and
/// the next prompt waited for it: on a local 27B that was fifteen seconds and
/// more -- and every turn, because a reasoning model's title came back empty
/// and was asked for again (found live, 2026-09-25). Now it runs while the
/// user reads the answer and types the next question.
///
/// **One model call at a time is the rule this keeps.** A local provider is
/// not safe to drive from two threads, so everything that may reach the model
/// -- the next turn, a slash command, the exit -- calls `settle()` first. That
/// waits for a title still in flight (seconds at most: a few dozen tokens with
/// the reasoning skipped) and records it.
class BackgroundTitle {
public:
    /// `progress` hears which model titled the chat, for `--verbose` (26b).
    BackgroundTitle(const harness::Harness& harness,
                    std::function<void(std::string_view)> progress = {})
        : harness_{harness}, progress_{std::move(progress)} {}

    BackgroundTitle(const BackgroundTitle&) = delete;
    BackgroundTitle& operator=(const BackgroundTitle&) = delete;
    BackgroundTitle(BackgroundTitle&&) = delete;
    BackgroundTitle& operator=(BackgroundTitle&&) = delete;

    /// An unwinding command must not wait out a whole title: cancelled, then
    /// joined (a future from std::async joins as it is destroyed).
    ~BackgroundTitle() {
        cancellation_.cancel();
    }

    /// Asks for a title when `session` has no name yet -- once per process, so
    /// a model that cannot produce one is not asked again after every turn.
    void start_if_due(const logger::Session& session) {
        if (asked_ || session.turns < 1 || !session.title.empty() || !session.custom_name.empty()) {
            return;
        }
        asked_ = true;
        // The utility model titles the chat when one is set, else the chat's
        // own backend, as before (26b).
        backend_ = helper_backend(harness_.config(), harness::ModelRole::Utility, session.backend);
        title_ =
            std::async(std::launch::async, [this, request = title_request(session, backend_)]() {
                try {
                    return sanitize_title(
                        harness_.chat(request, cancellation_).message.content.plain_text());
                } catch (const std::exception&) {
                    return std::string{};  // a failed title is cosmetic; it never costs a turn
                }
            });
    }

    /// Waits for a title in flight and records it on `session`.
    void settle(logger::Session& session) {
        if (!title_.valid()) {
            return;
        }
        const std::string title = title_.get();
        if (!title.empty() && session.title.empty()) {
            session.title = title;
            logger::save(session);
            if (progress_) {
                progress_("titled by " + backend_ + ": " + title);
            }
        }
    }

    /// Abandons a title in flight: the user interrupted, and did not ask for a
    /// model call to finish first.
    void cancel() const noexcept {
        cancellation_.cancel();
    }

private:
    const harness::Harness& harness_;
    std::function<void(std::string_view)> progress_;
    std::string backend_;
    harness::CancellationToken cancellation_ = harness::CancellationToken::create();
    std::future<std::string> title_;
    bool asked_ = false;
};

}  // namespace

ChatCommand::ChatCommand(MachineBudgetSource machine) : machine_{std::move(machine)} {}

std::string_view ChatCommand::name() const noexcept {
    return "chat";
}

std::string_view ChatCommand::summary() const noexcept {
    return "Start or resume an interactive conversation";
}

void ChatCommand::bind(CLI::App& root, const RootContext& context) {
    auto flags = std::make_shared<ChatFlags>();

    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->add_option("-m,--model", flags->model, "Backend or model to use")
        ->type_name(kBackendValue);
    cmd->add_option("-s,--system", flags->system_prompt, "System prompt for the session");
    flags->rag_option =
        cmd->add_option("--rag", flags->rag,
                        "Retrieve context from this collection each turn (see 'apogee embed'); "
                        "\"\" switches off the config's auto_rag for this session")
            ->type_name(kCollectionValue);
    cmd->add_option("--rag-limit", flags->rag_limit, "How many chunks to inject (default 4)");
    flags->retriever_option =
        cmd->add_option("--retriever", flags->retriever,
                        "How to search the collection: lexical, vector, hybrid, or auto")
            ->type_name(words_value(agentloop::retriever_names()))
            ->check([](const std::string& value) {
                return agentloop::valid_retriever(value)
                           ? std::string{}
                           : agentloop::retriever_values_message("", value);
            });
    flags->rerank_option =
        cmd->add_option(
               "--rerank", flags->rerank,
               "Backend that reorders retrieved chunks with one generation call, on (the utility "
               "model), or off")
            ->type_name(kBackendValue);
    cmd->add_option("--image", flags->images,
                    "An image to attach, as /attach does: seen as it is with the first message by "
                    "a model that can, described for one that cannot (repeatable)")
        ->type_name(kPathValue)
        ->allow_extra_args(false);
    cmd->add_option("--attach", flags->attach,
                    "A file, folder or glob to attach to the chat: indexed, inlined when it fits, "
                    "retrieved each turn when not (repeatable)")
        ->type_name(kPathValue)
        ->allow_extra_args(false);
    flags->temperature_option =
        cmd->add_option("-t,--temperature", flags->temperature, "Sampling temperature");
    flags->max_tokens_option =
        cmd->add_option("-n,--max-tokens", flags->max_tokens, "Maximum tokens per reply");
    cmd->add_option("--think", flags->think,
                    "Whether a reasoning model thinks first: on, off, or auto (per question)")
        ->check(CLI::IsMember({"on", "off", "auto"}));
    cmd->add_flag("--no-recall", flags->no_recall, "Recall no earlier chats in this run");
    cmd->add_option("--allow", flags->allow,
                    "Allow a tool for this chat without asking (repeatable, with --tools)")
        ->type_name(kToolValue)
        ->expected(1)
        ->allow_extra_args(false);
    cmd->add_option("--deny", flags->deny,
                    "Refuse a tool or website for this chat without asking (repeatable)")
        ->type_name(kToolValue)
        ->expected(1)
        ->allow_extra_args(false);
    cmd->add_option("--allow-host", flags->allow_hosts,
                    "Allow fetching from a website for this chat without asking (repeatable)")
        ->expected(1)
        ->allow_extra_args(false);
    flags->think_budget_option =
        cmd->add_option("--think-budget", flags->think_budget,
                        "The most tokens a reasoning model may think for before it answers")
            ->check(CLI::Range(std::int64_t{0}, harness::kMaxThinkingBudget));
    cmd->add_flag("--tools", flags->tools, "Let the model call tools");
    cmd->add_flag("--search", flags->search, "Enable the provider's server-side web search");
    cmd->add_flag("--no-color", flags->no_color, "Disable ANSI colour output");
    cmd->add_flag("--raw", flags->raw,
                  "Show answers' Markdown as written instead of rendering it on the terminal");
    cmd->add_option_function<std::string>(
           "--output-format",
           [flags](const std::string& value) {
               const std::optional<OutputFormat> parsed = output_format_from_string(value);
               if (!parsed.has_value()) {
                   throw CLI::ValidationError("--output-format",
                                              "expected 'text' or 'stream-json'");
               }
               flags->output_format = *parsed;
           },
           "Output format: text (default) or stream-json for a machine driver")
        ->type_name(words_value(format_names()));
    cmd->add_option_function<std::string>(
           "--input-format",
           [flags](const std::string& value) {
               const std::optional<InputFormat> parsed = input_format_from_string(value);
               if (!parsed.has_value()) {
                   throw CLI::ValidationError("--input-format", "expected 'text' or 'stream-json'");
               }
               flags->input_format = *parsed;
           },
           "Input format: text (default) or stream-json; follows --output-format if unset")
        ->type_name(words_value(format_names()));
    cmd->add_flag("-v,--verbose", flags->verbose, "Print progress notes");
    cmd->add_option("--resume", flags->resume, "Resume a saved conversation by id or name")
        ->type_name(kChatValue);
    cmd->add_option("--suite", flags->suite,
                    "Run under this suite -- its members answer for the roles it names -- or off "
                    "for none")
        ->type_name(kModelSuiteValue);
    cmd->add_flag("--warm", flags->warm,
                  "Load the suite's members now, on the progress line, rather than at first use");
    cmd->add_flag("--force", flags->force,
                  "Run the suite even when what it takes is over this machine's memory");
    cmd->add_flag("-q,--quiet", flags->quiet, "No progress line while the suite's members load");
    cmd->add_flag("-c,--continue", flags->cont, "Resume the most recent conversation");
    cmd->add_option("--branch", flags->branch,
                    "Branch under review for the git tools (the head); never checked out")
        ->type_name(kGitRefValue);
    cmd->add_option("--base", flags->base, "Ref to compare against (default: the default branch)")
        ->type_name(kGitRefValue);
    cmd->add_option("--remote", flags->remote, "Remote to resolve refs against (default origin)")
        ->type_name(kGitRemoteValue);
    cmd->add_flag("--fetch", flags->fetch, "Always fetch the refs before diffing");
    cmd->add_flag("--no-fetch", flags->no_fetch, "Never fetch; refuse a ref that is absent");

    cmd->callback([&context, flags, machine = machine_]() {
        const bool decorate = platform::is_terminal(platform::StandardStream::Out);

        harness::Config config;
        std::filesystem::path config_path;
        try {
            config_path = harness::resolve_config_path(context.config_path);
            config = harness::load_config(config_path);
        } catch (const harness::ConfigError& e) {
            fail_user(e.what());
        }
        // A suite named on the command line must exist before anything is
        // built for it (27d).
        if (!flags->suite.empty()) {
            harness::Config probe = config;
            if (const std::string refused = select_suite(probe, flags->suite); !refused.empty()) {
                fail_user("--suite: " + refused);
            }
        }

        // Every configured backend is constructed up front, which is what makes
        // /model an instant switch rather than a reconstruction -- and why
        // history has to be neutral IR rather than a vendor transcript.
        harness::Harness harness{config};
        backends::BuildOptions build_options;
        build_options.web_search = flags->search;
        build_options.config_path = config_path;
        const backends::BuildResult built = backends::build_providers(harness, build_options);

        if (built.constructed_count() == 0) {
            std::string message = "no usable backend is configured";
            if (!built.skipped_summary().empty()) {
                message += " -- " + built.skipped_summary();
            }
            fail_user(message);
        }

        TerminalWriter status_writer{std::cerr};
        CliReporter::Options reporter_options;
        reporter_options.answer_stream = &std::cout;
        reporter_options.decorate = decorate;
        reporter_options.verbosity =
            flags->verbose ? ansi::Verbosity::Verbose : ansi::Verbosity::Line;
        reporter_options.style =
            ansi::Style::detect(flags->no_color ? ansi::ColorMode::Never : ansi::ColorMode::Auto);
        reporter_options.width = static_cast<std::size_t>(platform::terminal_width().value_or(80));
        reporter_options.markdown = !flags->raw && config.ui.markdown;
        reporter_options.hyperlinks = ansi::hyperlinks_supported();
        CliReporter reporter{status_writer, reporter_options};
        const ansi::Style& style = reporter_options.style;
        // A model loading for any reason -- the chat's first turn, a
        // helper's first chore, an embedding -- says so on the spinner rather
        // than in silence (27e). Never into machine mode's stream. Every model
        // call that could load is over before the reporter goes: the title
        // and the attachments settle first, and both are declared after it.
        if (flags->output_format != OutputFormat::StreamJson) {
            harness.listen_for_loads(
                [&reporter](std::string_view backend, const harness::StatusEvent& event) {
                    reporter.on_model_load(backend, event);
                });
        }

        // --- resume ---------------------------------------------------------
        logger::Session session;
        logger::KnownDependencies known;
        known.backends = config.backend_names();
        known.suites = config.suite_names();
        known.check_suites = true;

        if (!flags->resume.empty() || flags->cont) {
            try {
                logger::LoadedSession loaded = flags->cont ? [&]() {
                    const std::optional<logger::Session> recent = logger::most_recent();
                    if (!recent.has_value()) {
                        fail_user("no saved conversation to continue");
                    }
                    return logger::load(recent->chat_id, known);
                }()
                                                           : logger::load(flags->resume, known);

                session = std::move(loaded.session);
                // Resume degrades, never fails: every problem is a note.
                for (const logger::ResumeWarning& warning : loaded.warnings) {
                    reporter.status().print_line(style.tag(ansi::Role::Warning) + " [resume] " +
                                                 warning.message);
                    if (warning.kind == logger::WarningKind::BackendMissing) {
                        session.backend.clear();  // fall back to the default
                    }
                }
            } catch (const CLI::RuntimeError&) {
                throw;
            } catch (const std::exception& e) {
                fail_user(e.what());
            }
        } else {
            session.chat_id = logger::new_chat_id();
        }

        // The chat's suite (27d): the flag, else what the chat last had, else
        // the config's default. Chosen before the chat's backend, which the
        // suite's chat member answers for.
        std::optional<std::string> chosen_suite;
        if (!flags->suite.empty()) {
            harness::Config probe = config;
            (void)select_suite(probe, flags->suite);  // validated above
            chosen_suite = probe.models.default_suite;
        } else if (session.suite.has_value() && session.suite->empty()) {
            chosen_suite = std::string{};
        } else if (session.suite.has_value()) {
            // Checked against the config as the chat was loaded: a suite since
            // deleted was dropped there, with a warning.
            if (harness::Config probe = config; select_suite(probe, *session.suite).empty()) {
                chosen_suite = probe.models.default_suite;
            }
        }
        if (chosen_suite.has_value() && *chosen_suite != config.models.default_suite) {
            for (const std::string& line :
                 activate_suite(harness, config, *chosen_suite, build_options)) {
                reporter.status().print_line(style.tag(ansi::Role::Warning) + " " + line);
            }
        }
        // A member naming nothing is refused before a turn could fall back to
        // models.default and answer from a model nobody chose.
        if (const std::string refused = validate_active_suite(config); !refused.empty()) {
            fail_user(refused);
        }
        // Admission (27e): the set the session runs under, priced against
        // this machine. Over budget it is refused here, before anything is
        // loaded or a turn could start, unless --force says to go ahead.
        std::optional<models::SuiteFootprint> footprint;
        if (!config.models.default_suite.empty()) {
            footprint = price_suite(config, config.models.default_suite, machine);
            if (const std::string refused = admission_refusal(*footprint, "--force");
                !refused.empty() && !flags->force) {
                fail_user(refused);
            }
        } else if (flags->warm) {
            fail_user(
                "--warm loads a suite's members, and this chat runs under none -- --suite "
                "<name> names one");
        }
        const bool forced =
            footprint.has_value() && footprint->admission() == models::Admission::OverBudget;
        // The members this session uses stay resident between its turns,
        // however far apart, the hold following the suite as /suite moves
        // it; it goes with the session.
        const harness::SessionHold hold{harness};
        if (!config.models.default_suite.empty()) {
            session.suite = config.models.default_suite;
        } else if (chosen_suite.has_value()) {
            session.suite = std::string{};  // turned off, and kept off on resume
        }
        // A suite named at launch speaks for a resumed chat's role too, as
        // /suite does mid-chat; -m still wins.
        if (!flags->suite.empty() && flags->model.empty()) {
            if (const harness::Resolution chat = harness::resolve_backend(
                    config, harness::RoleRequest{.role = harness::ModelRole::Chat});
                chat.from == harness::ResolvedFrom::Suite) {
                session.backend = chat.key;
            }
        }

        // Precedence: an explicit flag beats the saved value beats the default
        // -- expressed through the one shared resolver rather than restated
        // here, because a session's saved backend is exactly the "per-feature
        // pin" rung the chain already has.
        const std::string model = harness::resolve_backend_key(
            config, harness::RoleRequest{.role = harness::ModelRole::Chat,
                                         .override = flags->model,
                                         .entry_backend = session.backend});
        session.backend = model;
        // A resumed chat starts from its saved state when its backend kept
        // one (26j); a new one is named so it can be saved.
        harness.resume_conversation(session.backend, session.chat_id);
        // Saved again after the turn that reads a compacted history: the
        // state from before no longer matches it.
        int saved_compactions = session.compactions;
        // Recall across chats (26l): on unless the config or the flag says
        // not; a chat a process that died left due is summarised now.
        ChatRecall recall{harness, config, session, config.memory.recall && !flags->no_recall};
        for (const std::string& line : recall.catch_up({})) {
            reporter.status().print_line(style.dim("[memory] " + line));
        }

        // Retrieval settings follow the same precedence as every other saved
        // parameter: an explicit flag wins, else what the session last had.
        if (flags->retriever_option->count() > 0) {
            session.retriever = flags->retriever == "auto" ? std::string{} : flags->retriever;
        }
        if (flags->rerank_option->count() > 0) {
            session.rerank = flags->rerank;
        }
        if (flags->temperature_option->count() > 0) {
            session.params.temperature = flags->temperature;
        }
        if (flags->max_tokens_option->count() > 0) {
            session.params.max_tokens = flags->max_tokens;
        }
        if (!flags->think.empty()) {
            session.params.thinking = harness::thinking_mode_from_string(flags->think);
        }
        if (flags->think_budget_option->count() > 0) {
            session.params.thinking_budget = flags->think_budget;
        }
        if (!flags->system_prompt.empty()) {
            session.params.system_prompt = flags->system_prompt;
        }

        if (session.messages.empty() && !session.params.system_prompt.empty()) {
            session.messages.push_back(harness::ChatMessage::system(session.params.system_prompt));
        }

        logger::log(logger::Level::Info, "chat",
                    "session " + session.chat_id + " on backend " + model);

        // --- the review context, from flags -----------------------------------
        if (flags->fetch && flags->no_fetch) {
            fail_user("--fetch and --no-fetch are mutually exclusive");
        }
        agentloop::ReviewContext review;
        review.head = flags->branch;
        review.base = flags->base;
        review.remote = flags->remote.empty() ? "origin" : flags->remote;
        review.fetch = flags->fetch ? "always" : flags->no_fetch ? "never" : "auto";
        // Shared with the git tools and read at call time, so `/branch`
        // re-points them without rebuilding the registry.
        const auto live_review = std::make_shared<tools::ReviewDefaults>();
        const auto sync_review = [&]() {
            live_review->head = review.head;
            live_review->base = review.base;
            live_review->remote = review.remote;
            live_review->fetch = review.fetch;
        };
        sync_review();
        std::string review_note = agentloop::review_note(review);

        // --- tools ----------------------------------------------------------
        agent::ToolRegistry registry;
        const auto mcp_registry = std::make_shared<mcp::Registry>();
        if (flags->tools) {
            registry = make_built_in_tools(BuiltInToolOptions{
                .config = &config,
                .harness = &harness,
                .review = *live_review,
                .live_review = live_review,
                .mcp = mcp_registry,
                .mcp_status = mcp_status_line(reporter.status()),
                // A server's stderr never reaches the terminal unless asked
                // for: with --verbose it is the raw stream, otherwise the
                // bounded tail rides the connect-failure message.
                .mcp_server_log = flags->verbose
                                      ? mcp::StderrTail::Sink{[](std::string_view bytes) {
                                            std::cerr << bytes << std::flush;
                                        }}
                                      : mcp::StderrTail::Sink{}});
        }
        // What the chat's backend is offered: every registered tool, or the
        // toolset the active suite pins on that backend (27d) -- recomputed
        // only when a switch moves the pin.
        std::optional<agent::ToolRegistry> pinned_registry;
        std::optional<std::vector<std::string>> offered_pin;
        const agent::ToolRegistry* offered = &registry;
        // Past a dozen and a half tools, each turn offers the ones its
        // question needs (26g); one selection for the whole conversation.
        std::unique_ptr<agentloop::ToolSelection> selection;
        const auto offer_tools = [&]() {
            offered_pin = harness::suite_pins(config, session.backend).toolset;
            if (offered_pin.has_value()) {
                pinned_registry = apply_toolset(registry, *offered_pin);
                offered = &*pinned_registry;
            } else {
                pinned_registry.reset();
                offered = &registry;
            }
            std::string ranked_by;
            selection = make_tool_selection(harness, config, *offered, config_path, ranked_by);
            if (selection != nullptr && flags->verbose) {
                reporter.status().print_line("[tools] " + std::to_string(offered->size()) +
                                             " registered: each turn offers the ones its question "
                                             "needs, ranked by " +
                                             ranked_by);
            }
        };
        const auto reoffer_tools = [&]() {
            if (flags->tools &&
                harness::suite_pins(config, session.backend).toolset != offered_pin) {
                offer_tools();
            }
        };
        if (flags->tools) {
            offer_tools();
        }
        // The gate: config levels, then what the user answers for this
        // session. The prompt half is chosen per surface below.
        const auto approvals = std::make_shared<SessionApprovals>();
        const agent::PermissionChecker permission = make_permission_checker(config, approvals);
        // The session's answers, given at launch (26o): exactly the `session`
        // answer, given before the prompt would ask.
        const std::vector<std::string> gated =
            flags->tools ? gated_tools(registry) : std::vector<std::string>{};
        if (const PermissionPresets presets{
                .allow = flags->allow, .deny = flags->deny, .allow_hosts = flags->allow_hosts};
            !presets.empty()) {
            if (!flags->tools) {
                fail_user(
                    "--allow, --deny and --allow-host need --tools: without tools, nothing "
                    "is asked");
            }
            if (const std::string refused = seed_approvals(*approvals, presets, gated);
                !refused.empty()) {
                fail_user(refused);
            }
        }

        if (decorate) {
            // A base model is said in the banner and the spinner, all session
            // (26r) -- never on an answer.
            reporter.status().print_line(style.tag(ansi::Role::Apogee) + " " + model +
                                         (is_base_model(harness, model) ? "  ·  base model" : "") +
                                         banner_suite(config.models.default_suite, forced) +
                                         "  ·  chat " + session.chat_id +
                                         "  ·  /help for commands");
        }
        // What the suite takes of this machine, stated where the session
        // starts -- on a pipe too, on stderr -- and, asked for, its members
        // loaded now on the busy line rather than at their first use (27e).
        if (footprint.has_value()) {
            reporter.status().print_line(style.tag(ansi::Role::Apogee) + " " +
                                         admission_line(*footprint, forced));
        }
        if (flags->warm) {
            std::vector<std::string> unwarmed;
            {
                BusyLine busy{
                    std::cerr, "warming suite " + config.models.default_suite,
                    busy_options(flags->quiet || flags->output_format == OutputFormat::StreamJson)};
                unwarmed = warm_suite(harness, config, busy);
            }
            for (const std::string& line : unwarmed) {
                reporter.status().print_line(style.tag(ansi::Role::Warning) + " " + line);
            }
        }
        // Tools withheld from a base model are said once, at the start, on a
        // terminal and a pipe alike -- never into machine mode's stream.
        if (flags->tools && flags->output_format != OutputFormat::StreamJson &&
            is_base_model(harness, model)) {
            reporter.status().print_line(style.tag(ansi::Role::Warning) + " " +
                                         base_model_tools_note(model));
        }
        if (decorate) {
            // The banner stands apart from the first prompt.
            reporter.status().print_line("");
        }

        // --- machine mode ----------------------------------------------------
        //
        // A driven child: one JSON object per line in, the same typed event
        // stream out, many turns on one process. It shares `run_chat_turn` with
        // the REPL below, so context monitoring, compaction, the per-turn save
        // and auto-titling all reach a GUI-driven session too -- they were
        // never surface concerns, only the input was.
        //
        // No line editor, no slash commands, no typeahead flush: those are all
        // terminal affordances, and a driver has its own UI for every one of
        // them.
        // Unset follows --output-format: a driver asking for machine output is
        // a machine, and a human asking for text is a human, so the two
        // ordinary cases need one flag rather than two.
        const InputFormat input_format = flags->input_format.value_or(
            flags->output_format == OutputFormat::StreamJson ? InputFormat::StreamJson
                                                             : InputFormat::Text);

        // On `chat` the two directions must agree. A JSONL-emitting REPL has no
        // coherent meaning -- slash commands print through the terminal
        // reporter and have no protocol event -- and a driven session that
        // renders prose gives its driver nothing to parse. Saying so is the
        // point: the alternative is accepting the flag and quietly ignoring it,
        // which is how a driver ends up debugging output it never asked for.
        const bool machine_in = input_format == InputFormat::StreamJson;
        const bool machine_out = flags->output_format == OutputFormat::StreamJson;
        if (machine_in != machine_out) {
            fail_user(std::string{"--input-format "} + std::string{to_string(input_format)} +
                      " cannot be combined with --output-format " +
                      std::string{to_string(flags->output_format)} +
                      "; a driven chat session speaks the protocol in both directions");
        }

        const RagSettings rag_settings{.flag_given = flags->rag_option->count() > 0,
                                       .flag_value = flags->rag,
                                       .limit = flags->rag_limit,
                                       .config_path = config_path};

        if (input_format == InputFormat::StreamJson) {
            JsonReporter machine_reporter{std::cout};
            machine_reporter.begin_session(session.backend);

            const auto machine_notice = [](const std::string& message) {
                // stdout carries ONLY protocol events, so a diagnostic goes to
                // stderr -- the same discipline Apogee demands of the vendor
                // CLIs it drives, having been the consumer on the other side.
                std::cerr << "apogee: " << message << "\n";
            };

            const agentloop::AskFn driver_ask =
                flags->tools ? make_driver_ask_fn(machine_reporter, std::cin) : agentloop::AskFn{};

            BackgroundTitle title{harness, [&machine_reporter](std::string_view line) {
                                      machine_reporter.on_progress(line);
                                  }};
            // A driver has no terminal to be asked on, so a folder over the
            // size guard is refused, as on any pipe (26d).
            std::error_code cwd_error;
            const std::filesystem::path working_directory =
                std::filesystem::current_path(cwd_error);
            ChatAttachments attached{
                harness, session, ChatAttachments::index_for(session.chat_id),
                ChatAttachments::Hooks{
                    .say = [&machine_reporter](
                               const std::string& line,
                               bool /*warning*/) { machine_reporter.on_notice(line); },
                    .progress =
                        [&machine_reporter](const std::string& line) {
                            if (!line.empty()) {
                                machine_reporter.on_progress(line);
                            }
                        },
                    .confirm_large = {},
                    .save = true}};
            // `--image` is an attachment like any other (26e): read as it is
            // with the first message by a model that can, described for one
            // that cannot.
            for (const std::string& spec : flags->images) {
                (void)attached.attach(spec, working_directory);
            }
            for (const std::string& spec : flags->attach) {
                (void)attached.attach(spec, working_directory);
            }
            std::string line;
            while (std::getline(std::cin, line)) {
                const DriverMessage message = parse_driver_line(line);
                if (message.kind == DriverMessage::Kind::Attach && !message.text.empty()) {
                    (void)attached.attach(message.text, working_directory);
                    continue;
                }
                if (message.kind != DriverMessage::Kind::User || message.text.empty()) {
                    // Unknown types are ignored rather than fatal: the same
                    // tolerance this protocol asks of its own drivers.
                    continue;
                }
                title.settle(session);
                attach_mentions(attached, message.text, working_directory,
                                [&machine_reporter](const std::string& note) {
                                    machine_reporter.on_notice(note);
                                });
                attached.settle();

                // The driver reads structured input, so there IS someone to
                // answer a question -- the loop's "nil AskFn <=> never
                // advertised" rule is satisfied rather than sidestepped.
                run_chat_turn(harness, session, message.text, flags->tools ? offered : nullptr,
                              selection.get(), driver_ask,
                              ToolGate{permission, flags->tools ? make_driver_confirm_fn(
                                                                      machine_reporter, std::cin,
                                                                      config_path, approvals)
                                                                : agent::ConfirmFn{}},
                              machine_reporter, machine_notice, rag_settings, review_note,
                              &attached, &recall);
                title.start_if_due(session);
                if (session.compactions != saved_compactions) {
                    saved_compactions = session.compactions;
                    harness.save_conversation(session.backend, session.chat_id,
                                              progress_sink(machine_reporter));
                }

                harness::ChatResponse response;
                response.message = session.messages.empty() ? harness::ChatMessage::assistant("")
                                                            : session.messages.back();
                response.model = session.backend;
                machine_reporter.emit_result(response);
            }

            // stdin closed: the driver is done. Everything is already persisted
            // by the per-turn save, so exiting is clean by construction.
            title.settle(session);
            logger::save(session);
            if (const std::string said = recall.finish({}); !said.empty()) {
                machine_notice(said);
            }
            harness.save_conversation(session.backend, session.chat_id,
                                      progress_sink(machine_reporter));
            return;
        }

        // --- attachments (26d) ------------------------------------------------
        std::error_code cwd_error;
        const std::filesystem::path working_directory = std::filesystem::current_path(cwd_error);
        const bool asked_on_terminal = platform::is_terminal(platform::StandardStream::In);
        ChatAttachments attached{
            harness, session, ChatAttachments::index_for(session.chat_id),
            ChatAttachments::Hooks{
                .say =
                    [&reporter, &style](const std::string& line, bool warning) {
                        reporter.status().print_line(
                            style.tag(warning ? ansi::Role::Warning : ansi::Role::Apogee) + " " +
                            line);
                    },
                .progress =
                    [&reporter, &style](const std::string& line) {
                        if (line.empty()) {
                            reporter.status().clear();
                        } else {
                            reporter.status().set(style.tag(ansi::Role::Apogee) + " " + line);
                        }
                    },
                // A pipe cannot be asked, so a folder over the size guard is
                // refused there.
                .confirm_large =
                    asked_on_terminal
                        ? std::function<bool(const std::string&)>{[&reporter, &style](
                                                                      const std::string& question) {
                              reporter.status().print_line(style.tag(ansi::Role::Warning) + " " +
                                                           question + " [y/N]");
                              std::string answer;
                              if (!std::getline(std::cin, answer)) {
                                  return false;
                              }
                              return answer == "y" || answer == "Y" || answer == "yes";
                          }}
                        : std::function<bool(const std::string&)>{},
                .save = true}};
        for (const std::string& spec : flags->images) {
            (void)attached.attach(spec, working_directory);
        }
        for (const std::string& spec : flags->attach) {
            (void)attached.attach(spec, working_directory);
        }

        // Once, immediately before the first prompt -- never between turns.
        discard_startup_typeahead();

        // `/` lists the commands as they are typed, a command's values follow
        // it, and `@` completes paths -- all from the one command table. A
        // pipe gets none of it: the plain reader never asks.
        EditingLineReader::Options reader_options;
        reader_options.history_path = default_history_path();
        // A folder deleted under the process lists as nothing, never a throw.
        ChatCompletionSources completion = chat_completion_sources(config, working_directory);
        completion.attachment_names = [&attached] { return attached.names(); };
        completion.gated_tools = [&gated] { return gated; };
        completion.session_permissions = [approvals] {
            std::vector<std::string> names;
            for (const auto* set : {&approvals->tools, &approvals->denied_tools, &approvals->hosts,
                                    &approvals->denied_hosts}) {
                names.insert(names.end(), set->begin(), set->end());
            }
            return names;
        };
        reader_options.suggest = [sources = std::move(completion)](std::string_view before_cursor) {
            return suggest_chat_input(before_cursor, sources);
        };
        reader_options.live = true;
        reader_options.color = style.color_enabled();
        const std::unique_ptr<LineReader> reader =
            make_line_reader(std::move(reader_options), std::cin);

        // --- capture: this conversation as one knowledge record --------------
        //
        // The loaded model is the clerk -- no second backend, no second load
        // -- and the record goes through the same core as `knowledge
        // capture`, so `/capture` and the command produce the same record
        // from the same transcript. The session's own --retriever describes
        // how its DOCUMENTS are searched and says nothing about how a record
        // should be indexed, so only the collection's pin and auto apply.
        const auto capture_session = [&](knowledge::Overrides overrides) {
            // The clerk is a model call: one at a time (26e).
            attached.settle();
            const std::string transcript = logger::transcript_text(session.messages);
            if (transcript.empty()) {
                reporter.status().print_line(
                    style.tag(ansi::Role::Warning) +
                    " nothing to capture yet -- have a conversation first");
                return;
            }
            if (overrides.source.empty()) {
                overrides.source = "chat";
            }
            CaptureInputs inputs;
            inputs.raw = transcript;
            inputs.overrides = std::move(overrides);
            // The clerk is the chat's own model -- loaded already, so no second
            // load (Milestone Y) -- unless a utility model is named (26b).
            const std::string utility = named_utility(config);
            const std::string clerk = utility.empty() ? session.backend : utility;
            // Narrated in the thinking block like any side call, which then
            // collapses: the clerk is a model call of its own (26n).
            std::optional<agentloop::SideCallScope> said;
            said.emplace(
                [&reporter](const agentloop::SideCall& call) { reporter.on_side_call(call); },
                "clerk", "distilling this conversation into a record with " + clerk);
            const CaptureResult result =
                capture_and_store(harness, config, config_path, inputs,
                                  knowledge::make_structured_clerk(harness, clerk));
            said.reset();
            reporter.on_clear_status();
            if (!result.ok()) {
                // The clerk named, as the line its narration replaced did:
                // on a pipe that line is not drawn at all (26n).
                reporter.status().print_line(style.tag(ansi::Role::Error) + " capture by " + clerk +
                                             " failed: " + result.error);
                return;
            }
            for (const std::string& note : result.notes) {
                reporter.status().print_line(style.tag(ansi::Role::Warning) + " " + note);
            }
            reporter.status().print_line(style.tag(ansi::Role::Apogee) + " captured " +
                                         result.record.id + " [" + result.record.status + "] -- " +
                                         preview_text(result.record.intent, 80));
        };

        // --- the REPL --------------------------------------------------------
        BackgroundTitle title{harness,
                              [&reporter](std::string_view line) { reporter.on_progress(line); }};
        bool running = true;
        while (running) {
            // The editor draws its own prompt; the plain reader ignores it and
            // the prompt goes to stderr so a piped run's stdout stays clean.
            std::optional<std::string> line;
            if (reader->interactive()) {
                line = reader->read(style.colorize("You: ", ansi::Color::Green));
            } else {
                if (decorate) {
                    std::cerr << style.colorize("You: ", ansi::Color::Green) << std::flush;
                }
                line = reader->read({});
            }
            if (!line.has_value()) {
                break;  // EOF, or Ctrl-D / Ctrl-C at the editor
            }

            const std::string input = trim(*line);
            if (input.empty()) {
                continue;
            }
            // Only non-empty lines enter the history, so recall is not padded
            // with blanks.
            reader->remember(input);

            // Before anything below can reach the model.
            title.settle(session);

            if (const std::optional<SlashCommand> command = parse_slash(input);
                command.has_value()) {
                const std::string& argument = command->argument;
                const ChatCommandSpec* spec = find_chat_command(command->name);
                if (spec == nullptr) {
                    reporter.status().print_line(style.tag(ansi::Role::Error) +
                                                 " unknown command '/" + command->name +
                                                 "' -- /help lists them");
                    continue;
                }

                // Dispatched through the table: a verb only runs if the table
                // names it, and a row added without a case here fails the
                // build -- the one-table rule, held by the compiler.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic error "-Wswitch"
#endif
                switch (spec->id) {
                    case ChatVerb::Exit:
                        running = false;
                        break;
                    case ChatVerb::Recall:
                        if (argument.empty()) {
                            std::string state = "off";
                            if (recall.active()) {
                                state = "on";
                            } else if (config.memory.recall && !flags->no_recall) {
                                state = "off for this chat";
                            }
                            reporter.status().print_line(style.tag(ansi::Role::Apogee) +
                                                         " recall: " + state);
                        } else if (argument == "on" || argument == "off") {
                            recall.set_session(argument == "on");
                        } else {
                            reporter.status().print_line(style.tag(ansi::Role::Error) +
                                                         " not on or off: '" + argument + "'");
                        }
                        break;
                    case ChatVerb::Permissions:
                        for (const std::string& line :
                             describe_permissions(config, *approvals, gated)) {
                            reporter.status().print_line("  " + line);
                        }
                        break;
                    case ChatVerb::Allow:
                    case ChatVerb::Deny:
                    case ChatVerb::Revoke: {
                        if (argument.empty()) {
                            // Alone, `/allow` lists: listing beats an error.
                            for (const std::string& line :
                                 describe_permissions(config, *approvals, gated)) {
                                reporter.status().print_line("  " + line);
                            }
                            break;
                        }
                        const GatedName name = name_gated(argument, gated);
                        if (!name.error.empty()) {
                            reporter.status().print_line(style.tag(ansi::Role::Error) + " " +
                                                         name.error);
                            break;
                        }
                        const std::string what = name.host ? "website " + name.key : name.key;
                        if (spec->id == ChatVerb::Allow) {
                            allow_for_session(*approvals, name);
                            const bool config_denies =
                                !name.host && config.permissions.level(name.key) ==
                                                  harness::PermissionLevel::Deny;
                            reporter.status().print_line(
                                style.tag(ansi::Role::Apogee) + " " + what +
                                (config_denies
                                     ? " stays denied: the config says deny, and a chat never "
                                       "loosens that"
                                     : " allowed for this chat"));
                        } else if (spec->id == ChatVerb::Deny) {
                            deny_for_session(*approvals, name);
                            reporter.status().print_line(style.tag(ansi::Role::Apogee) + " " +
                                                         what + " denied for this chat");
                        } else if (revoke_for_session(*approvals, name)) {
                            reporter.status().print_line(style.tag(ansi::Role::Apogee) + " " +
                                                         what + ": asked again from now on");
                        } else if (!name.host && config.permissions.levels.contains(name.key)) {
                            reporter.status().print_line(
                                style.tag(ansi::Role::Apogee) + " " + what +
                                " is the config's answer -- change it with 'apogee config "
                                "set-permission " +
                                name.key + " ask'");
                        } else {
                            reporter.status().print_line(style.tag(ansi::Role::Apogee) +
                                                         " this chat has no answer for " + what);
                        }
                        break;
                    }
                    case ChatVerb::Private:
                        recall.make_private();
                        logger::save(session);
                        reporter.status().print_line(
                            style.tag(ansi::Role::Apogee) +
                            " this chat is private: it will never be summarised for recall");
                        break;
                    case ChatVerb::Help:
                        for (const std::string& row : chat_help_lines(
                                 reader->interactive() ? static_cast<std::size_t>(
                                                             platform::terminal_width().value_or(0))
                                                       : 0)) {
                            reporter.status().print_line(row);
                        }
                        break;
                    case ChatVerb::Models:
                        for (const std::string& backend : config.backend_names()) {
                            reporter.status().print_line(
                                (backend == session.backend ? "* " : "  ") + backend);
                        }
                        break;
                    case ChatVerb::Model:
                        if (argument.empty()) {
                            reporter.status().print_line(session.backend);
                        } else if (config.find_backend(argument) == nullptr) {
                            reporter.status().print_line(style.tag(ansi::Role::Error) +
                                                         " no backend named '" + argument + "'");
                        } else {
                            // Instant, and history carries over: every backend
                            // was constructed up front and history is neutral IR.
                            session.backend = argument;
                            harness.resume_conversation(session.backend, session.chat_id);
                            // A suite's toolset pin follows the backend (27d).
                            reoffer_tools();
                            const bool base = is_base_model(harness, argument);
                            reporter.status().print_line(style.tag(ansi::Role::Apogee) +
                                                         " switched to " + argument +
                                                         (base ? " -- a base model" : ""));
                            if (base && flags->tools) {
                                reporter.status().print_line(style.tag(ansi::Role::Warning) + " " +
                                                             base_model_tools_note(argument));
                            }
                        }
                        break;
                    case ChatVerb::Suite: {
                        if (argument.empty()) {
                            reporter.status().print_line(style.tag(ansi::Role::Apogee) +
                                                         " suite: " + active_suite_summary(config));
                            // The session's own status (27e): each member
                            // resident or not, and the set's total.
                            if (!config.models.default_suite.empty()) {
                                for (const std::string& row : footprint_lines(
                                         price_suite(config, config.models.default_suite, machine),
                                         [&harness](std::string_view backend) {
                                             return harness.resident(backend);
                                         })) {
                                    reporter.status().print_line(row);
                                }
                            }
                            break;
                        }
                        const SuiteArgument asked = parse_suite_argument(argument);
                        harness::Config probe = config;
                        std::string refused = asked.error;
                        if (refused.empty()) {
                            refused = select_suite(probe, asked.suite);
                        }
                        if (refused.empty()) {
                            refused = validate_active_suite(probe);
                        }
                        // Admission (27e), before anything is rebuilt.
                        std::optional<models::SuiteFootprint> priced;
                        if (refused.empty() && !probe.models.default_suite.empty()) {
                            priced = price_suite(probe, probe.models.default_suite, machine);
                            if (!asked.force) {
                                refused = admission_refusal(
                                    *priced, "/suite " + probe.models.default_suite + " --force");
                            }
                        }
                        if (refused.empty() && asked.warm && probe.models.default_suite.empty()) {
                            refused = "--warm loads a suite's members; off has none";
                        }
                        if (!refused.empty()) {
                            reporter.status().print_line(style.tag(ansi::Role::Error) + " " +
                                                         refused);
                            break;
                        }
                        // Rebuilding a backend is a model call's business:
                        // what is reading an attachment settles first.
                        attached.settle();
                        for (const std::string& said : activate_suite(
                                 harness, config, probe.models.default_suite, build_options)) {
                            reporter.status().print_line(style.tag(ansi::Role::Warning) + " " +
                                                         said);
                        }
                        session.suite = config.models.default_suite;
                        // The suite's chat member speaks for the conversation,
                        // as /model would; with none, or off, it stays put.
                        std::string moved;
                        if (const harness::Resolution chat = harness::resolve_backend(
                                config, harness::RoleRequest{.role = harness::ModelRole::Chat});
                            chat.from == harness::ResolvedFrom::Suite &&
                            chat.key != session.backend &&
                            config.find_backend(chat.key) != nullptr) {
                            session.backend = chat.key;
                            harness.resume_conversation(session.backend, session.chat_id);
                            moved = chat.key;
                        }
                        reoffer_tools();
                        logger::save(session);
                        if (config.models.default_suite.empty()) {
                            reporter.status().print_line(
                                style.tag(ansi::Role::Apogee) +
                                " suite off -- the roles follow the global pointers; the "
                                "conversation stays on " +
                                session.backend);
                        } else {
                            reporter.status().print_line(style.tag(ansi::Role::Apogee) + " suite " +
                                                         active_suite_summary(config));
                            if (priced.has_value()) {
                                reporter.status().print_line(
                                    style.tag(ansi::Role::Apogee) + " " +
                                    admission_line(*priced, priced->admission() ==
                                                                models::Admission::OverBudget));
                            }
                            if (!moved.empty()) {
                                reporter.status().print_line(
                                    style.tag(ansi::Role::Apogee) + " switched to " + moved +
                                    (is_base_model(harness, moved) ? " -- a base model" : ""));
                            }
                            if (asked.warm) {
                                std::vector<std::string> unwarmed;
                                {
                                    BusyLine busy{std::cerr,
                                                  "warming suite " + config.models.default_suite,
                                                  busy_options(flags->quiet)};
                                    unwarmed = warm_suite(harness, config, busy);
                                }
                                for (const std::string& said : unwarmed) {
                                    reporter.status().print_line(style.tag(ansi::Role::Warning) +
                                                                 " " + said);
                                }
                            }
                        }
                        break;
                    }
                    case ChatVerb::Branch:
                        if (argument.empty()) {
                            reporter.status().print_line(
                                style.tag(ansi::Role::Apogee) + " review: " +
                                (review.active()
                                     ? agentloop::review_summary(review)
                                     : "off -- /branch <head>, <base>..<head>, or off"));
                        } else {
                            // Deterministic from the argument: the tools'
                            // defaults and the note change together, the
                            // transcript not at all. Free text in the next
                            // question changes nothing about which diff the
                            // tools compare.
                            review = agentloop::parse_branch_arg(argument, review);
                            sync_review();
                            review_note = agentloop::review_note(review);
                            reporter.status().print_line(
                                style.tag(ansi::Role::Apogee) + " review " +
                                (review.active() ? agentloop::review_summary(review) : "off"));
                        }
                        break;
                    case ChatVerb::Capture: {
                        // An argument is a status when it is one, else a link.
                        knowledge::Overrides overrides;
                        if (!argument.empty()) {
                            const std::string status = knowledge::normalize_status(argument);
                            if (knowledge::is_valid_status(status)) {
                                overrides.status = status;
                            } else {
                                overrides.link = argument;
                            }
                        }
                        capture_session(std::move(overrides));
                        break;
                    }
                    case ChatVerb::System:
                        session.params.system_prompt = argument;
                        reporter.status().print_line(style.tag(ansi::Role::Apogee) +
                                                     " system prompt updated");
                        break;
                    case ChatVerb::Temperature:
                        try {
                            session.params.temperature = std::stod(argument);
                        } catch (const std::exception&) {
                            reporter.status().print_line(style.tag(ansi::Role::Error) +
                                                         " not a number: '" + argument + "'");
                        }
                        break;
                    case ChatVerb::Think:
                        if (argument.empty()) {
                            const harness::Thinking thinking = resolve_thinking(
                                session.params.thinking, session.params.thinking_budget,
                                harness.config(), session.backend);
                            reporter.status().print_line(
                                style.tag(ansi::Role::Apogee) +
                                " thinking: " + std::string{harness::to_string(thinking.mode)} +
                                (thinking.budget.has_value()
                                     ? ", at most " + std::to_string(*thinking.budget) + " tokens"
                                     : std::string{}));
                        } else if (const std::optional<harness::ThinkingMode> mode =
                                       harness::thinking_mode_from_string(argument);
                                   mode.has_value()) {
                            session.params.thinking = mode;
                        } else {
                            reporter.status().print_line(style.tag(ansi::Role::Error) +
                                                         " not a thinking mode: '" + argument +
                                                         "' (on, off or auto)");
                        }
                        break;
                    case ChatVerb::MaxTokens:
                        try {
                            session.params.max_tokens = std::stoll(argument);
                        } catch (const std::exception&) {
                            reporter.status().print_line(style.tag(ansi::Role::Error) +
                                                         " not a number: '" + argument + "'");
                        }
                        break;
                    case ChatVerb::Retriever:
                        if (argument.empty()) {
                            reporter.status().print_line(
                                style.tag(ansi::Role::Rag) + " retriever: " +
                                (session.retriever.empty() ? "auto" : session.retriever) +
                                (session.retriever.empty()
                                     ? " -- vector when the collection's vectors match the "
                                       "embedding backend, else lexical; hybrid only when asked"
                                     : ""));
                        } else if (!agentloop::valid_retriever(argument)) {
                            reporter.status().print_line(
                                style.tag(ansi::Role::Error) + " " +
                                agentloop::retriever_values_message("/retriever", argument));
                        } else {
                            session.retriever = argument == "auto" ? std::string{} : argument;
                            // Persisted now, so a resume continues with this.
                            logger::save(session);
                            reporter.status().print_line(
                                style.tag(ansi::Role::Rag) + " retriever set to " +
                                (session.retriever.empty() ? "auto" : session.retriever));
                        }
                        break;
                    case ChatVerb::Rerank:
                        if (argument.empty()) {
                            reporter.status().print_line(
                                style.tag(ansi::Role::Rag) + " rerank: " +
                                (session.rerank.empty() ? "following each collection's rerank: pin"
                                                        : session.rerank) +
                                " -- /rerank <backend>|off|auto");
                        } else if (argument == "auto") {
                            session.rerank.clear();
                            logger::save(session);
                            reporter.status().print_line(style.tag(ansi::Role::Rag) +
                                                         " rerank follows the collection's pin");
                        } else if (!agentloop::valid_rerank(argument, config)) {
                            reporter.status().print_line(style.tag(ansi::Role::Error) +
                                                         " no backend named '" + argument + "'");
                        } else {
                            session.rerank = argument;
                            logger::save(session);
                            reporter.status().print_line(style.tag(ansi::Role::Rag) +
                                                         " rerank set to " + argument);
                        }
                        break;
                    case ChatVerb::Title:
                        session.custom_name = argument;
                        logger::save(session);
                        reporter.status().print_line(style.tag(ansi::Role::Apogee) + " renamed");
                        break;
                    case ChatVerb::Attach: {
                        if (argument.empty()) {
                            reporter.status().print_line(
                                style.tag(ansi::Role::Error) +
                                " /attach takes a file, a folder or a glob");
                            break;
                        }
                        (void)attached.attach(unquoted(argument), working_directory);
                        break;
                    }
                    case ChatVerb::Attachments: {
                        attached.settle();
                        const std::vector<std::string> lines = attached.describe();
                        if (lines.empty()) {
                            reporter.status().print_line(style.tag(ansi::Role::Apogee) +
                                                         " nothing attached -- /attach <path>");
                        }
                        for (const std::string& row : lines) {
                            reporter.status().print_line(style.tag(ansi::Role::Apogee) + " " + row);
                        }
                        break;
                    }
                    case ChatVerb::Detach: {
                        attached.settle();
                        const std::string name = unquoted(argument);
                        reporter.status().print_line(
                            attached.detach(name)
                                ? style.tag(ansi::Role::Apogee) + " detached " + name
                                : style.tag(ansi::Role::Error) + " nothing attached as '" + name +
                                      "' -- /attachments lists them");
                        break;
                    }
                    case ChatVerb::Compact: {
                        // A model call: what is reading an attachment
                        // settles first (26e).
                        attached.settle();
                        const std::string compactor = helper_backend(
                            harness.config(), harness::ModelRole::Utility, session.backend);
                        session.messages =
                            agentloop::compact_history(harness, session.messages, compactor);
                        ++session.compactions;
                        attached.after_compaction();
                        logger::save(session);
                        reporter.status().print_line(
                            style.tag(ansi::Role::Apogee) + " history compacted" +
                            (compactor == session.backend ? "" : " by " + compactor));
                        break;
                    }
                }
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
                continue;
            }

            // --- the turn ------------------------------------------------------
            // What the message mentions with `@` is attached as `/attach`
            // would, and what is still indexing settles first (26d).
            attach_mentions(attached, input, working_directory,
                            [&reporter, &style](const std::string& note) {
                                reporter.status().print_line(style.dim(note));
                            });
            attached.settle();
            if (decorate) {
                // One blank line between the question and whatever answers it
                // -- the thinking block, or the answer itself -- as there is
                // one after the answer.
                reporter.status().print_line("");
            }
            // Keystrokes typed while the model answers stay unseen, queued for
            // the next prompt, which shows them once -- instead of echoing into
            // the answer and then again at the prompt (2026-09-23).
            std::optional<platform::TypeaheadGuard> typeahead;
            if (reader->interactive()) {
                typeahead.emplace();
            }
            reporter.set_resting_label(
                is_base_model(harness, session.backend) ? "Thinking… · base model" : "Thinking…");
            run_chat_turn(
                harness, session, input, flags->tools ? offered : nullptr, selection.get(),
                flags->tools ? terminal_ask_fn(reporter.status(), style) : agentloop::AskFn{},
                ToolGate{permission, flags->tools ? terminal_confirm_fn(reporter.status(), style,
                                                                        config_path, approvals)
                                                  : agent::ConfirmFn{}},
                reporter,
                [&reporter, &style](const std::string& message) {
                    // Above the thinking block when side calls have opened
                    // one (26n): never inside its rows.
                    reporter.keep_line(style.tag(ansi::Role::Warning) + " " + message);
                },
                rag_settings, review_note, &attached, &recall);
            title.start_if_due(session);
            if (session.compactions != saved_compactions) {
                saved_compactions = session.compactions;
                harness.save_conversation(session.backend, session.chat_id,
                                          progress_sink(reporter));
            }
            typeahead.reset();
            if (decorate) {
                // One blank line between an answer and the next prompt, so
                // turns read as turns rather than one run of text.
                reporter.status().print_line("");
            }
        }

        if (reader->interrupted()) {
            title.cancel();
        }
        title.settle(session);
        logger::save(session);
        // The model's state too, on a clean exit only (26j): an interrupt
        // asked for nothing more, and the transcript is saved either way.
        if (!reader->interrupted()) {
            // The chat's summary for recall too (26l): only a clean exit.
            if (const std::string said = recall.finish({}); !said.empty()) {
                reporter.status().print_line(style.dim("[memory] " + said));
            }
            harness.save_conversation(session.backend, session.chat_id, progress_sink(reporter));
        }
        // Opt-in auto-capture on a CLEAN exit -- /exit, /quit, the end of the
        // input -- with the still-loaded model as the clerk. Never on an
        // interrupt: a user who hit Ctrl-C did not ask for a model call, and
        // the transcript is already saved either way. Best-effort: a failure
        // is a line, never a non-zero exit.
        if (config.knowledge.auto_capture && !reader->interrupted()) {
            capture_session({});
        }
        if (decorate) {
            reporter.status().print_line(style.tag(ansi::Role::Apogee) + " saved " +
                                         session.chat_id);
        }
    });
}

}  // namespace apogee::commands
