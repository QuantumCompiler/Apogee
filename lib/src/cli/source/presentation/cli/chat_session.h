#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cli/chat_completer.h"
#include "cli/command.h"
#include "cli/suite_residency.h"
#include "contracts/config.h"
#include "machine/json_reporter.h"

namespace CLI {
class App;
class Option;
}  // namespace CLI

/// The session core (27s): what `apogee chat` is, and `apogee execute` with
/// it -- one session machinery, parameterized by mode, never a copied loop.
///
/// **Chat is the first consumer of its own generalization.** Everything a
/// chat session does -- its flags, the suite chosen, priced and held, the
/// providers built once so `/model` is instant, resume, recall, the tools and
/// their gate, the permission presets, the busy line, the REPL with its one
/// command table and completion, machine mode, the per-turn save, the title,
/// capture -- is written here once, and `ChatCommand` is a thin face over it.
/// `ExecuteCommand` is the other: the same session opened with a suite, which
/// it insists on, and whose symphonies are playable (`/play`, `/symphonies`).
/// A fix in chat's core is a fix in execute by construction.
///
/// **The mode changes exactly what execute is, and nothing else**: the
/// command's name in what it says; a suite required -- named, resumed or the
/// config's default, never `off` -- with the way to configure one when there
/// is none; the banner's symphony count; the table's execute rows; and in
/// machine mode a `user` line that is a `/play` command played as one. Chat's
/// whole surface is held byte for byte to what it was before the core was
/// shared (`chat_session_test`'s golden).
namespace apogee::commands {

/// The command a `mode` session is: `chat` or `execute`.
[[nodiscard]] std::string_view session_command(SessionMode mode) noexcept;

/// What a slash command asked the REPL to do.
enum class SlashOutcome : std::uint8_t {
    /// Not a slash command; treat the line as a prompt.
    NotACommand,
    /// Handled; carry on.
    Handled,
    /// Handled; leave the REPL.
    Exit,
};

/// One parsed slash command.
struct SlashCommand {
    std::string name;      ///< without the leading '/'
    std::string argument;  ///< everything after the first space, trimmed
};

/// Parses a line as a slash command, or nullopt when it is a prompt.
///
/// A line is a command only when it starts with `/` AND the word after it
/// contains no whitespace — so `/help` is a command and `/usr/bin/env is a
/// path` is a question about a path.
[[nodiscard]] std::optional<SlashCommand> parse_slash(std::string_view line);

/// A session's flags -- chat's, every one; execute takes the same set.
struct SessionFlags {
    std::string model;
    std::string system_prompt;
    std::vector<std::string> images;
    /// Files, folders and globs attached from the start (26d).
    std::vector<std::string> attach;
    /// The method those attaches take (27p): `code` or `off`; empty leaves
    /// it to the config's `attachments.graph`, else the built-in.
    std::string graph;
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
    /// The suite the session runs under (27d), or `off` (chat only); empty
    /// leaves it to what the session last had, else the config's default
    /// suite.
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

/// Declares a session's flags on `command`, in chat's order, into `flags`.
/// Under execute `--suite` says what it is for there: never `off`.
void bind_session_flags(CLI::App& command, const std::shared_ptr<SessionFlags>& flags,
                        SessionMode mode);

/// Runs a `mode` session with `flags` -- the whole of `apogee chat`, and of
/// `apogee execute`. `machine` is where a suite's admission reads the
/// machine's budget (27e). Fails by throwing `CLI::RuntimeError` with the
/// exit code, the reason said on stderr first.
void run_session(const RootContext& context, const SessionFlags& flags,
                 const MachineBudgetSource& machine, SessionMode mode);

/// Why an execute session cannot open under `config` as it stands (27s):
/// no suite was named, the session had none and the config has no default --
/// with the way to one: the suites there are and `config set-default-suite`,
/// or, with none configured, `config add-suite`.
[[nodiscard]] std::string execute_suite_refusal(const harness::Config& config);

/// The line a session opens with on a terminal, after its tag: the model --
/// `· base model` when it is one (26r) -- the suite (27d, 27e), under execute
/// how many symphonies it can play (27s), the chat's id, and where the
/// commands are.
[[nodiscard]] std::string session_banner(SessionMode mode, std::string_view model, bool base_model,
                                         std::string_view suite, bool forced,
                                         std::size_t symphonies, std::string_view chat_id);

}  // namespace apogee::commands
