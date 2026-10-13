#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "agent/tool.h"
#include "agentloop/loop.h"
#include "agentloop/reporter.h"
#include "harness/harness.h"

/// The delegate (41e): a bounded sub-session as a tool.
///
/// A long job stays inside its context window by handing a self-contained
/// piece to a fresh context and keeping only the conclusion. `delegate`
/// does that **with the one loop, re-entered** -- never a second loop: its
/// call runs `agentloop::run` over a fresh conversation, on the session's
/// model (or a role's, through the one resolver), with the tools the parent
/// offers less `delegate` itself -- so depth is exactly one -- under **the
/// parent's own gate**: the config's permissions, the session's answers and
/// mode, a prompt reaching the person where the parent's would, and nobody
/// to ask denying as it does for the parent. The parent's cancellation stops
/// it mid-sub-turn. It is bounded by construction: `kDelegateSubTurns`
/// model calls, each answer capped at `kDelegateAnswerTokens`, the spend
/// said in the result whether it finished or hit a cap. It is narrated as a
/// side call carrying its tool lines, and nothing of it is saved but its
/// result -- the parent's tool call's detail, never a chat of its own.
///
/// `consult` (27f) is untouched: a brief-only member call stays one; this is
/// a different tool over the loop itself.
namespace apogee::tools {

/// The bounds (2026-10-13, the item's default): model calls a delegated
/// sub-session may make, and the most each answer may run to.
inline constexpr int kDelegateSubTurns = 8;
inline constexpr std::int64_t kDelegateAnswerTokens = 4096;

/// What a `delegate` call re-enters the loop with: the turn that made it.
struct DelegateContext {
    const harness::Harness* harness = nullptr;
    /// The tools the parent's turn offers; the sub-session gets them less
    /// `delegate`.
    const agent::ToolRegistry* tools = nullptr;
    /// The parent turn's options -- its model, thinking, gate and mode note,
    /// cancellation -- which the sub-session's are made from.
    agentloop::Options parent;
    /// Where the delegation is narrated: the parent's reporter.
    agentloop::Reporter* reporter = nullptr;
    /// The parent's standing context and mode note (41b, 41c, 39b), ridden
    /// as the sub-session's system context: a plan-mode parent's delegate
    /// plans too, and a project's rules bind it.
    std::vector<std::string> notes;
};

/// Makes `context` the one `delegate` uses on this thread, for the life of
/// the object; the one before is current again after.
class DelegateScope {
public:
    explicit DelegateScope(DelegateContext context);
    ~DelegateScope();

    DelegateScope(const DelegateScope&) = delete;
    DelegateScope& operator=(const DelegateScope&) = delete;
    DelegateScope(DelegateScope&&) = delete;
    DelegateScope& operator=(DelegateScope&&) = delete;

private:
    DelegateContext context_;
    DelegateContext* previous_ = nullptr;
};

/// One delegation, run in `context` with the call's `arguments` (`{"brief",
/// "role"?}`): the sub-session's answer and a one-line account of what it
/// spent, or a refusal -- no brief, an unknown role, a role whose backend
/// bills per call.
[[nodiscard]] agent::ToolOutcome run_delegate(const DelegateContext& context,
                                              std::string_view arguments);

/// The tools a delegated sub-session is offered: `registry` less `delegate`,
/// its environment note kept.
[[nodiscard]] agent::ToolRegistry delegate_kit(const agent::ToolRegistry& registry);

/// What the environment note says of `delegate` where it is registered:
/// when to hand work off, and that the brief is all the delegate gets.
[[nodiscard]] std::string delegate_policy();

/// Registers `delegate` into `registry`.
void register_delegate_tool(agent::ToolRegistry& registry);

}  // namespace apogee::tools
