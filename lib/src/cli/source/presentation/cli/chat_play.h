#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "agentloop/member_call.h"
#include "agentloop/reporter.h"
#include "cli/chat_completer.h"
#include "contracts/cancellation.h"
#include "contracts/config.h"
#include "harness/harness.h"
#include "logger/session.h"
#include "symphony/definition.h"
#include "symphony/runner.h"

/// A symphony played in a session (27s): `/play <symphony> [input]` in an
/// execute session's REPL, and a `user` line holding one in its machine mode.
///
/// **A play is the session's history, not a side channel.** The line as
/// typed and the symphony's output land in the transcript as one ordinary
/// exchange -- saved, resumable, compactable, recallable -- so the root model
/// reads what a play said on the next bare question exactly as it reads any
/// earlier answer. The stages are narration: each one 27q's member call on
/// the session's own `MemberCalls`, said through `Reporter::on_side_call` as
/// a consult is (`stage 1/2 summarize — asking utility (l3b): …`), which the
/// terminal draws in the thinking block and machine mode carries as the
/// `tool_status` it already emits -- no event of its own. The output is said
/// as an answer is, so the terminal renders it and a driver reads it in the
/// answer deltas and the turn's `result`.
///
/// **Played as `symphonies play` plays it**, through the one walk: the
/// definition found among every source, refused before anything is sent when
/// it cannot be played with what was given, the whole walk under one budget
/// (`symphony_caps`), the user's initiative free to reach a billed member.
/// `/play` gives a symphony text alone: one that takes an image is refused
/// with the way to play it with one.
namespace apogee::commands {

class ChatRecall;

/// `/play`'s shape, for its refusals.
inline constexpr std::string_view kPlayShape = "/play <symphony> [input]";

/// `/play`'s argument (27s): the symphony -- a name, or a spec file's path --
/// then the input, the rest of the line as typed.
struct PlayArgument {
    std::string symphony;
    std::string input;
    /// Why it cannot be read, naming the shape; empty when it can.
    std::string error;
};

[[nodiscard]] PlayArgument parse_play_argument(std::string_view argument);

/// The symphonies a session over `config` can play: every source, the spec
/// files under the data directory of the config at `config_path`.
[[nodiscard]] symphony::Catalog session_catalog(const harness::Config& config,
                                                const std::filesystem::path& config_path);

/// A play ready to run, or why it cannot: the definition found and checked
/// with its input before anything is sent.
struct PreparedPlay {
    symphony::Definition definition;
    symphony::Catalog catalog;
    symphony::PlayInput input;
    /// Why it is refused -- no such symphony, one that cannot be played, an
    /// input it reads and was not given, an image it takes -- with the way
    /// out; empty when it can run.
    std::string refusal;
};

[[nodiscard]] PreparedPlay prepare_play(const harness::Config& config,
                                        const std::filesystem::path& config_path,
                                        const PlayArgument& asked);

/// What a play in a session came to.
struct PlayTurnResult {
    /// The output reached the session as an exchange, saved.
    bool completed = false;
    /// Why not, naming the stage: a stage refused or failed, an answer
    /// outside its schema, the play's budget spent, a cancellation.
    std::string failure;
    /// The failure was a member's -- a provider's error, an answer that broke
    /// its schema -- rather than a refusal before anything was sent.
    bool member_failure = false;
    bool cancelled = false;
};

/// Plays `play` in `session` on `harness` under its active suite, each stage
/// a call on `member_calls`' own turn, narrated through `reporter`; the
/// output said through `reporter` as an answer, and appended to the session
/// after `line` -- the line as typed -- as one exchange, the session saved.
/// A play that stops leaves the session as it was. Never throws for a stage
/// that fails or a cancellation: the result says it.
[[nodiscard]] PlayTurnResult run_play_turn(const harness::Harness& harness,
                                           logger::Session& session, const std::string& line,
                                           const PreparedPlay& play,
                                           agentloop::MemberCalls& member_calls,
                                           agentloop::Reporter& reporter, ChatRecall* recall,
                                           const harness::CancellationToken& cancellation = {});

/// `/play`'s completion: each symphony `catalog` holds that `/play` can play,
/// with its description -- never one it refuses whatever input is typed (an
/// image it takes, a definition that cannot be played), by the refusal
/// `prepare_play` gives.
[[nodiscard]] std::vector<NamedChoice> symphony_choices(const symphony::Catalog& catalog);

/// The banner's count of what a session can play: `1 symphony`, `3
/// symphonies`.
[[nodiscard]] std::string symphony_count(std::size_t count);

}  // namespace apogee::commands
