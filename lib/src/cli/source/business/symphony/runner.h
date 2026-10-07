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
#include "symphony/definition.h"

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
///
/// **A played symphony is the same walk** (27r). A stage that plays another
/// symphony recurses through this walk, on the same turn, with its rendered
/// `input:` as that symphony's `{{input}}`: its stages are the same member
/// calls an inline stage makes, indistinguishable on the wire, and its
/// output is the stage's answer, threaded on as any answer is. Before the
/// first call the whole walk is checked against the catalog -- a loop, the
/// depth, a name nothing defines -- so a late-bound spec file that cycles is
/// refused with nothing sent. **One budget for the whole walk**: the member
/// calls and the answer tokens every symphony it reaches spends count
/// against one cap, and a walk that reaches it stops there, its position
/// and its spend named, with no answer. **Positions** are said as the
/// symphonies from the root down -- `outer → inner, stage 2/3 verify` -- in
/// every narration line and every failure of a walk that plays another;
/// one that plays none reads exactly as 27q's.
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
    /// The member calls the walk may make from `calls`' turn -- every
    /// symphony it reaches together (27r): 0 is the walk's own count, each
    /// stage once, so a play is never refused for its size; a caller playing
    /// inside a turn with a budget of its own, or a config capping a play
    /// (`symphony_caps.stage_calls`), passes that budget.
    std::int64_t per_turn = 0;
    /// The answer tokens the whole walk may produce (27r,
    /// `symphony_caps.answer_tokens`): each answer counted as its provider
    /// reports it, else as its text estimates. 0 is no cap.
    std::int64_t answer_tokens = 0;
};

/// One stage's outcome.
struct StageResult {
    std::string name;
    /// The role it played; empty for a stage that played a symphony.
    std::string role;
    /// The symphony it played (27r); empty for a role stage.
    std::string play;
    /// The backend that answered, or would have; empty for a play stage.
    std::string backend;
    /// A play stage's is the played symphony's output.
    std::string answer;
    /// The answer reached the stage's cap and was cut there (a play stage:
    /// its played symphony's last stage was).
    bool cut = false;
    /// A play stage's: its stages' tokens together, when any said.
    std::optional<std::int64_t> tokens;
    /// A play stage's: its stages' together.
    double seconds = 0.0;
    /// A play stage's: the played symphony's stages, in order (27r).
    std::vector<StageResult> stages;
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
    /// The walk stopped at its budget (27r): the member calls or the answer
    /// tokens it was given were spent.
    bool budget_spent = false;
    /// What the whole walk spent: the member calls it made (answered or
    /// failed) and the answer tokens they produced.
    std::int64_t calls = 0;
    std::int64_t tokens = 0;

    [[nodiscard]] bool ok() const noexcept {
        return failure.empty();
    }
};

/// A stage's narration label: `stage 1/2 summarize` (26n's `SideCall::role`).
[[nodiscard]] std::string stage_label(std::size_t index, std::size_t count,
                                      const harness::SymphonyStage& stage);

/// A stage's position in a walk (27r): `stage_label` alone in a symphony
/// that plays no other, and after the path of symphonies from the root down
/// in one that does -- `outer → inner, stage 2/3 verify`, `outer, stage 1/2
/// summary` -- so a chain's every line says whose stage is running.
[[nodiscard]] std::string stage_position(const std::vector<std::string>& path, bool chained,
                                         std::size_t index, std::size_t count,
                                         const harness::SymphonyStage& stage);

/// The member call stage `index` (0-based) of `spec` makes, its brief
/// rendered from `values` -- `input` and the earlier stages' answers by name.
/// Exposed so the wire is testable on its own.
[[nodiscard]] agentloop::MemberCall stage_call(const harness::SymphonySpec& spec, std::size_t index,
                                               const std::map<std::string, std::string>& values,
                                               const PlayInput& input, const PlayOptions& options);

/// Why `spec` cannot be played with `input` -- its validation problems
/// against `catalog` (a loop, the depth, a symphony it plays that cannot be
/// played, 27r), a missing input it reads, an image it needs or does not
/// take -- or empty.
[[nodiscard]] std::string refusal(const harness::SymphonySpec& spec, const Catalog& catalog,
                                  const PlayInput& input);

/// `refusal` with no other symphony to play: a definition with a play stage
/// is refused.
[[nodiscard]] std::string refusal(const harness::SymphonySpec& spec, const PlayInput& input);

/// Plays `spec` with `input`, every stage -- and every stage of each
/// symphony it plays, found in `catalog` -- a call against `calls`' open
/// turn, on the harness `calls` holds, under its active suite, whose
/// narration says each stage and whose cancellation stops the walk
/// (`CancelledError` is thrown through). Refused before any call when
/// `refusal` says so. Never throws for a refusal, a failed stage or a spent
/// budget: the result says it.
[[nodiscard]] PlayResult play(const harness::SymphonySpec& spec, const Catalog& catalog,
                              const PlayInput& input, agentloop::MemberCalls& calls,
                              const PlayOptions& options = {});

/// `play` with no other symphony to play.
[[nodiscard]] PlayResult play(const harness::SymphonySpec& spec, const PlayInput& input,
                              agentloop::MemberCalls& calls, const PlayOptions& options = {});

}  // namespace apogee::symphony
