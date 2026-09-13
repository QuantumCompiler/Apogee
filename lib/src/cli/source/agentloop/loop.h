#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "agent/tool.h"
#include "agentloop/content.h"
#include "agentloop/question.h"
#include "agentloop/reporter.h"
#include "harness/harness.h"
#include "harness/types.h"

/// The model→tool→model loop.
///
/// **Extracted before the surfaces multiply, not after.** That ordering is
/// Ommi's most load-bearing sequencing lesson: the loop behind a Reporter is
/// what kept its four front-ends consistent, and what made deleting one a local
/// change. `apogee complete --tools` is the first consumer; chat and serve are
/// thin adapters over the same `run()`.
///
/// The loop operates on the IR only and includes no backend. A provider's tool
/// dialect is translated at the backends boundary, so the loop never learns
/// that Anthropic calls it `tool_use` and OpenAI calls it `tool_calls`.
namespace apogee::agentloop {

struct Options {
    /// Backend or model name; empty means the configured default.
    std::string model;

    std::optional<double> temperature;
    std::optional<std::int64_t> max_tokens;

    /// Tools the model may call. Empty means a plain single-turn completion.
    const agent::ToolRegistry* tools = nullptr;

    /// Prompts the user. **Null means `ask_user` is never advertised** — not
    /// advertised-and-refused, absent from the request entirely. A model told
    /// it may ask questions on a surface with nobody attached will eventually
    /// ask one, and then either hang or invent the answer.
    AskFn ask;

    /// Gates destructive tools. See agent::permitted — Ask with no confirm
    /// function resolves to deny.
    agent::PermissionChecker permission;
    agent::ConfirmFn confirm;

    /// Injected into each outgoing request at `transient_at`, never appended to
    /// history. The seam RAG will ride; see splice_transient.
    std::vector<harness::ChatMessage> transient_prefix;
    std::size_t transient_at = 0;

    /// Whether the final answer is delivered live through the Reporter.
    /// Status reporting happens either way.
    bool stream_answer = true;

    /// Hard ceiling on model→tool→model cycles.
    ///
    /// A model can loop calling the same tool forever, and without a bound the
    /// only symptom is a request that never returns while spending money. When
    /// it trips, the loop asks for a final answer with tools withdrawn rather
    /// than erroring — the user gets something usable instead of nothing.
    int max_iterations = 12;

    harness::CancellationToken cancellation;
};

struct RunResult {
    /// The final assistant text.
    std::string answer;

    /// Tokens across every model call in the run. `estimated` is true if ANY
    /// call fell back to estimation — a partly-exact total is an estimate.
    TokenCount tokens;

    /// The same accounting split into prompt and completion, summed over the
    /// calls that REPORTED usage and nothing else. `reported()` is false when
    /// no call did -- a served response then omits its usage block rather
    /// than sending zeros, because absent is not zero.
    harness::Usage usage;

    /// Why the FINAL model call stopped, as the provider reported it. A served
    /// response passes it on, because a client reads `length` as "the answer
    /// was cut short" -- and an empty answer from a reasoning model that spent
    /// its whole budget thinking is exactly that, not a `stop`.
    harness::FinishReason finish_reason = harness::FinishReason::Stop;

    /// Model→tool→model cycles performed.
    int iterations = 0;

    /// Whether max_iterations was hit and the answer came from the
    /// tools-withdrawn final call.
    bool hit_iteration_limit = false;
};

/// Drives the loop until the model stops asking for tools.
///
/// `history` is extended in place with every turn, so a caller that persists a
/// conversation gets exactly what was said. **Injected transient content never
/// lands there.**
///
/// On an aborted tool phase — the user cancelling an `ask_user` prompt — the
/// half-turn is rolled back out of history before the exception propagates, so
/// no dangling assistant message with unanswered tool calls is left behind.
[[nodiscard]] RunResult run(const harness::Harness& harness,
                            std::vector<harness::ChatMessage>& history, const Options& options,
                            Reporter& reporter);

/// Convenience overload for callers that want no output at all.
[[nodiscard]] RunResult run(const harness::Harness& harness,
                            std::vector<harness::ChatMessage>& history, const Options& options);

/// The tool definitions for a request under `options`: the registry's tools,
/// plus `ask_user` when an AskFn is present.
///
/// Exposed for testing the advertisement rule directly — it is a contract, not
/// an implementation detail.
[[nodiscard]] std::vector<harness::Tool> advertised_tools(const Options& options);

}  // namespace apogee::agentloop
