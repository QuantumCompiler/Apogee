#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "agentloop/member_call.h"
#include "contracts/config.h"
#include "contracts/types.h"

/// The stage walk (27q): a symphony played -- its stages in order, each one
/// bounded, brief-only member call through the one calling path
/// (`agentloop::MemberCalls::call`, over `call_member`), each brief the
/// stage's template rendered from the input and the earlier answers, and
/// the last stage's answer the output.
///
/// **Serial generations, by construction.** N stages are N member calls,
/// one after another, under the process-wide member-call gate -- the
/// feature's honest cost, stated rather than hidden: small members at pinned
/// windows keep each one cheap, and a deep symphony is seconds anyway.
///
/// **Deterministic around the calls.** Validation, rendering, ordering and
/// threading are pure; the members' answers are the only nondeterminism,
/// which is what lets the walk be pinned over scripted providers: each
/// recorded request is exactly its rendered template, and nothing else --
/// no history, no system prompt, no tools (a stage calls none).
///
/// **A failing stage stops the walk, named.** A refusal (a cap, a member
/// that is not there or cannot read the image), a member that fails, or a
/// schema stage whose answer does not hold to its schema ends the play with
/// `stage 2/2 verify (chat): ...` and no output: an earlier stage's answer is
/// never handed back as though it were the symphony's.
namespace apogee::symphony {

/// What a play is given.
struct PlayInput {
    /// `{{input}}`.
    std::string text;
    /// The input's image, as the parts a member reads (`ContentPart`s with a
    /// `data:` URI), for the stages marked `image: true`. Empty for none.
    std::vector<harness::ContentPart> image;
};

/// How a play runs.
struct PlayOptions {
    /// The conversation's backend: a helper role with no member and no
    /// pointer falls back to it, as the one chain says. Empty for none.
    std::string conversation;
    /// Refuse a member billed per call. Off for a play the user starts --
    /// the user's initiative spends; on for one a model would start (27t's
    /// business), where the model's initiative never does.
    bool local_only = false;
    /// The member calls the walk may make from `calls`' turn: 0 is one per
    /// stage, the play's own; a caller playing inside a turn with a budget
    /// of its own passes that budget.
    std::int64_t per_turn = 0;
};

/// One stage's outcome.
struct StageResult {
    std::string name;
    std::string role;
    /// The backend that answered, or would have.
    std::string backend;
    std::string answer;
    /// The answer reached the stage's cap and was cut there.
    bool cut = false;
    std::optional<std::int64_t> tokens;
    double seconds = 0.0;
};

/// A play's outcome.
struct PlayResult {
    /// Every stage that answered, in order.
    std::vector<StageResult> stages;
    /// The last stage's answer -- set only when every stage answered.
    std::string output;
    /// Why the play stopped, naming the stage; empty when it finished.
    std::string failure;
    /// The stage it stopped at, 1-based; 0 when it never began (an invalid
    /// definition, an input it cannot take) or finished.
    std::size_t failed_stage = 0;
    /// The failure was a member's -- a provider error, an answer that broke
    /// its schema -- rather than a refusal before anything was sent.
    bool member_failure = false;

    [[nodiscard]] bool ok() const noexcept {
        return failure.empty();
    }
};

/// A stage's narration label: `stage 1/2 summarize` (26n's `SideCall::role`).
[[nodiscard]] std::string stage_label(std::size_t index, std::size_t count,
                                      const harness::SymphonyStage& stage);

/// The member call stage `index` (0-based) of `spec` makes, its brief
/// rendered from `values` -- `input` and the earlier stages' answers by name.
/// Exposed so the wire is testable on its own.
[[nodiscard]] agentloop::MemberCall stage_call(const harness::SymphonySpec& spec, std::size_t index,
                                               const std::map<std::string, std::string>& values,
                                               const PlayInput& input, const PlayOptions& options);

/// Why `spec` cannot be played with `input` -- its validation problems, a
/// missing input it reads, an image it needs or does not take -- or empty.
[[nodiscard]] std::string refusal(const harness::SymphonySpec& spec, const PlayInput& input);

/// Plays `spec` with `input`, every stage a call against `calls`' open turn
/// -- on the harness `calls` holds, under its active suite -- whose
/// narration says each stage and whose cancellation stops the walk
/// (`CancelledError` is thrown through). Never throws for a refusal or a
/// failed stage: the result says it.
[[nodiscard]] PlayResult play(const harness::SymphonySpec& spec, const PlayInput& input,
                              agentloop::MemberCalls& calls, const PlayOptions& options = {});

}  // namespace apogee::symphony
