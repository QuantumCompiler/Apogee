#include "cli/chat_session.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <utility>

#include "agentloop/content.h"
#include "agentloop/loop.h"
#include "agentloop/member_call.h"
#include "agentloop/query_rewrite.h"
#include "agentloop/rag.h"
#include "agentloop/reasoning_summary.h"
#include "agentloop/rerank.h"
#include "agentloop/retriever.h"
#include "agentloop/review_context.h"
#include "agentloop/validate.h"
#include "ansi/ansi.h"
#include "backends/factory.h"
#include "backends/model_roster.h"
#include "backends/provider_cache.h"
#include "cli/acp_cmd.h"
#include "cli/chat_attachments.h"
#include "cli/chat_completer.h"
#include "cli/chat_history.h"
#include "cli/chat_play.h"
#include "cli/chat_recall.h"
#include "cli/chat_turn.h"
#include "cli/config_suites.h"
#include "cli/embed.h"
#include "cli/helpers.h"
#include "cli/permissions.h"
#include "cli/project_context.h"
#include "cli/provider_offer.h"
#include "cli/session_output.h"
#include "cli/suite_residency.h"
#include "cli/symphonies_cmd.h"
#include "contracts/assets.h"
#include "contracts/config.h"
#include "contracts/errors.h"
#include "contracts/layout.h"
#include "contracts/paths.h"
#include "contracts/utf8.h"
#include "harness/context_windows.h"
#include "harness/roles.h"
#include "knowledge/clerk.h"
#include "knowledge/record.h"
#include "logger/operational.h"
#include "machine/driver_input.h"
#include "machine/json_reporter.h"
#include "mcp/registry.h"
#include "modelstore/footprint.h"
#include "operations/backend_names.h"
#include "operations/knowledge_core.h"
#include "operations/suites.h"
#include "platform/platform.h"
#include "symphony/tools.h"
#include "tools/consult.h"
#include "tools/delegate.h"
#include "tools/git.h"
#include "tools/todo.h"
#include "views/ask_prompt.h"
#include "views/cli_reporter.h"
#include "views/input_gate.h"
#include "views/line_reader.h"
#include "views/terminal.h"

namespace apogee::commands {
namespace {

[[noreturn]] void fail_user(SessionMode mode, const std::string& message) {
    std::cerr << "apogee " << session_command(mode) << ": " << message << "\n";
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

/// `words` as a list: `a, b and c` with `between` and `last` the joins.
std::string join_words(const std::vector<std::string>& words, std::string_view between,
                       std::string_view last) {
    std::string joined;
    for (std::size_t i = 0; i < words.size(); ++i) {
        if (i > 0) {
            joined += i + 1 == words.size() ? last : between;
        }
        joined += words.at(i);
    }
    return joined;
}

/// `/effort`'s line (39a): the level, or `auto` with the budget in force,
/// and whether thinking is on -- a level spends nothing with it off.
std::string effort_line(const harness::Thinking& thinking) {
    std::string line = "effort: " + (thinking.effort.has_value()
                                         ? std::string{harness::to_string(*thinking.effort)}
                                         : std::string{"auto"});
    line += " (thinking " + std::string{harness::to_string(thinking.mode)};
    if (!thinking.effort.has_value() && thinking.budget.has_value()) {
        line += ", at most " + std::to_string(*thinking.budget) + " tokens";
    }
    return line + ")";
}

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

/// The lines an MCP startup said that stay -- its warnings: a server skipped,
/// one an ACP host's displaced (56b) -- kept for a driver, which reads no
/// stderr, and said as one `notice`. The registry's writer may also be
/// called from a client's reader thread, so the lines are kept under a lock,
/// and only until they are taken.
class McpWarnings {
public:
    void add(std::string_view line) {
        if (line.find("warning") == std::string_view::npos) {
            return;  // progress: transient, as the status line has it
        }
        const std::lock_guard lock{mutex_};
        if (open_) {
            lines_.emplace_back(line);
        }
    }

    /// Every line kept, one to a line; none are kept after.
    [[nodiscard]] std::string take() {
        const std::lock_guard lock{mutex_};
        open_ = false;
        std::string said;
        for (const std::string& line : lines_) {
            said += (said.empty() ? "" : "\n") + line;
        }
        lines_.clear();
        return said;
    }

private:
    std::mutex mutex_;
    std::vector<std::string> lines_;
    bool open_ = true;
};

/// What the conversation runs on, as the banner, the header and `/model` say
/// it: its backend, and the roster model pinned on it (33).
[[nodiscard]] std::string model_label(const logger::Session& session) {
    return session.model.empty() ? session.backend : session.backend + " (" + session.model + ")";
}

/// Moves the conversation's roster pin (33) to `pinned` on `backend` -- ""
/// for none: the entry it leaves runs the model its file names again, the one
/// it lands on runs `pinned`, each rebuilt as a suite switch rebuilds what it
/// re-pins (27d). One pin at most, and only on the conversation's backend --
/// the caller moves `session.backend` to `backend` once this succeeds. A
/// backend that cannot be rebuilt puts every pin back as it was and returns
/// why.
[[nodiscard]] std::string move_pin(harness::Harness& harness, logger::Session& session,
                                   const std::string& backend, const std::string& pinned,
                                   const backends::BuildOptions& options) {
    const bool moved = session.backend != backend || session.model != pinned;
    const bool leaving = moved && !session.model.empty();
    const bool landing = moved && !pinned.empty();
    std::vector<std::string> rebuilt;
    if (leaving) {
        (void)harness.pin_model(session.backend, {});
        rebuilt.push_back(session.backend);
    }
    if (landing) {
        (void)harness.pin_model(backend, pinned);
        if (rebuilt.empty() || rebuilt.front() != backend) {
            rebuilt.push_back(backend);
        }
    }
    if (rebuilt.empty()) {
        return {};
    }
    for (const backends::BackendStatus& status :
         backends::rebuild_providers(harness, rebuilt, options).statuses) {
        if (!status.constructed) {
            if (landing) {
                (void)harness.pin_model(backend, {});
            }
            if (leaving) {
                (void)harness.pin_model(session.backend, session.model);
            }
            (void)backends::rebuild_providers(harness, rebuilt, options);
            return status.name + " could not be rebuilt: " + status.reason;
        }
    }
    session.model = pinned;
    return {};
}

/// What a private chat says at its start (52d).
constexpr std::string_view kPrivateLine =
    "private: nothing of this chat is written -- no transcript, no recall";

/// What a session says, once, when its chat was deleted outside it while it
/// ran (55b): the save that found the file gone wrote nothing, and no later
/// one will.
[[nodiscard]] std::string deleted_line(std::string_view chat_id) {
    return "chat " + std::string{chat_id} +
           " was deleted while this session ran -- it stays deleted, and nothing more of it is "
           "saved";
}

/// A private chat's own folder (52d), in the system's temporary directory
/// and removed with the run: where its attachments are indexed, so nothing
/// of the conversation lands in the data directory. Inert for any other.
class PrivateFolder {
public:
    explicit PrivateFolder(bool used) {
        if (used) {
            path_ = std::filesystem::temp_directory_path() /
                    ("apogee-private-" + std::to_string(std::random_device{}()));
        }
    }

    PrivateFolder(const PrivateFolder&) = delete;
    PrivateFolder& operator=(const PrivateFolder&) = delete;
    PrivateFolder(PrivateFolder&&) = delete;
    PrivateFolder& operator=(PrivateFolder&&) = delete;

    ~PrivateFolder() {
        if (!path_.empty()) {
            std::error_code code;
            std::filesystem::remove_all(path_, code);
        }
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

/// A new chat closed before its first turn writes nothing (M28) -- and so
/// keeps nothing: the index its attachments were read into as they were
/// attached goes as the session ends, or it would lie under the data
/// directory with no chat to list or delete it by. A private chat's index is
/// its run's folder's, and goes with that.
class UnwrittenIndex {
public:
    explicit UnwrittenIndex(const logger::Session& session) : session_{session} {}

    UnwrittenIndex(const UnwrittenIndex&) = delete;
    UnwrittenIndex& operator=(const UnwrittenIndex&) = delete;
    UnwrittenIndex(UnwrittenIndex&&) = delete;
    UnwrittenIndex& operator=(UnwrittenIndex&&) = delete;

    ~UnwrittenIndex() {
        if (session_.unwritten() && !session_.never_written) {
            ChatAttachments::remove_index(session_.chat_id);
        }
    }

private:
    const logger::Session& session_;
};

/// What moving one of the session's run settings did (52d): whether it took,
/// the line saying so, and a warning beside it when there is one. The one
/// setter `/system`, `/temperature`, `/think`, `/effort` and `/model` and a
/// driver's `set` line each reach -- one behavior, said in the same words.
struct SettingSaid {
    bool ok = true;
    std::string line;
    std::string warning{};
};

/// The permission mode, said as `/permission-mode` says it.
[[nodiscard]] std::string permission_mode_line(PermissionMode mode) {
    return "permission mode: " + std::string{to_string(mode)} + " -- " +
           std::string{permission_mode_description(mode)};
}

/// The permission mode moved to `word` (39b): the words `--permission-mode`
/// takes, read by the gate at each call -- the one setter `/permission-mode`
/// and a driver's `set` line (56a) each reach. Bypass only in a session that
/// allows it: launched so, or given the yes `/permission-mode` asks for at a
/// keyboard first -- a driver has nobody there to say it.
[[nodiscard]] SettingSaid set_permission_mode(SessionApprovals& approvals, std::string_view word) {
    const std::optional<PermissionMode> chosen = permission_mode_from_string(word);
    if (!chosen.has_value()) {
        return SettingSaid{.ok = false,
                           .line = "not a permission mode: '" + std::string{word} +
                                   "' (manual, accept-edits, plan, dont-ask or bypass)"};
    }
    if (*chosen == PermissionMode::Bypass && !approvals.bypass_allowed) {
        return SettingSaid{.ok = false,
                           .line =
                               "bypass needs --allow-bypass (or --permission-mode bypass) at "
                               "launch -- permission mode: " +
                               std::string{to_string(approvals.mode.load())}};
    }
    approvals.mode = *chosen;
    return SettingSaid{.ok = true, .line = permission_mode_line(*chosen)};
}

/// The prompt is the transcript's first message, put there as the chat
/// opened: replaced in place -- matched by the prompt it held, so a
/// compaction's summary is never taken for it -- or added at the head, so
/// the next turn is sent under it. Until 52d `/system` moved only the
/// setting, and every request after kept the old prompt (found 2026-10-20).
[[nodiscard]] SettingSaid set_system_prompt(logger::Session& session, std::string_view text) {
    const std::string before = session.params.system_prompt;
    session.params.system_prompt = harness::valid_utf8(text);
    const std::string& now = session.params.system_prompt;
    std::vector<harness::ChatMessage>& messages = session.messages;
    const bool held = !before.empty() && !messages.empty() &&
                      messages.front().role == harness::Role::System &&
                      messages.front().content.plain_text() == before;
    if (held && now.empty()) {
        messages.erase(messages.begin());
    } else if (held) {
        messages.front() = harness::ChatMessage::system(now);
    } else if (!now.empty()) {
        messages.insert(messages.begin(), harness::ChatMessage::system(now));
    }
    return {.line = "system prompt updated"};
}

[[nodiscard]] SettingSaid set_temperature(logger::Session& session, const std::string& text) {
    try {
        session.params.temperature = std::stod(text);
    } catch (const std::exception&) {
        return {.ok = false, .line = "not a number: '" + text + "'"};
    }
    return {.line = "temperature: " + text};
}

[[nodiscard]] SettingSaid set_thinking(logger::Session& session, const std::string& mode) {
    const std::optional<harness::ThinkingMode> thinking = harness::thinking_mode_from_string(mode);
    if (!thinking.has_value()) {
        return {.ok = false, .line = "not a thinking mode: '" + mode + "' (on, off or auto)"};
    }
    session.params.thinking = thinking;
    return {.line = "thinking: " + std::string{harness::to_string(*thinking)}};
}

/// How hard the model thinks (39a): a level stands in for the session's
/// budget, and asking for one turns thinking on when it was off -- a level
/// with thinking off would do nothing. Empty says where it stands.
[[nodiscard]] SettingSaid set_effort(logger::Session& session, const harness::Harness& harness,
                                     const std::string& level) {
    const auto resolved = [&]() {
        return resolve_thinking(session.params.thinking, session.params.thinking_budget,
                                harness.config(), session.backend, session.params.effort);
    };
    if (level == "auto") {
        session.params.effort.reset();
    } else if (const std::optional<harness::Effort> effort = harness::effort_from_string(level);
               effort.has_value()) {
        session.params.effort = effort;
        session.params.thinking_budget.reset();
        if (resolved().off()) {
            session.params.thinking = harness::ThinkingMode::On;
        }
    } else if (!level.empty()) {
        return {.ok = false,
                .line = "not an effort level: '" + level + "' (" +
                        join_words(effort_words(), ", ", " or ") + ", or auto)"};
    }
    return {.line = effort_line(resolved()) +
                    (level.empty() ? " -- " + join_words(effort_words(), ", ", ", ") + ", or auto"
                                   : std::string{})};
}

/// What -m takes (33): a backend, an entry's model, or a roster's model on
/// its sole configured owner -- read against the file's view, so a pin is
/// never mistaken for its entry's own model. `settle` runs before a pin
/// moves: rebuilding a backend is a model call's business. A backend this
/// session is not running -- configured, but it did not start -- is refused
/// in M26's words, from what the session's start built (`built`), as `-m`
/// refuses it at launch (56a).
[[nodiscard]] SettingSaid switch_model(harness::Harness& harness, logger::Session& session,
                                       const harness::Config& config, const std::string& argument,
                                       const backends::BuildOptions& options,
                                       const backends::BuildResult& built, bool tools,
                                       const std::function<void()>& settle) {
    const SessionModel asked = resolve_session_model(config, argument);
    if (asked.backend.empty()) {
        return {
            .ok = false,
            .line = asked.refusal.empty() ? "no backend named '" + argument + "'" : asked.refusal};
    }
    // Asked of the harness, which knows a backend a rebuild has started
    // since: the router would answer for one it does not run from the
    // default, under this one's name, without a word.
    if (const std::vector<std::string> running = harness.provider_names();
        !std::ranges::binary_search(running, asked.backend)) {
        std::string refused = named_backend_refusal(
            config, built,
            configured_backend_key(config, argument).empty() ? asked.backend : argument);
        if (refused.empty()) {
            refused = "backend '" + asked.backend + "' is not running in this session";
        }
        return {.ok = false, .line = refused};
    }
    if (!asked.pinned.empty() || !session.model.empty()) {
        settle();
    }
    if (const std::string refused =
            move_pin(harness, session, asked.backend, asked.pinned, options);
        !refused.empty()) {
        return {.ok = false, .line = refused};
    }
    // Instant, and history carries over: every backend was constructed up
    // front and history is neutral IR.
    session.backend = asked.backend;
    harness.resume_conversation(session.backend, session.chat_id);
    const bool base = is_base_model(harness, session.backend);
    SettingSaid said{.line =
                         "switched to " +
                         (asked.pinned.empty() ? session.backend
                                               : asked.pinned + " -- " + roster_pin_note(asked)) +
                         (base ? " -- a base model" : "")};
    if (base && tools) {
        said.warning = base_model_tools_note(session.backend);
    }
    return said;
}

/// Detaches `name`, as `/detach` and a driver's `detach` line both do (52d):
/// what is still indexing settles first.
[[nodiscard]] SettingSaid detach_named(ChatAttachments& attached, const std::string& name) {
    attached.settle();
    if (attached.detach(name)) {
        return {.line = "detached " + name};
    }
    return {.ok = false, .line = "nothing attached as '" + name + "' -- /attachments lists them"};
}

/// Compacts the conversation now, as `/compact` and a driver's `compact`
/// line both do (52d): a model call, so what is reading an attachment
/// settles first (26e); the attachments told; the session saved -- each only
/// when something was folded (55b). Returns the line to say, `ok` false for
/// a compaction that failed.
[[nodiscard]] SettingSaid compact_attached(const harness::Harness& harness,
                                           logger::Session& session, ChatAttachments& attached) {
    attached.settle();
    const CompactionDone done = compact_conversation(harness, session);
    if (done.compacted()) {
        attached.after_compaction();
        logger::save(session);
    }
    return {.ok = done.outcome != agentloop::Compaction::Outcome::Failed,
            .line = compaction_line(done, session.backend)};
}

/// The models each owning entry runs from its type's roster (M13, 35), by
/// the entry's key -- what `/models` lists under it.
[[nodiscard]] std::map<std::string, std::vector<std::string>, std::less<>> roster_models(
    const harness::Config& config) {
    std::map<std::string, std::vector<std::string>, std::less<>> owned;
    for (const auto& [type, roster] : backends::known_rosters().rosters) {
        const auto owner =
            std::ranges::find_if(config.backends, [&wanted = type](const auto& entry) {
                return harness::to_string(entry.second.type) == wanted;
            });
        if (owner == config.backends.end()) {
            continue;
        }
        for (const backends::RosterModel& model : roster.models) {
            owned[owner->first].push_back(model.id);
        }
    }
    return owned;
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
/// -- the next turn, a slash command, a driver's `attach`, `compact` or model
/// `set` line (55a), the exit -- calls `settle()` first. That waits for a
/// title still in flight (seconds at most: a few dozen tokens with the
/// reasoning skipped) and records it.
/// A turn's span on the session's front-end (32c): the token that stops it,
/// and its end said however the turn leaves -- answered, cancelled, thrown.
struct TurnScope {
    explicit TurnScope(SessionOutput& output) : out{output}, token{output.begin_turn()} {}

    ~TurnScope() {
        out.end_turn();
    }

    TurnScope(const TurnScope&) = delete;
    TurnScope& operator=(const TurnScope&) = delete;
    TurnScope(TurnScope&&) = delete;
    TurnScope& operator=(TurnScope&&) = delete;

    SessionOutput& out;
    harness::CancellationToken token;
};

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
    /// Never for a private chat (55a): nothing would keep its title, and
    /// asking for one sends its questions to a second model, perhaps a cloud
    /// one.
    void start_if_due(const logger::Session& session) {
        if (asked_ || session.never_written || session.turns < 1 || !session.title.empty() ||
            !session.custom_name.empty()) {
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

std::string_view session_command(SessionMode mode) noexcept {
    return mode == SessionMode::Execute ? "execute" : "chat";
}

void bind_session_flags(CLI::App& command, const std::shared_ptr<SessionFlags>& flags,
                        SessionMode mode) {
    CLI::App* cmd = &command;

    cmd->add_option("-m,--model", flags->model, "Backend or model to use")
        ->type_name(kBackendOrModelValue);
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
    cmd->add_option("--graph", flags->graph,
                    "For the --attach given here: code builds a folder's code graph, off indexes "
                    "its chunks alone (default: the config's attachments.graph, else code)")
        ->type_name(words_value(harness::attachment_graph_method_names()))
        ->check([](const std::string& value) {
            return harness::attachment_graph_method_from_string(value).has_value()
                       ? std::string{}
                       : harness::attachment_graph_values_message("", value);
        });
    flags->temperature_option =
        cmd->add_option("-t,--temperature", flags->temperature, "Sampling temperature");
    flags->max_tokens_option =
        cmd->add_option("-n,--max-tokens", flags->max_tokens, "Maximum tokens per reply");
    cmd->add_option("--think", flags->think,
                    "Whether a reasoning model thinks first: on, off, or auto (per question)")
        ->check(CLI::IsMember({"on", "off", "auto"}));
    cmd->add_flag("--no-recall", flags->no_recall, "Recall no earlier chats in this run");
    cmd->add_flag("--private", flags->private_chat,
                  "Write nothing of this chat: no transcript, no recall, its attachments indexed "
                  "in a place gone with the run");
    cmd->add_option("--allow", flags->allow,
                    "Allow a tool for this chat without asking (repeatable, with --tools)")
        ->type_name(kToolValue)
        ->expected(1)
        ->allow_extra_args(false)
        ->multi_option_policy(CLI::MultiOptionPolicy::TakeAll);
    cmd->add_option("--deny", flags->deny,
                    "Refuse a tool or website for this chat without asking (repeatable)")
        ->type_name(kToolValue)
        ->expected(1)
        ->allow_extra_args(false)
        ->multi_option_policy(CLI::MultiOptionPolicy::TakeAll);
    cmd->add_option("--allow-host", flags->allow_hosts,
                    "Allow fetching from a website for this chat without asking (repeatable)")
        ->expected(1)
        ->allow_extra_args(false)
        ->multi_option_policy(CLI::MultiOptionPolicy::TakeAll);
    cmd->add_option("--permission-mode", flags->permission_mode,
                    "How much runs without asking: manual, accept-edits, plan, dont-ask or bypass")
        ->check(CLI::IsMember(permission_mode_words()));
    cmd->add_flag("--allow-bypass", flags->allow_bypass,
                  "Let /permission-mode and Shift+Tab choose bypass, where nothing asks");
    flags->think_budget_option =
        cmd->add_option("--think-budget", flags->think_budget,
                        "The most tokens a reasoning model may think for before it answers")
            ->check(CLI::Range(std::int64_t{0}, harness::kMaxThinkingBudget));
    cmd->add_option("--effort", flags->effort,
                    "How hard a reasoning model thinks: low, medium, high, xhigh or max")
        ->check(CLI::IsMember(effort_words()))
        ->excludes(flags->think_budget_option);
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
    // An execute session always runs under a suite (27s): its `--suite` has
    // no `off`.
    cmd->add_option("--suite", flags->suite,
                    mode == SessionMode::Execute
                        ? "Run under this suite -- its members answer for the roles it names, its "
                          "symphonies play on them (default: models.default_suite)"
                        : "Run under this suite -- its members answer for the roles it names -- "
                          "or off for none")
        ->type_name(mode == SessionMode::Execute ? kModelSuiteValue : kModelSuiteOrOffValue);
    // The Orchestrator (27t): execute's alone, so chat's surface is what it
    // was.
    if (mode == SessionMode::Execute) {
        cmd->add_flag("--orchestrate", flags->orchestrate,
                      "Let the suite's chat model play its symphonies on its own initiative, each "
                      "offered to it as a tool, on local members only (as the suite's "
                      "orchestrate: true does)");
    }
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
}

void run_session(const RootContext& context, const SessionFlags& session_flags,
                 const MachineBudgetSource& machine, SessionMode mode, SessionOutput* front) {
    const SessionFlags* flags = &session_flags;
    // A front-end given -- the full-screen shell's session view (32c) --
    // decorates as a terminal does: its banner, its spacing.
    const bool decorate = front != nullptr || platform::is_terminal(platform::StandardStream::Out);

    // The MCP servers an ACP host handed this session (56b), read first and
    // the handover removed: its env values are credentials, and they live in
    // this process from here, never on disk.
    std::vector<std::string> host_server_notes;
    const std::vector<mcp::ServerSpec> host_servers =
        flags->host_servers.empty() ? std::vector<mcp::ServerSpec>{}
                                    : host_mcp_servers(flags->host_servers, host_server_notes);

    harness::Config config;
    std::filesystem::path config_path;
    try {
        config_path = harness::resolve_config_path(context.config_path);
        config = harness::load_config(config_path);
    } catch (const harness::ConfigError& e) {
        fail_user(mode, e.what());
    }
    // The first-launch registration offer (28b): an interactive start with no
    // provider backend configured, once ever, offering what the last explicit
    // scan cached. It reads the cache and never probes -- this is a startup
    // path (`cli.no_provider_probes`).
    {
        OfferContext offer;
        // Asked on the terminal's stdin: never under the shell, which owns it.
        offer.interactive =
            front == nullptr && platform::is_terminal(platform::StandardStream::In) &&
            platform::is_terminal(platform::StandardStream::Out) && !stdin_is_piped();
        offer.machine = flags->output_format != OutputFormat::Text ||
                        flags->input_format.value_or(InputFormat::Text) != InputFormat::Text;
        offer.quiet = flags->quiet;
        offer.config_path = config_path;
        offer.cache_path = harness::provider_cache_path();
        offer.today = backends::cache_day(std::chrono::system_clock::now());
        try {
            if (offer_registration(offer, config, std::cin, std::cerr) ==
                OfferOutcome::Registered) {
                config = harness::load_config(config_path);
            }
        } catch (const std::exception& e) {
            fail_user(mode, e.what());
        }
    }
    // A suite named on the command line must exist before anything is
    // built for it (27d).
    if (!flags->suite.empty()) {
        harness::Config probe = config;
        if (const std::string refused = select_suite(probe, flags->suite); !refused.empty()) {
            fail_user(mode, "--suite: " + refused);
        }
    }
    // Execute opens on three rungs (41a): a suite -- named, or the default
    // -- else bare on one model, `-m`'s or `models.default`, suite-less
    // through the role resolver's own chain exactly as `chat` runs. Refused
    // before anything is built only with none of them and no session to
    // resume; `--orchestrate` is a suite's, refused without one.
    if (mode == SessionMode::Execute) {
        if (flags->suite == harness::kSuiteOff) {
            fail_user(mode,
                      "--suite off names no suite, and execute opens a session with one or bare "
                      "on a model -- leave --suite out, or 'apogee execute -m <backend>' opens "
                      "it bare");
        }
        const bool suite = !flags->suite.empty() || !config.models.default_suite.empty();
        // What it would open on bare, asked of the one resolver: `-m`, else
        // the chat role's rungs down to `models.default`.
        if (!suite && harness::resolve_chat_backend(config, flags->model).empty() &&
            flags->resume.empty() && !flags->cont) {
            fail_user(mode, execute_suite_refusal(config));
        }
        if (!suite && flags->orchestrate && flags->resume.empty() && !flags->cont) {
            fail_user(mode, execute_orchestrate_refusal(config));
        }
    }
    // `--graph` is how the launch's own attaches are indexed (27p): with
    // none, it would read as the chat's default, which is the config's.
    if (!flags->graph.empty() && flags->attach.empty()) {
        fail_user(mode,
                  "--graph applies to the --attach given with it -- for every attach, set "
                  "attachments.graph in the config; for one, /attach <path> --graph=code|off");
    }
    const std::optional<harness::AttachmentGraphMethod> launch_graph =
        harness::attachment_graph_method_from_string(flags->graph);

    // Every configured backend is constructed up front, which is what makes
    // /model an instant switch rather than a reconstruction -- and why
    // history has to be neutral IR rather than a vendor transcript.
    // A model that is neither a backend key nor an entry's `model:` may be a
    // vendor roster's (M13), resolved as `/model` resolves it (33): a sole
    // configured owner runs it for this chat, pinned on the Harness's view of
    // that entry -- the config file, and this session's view of it, never
    // change; two owners refuse naming both; none falls through to the
    // existing refusal.
    SessionFlags roster_adjusted;
    const SessionModel launched = resolve_session_model(config, flags->model);
    if (!launched.refusal.empty()) {
        fail_user(mode, launched.refusal);
    }
    if (!launched.pinned.empty()) {
        std::cerr << "model '" << launched.pinned << "' -- " << roster_pin_note(launched) << "\n";
    }
    // The router is asked by the entry's key whenever the name is none it
    // knows -- a roster's model, or `<backend>:<model>` naming the entry's
    // own (34), which pins nothing: handed on as typed, it found no such key
    // and answered from the default (M29).
    if (!launched.backend.empty() && configured_backend_key(config, flags->model).empty()) {
        roster_adjusted = *flags;
        roster_adjusted.model = launched.backend;
        flags = &roster_adjusted;
    }
    harness::Harness harness{config};
    if (!launched.pinned.empty()) {
        (void)harness.pin_model(launched.backend, launched.pinned);
    }
    backends::BuildOptions build_options;
    build_options.web_search = flags->search;
    build_options.config_path = config_path;
    const backends::BuildResult built = backends::build_providers(harness, build_options);

    // A backend named by `-m` answers under its own name or not at all
    // (M29): one that did not start, or a name the config defines nowhere, is
    // refused at launch with its own problem, as `complete -m` refuses it --
    // before the router could answer for it from the default. A later
    // request keeps the router's fallback.
    if (const std::string refused = named_backend_refusal(config, built, flags->model);
        !refused.empty()) {
        fail_user(mode, refused);
    }
    if (built.constructed_count() == 0) {
        std::string message = "no usable backend is configured";
        if (!built.skipped_summary().empty()) {
            message += " -- " + built.skipped_summary();
        }
        fail_user(mode, message);
    }

    // Where everything below writes and reads (32c): the terminal's status
    // line, line editor and answer view, built here as they always were --
    // or the front-end the caller gave, the shell's session view.
    std::unique_ptr<TerminalOutput> terminal_output;
    if (front == nullptr) {
        terminal_output = std::make_unique<TerminalOutput>(
            TerminalOutput::Options{.decorate = decorate,
                                    .verbose = flags->verbose,
                                    .no_color = flags->no_color,
                                    .markdown = !flags->raw && config.ui.markdown});
    }
    SessionOutput& out = front != nullptr ? *front : *terminal_output;
    const ansi::Style& style = out.style();
    // A model loading for any reason -- the chat's first turn, a
    // helper's first chore, an embedding -- says so on the spinner rather
    // than in silence (27e). Never into machine mode's stream. Every model
    // call that could load is over before the reporter goes: the title
    // and the attachments settle first, and both are declared after it.
    if (flags->output_format != OutputFormat::StreamJson) {
        harness.listen_for_loads(
            [&out](std::string_view backend, const harness::StatusEvent& event) {
                out.on_model_load(backend, event);
            });
    }

    // --- resume ---------------------------------------------------------
    logger::Session session;
    // What a resume could not carry over (55b): the terminal's `[resume]`
    // warning lines, and a driver's `notice`s once its `session` event is out
    // -- never stderr alone, which a driver does not draw. The suite's own
    // lines at launch -- its warnings and its admission (27e) -- are said the
    // same way, after them (M29).
    const bool machine_stream = flags->output_format == OutputFormat::StreamJson;
    std::vector<std::string> opening_notes;
    const auto resume_note = [&](const std::string& message) {
        if (machine_stream) {
            opening_notes.push_back("resume: " + message);
        } else {
            out.print_line(style.tag(ansi::Role::Warning) + " [resume] " + message);
        }
    };
    const auto suite_note = [&](ansi::Role role, const std::string& line) {
        if (machine_stream) {
            opening_notes.push_back(line);
        } else {
            out.print_line(style.tag(role) + " " + line);
        }
    };
    logger::KnownDependencies known;
    known.backends = config.backend_names();
    known.suites = config.suite_names();
    known.check_suites = true;

    if (flags->private_chat && (!flags->resume.empty() || flags->cont)) {
        fail_user(mode,
                  "--private starts a chat that is never written -- it cannot resume one "
                  "already saved");
    }
    if (!flags->resume.empty() || flags->cont) {
        try {
            logger::LoadedSession loaded = flags->cont ? [&]() {
                const std::optional<logger::Session> recent = logger::most_recent();
                if (!recent.has_value()) {
                    fail_user(mode, "no saved conversation to continue");
                }
                return logger::load(recent->chat_id, known);
            }()
                                                       : logger::load(flags->resume, known);

            session = std::move(loaded.session);
            // Resume degrades, never fails: every problem is a note.
            for (const logger::ResumeWarning& warning : loaded.warnings) {
                resume_note(warning.message);
                if (warning.kind == logger::WarningKind::BackendMissing) {
                    session.backend.clear();  // fall back to the default
                }
            }
        } catch (const CLI::RuntimeError&) {
            throw;
        } catch (const std::exception& e) {
            fail_user(mode, e.what());
        }
    } else {
        session.chat_id = logger::new_chat_id();
        // Written once it has a turn (M28): one closed before leaves nothing.
        session.new_chat = true;
    }
    // A private chat (52d) is never written -- `save` does nothing for it --
    // and never summarised.
    session.never_written = flags->private_chat;
    session.private_chat = session.private_chat || flags->private_chat;
    // The backend a resumed chat was left on, which a roster pin rides (33).
    const std::string saved_backend = session.backend;

    // The chat's suite (27d): the flag, else what the chat last had, else
    // the config's default. Chosen before the chat's backend, which the
    // suite's chat member answers for.
    std::optional<std::string> chosen_suite;
    if (!flags->suite.empty()) {
        harness::Config probe = config;
        (void)select_suite(probe, flags->suite);  // validated above
        chosen_suite = probe.models.default_suite;
    } else if (session.suite.has_value() && session.suite->empty()) {
        // A session that turned its suite off resumes off -- in chat. An
        // execute session runs under one, so it takes the default (27s).
        if (mode == SessionMode::Chat) {
            chosen_suite = std::string{};
        }
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
            suite_note(ansi::Role::Warning, line);
        }
    }
    // A resumed session that had no suite, under a config with no default,
    // runs bare (41a) -- unless it was asked to orchestrate, which only a
    // suite can.
    if (mode == SessionMode::Execute && config.models.default_suite.empty() && flags->orchestrate) {
        fail_user(mode, execute_orchestrate_refusal(config));
    }
    // A member naming nothing is refused before a turn could fall back to
    // models.default and answer from a model nobody chose.
    if (const std::string refused = validate_active_suite(config); !refused.empty()) {
        fail_user(mode, refused);
    }
    // Admission (27e): the set the session runs under, priced against
    // this machine. Over budget it is refused here, before anything is
    // loaded or a turn could start, unless --force says to go ahead.
    std::optional<models::SuiteFootprint> footprint;
    if (!config.models.default_suite.empty()) {
        footprint = price_suite(config, config.models.default_suite, machine);
        if (const std::string refused = admission_refusal(*footprint, "--force");
            !refused.empty() && !flags->force) {
            fail_user(mode, refused);
        }
    } else if (flags->warm) {
        fail_user(mode,
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
    // A bare execute (41a) with nothing to open on: a resumed session that
    // named no backend, under a config with no default.
    if (mode == SessionMode::Execute && model.empty()) {
        fail_user(mode, execute_suite_refusal(config));
    }
    session.backend = model;
    // The chat's roster pin (33): the launch's; else, unless -m named a
    // backend, the one the chat was left on -- pinned again when the chat
    // lands back on its backend, and said as the launch's is. Anything else
    // runs its backend's own model.
    if (!launched.pinned.empty()) {
        session.model = launched.pinned;
    } else if (!flags->model.empty() || session.backend != saved_backend) {
        session.model.clear();
    } else if (!session.model.empty()) {
        // Said as it was pinned: a roster's model as the roster's, any
        // other as `<backend>:<model>`'s (34).
        const std::string saved_model = std::exchange(session.model, {});
        SessionModel resumed = resolve_session_model(config, saved_model);
        if (resumed.backend != session.backend || resumed.pinned != saved_model) {
            resumed = SessionModel{.backend = session.backend, .pinned = saved_model};
        }
        if (const std::string refused =
                move_pin(harness, session, resumed.backend, resumed.pinned, build_options);
            refused.empty()) {
            std::cerr << "model '" << resumed.pinned << "' -- " << roster_pin_note(resumed) << "\n";
        } else {
            resume_note(resumed.pinned + " could not be pinned again (" + refused +
                        ") -- the chat runs on " + resumed.backend + "'s own model");
        }
    }
    // A resumed chat starts from its saved state when its backend kept
    // one (26j); a new one is named so it can be saved.
    harness.resume_conversation(session.backend, session.chat_id);
    // Saved again after the turn that reads a compacted history: the
    // state from before no longer matches it.
    int saved_compactions = session.compactions;
    // Recall across chats (26l): on unless the config or the flag says
    // not; a chat a process that died left due is summarised now.
    ChatRecall recall{harness, config, session,
                      config.memory.recall && !flags->no_recall && !flags->private_chat};
    // Where the chat's attachments are indexed: its own index beside its
    // file -- or, for a private chat (52d), a folder of the run's own,
    // removed as it ends, so nothing of the conversation stays in the data
    // directory.
    const PrivateFolder private_folder{flags->private_chat};
    // A new chat that ends with no turn was never written (M28), so its index
    // goes too -- after everything reading it, declared below, has gone.
    const UnwrittenIndex unwritten_index{session};
    const std::filesystem::path attachment_index =
        flags->private_chat ? private_folder.path() / "index.db"
                            : ChatAttachments::index_for(session.chat_id);
    // The backend's saved state of the conversation (26j) -- none for a
    // private chat, whose words go nowhere, nor for a chat not written.
    const auto save_state = [&harness, &session](const harness::StatusSink& on_status) {
        if (!session.unwritten()) {
            harness.save_conversation(session.backend, session.chat_id, on_status);
        }
    };
    for (const std::string& line : recall.catch_up({})) {
        out.print_line(style.dim("[memory] " + line));
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
    // A level and a budget are one control (39a): the one given at launch
    // replaces whichever the resumed chat held.
    if (flags->think_budget_option->count() > 0) {
        session.params.thinking_budget = flags->think_budget;
        session.params.effort.reset();
    }
    if (!flags->effort.empty()) {
        session.params.effort = harness::effort_from_string(flags->effort);
        session.params.thinking_budget.reset();
    }
    // A system prompt enters history and the session file as typed, so as
    // text whatever bytes the shell handed in.
    if (!flags->system_prompt.empty()) {
        session.params.system_prompt = harness::valid_utf8(flags->system_prompt);
    }

    if (session.messages.empty() && !session.params.system_prompt.empty()) {
        session.messages.push_back(harness::ChatMessage::system(session.params.system_prompt));
    }

    logger::log(logger::Level::Info, std::string{session_command(mode)},
                "session " + session.chat_id + " on backend " + model);

    // --- the review context, from flags -----------------------------------
    if (flags->fetch && flags->no_fetch) {
        fail_user(mode, "--fetch and --no-fetch are mutually exclusive");
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
    // What the servers' startup said that stays -- a server skipped, one a
    // host's displaced (56b) -- kept for a driver, which reads no stderr:
    // one `notice` after its `session` event, every line of it. The status
    // line has them as before.
    const auto mcp_warnings = std::make_shared<McpWarnings>();
    const auto mcp_status = [status = out.mcp_status(), mcp_warnings,
                             keep = machine_stream](std::string_view line) {
        if (status) {
            status(line);
        }
        if (keep) {
            mcp_warnings->add(line);
        }
    };
    if (!flags->tools && !host_servers.empty()) {
        host_server_notes.emplace_back(
            "the host's MCP servers are not connected: this session runs without --tools");
    }
    for (const std::string& note : host_server_notes) {
        mcp_status("[mcp] warning: " + note);
    }
    if (flags->tools) {
        registry = make_built_in_tools(BuiltInToolOptions{
            .config = &config,
            .harness = &harness,
            .review = *live_review,
            .live_review = live_review,
            .mcp = mcp_registry,
            .mcp_status = mcp_status,
            // A server's stderr never reaches the terminal unless asked
            // for: with --verbose it is the raw stream, otherwise the
            // bounded tail rides the connect-failure message.
            .mcp_server_log = flags->verbose ? mcp::StderrTail::Sink{[](std::string_view bytes) {
                std::cerr << bytes << std::flush;
            }}
                                             : mcp::StderrTail::Sink{},
            .session_mcp_servers = host_servers});
        // The session todo (41d): the plan, kept by the model, drawn by
        // every front end; its board is this session's, set below.
        tools::register_todo_tool(registry);
        // The delegate (41e): a bounded sub-session, re-entering the loop
        // under this session's gate; its context is each turn's.
        tools::register_delegate_tool(registry);
    }
    if (std::string said = mcp_warnings->take(); !said.empty()) {
        opening_notes.push_back(std::move(said));
    }
    // What the chat's backend is offered: every registered tool, or the
    // toolset the active suite pins on that backend (27d), and the
    // consult tool when the suite designates members (27f) -- recomputed
    // only when a switch moves the pin or the members on offer.
    std::optional<agent::ToolRegistry> pinned_registry;
    std::optional<std::vector<std::string>> offered_pin;
    std::string offered_consult;
    const agent::ToolRegistry* offered = &registry;
    // The conversation's consults, counted per turn (27f).
    const auto member_calls = std::make_shared<agentloop::MemberCalls>(harness);
    // Past a dozen and a half tools, each turn offers the ones its
    // question needs (26g); one selection for the whole conversation.
    std::unique_ptr<agentloop::ToolSelection> selection;
    // The `graph` toolset read through the chat's code graph while a
    // graphed folder is attached (27o): the scoped set in place of the
    // configured graphs', so a model walks the attached code instead of
    // inventing paths -- in when the first graphed folder settles, out
    // when the last is detached, looked at before every turn.
    std::optional<AttachmentGraphScope> offered_scope;
    std::optional<agent::ToolRegistry> scoped_registry;
    const auto base_tools = [&]() -> const agent::ToolRegistry& {
        return scoped_registry.has_value() ? *scoped_registry : registry;
    };
    // The Orchestrator (27t): an execute session that orchestrates --
    // `--orchestrate`, or the suite's own `orchestrate: true` -- offers its
    // root the symphonies as tools, each one it can play here; re-offered
    // when a switch moves the suite or the conversation. Off, none exists.
    std::optional<symphony::OrchestraOffer> offered_orchestra;
    const auto orchestrates = [&]() {
        return mode == SessionMode::Execute &&
               symphony::orchestrating(harness.config(), flags->orchestrate);
    };
    const auto orchestra_now = [&]() -> std::optional<symphony::OrchestraOffer> {
        if (!orchestrates()) {
            return std::nullopt;
        }
        return symphony::orchestra_offer(harness, session_catalog(harness.config(), config_path),
                                         session.backend);
    };
    // The turn's tools: `--tools`, or the symphonies an orchestrating
    // session offers -- its own consent to the model starting a play.
    const auto tools_on = [&]() {
        return flags->tools ||
               (offered_orchestra.has_value() && !offered_orchestra->offered.empty());
    };
    const auto offer_tools = [&]() {
        offered_pin = flags->tools ? harness::suite_pins(config, session.backend).toolset
                                   : std::optional<std::vector<std::string>>{};
        const tools::ConsultOffer consult =
            flags->tools ? tools::consult_offer(harness) : tools::ConsultOffer{};
        if (consult.description != offered_consult) {
            for (const std::string& note : consult.notes) {
                out.print_line(style.tag(ansi::Role::Warning) + " " + note);
            }
        }
        offered_consult = consult.description;
        std::optional<symphony::OrchestraOffer> orchestra;
        if (offered_pin.has_value() || !consult.description.empty() || orchestrates()) {
            // The consult tool joins after the pin: a suite's
            // `consultable:` is its own switch, never a toolset's -- and so
            // is orchestration (27t).
            pinned_registry =
                offered_pin.has_value() ? apply_toolset(base_tools(), *offered_pin) : base_tools();
            if (flags->tools) {
                (void)tools::register_consult_tool(*pinned_registry, harness, member_calls);
            }
            if (orchestrates()) {
                orchestra = symphony::register_play_tools(
                    *pinned_registry, harness, session_catalog(harness.config(), config_path),
                    symphony::PlayToolContext{.calls = member_calls, .conversation = [&session]() {
                                                  return session.backend;
                                              }});
            }
            offered = &*pinned_registry;
        } else {
            pinned_registry.reset();
            offered = &base_tools();
        }
        if (orchestra.has_value() && orchestra != offered_orchestra) {
            // Each symphony not offered, said once with why: one a member
            // or a cap keeps out, always; one whose definition does --
            // it takes an image -- when asked for.
            for (const symphony::Withheld& withheld : orchestra->withheld) {
                const std::string line =
                    "orchestrate: not offering " + withheld.symphony + " -- " + withheld.reason;
                if (!withheld.structural) {
                    out.print_line(style.tag(ansi::Role::Warning) + " " + line);
                } else if (flags->verbose) {
                    out.print_line(style.tag(ansi::Role::Apogee) + " " + line);
                }
            }
            if (orchestra->offered.empty()) {
                out.print_line(
                    style.tag(ansi::Role::Warning) +
                    " orchestrate: no symphony can be offered as a tool -- the model answers "
                    "without plays");
            }
        }
        offered_orchestra = std::move(orchestra);
        std::string ranked_by;
        selection = make_tool_selection(harness, config, *offered, config_path, ranked_by);
        if (selection != nullptr && flags->verbose) {
            out.print_line("[tools] " + std::to_string(offered->size()) +
                           " registered: each turn offers the ones its question "
                           "needs, ranked by " +
                           ranked_by);
        }
    };
    const auto reoffer_tools = [&]() {
        if ((flags->tools && (harness::suite_pins(config, session.backend).toolset != offered_pin ||
                              tools::consult_offer(harness).description != offered_consult)) ||
            orchestra_now() != offered_orchestra) {
            offer_tools();
        }
    };
    if (flags->tools || orchestrates()) {
        offer_tools();
    }
    const auto rescope_tools = [&](const ChatAttachments& attachments) {
        if (!flags->tools) {
            return;
        }
        std::optional<AttachmentGraphScope> scope = attachments.graph_scope();
        if (scope == offered_scope) {
            return;
        }
        offered_scope = std::move(scope);
        if (offered_scope.has_value()) {
            scoped_registry = attachment_graph_tools(registry, *offered_scope);
        } else {
            scoped_registry.reset();
        }
        offer_tools();
    };
    // The gate: config levels, then what the user answers for this
    // session. The prompt half is chosen per surface below.
    const auto approvals = std::make_shared<SessionApprovals>();
    // The mode the session starts in (39b); bypass only when allowed at
    // launch, as Claude Code allows it.
    approvals->bypass_allowed = flags->allow_bypass || flags->permission_mode == "bypass";
    if (const std::optional<PermissionMode> start =
            permission_mode_from_string(flags->permission_mode);
        start.has_value()) {
        approvals->mode = *start;
    }
    const agent::PermissionChecker permission = make_permission_checker(config, approvals);
    // The session's answers, given at launch (26o): exactly the `session`
    // answer, given before the prompt would ask.
    const std::vector<std::string> gated =
        flags->tools ? gated_tools(registry) : std::vector<std::string>{};
    const std::vector<std::string> edits =
        flags->tools ? edit_tools(registry) : std::vector<std::string>{};
    if (const PermissionPresets presets{
            .allow = flags->allow, .deny = flags->deny, .allow_hosts = flags->allow_hosts};
        !presets.empty()) {
        if (!flags->tools) {
            fail_user(mode,
                      "--allow, --deny and --allow-host need --tools: without tools, nothing "
                      "is asked");
        }
        if (const std::string refused = seed_approvals(*approvals, presets, gated);
            !refused.empty()) {
            fail_user(mode, refused);
        }
    }

    // Execute's project opening (41b): opened in a project -- a git
    // repository, or a folder holding a context file -- it reads AGENTS.md
    // (else CLAUDE.md) into every request as standing context, and attaches
    // the folder through the one attach driver unless `--attach` says
    // otherwise. Chat opens as it always has.
    ProjectOpening project;
    if (mode == SessionMode::Execute) {
        std::error_code here_error;
        project = read_project_opening(std::filesystem::current_path(here_error));
    }
    // The coder persona (41c): a project session's way of working, carried
    // ahead of the project's own file so the file, closer to the
    // conversation, wins where they differ. `-s` -- given now, or saved with
    // a resumed session -- replaces it whole.
    std::vector<std::string> standing;
    std::optional<std::string> persona_line;
    if (mode == SessionMode::Execute && project.project) {
        if (session.params.system_prompt.empty()) {
            standing.emplace_back(harness::find_bundled_persona("coder")->text);
            persona_line =
                "persona: coder, the shipped coding prompt, in every request -- -s "
                "replaces it";
        } else {
            persona_line = "persona: your -s prompt, in place of the coder persona";
        }
    }
    if (project.context.has_value()) {
        standing.push_back(project_context_note(*project.context));
    }
    const std::vector<std::string> launch_attaches =
        mode == SessionMode::Execute ? opening_attaches(flags->attach, project.project)
                                     : flags->attach;

    if (decorate) {
        // A base model is said in the banner and the spinner, all session
        // (26r) -- never on an answer. Under execute the banner counts what
        // the session can play (27s).
        out.print_line(
            style.tag(ansi::Role::Apogee) + " " +
            session_banner(mode, model_label(session), is_base_model(harness, model),
                           config.models.default_suite, forced,
                           mode == SessionMode::Execute
                               ? session_catalog(harness.config(), config_path).definitions.size()
                               : 0,
                           offered_orchestra.has_value()
                               ? std::optional<std::size_t>{offered_orchestra->offered.size()}
                               : std::nullopt,
                           session.chat_id));
    }
    // The same facts as the banner, for a front-end that keeps them in view
    // apart from the transcript (32c) -- said again as /model and /suite move
    // them.
    const auto refresh_header = [&]() {
        out.set_header(session_banner(
            mode, model_label(session), is_base_model(harness, session.backend),
            config.models.default_suite, !config.models.default_suite.empty() && forced,
            mode == SessionMode::Execute
                ? session_catalog(harness.config(), config_path).definitions.size()
                : 0,
            offered_orchestra.has_value()
                ? std::optional<std::size_t>{offered_orchestra->offered.size()}
                : std::nullopt,
            session.chat_id));
    };
    refresh_header();
    // What the suite takes of this machine, stated where the session
    // starts -- on a pipe too, on stderr; to a driver, a `notice` after its
    // `session` event (M29) -- and, asked for, its members loaded now on the
    // busy line rather than at their first use (27e).
    if (footprint.has_value()) {
        suite_note(ansi::Role::Apogee, admission_line(*footprint, forced));
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
            suite_note(ansi::Role::Warning, line);
        }
    }
    // A private chat says so at its start (52d), on a terminal, a pipe and
    // the shell alike; machine mode says it in its own stream.
    if (flags->private_chat && flags->output_format != OutputFormat::StreamJson) {
        out.print_line(style.tag(ansi::Role::Apogee) + " " + std::string{kPrivateLine});
    }
    // Tools withheld from a base model are said once, at the start, on a
    // terminal and a pipe alike -- never into machine mode's stream.
    if (tools_on() && flags->output_format != OutputFormat::StreamJson &&
        is_base_model(harness, model)) {
        out.print_line(style.tag(ansi::Role::Warning) + " " + base_model_tools_note(model));
    }
    if (decorate) {
        // The banner stands apart from the first prompt.
        out.print_line("");
    }

    // The chat as the session opened it (M28). Its close writes it only when
    // the session changed it: a resumed chat left with no new turn and
    // nothing moved keeps its file as it was -- and a new chat with no turn
    // is written by nothing at all, `save` refusing it.
    const std::string opened = logger::serialize(session);
    const auto save_if_changed = [&session, &opened]() {
        if (logger::serialize(session) != opened) {
            logger::save(session);
        }
    };

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
        fail_user(mode, std::string{"--input-format "} + std::string{to_string(input_format)} +
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
        // What this session can do, before its first turn (28d): every
        // inbound line type, tools as wired, and `ask_user` and the
        // permission prompt reaching the driver exactly when tools do.
        // Each accepted `user` line is a numbered turn (28f), counted past
        // the turns a resumed conversation already holds -- the one count
        // `chats show` numbers them by (55b), which the load reconciled with
        // the answers the file holds. The chat it writes is named (M28) --
        // none for a private one -- and the suite it runs under, fresh or
        // resumed, with whether an execute session orchestrates (M29).
        std::int64_t next_turn = static_cast<std::int64_t>(logger::counted_turns(session)) + 1;
        machine_reporter.begin_session(
            session.backend,
            MachineCapabilities{
                .accepts = {machine_inbound_types().begin(), machine_inbound_types().end()},
                .tools = tools_on(),
                .ask = flags->tools},
            next_turn, flags->private_chat, session.chat_id, config.models.default_suite,
            mode == SessionMode::Execute ? std::optional<bool>{orchestrates()} : std::nullopt);
        // A chat deleted outside while this session runs stays deleted
        // (55b): the save that finds it gone writes nothing more, and the
        // driver is told once.
        bool deleted_said = false;
        const auto say_deleted = [&session, &machine_reporter, &deleted_said]() {
            if (session.deleted && !std::exchange(deleted_said, true)) {
                machine_reporter.on_notice(deleted_line(session.chat_id));
            }
        };
        // The todo's board (41d): this conversation's list, each change an
        // event; a resumed list is said as the session opens.
        const tools::TodoScope todo_scope{tools::TodoBoard{
            .list = &session.todo, .changed = [&machine_reporter](const harness::TodoList& todo) {
                machine_reporter.on_todo(todo);
            }}};
        if (!session.todo.empty()) {
            machine_reporter.on_todo(session.todo);
        }
        // What the resume could not carry over (55b), then the suite's lines
        // (M29), said where the driver draws them: one `notice` each, before
        // anything the session reads.
        for (const std::string& note : opening_notes) {
            machine_reporter.on_notice(note);
        }
        // The driver's lines, read while a turn runs, so a `cancel` reaches
        // it (28f); the loop, a question and the permission prompt all take
        // their lines from this one reader.
        DriverInput driver{std::cin};

        const auto machine_notice = [](const std::string& message) {
            // stdout carries ONLY protocol events, so a diagnostic goes to
            // stderr -- the same discipline Apogee demands of the vendor
            // CLIs it drives, having been the consumer on the other side.
            std::cerr << "apogee: " << message << "\n";
        };

        const agentloop::AskFn driver_ask =
            flags->tools ? make_driver_ask_fn(machine_reporter, driver) : agentloop::AskFn{};

        BackgroundTitle title{harness, [&machine_reporter](std::string_view line) {
                                  machine_reporter.on_progress(line);
                              }};
        // A driver has no terminal to be asked on, so a folder over the
        // size guard is refused, as on any pipe (26d).
        std::error_code cwd_error;
        const std::filesystem::path working_directory = std::filesystem::current_path(cwd_error);
        ChatAttachments attached{
            harness, session, attachment_index,
            ChatAttachments::Hooks{
                .say = [&machine_reporter](const std::string& line,
                                           bool /*warning*/) { machine_reporter.on_notice(line); },
                .progress =
                    [&machine_reporter](const std::string& line) {
                        if (!line.empty()) {
                            machine_reporter.on_progress(line);
                        }
                    },
                .confirm_large = {},
                .save = true,
                // A folder of code builds its graph (27n), unless the
                // config or the attach says otherwise (27p).
                .built_in_graph = harness::AttachmentGraphMethod::Code}};
        // `--image` is an attachment like any other (26e): read as it is
        // with the first message by a model that can, described for one
        // that cannot.
        for (const std::string& spec : flags->images) {
            (void)attached.attach(spec, working_directory);
        }
        // The project opening said as the banner says it (41b), before the
        // folder's own attach lines.
        if (flags->private_chat) {
            machine_reporter.on_notice(std::string{kPrivateLine});
        }
        if (persona_line.has_value()) {
            machine_reporter.on_notice(*persona_line);
        }
        if (project.context.has_value()) {
            machine_reporter.on_notice(project_context_line(*project.context));
        }
        if (!project.unreadable.empty()) {
            machine_reporter.on_notice("project context: " + project.unreadable);
        }
        for (const std::string& spec : launch_attaches) {
            (void)attached.attach(spec, working_directory, attached.graph_method(launch_graph));
        }
        bool first_line = true;
        while (const std::optional<DriverLine> got = driver.next_line()) {
            const DriverMessage message = parse_driver_line(got->text);
            const bool opening = std::exchange(first_line, false);
            if (message.kind == DriverMessage::Kind::Hello) {
                // A driver introducing itself (28d): recorded on the first
                // line, harmless anywhere -- never a session-ending error.
                if (opening) {
                    logger::log(logger::Level::Info, std::string{session_command(mode)},
                                describe_hello(message));
                } else {
                    machine_notice("a hello after the first line is ignored");
                }
                continue;
            }
            if (message.kind == DriverMessage::Kind::Attach && !message.text.empty()) {
                // The line's own `graph`, as `/attach`'s `--graph` (27p):
                // a word outside the set attaches nothing, and says why.
                std::optional<harness::AttachmentGraphMethod> graph;
                if (!message.graph.empty()) {
                    graph = harness::attachment_graph_method_from_string(message.graph);
                    if (!graph.has_value()) {
                        machine_reporter.on_notice(
                            message.text + " not attached: " +
                            harness::attachment_graph_values_message("graph", message.graph));
                        continue;
                    }
                }
                // Reading it may reach a model -- the embedder, the vision
                // helper -- so a title in flight settles first, as before
                // the REPL's `/attach` (55a).
                title.settle(session);
                (void)attached.attach(message.text, working_directory,
                                      attached.graph_method(graph));
                say_deleted();
                continue;
            }
            // The session's own doors (52d), each its slash command's core:
            // said as notices, effective from the next turn.
            if (message.kind == DriverMessage::Kind::Detach) {
                machine_reporter.on_notice(detach_named(attached, message.text).line);
                say_deleted();
                continue;
            }
            if (message.kind == DriverMessage::Kind::Compact) {
                // A model call: the title in flight settles first, as the
                // REPL's slash dispatch does (55a) -- one call at a time.
                title.settle(session);
                machine_reporter.on_notice(compact_attached(harness, session, attached).line);
                say_deleted();
                continue;
            }
            if (message.kind == DriverMessage::Kind::Set) {
                const std::string keys =
                    "model, system, think, effort, temperature or permission_mode";
                if (!message.refused.empty()) {
                    std::string named;
                    for (const std::string& key : message.refused) {
                        named += (named.empty() ? "'" : ", '") + key + "'";
                    }
                    machine_reporter.on_notice(
                        "set: " + named + (message.refused.size() == 1 ? " is" : " are") +
                        " not a setting a session moves, as text -- " + keys + "; nothing was set");
                    continue;
                }
                if (message.settings.empty()) {
                    machine_reporter.on_notice("set: nothing to set -- " + keys);
                    continue;
                }
                for (const auto& [key, value] : message.settings) {
                    SettingSaid said;
                    if (key == "model") {
                        // A rebuild is a model call's business: the title in
                        // flight settles first, as before `/model` (55a).
                        title.settle(session);
                        said = switch_model(harness, session, config, value, build_options, built,
                                            flags->tools, [&attached]() { attached.settle(); });
                        if (said.ok) {
                            reoffer_tools();
                        }
                    } else if (key == "system") {
                        said = set_system_prompt(session, value);
                    } else if (key == "think") {
                        said = set_thinking(session, value);
                    } else if (key == "effort") {
                        said = set_effort(session, harness, value);
                    } else if (key == "permission_mode") {
                        said = set_permission_mode(*approvals, value);
                    } else {
                        said = set_temperature(session, value);
                    }
                    machine_reporter.on_notice(said.ok ? said.line + " -- from the next turn"
                                                       : "set " + key + ": " + said.line);
                    if (!said.warning.empty()) {
                        machine_reporter.on_notice(said.warning);
                    }
                }
                continue;
            }
            if (message.kind != DriverMessage::Kind::User || message.text.empty()) {
                // Unknown types are ignored rather than fatal: the same
                // tolerance this protocol asks of its own drivers. An empty
                // `user` line opens no turn.
                driver.end_turn(got->sequence);
                continue;
            }
            // The turn, numbered (28f): every event until its `result` or
            // `error` carries it, and a `cancel` until then ends it the way
            // Ctrl-C would -- the half-turn rolled back, the session left as
            // the last finished turn left it.
            const std::int64_t turn = next_turn++;
            machine_reporter.begin_turn(turn);
            const logger::Session before = session;
            const auto close_turn = [&machine_reporter, &driver, &got]() {
                machine_reporter.end_turn();
                driver.end_turn(got->sequence);
            };
            const auto cancelled_result = [&]() {
                session = before;
                logger::save(session);
                say_deleted();
                harness::ChatResponse response;
                response.message = harness::ChatMessage::assistant(machine_reporter.turn_text());
                response.model = session.backend;
                response.finish_reason = harness::FinishReason::Cancelled;
                machine_reporter.emit_result(response);
            };
            title.settle(session);
            // An execute session plays a `user` line that is a `/play`
            // command, as its REPL does (27s): found through the one table,
            // whose execute rows alone offer it, its stages the `tool_status`
            // lines side calls already are, its output the turn's answer and
            // `result` -- or an `error` saying why not. Any other line, slash
            // or not -- and every line of a chat -- is a prompt.
            const std::optional<SlashCommand> command = parse_slash(message.text);
            const ChatCommandSpec* spec =
                command.has_value() ? find_chat_command(command->name, mode) : nullptr;
            if (spec != nullptr && spec->id == ChatVerb::Play) {
                attached.settle();
                const PlayTurnResult played =
                    run_play_turn(harness, session, message.text,
                                  prepare_play(harness.config(), config_path,
                                               parse_play_argument(command->argument)),
                                  *member_calls, machine_reporter, &recall, got->turn);
                say_deleted();
                if (played.cancelled) {
                    machine_notice(played.failure);
                    cancelled_result();
                    close_turn();
                    continue;
                }
                if (!played.completed) {
                    machine_notice(played.failure);
                    machine_reporter.emit_error(played.failure);
                    close_turn();
                    continue;
                }
                title.start_if_due(session);
                harness::ChatResponse response;
                response.message = session.messages.back();
                response.model = session.backend;
                machine_reporter.emit_result(response);
                close_turn();
                continue;
            }
            attach_mentions(
                attached, message.text, working_directory,
                [&machine_reporter](const std::string& note) { machine_reporter.on_notice(note); });
            attached.settle();
            rescope_tools(attached);

            // The driver reads structured input, so there IS someone to
            // answer a question -- the loop's "nil AskFn <=> never
            // advertised" rule is satisfied rather than sidestepped.
            const ChatTurnResult outcome = run_chat_turn(
                harness, session, message.text, tools_on() ? offered : nullptr, selection.get(),
                member_calls.get(), driver_ask,
                ToolGate{permission,
                         flags->tools ? make_driver_confirm_fn(machine_reporter, driver,
                                                               config_path, approvals)
                                      : agent::ConfirmFn{},
                         approvals},
                machine_reporter, machine_notice, rag_settings, review_note, &attached, &recall,
                got->turn, /*summarize_reasoning=*/false, standing);
            say_deleted();
            if (outcome.cancelled) {
                cancelled_result();
                close_turn();
                continue;
            }
            if (!outcome.error.empty()) {
                // A turn that failed ends in `error`, as the protocol has
                // always said -- not in a `result` whose text was the user's
                // own line (found pinning the turn accounting, 28f).
                machine_reporter.emit_error(outcome.error);
                close_turn();
                continue;
            }
            title.start_if_due(session);
            if (session.compactions != saved_compactions) {
                saved_compactions = session.compactions;
                save_state(progress_sink(machine_reporter));
            }

            harness::ChatResponse response;
            response.message = session.messages.empty() ? harness::ChatMessage::assistant("")
                                                        : session.messages.back();
            response.model = session.backend;
            machine_reporter.emit_result(response);
            close_turn();
        }

        // stdin closed: the driver is done. Everything is already persisted
        // by the per-turn save, so exiting is clean by construction -- and a
        // session never sent a turn writes nothing (M28), nor one deleted
        // while it ran (55b).
        title.settle(session);
        save_if_changed();
        say_deleted();
        if (const std::string said = recall.finish({}); !said.empty()) {
            machine_notice(said);
        }
        save_state(progress_sink(machine_reporter));
        return;
    }

    // --- attachments (26d) ------------------------------------------------
    std::error_code cwd_error;
    const std::filesystem::path working_directory = std::filesystem::current_path(cwd_error);
    ChatAttachments attached{
        harness, session, attachment_index,
        ChatAttachments::Hooks{
            .say =
                [&out, &style](const std::string& line, bool warning) {
                    out.print_line(style.tag(warning ? ansi::Role::Warning : ansi::Role::Apogee) +
                                   " " + line);
                },
            .progress =
                [&out, &style](const std::string& line) {
                    if (line.empty()) {
                        out.clear_status();
                    } else {
                        out.set_status(style.tag(ansi::Role::Apogee) + " " + line);
                    }
                },
            // A pipe cannot be asked, so a folder over the size guard is
            // refused there.
            .confirm_large = out.ask_yes_no(),
            .save = true,
            // A folder of code builds its graph (27n), unless the config
            // or the attach says otherwise (27p).
            .built_in_graph = harness::AttachmentGraphMethod::Code}};
    for (const std::string& spec : flags->images) {
        (void)attached.attach(spec, working_directory);
    }
    if (persona_line.has_value()) {
        out.print_line(style.tag(ansi::Role::Apogee) + " " + *persona_line);
    }
    if (project.context.has_value()) {
        out.print_line(style.tag(ansi::Role::Apogee) + " " +
                       project_context_line(*project.context));
    }
    if (!project.unreadable.empty()) {
        out.print_line(style.tag(ansi::Role::Warning) + " project context: " + project.unreadable);
    }
    for (const std::string& spec : launch_attaches) {
        (void)attached.attach(spec, working_directory, attached.graph_method(launch_graph));
    }
    // The todo's board (41d): this conversation's list, each change drawn
    // where this front end draws it; a resumed list shown as it was left.
    const tools::TodoScope todo_scope{tools::TodoBoard{
        .list = &session.todo,
        .changed = [&out](const harness::TodoList& todo) { out.reporter().on_todo(todo); }}};
    if (!session.todo.empty()) {
        out.reporter().on_todo(session.todo);
    }

    // Once, immediately before the first prompt -- never between turns.
    if (out.terminal()) {
        discard_startup_typeahead();
    }

    // `/` lists the commands as they are typed, a command's values follow
    // it, and `@` completes paths -- all from the one command table. A
    // pipe gets none of it: the plain reader never asks.
    EditingLineReader::Options reader_options;
    // A private chat (55a) keeps its lines for its own up-arrow and writes
    // them nowhere: no history path, so the shared file is neither read nor
    // written, and no later session recalls what was typed here.
    if (!session.never_written) {
        reader_options.history_path = default_history_path();
    }
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
    // An execute session's rows, and the symphonies `/play` takes, read as
    // `/play` finds them (27s).
    completion.mode = mode;
    // The shell draws its menu rows whole, so it can match loosely; the
    // terminal's replxx rows need prefixes (2026-10-11).
    completion.loose = !out.terminal();
    completion.menu = !out.terminal();
    if (mode == SessionMode::Execute) {
        completion.symphonies = [&harness, config_path] {
            return symphony_choices(session_catalog(harness.config(), config_path));
        };
    }
    reader_options.suggest = [sources = std::move(completion)](std::string_view before_cursor) {
        return suggest_chat_input(before_cursor, sources);
    };
    reader_options.live = true;
    reader_options.color = style.color_enabled();
    // Shift+Tab moves the permission mode on, as Claude Code's does (39b),
    // and the prompt carries the mode's words. Both read the shared
    // approvals alone, so the shell may call them from its own thread.
    reader_options.on_shift_tab = [approvals] {
        approvals->mode =
            next_permission_mode(approvals->mode.load(), approvals->bypass_allowed.load());
    };
    reader_options.prompt_tag = [approvals, style] {
        return permission_mode_label(approvals->mode.load(), style);
    };
    const std::unique_ptr<LineReader> reader = out.line_reader(std::move(reader_options));

    // How the reasoning is drawn while it streams (43): the config's, flipped
    // for this chat by `/thinking-display`. A summary is asked for only where
    // the front end draws the reasoning at all.
    harness::ThinkingDisplay thinking_display = config.ui.thinking_display;
    const auto summarizing = [&thinking_display, &out]() {
        return thinking_display == harness::ThinkingDisplay::Summary && out.draws_reasoning();
    };
    out.set_thinking_summarized(summarizing());

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
            out.print_line(style.tag(ansi::Role::Warning) +
                           " nothing to capture yet -- have a conversation first");
            return;
        }
        if (overrides.source.empty()) {
            overrides.source = "chat";
        }
        CaptureInputs inputs;
        inputs.raw = transcript;
        inputs.overrides = std::move(overrides);
        // A verifier checking the record (27g) is said where the clerk is.
        inputs.narrate = [&out](const agentloop::SideCall& call) {
            out.reporter().on_side_call(call);
        };
        // The clerk is the chat's own model -- loaded already, so no second
        // load (Milestone Y) -- unless a utility model is named (26b).
        const std::string utility = named_utility(config);
        const std::string clerk = utility.empty() ? session.backend : utility;
        // Narrated in the thinking block like any side call, which then
        // collapses: the clerk is a model call of its own (26n).
        std::optional<agentloop::SideCallScope> said;
        said.emplace([&out](const agentloop::SideCall& call) { out.reporter().on_side_call(call); },
                     "clerk", "distilling this conversation into a record with " + clerk);
        const CaptureResult result = capture_and_store(
            harness, config, config_path, inputs, knowledge::make_structured_clerk(harness, clerk));
        said.reset();
        out.reporter().on_clear_status();
        if (!result.ok()) {
            // The clerk named, as the line its narration replaced did:
            // on a pipe that line is not drawn at all (26n).
            out.print_line(style.tag(ansi::Role::Error) + " capture by " + clerk +
                           " failed: " + result.error);
            return;
        }
        for (const std::string& note : result.notes) {
            out.print_line(style.tag(ansi::Role::Warning) + " " + note);
        }
        out.print_line(style.tag(ansi::Role::Apogee) + " captured " + result.record.id + " [" +
                       result.record.status + "] -- " + preview_text(result.record.intent, 80));
        if (result.validation.has_value()) {
            const bool clean = result.validation->result == agentloop::Validated::Result::Passed ||
                               result.validation->result == agentloop::Validated::Result::Revised;
            for (const std::string& line : agentloop::extraction_lines(*result.validation)) {
                out.print_line(style.tag(clean ? ansi::Role::Apogee : ansi::Role::Warning) + " " +
                               line);
            }
        }
    };

    // What the session holds in memory, for a front-end that shows it (32e):
    // said before each line is read, so after every turn and every switch.
    const auto report_held = [&]() {
        std::vector<std::string> held;
        for (const std::string& backend : config.backend_names()) {
            if (harness.resident(backend).value_or(false)) {
                held.push_back(backend);
            }
        }
        out.set_held(std::move(held));
    };

    // --- the REPL --------------------------------------------------------
    BackgroundTitle title{harness,
                          [&out](std::string_view line) { out.reporter().on_progress(line); }};
    // A chat deleted outside while this session runs stays deleted (55b):
    // said once, before the prompt after the save that found it gone.
    bool deleted_said = false;
    const auto say_deleted = [&]() {
        if (session.deleted && !std::exchange(deleted_said, true)) {
            out.print_line(style.tag(ansi::Role::Warning) + " " + deleted_line(session.chat_id));
        }
    };
    bool running = true;
    while (running) {
        say_deleted();
        report_held();
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
        // with blanks -- a private chat's in memory alone (55a).
        reader->remember(input);

        // Before anything below can reach the model.
        title.settle(session);

        if (const std::optional<SlashCommand> command = parse_slash(input); command.has_value()) {
            const std::string& argument = command->argument;
            const ChatCommandSpec* spec = find_chat_command(command->name, mode);
            if (spec == nullptr) {
                out.print_line(style.tag(ansi::Role::Error) + " unknown command '/" +
                               command->name + "' -- /help lists them");
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
                case ChatVerb::Symphonies: {
                    // What `/play` can play (27s), as `symphonies list` lists
                    // it.
                    const symphony::Catalog catalog =
                        session_catalog(harness.config(), config_path);
                    for (const std::string& row : symphony_list_lines(catalog)) {
                        out.print_line("  " + row);
                    }
                    for (const std::string& problem : catalog.problems) {
                        out.print_line(style.tag(ansi::Role::Warning) + " skipped " + problem);
                    }
                    break;
                }
                case ChatVerb::Play: {
                    // A symphony played on the input (27s): refused before
                    // anything is sent when it cannot be played with what was
                    // typed; else each stage in the thinking block and the
                    // output this session's answer, kept as an exchange.
                    const PreparedPlay prepared =
                        prepare_play(harness.config(), config_path, parse_play_argument(argument));
                    if (!prepared.refusal.empty()) {
                        out.print_line(style.tag(ansi::Role::Error) + " " + prepared.refusal);
                        break;
                    }
                    // Its stages are model calls: one at a time (26e).
                    attached.settle();
                    if (decorate) {
                        out.print_line("");
                    }
                    // As a turn: keystrokes typed while it plays wait unseen.
                    std::optional<platform::TypeaheadGuard> typeahead;
                    if (reader->interactive() && out.terminal()) {
                        typeahead.emplace();
                    }
                    const PlayTurnResult played = [&]() {
                        const TurnScope turn{out};
                        return run_play_turn(harness, session, input, prepared, *member_calls,
                                             out.reporter(), &recall, turn.token);
                    }();
                    typeahead.reset();
                    if (played.completed) {
                        title.start_if_due(session);
                    } else {
                        out.print_line(style.tag(ansi::Role::Error) + " " + played.failure);
                    }
                    if (decorate) {
                        out.print_line("");
                    }
                    break;
                }
                case ChatVerb::Recall:
                    if (argument.empty()) {
                        std::string state = "off";
                        if (recall.active()) {
                            state = "on";
                        } else if (config.memory.recall && !flags->no_recall &&
                                   !flags->private_chat) {
                            state = "off for this chat";
                        }
                        out.print_line(style.tag(ansi::Role::Apogee) + " recall: " + state);
                    } else if (argument == "on" || argument == "off") {
                        recall.set_session(argument == "on");
                    } else {
                        out.print_line(style.tag(ansi::Role::Error) + " not on or off: '" +
                                       argument + "'");
                    }
                    break;
                case ChatVerb::Permissions:
                    for (const std::string& row :
                         describe_permissions(config, *approvals, gated, edits)) {
                        out.print_line("  " + row);
                    }
                    break;
                case ChatVerb::PermissionMode: {
                    // How much runs without asking (39b): the mode is read by
                    // the gate at each call, so it holds from the next one.
                    // The setter is the one a driver's `set` line reaches.
                    if (!argument.empty()) {
                        if (permission_mode_from_string(argument) == PermissionMode::Bypass &&
                            !approvals->bypass_allowed) {
                            // Not allowed at launch: asked once (39b, the
                            // user's call), and a yes puts bypass in this
                            // session's Shift+Tab cycle too. Nobody to ask
                            // -- a pipe -- is a no.
                            const std::function<bool(const std::string&)> ask = out.ask_yes_no();
                            if (!ask) {
                                out.print_line(style.tag(ansi::Role::Error) +
                                               " bypass needs a yes from someone at the "
                                               "keyboard, or --allow-bypass at launch");
                                break;
                            }
                            if (!ask("Run every tool without asking for the rest of this "
                                     "session? A config deny or /deny still refuses.")) {
                                out.print_line(style.tag(ansi::Role::Permission) +
                                               " bypass not chosen -- permission mode: " +
                                               std::string{to_string(approvals->mode.load())});
                                break;
                            }
                            approvals->bypass_allowed = true;
                        }
                        if (const SettingSaid said = set_permission_mode(*approvals, argument);
                            !said.ok) {
                            out.print_line(style.tag(ansi::Role::Error) + " " + said.line);
                            break;
                        }
                    }
                    out.print_line(style.tag(ansi::Role::Permission) + " " +
                                   permission_mode_line(approvals->mode.load()) +
                                   (argument.empty()
                                        ? std::string{" (manual, accept-edits, plan, dont-ask "
                                                      "or bypass; Shift+Tab cycles)"}
                                        : std::string{}));
                    break;
                }
                case ChatVerb::Allow:
                case ChatVerb::Deny:
                case ChatVerb::Revoke: {
                    if (argument.empty()) {
                        // Alone, `/allow` lists: listing beats an error.
                        for (const std::string& row :
                             describe_permissions(config, *approvals, gated)) {
                            out.print_line("  " + row);
                        }
                        break;
                    }
                    const GatedName name = name_gated(argument, gated);
                    if (!name.error.empty()) {
                        out.print_line(style.tag(ansi::Role::Error) + " " + name.error);
                        break;
                    }
                    const std::string what = name.host ? "website " + name.key : name.key;
                    if (spec->id == ChatVerb::Allow) {
                        allow_for_session(*approvals, name);
                        const bool config_denies =
                            !name.host &&
                            config.permissions.level(name.key) == harness::PermissionLevel::Deny;
                        out.print_line(
                            style.tag(ansi::Role::Apogee) + " " + what +
                            (config_denies
                                 ? " stays denied: the config says deny, and a chat never "
                                   "loosens that"
                                 : " allowed for this chat"));
                    } else if (spec->id == ChatVerb::Deny) {
                        deny_for_session(*approvals, name);
                        out.print_line(style.tag(ansi::Role::Apogee) + " " + what +
                                       " denied for this chat");
                    } else if (revoke_for_session(*approvals, name)) {
                        out.print_line(style.tag(ansi::Role::Apogee) + " " + what +
                                       ": asked again from now on");
                    } else if (!name.host && config.permissions.levels.contains(name.key)) {
                        out.print_line(style.tag(ansi::Role::Apogee) + " " + what +
                                       " is the config's answer -- change it with 'apogee config "
                                       "set-permission " +
                                       name.key + " ask'");
                    } else {
                        out.print_line(style.tag(ansi::Role::Apogee) +
                                       " this chat has no answer for " + what);
                    }
                    break;
                }
                case ChatVerb::Private:
                    recall.make_private();
                    logger::save(session);
                    out.print_line(style.tag(ansi::Role::Apogee) +
                                   " this chat is private: it will never be summarised for recall");
                    break;
                case ChatVerb::Help:
                    for (const std::string& row :
                         chat_help_lines(reader->interactive() ? out.width() : 0, mode)) {
                        out.print_line(row);
                    }
                    break;
                case ChatVerb::Models: {
                    // Each backend, and under it what else it runs (35): the
                    // models `/model <backend>:<model>` -- or the bare id --
                    // switches to.
                    const auto owned = roster_models(config);
                    for (const std::string& backend : config.backend_names()) {
                        out.print_line(backend == session.backend ? "* " + model_label(session)
                                                                  : "  " + backend);
                        if (const auto listed = owned.find(backend); listed != owned.end()) {
                            for (const std::string& roster_id : listed->second) {
                                out.print_line(style.dim("      " + backend + ":" + roster_id));
                            }
                        }
                    }
                    break;
                }
                case ChatVerb::Model: {
                    if (argument.empty()) {
                        out.print_line(model_label(session));
                        break;
                    }
                    const SettingSaid said =
                        switch_model(harness, session, config, argument, build_options, built,
                                     flags->tools, [&attached]() { attached.settle(); });
                    if (!said.ok) {
                        out.print_line(style.tag(ansi::Role::Error) + " " + said.line);
                        break;
                    }
                    // A suite's toolset pin follows the backend (27d).
                    reoffer_tools();
                    refresh_header();
                    out.print_line(style.tag(ansi::Role::Apogee) + " " + said.line);
                    if (!said.warning.empty()) {
                        out.print_line(style.tag(ansi::Role::Warning) + " " + said.warning);
                    }
                    break;
                }
                case ChatVerb::Suite: {
                    if (argument.empty()) {
                        out.print_line(style.tag(ansi::Role::Apogee) +
                                       " suite: " + active_suite_summary(config));
                        // The session's own status (27e): each member
                        // resident or not, and the set's total.
                        if (!config.models.default_suite.empty()) {
                            for (const std::string& row : footprint_lines(
                                     price_suite(config, config.models.default_suite, machine),
                                     [&harness](std::string_view backend) {
                                         return harness.resident(backend);
                                     })) {
                                out.print_line(row);
                            }
                        }
                        break;
                    }
                    const SuiteArgument asked = parse_suite_argument(argument);
                    harness::Config probe = config;
                    std::string refused = asked.error;
                    // An execute session always runs under a suite (27s).
                    if (refused.empty() && mode == SessionMode::Execute &&
                        asked.suite == harness::kSuiteOff) {
                        refused =
                            "execute runs under a suite -- /suite <name> switches to another; "
                            "'apogee chat' is a session without one";
                    }
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
                        out.print_line(style.tag(ansi::Role::Error) + " " + refused);
                        break;
                    }
                    // Rebuilding a backend is a model call's business:
                    // what is reading an attachment settles first.
                    attached.settle();
                    for (const std::string& said : activate_suite(
                             harness, config, probe.models.default_suite, build_options)) {
                        out.print_line(style.tag(ansi::Role::Warning) + " " + said);
                    }
                    session.suite = config.models.default_suite;
                    // The suite's chat member speaks for the conversation,
                    // as /model would; with none, or off, it stays put.
                    std::string moved;
                    if (const harness::Resolution chat = harness::resolve_backend(
                            config, harness::RoleRequest{.role = harness::ModelRole::Chat});
                        chat.from == harness::ResolvedFrom::Suite && chat.key != session.backend &&
                        config.find_backend(chat.key) != nullptr) {
                        // A roster pin stays with the backend it rode (33).
                        if (const std::string unpinned =
                                move_pin(harness, session, chat.key, {}, build_options);
                            !unpinned.empty()) {
                            out.print_line(style.tag(ansi::Role::Warning) + " " + unpinned);
                            session.model.clear();  // the conversation moves on regardless
                        }
                        session.backend = chat.key;
                        harness.resume_conversation(session.backend, session.chat_id);
                        moved = chat.key;
                    }
                    reoffer_tools();
                    refresh_header();
                    logger::save(session);
                    if (config.models.default_suite.empty()) {
                        out.print_line(style.tag(ansi::Role::Apogee) +
                                       " suite off -- the roles follow the global pointers; the "
                                       "conversation stays on " +
                                       session.backend);
                    } else {
                        out.print_line(style.tag(ansi::Role::Apogee) + " suite " +
                                       active_suite_summary(config));
                        if (priced.has_value()) {
                            out.print_line(
                                style.tag(ansi::Role::Apogee) + " " +
                                admission_line(
                                    *priced, priced->admission() == models::Admission::OverBudget));
                        }
                        if (!moved.empty()) {
                            out.print_line(
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
                                out.print_line(style.tag(ansi::Role::Warning) + " " + said);
                            }
                        }
                    }
                    break;
                }
                case ChatVerb::Branch:
                    if (argument.empty()) {
                        out.print_line(style.tag(ansi::Role::Apogee) + " review: " +
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
                        out.print_line(
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
                    out.print_line(style.tag(ansi::Role::Apogee) + " " +
                                   set_system_prompt(session, argument).line);
                    break;
                case ChatVerb::Temperature:
                    // Said only when refused, as it always was.
                    if (const SettingSaid said = set_temperature(session, argument); !said.ok) {
                        out.print_line(style.tag(ansi::Role::Error) + " " + said.line);
                    }
                    break;
                case ChatVerb::Think:
                    if (argument.empty()) {
                        const harness::Thinking thinking = resolve_thinking(
                            session.params.thinking, session.params.thinking_budget,
                            harness.config(), session.backend, session.params.effort);
                        out.print_line(
                            style.tag(ansi::Role::Apogee) +
                            " thinking: " + std::string{harness::to_string(thinking.mode)} +
                            (thinking.budget.has_value()
                                 ? ", at most " + std::to_string(*thinking.budget) + " tokens"
                                 : std::string{}) +
                            (thinking.effort.has_value()
                                 ? ", effort " + std::string{harness::to_string(*thinking.effort)}
                                 : std::string{}));
                    } else if (const SettingSaid said = set_thinking(session, argument); !said.ok) {
                        out.print_line(style.tag(ansi::Role::Error) + " " + said.line);
                    }
                    break;
                case ChatVerb::ThinkingDisplay: {
                    // How the reasoning is drawn while it streams (43), for
                    // this chat; the route says who writes the line, or why
                    // none can be had, as the loop will find it.
                    if (!argument.empty()) {
                        const std::optional<harness::ThinkingDisplay> chosen =
                            harness::thinking_display_from_string(argument);
                        if (!chosen.has_value()) {
                            out.print_line(style.tag(ansi::Role::Error) +
                                           " not a thinking display: '" + argument +
                                           "' (summary or raw)");
                            break;
                        }
                        thinking_display = *chosen;
                        out.set_thinking_summarized(summarizing());
                    }
                    std::string status =
                        "thinking display: " + std::string{harness::to_string(thinking_display)};
                    if (thinking_display == harness::ThinkingDisplay::Raw) {
                        status += " -- the reasoning as it streams";
                    } else {
                        const agentloop::SummaryRoute route = agentloop::route_summary(
                            harness, session.backend, named_utility(harness.config()));
                        status +=
                            route.vendor ? " -- this model's reasoning is its vendor's own "
                                           "summary, shown as it is"
                            : !route.model.empty()
                                ? " -- a line of what it is doing, written by " + route.model
                                : " -- but the reasoning shows as it streams: " + route.why_not;
                    }
                    out.print_line(style.tag(ansi::Role::Apogee) + " " + status +
                                   (argument.empty() ? " (summary or raw)" : ""));
                    break;
                }
                case ChatVerb::Effort: {
                    const SettingSaid said = set_effort(session, harness, argument);
                    out.print_line(style.tag(said.ok ? ansi::Role::Apogee : ansi::Role::Error) +
                                   " " + said.line);
                    break;
                }
                case ChatVerb::MaxTokens:
                    try {
                        session.params.max_tokens = std::stoll(argument);
                    } catch (const std::exception&) {
                        out.print_line(style.tag(ansi::Role::Error) + " not a number: '" +
                                       argument + "'");
                    }
                    break;
                case ChatVerb::Retriever:
                    if (argument.empty()) {
                        out.print_line(
                            style.tag(ansi::Role::Rag) + " retriever: " +
                            (session.retriever.empty() ? "auto" : session.retriever) +
                            (session.retriever.empty()
                                 ? " -- vector when the collection's vectors match the "
                                   "embedding backend, else lexical; hybrid only when asked"
                                 : ""));
                    } else if (!agentloop::valid_retriever(argument)) {
                        out.print_line(style.tag(ansi::Role::Error) + " " +
                                       agentloop::retriever_values_message("/retriever", argument));
                    } else {
                        session.retriever = argument == "auto" ? std::string{} : argument;
                        // Persisted now, so a resume continues with this.
                        logger::save(session);
                        out.print_line(style.tag(ansi::Role::Rag) + " retriever set to " +
                                       (session.retriever.empty() ? "auto" : session.retriever));
                    }
                    break;
                case ChatVerb::Rerank:
                    if (argument.empty()) {
                        out.print_line(style.tag(ansi::Role::Rag) + " rerank: " +
                                       (session.rerank.empty()
                                            ? "following each collection's rerank: pin"
                                            : session.rerank) +
                                       " -- /rerank <backend>|off|auto");
                    } else if (argument == "auto") {
                        session.rerank.clear();
                        logger::save(session);
                        out.print_line(style.tag(ansi::Role::Rag) +
                                       " rerank follows the collection's pin");
                    } else if (!agentloop::valid_rerank(argument, config)) {
                        out.print_line(style.tag(ansi::Role::Error) + " no backend named '" +
                                       argument + "'");
                    } else {
                        session.rerank = argument;
                        logger::save(session);
                        out.print_line(style.tag(ansi::Role::Rag) + " rerank set to " + argument);
                    }
                    break;
                case ChatVerb::Title:
                    session.custom_name = argument;
                    logger::save(session);
                    out.print_line(style.tag(ansi::Role::Apogee) + " renamed");
                    break;
                case ChatVerb::Attach: {
                    // The path first, then its flags (27p): `--graph`
                    // over the config's default, for this attach alone.
                    const AttachArgument asked = parse_attach_argument(argument);
                    if (!asked.error.empty()) {
                        out.print_line(style.tag(ansi::Role::Error) + " " + asked.error);
                        break;
                    }
                    if (asked.spec.empty()) {
                        out.print_line(style.tag(ansi::Role::Error) +
                                       " /attach takes a file, a folder or a glob -- " +
                                       std::string{kAttachShape});
                        break;
                    }
                    (void)attached.attach(asked.spec, working_directory,
                                          attached.graph_method(asked.graph));
                    break;
                }
                case ChatVerb::Attachments: {
                    attached.settle();
                    const std::vector<std::string> lines = attached.describe();
                    if (lines.empty()) {
                        out.print_line(style.tag(ansi::Role::Apogee) +
                                       " nothing attached -- /attach <path>");
                    }
                    for (const std::string& row : lines) {
                        out.print_line(style.tag(ansi::Role::Apogee) + " " + row);
                    }
                    break;
                }
                case ChatVerb::Todo: {
                    const std::vector<std::string> lines = harness::describe_todo(session.todo);
                    if (lines.empty()) {
                        out.print_line(style.tag(ansi::Role::Apogee) +
                                       (flags->tools ? " no todo yet -- the model keeps one with "
                                                       "todo_write when a task has steps"
                                                     : " no todo -- a session with --tools keeps "
                                                       "one"));
                    }
                    for (const std::string& row : lines) {
                        out.print_line(style.tag(ansi::Role::Apogee) + " " + row);
                    }
                    break;
                }
                case ChatVerb::Detach: {
                    const SettingSaid said = detach_named(attached, unquoted(argument));
                    out.print_line(style.tag(said.ok ? ansi::Role::Apogee : ansi::Role::Error) +
                                   " " + said.line);
                    break;
                }
                case ChatVerb::Check: {
                    // The answer seam on request (27g): the suite's
                    // verifier, once, briefed with the question and the
                    // answer alone; on an objection the chat's model
                    // answers it once. Both said; nothing added to the
                    // conversation.
                    const agentloop::VerifierRole role = agentloop::verifier_role(harness.config());
                    if (!role.missing.empty()) {
                        out.print_line(style.tag(ansi::Role::Error) + " check: " + role.missing);
                        break;
                    }
                    if (session.messages.empty() ||
                        session.messages.back().role != harness::Role::Assistant ||
                        session.messages.back().content.plain_text().empty()) {
                        out.print_line(style.tag(ansi::Role::Error) +
                                       " check: there is no answer to check yet");
                        break;
                    }
                    attached.settle();
                    const agentloop::SideCallSink side = [&out](const agentloop::SideCall& call) {
                        out.reporter().on_side_call(call);
                    };
                    agentloop::Validated checked;
                    try {
                        // A turn of its own: the suite's cap bounds it,
                        // as it bounds a turn's consults.
                        const agentloop::MemberCalls::Turn turn =
                            member_calls->begin_turn(side, {});
                        const agentloop::Verifier verifier =
                            agentloop::bind_verifier(harness, *member_calls, role.role);
                        checked =
                            agentloop::check_answer(harness, session.backend, session.messages,
                                                    verifier, side, session.params.max_tokens, {});
                    } catch (const harness::HarnessError& e) {
                        out.reporter().on_clear_status();
                        out.print_line(style.tag(ansi::Role::Error) + " check: " + e.what());
                        break;
                    }
                    out.reporter().on_clear_status();
                    const bool agreed = checked.result == agentloop::Validated::Result::Passed;
                    for (const std::string& row : agentloop::answer_lines(checked)) {
                        out.print_line(
                            style.tag(agreed ? ansi::Role::Apogee : ansi::Role::Warning) + " " +
                            row);
                    }
                    break;
                }
                case ChatVerb::Compact: {
                    const SettingSaid said = compact_attached(harness, session, attached);
                    out.print_line(style.tag(said.ok ? ansi::Role::Apogee : ansi::Role::Warning) +
                                   " " + said.line);
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
        attach_mentions(
            attached, input, working_directory,
            [&out, &style](const std::string& note) { out.print_line(style.dim(note)); });
        attached.settle();
        rescope_tools(attached);
        if (decorate) {
            // One blank line between the question and whatever answers it
            // -- the thinking block, or the answer itself -- as there is
            // one after the answer.
            out.print_line("");
        }
        // Keystrokes typed while the model answers stay unseen, queued for
        // the next prompt, which shows them once -- instead of echoing into
        // the answer and then again at the prompt (2026-09-23).
        std::optional<platform::TypeaheadGuard> typeahead;
        if (reader->interactive() && out.terminal()) {
            typeahead.emplace();
        }
        out.set_resting_label(is_base_model(harness, session.backend) ? "Thinking… · base model"
                                                                      : "Thinking…");
        {
            const TurnScope turn{out};
            run_chat_turn(
                harness, session, input, tools_on() ? offered : nullptr, selection.get(),
                member_calls.get(), flags->tools ? out.ask_fn() : agentloop::AskFn{},
                ToolGate{permission,
                         flags->tools ? out.confirm_fn(config_path, approvals) : agent::ConfirmFn{},
                         approvals},
                out.reporter(),
                [&out, &style](const std::string& message) {
                    // Above the thinking block when side calls have opened
                    // one (26n): never inside its rows.
                    out.keep_line(style.tag(ansi::Role::Warning) + " " + message);
                },
                rag_settings, review_note, &attached, &recall, turn.token, summarizing(), standing);
        }
        title.start_if_due(session);
        if (session.compactions != saved_compactions) {
            saved_compactions = session.compactions;
            save_state(progress_sink(out.reporter()));
        }
        typeahead.reset();
        if (decorate) {
            // One blank line between an answer and the next prompt, so
            // turns read as turns rather than one run of text.
            out.print_line("");
        }
    }

    if (reader->interrupted()) {
        title.cancel();
    }
    title.settle(session);
    save_if_changed();
    say_deleted();
    // The model's state too, on a clean exit only (26j): an interrupt
    // asked for nothing more, and the transcript is saved either way.
    if (!reader->interrupted()) {
        // The chat's summary for recall too (26l): only a clean exit.
        if (const std::string said = recall.finish({}); !said.empty()) {
            out.print_line(style.dim("[memory] " + said));
        }
        save_state(progress_sink(out.reporter()));
    }
    // Opt-in auto-capture on a CLEAN exit -- /exit, /quit, the end of the
    // input -- with the still-loaded model as the clerk. Never on an
    // interrupt: a user who hit Ctrl-C did not ask for a model call, and
    // the transcript is already saved either way. Best-effort: a failure
    // is a line, never a non-zero exit.
    // Never for a private chat (52d): its words go nowhere -- nor for one
    // deleted while it ran (55b), whose words were deleted with it.
    if (config.knowledge.auto_capture && !reader->interrupted() && !session.never_written &&
        !session.deleted) {
        capture_session({});
    }
    if (decorate) {
        std::string left = " saved " + session.chat_id;
        if (session.never_written) {
            left = " private: nothing of this chat was written";
        } else if (session.deleted) {
            left = " not saved -- " + session.chat_id + " was deleted while this session ran";
        } else if (session.unwritten()) {
            left = " nothing saved -- the chat had no turn";  // M28
        }
        out.print_line(style.tag(ansi::Role::Apogee) + left);
    }
    out.set_held({});
}

std::string execute_suite_refusal(const harness::Config& config) {
    // The suite half as it was (27s); the bare door (41a) after it.
    const std::string bare =
        " -- or open it bare on one model: apogee execute -m <backend>, or with a default "
        "backend set (apogee config set-default <backend>)";
    if (config.suite_names().empty()) {
        return "execute opens a session with a suite, and none is configured -- add one: "
               "apogee config add-suite <name> --chat <backend> [--utility <backend> ...], then "
               "run it with --suite <name>, or make it the default: apogee config "
               "set-default-suite <name>" +
               bare;
    }
    return "execute opens a session with a suite, and none is named or the default -- run one "
           "with --suite <name> (" +
           known_suites(config) +
           "), or make one the default: apogee config set-default-suite <name>" + bare;
}

std::string execute_orchestrate_refusal(const harness::Config& config) {
    const std::string suites =
        config.suite_names().empty() ? std::string{} : " (" + known_suites(config) + ")";
    return "--orchestrate offers a suite's symphonies to its chat model, and this session runs "
           "under none -- run one with --suite <name>" +
           suites + ", or make one the default: apogee config set-default-suite <name>";
}

std::string orchestration_count(std::size_t offered) {
    return "orchestrating " + (offered == 0 ? std::string{"none"} : std::to_string(offered));
}

std::string session_banner(SessionMode mode, std::string_view model, bool base_model,
                           std::string_view suite, bool forced, std::size_t symphonies,
                           std::optional<std::size_t> orchestrated, std::string_view chat_id) {
    std::string banner{model};
    if (base_model) {
        banner += "  ·  base model";
    }
    banner += banner_suite(suite, forced);
    if (mode == SessionMode::Execute) {
        // Bare (41a): said, where a suite would be named.
        if (suite.empty()) {
            banner += "  ·  no suite";
        }
        banner += "  ·  " + symphony_count(symphonies);
        if (orchestrated.has_value()) {
            banner += "  ·  " + orchestration_count(*orchestrated);
        }
    }
    banner += "  ·  chat ";
    banner += chat_id;
    banner += "  ·  /help for commands";
    return banner;
}

}  // namespace apogee::commands
