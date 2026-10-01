#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "agentloop/content.h"
#include "harness/harness.h"
#include "harness/types.h"

/// The per-turn context budget (26c): each request assembled against the
/// model's real window, by priority, instead of from each source's own cap.
///
/// On a local model every token sent is read at about a hundred a second on
/// a 27B, and a small model's attention degrades as unrelated text piles up.
/// So what is sent is shaped here, once, for every surface:
///
/// - **A finished turn's tool results are sent as stubs**: one line naming
///   the call, its size, and that it can be called again. The answer that used
///   a result already carries what mattered. Only what is SENT changes; the
///   saved transcript keeps every result whole.
/// - **Retrieval asks for its share** (`share(Retrieval)`) and injects only
///   the chunks that fit it.
/// - **Everything else is trimmed only on overflow** -- when the whole request
///   would not fit the window after the answer's reserve -- in reverse
///   priority, each source first down to its share and then below it:
///   earlier exchanges, oldest first, to none; this turn's tool results,
///   oldest first, to their share; then, still over, those results to the
///   newest alone and the injected context to none. Every trim is said.
///
/// **An unknown window never reads as room to spare**: with no window,
/// nothing is sized by it, the sources keep their fixed caps, and nothing is
/// trimmed -- only the stubs, which need no window, still apply.
namespace apogee::agentloop {

/// The answer's reserve when neither the request nor its backend names a
/// `max_tokens`: the larger of the providers' own defaults.
inline constexpr std::int64_t kDefaultReplyReserve = 4096;

/// A finished turn's tool result this size or smaller is sent whole: a stub
/// is no smaller, and it would lose the content for nothing.
inline constexpr std::size_t kStubbedAbove = 512;

/// The sources that are given a share, in priority order. The system prompt,
/// the tools and the question come before them all and are never trimmed;
/// history, last, takes whatever the others leave.
enum class BudgetSource : std::uint8_t { Attachments, Retrieval, ToolResults };

/// Each source's share of the window after the reserve (26c, defaults taken).
struct BudgetShares {
    double attachments = 0.30;
    double retrieval = 0.20;
    double tool_results = 0.25;
};

/// A request's window and what is held back from it for the answer.
struct ContextBudget {
    /// The model's window in tokens; 0 when unknown.
    std::int64_t window = 0;
    /// Held back for the answer: its `max_tokens`.
    std::int64_t reserve = kDefaultReplyReserve;
    BudgetShares shares;

    [[nodiscard]] bool known() const noexcept {
        return window > 0;
    }

    /// The window after the reserve; 0 when the window is unknown.
    [[nodiscard]] std::int64_t available() const noexcept;

    /// `source`'s share of `available()`; 0 when the window is unknown, so a
    /// caller must ask `known()` first rather than read 0 as "nothing fits".
    [[nodiscard]] std::int64_t share(BudgetSource source) const noexcept;
};

/// Counts a request's prompt tokens, and says whether the count is exact.
using TokenCounter = std::function<TokenCount(const harness::ChatRequest&)>;

/// A budget and the way to count against it: what a request is assembled
/// with, and what a source that sizes itself is handed.
struct TurnBudget {
    ContextBudget budget;
    /// Empty: estimate.
    TokenCounter count;

    /// `request`'s tokens by `count`, or the estimate when it is empty.
    [[nodiscard]] TokenCount tokens(const harness::ChatRequest& request) const;
};

/// The budget for a request to `model`: the window the harness knows for it,
/// and `max_tokens` held back -- else the backend's configured `max_tokens`,
/// else `kDefaultReplyReserve`.
[[nodiscard]] ContextBudget budget_for(const harness::Harness& harness, const std::string& model,
                                       std::optional<std::int64_t> max_tokens);

/// The estimate for a whole request: its messages and its tool definitions.
[[nodiscard]] TokenCount estimate_request_tokens(const harness::ChatRequest& request);

/// Counts exactly where `model`'s provider can (a loaded local model), and
/// estimates where not.
[[nodiscard]] TokenCounter token_counter(const harness::Harness& harness, const std::string& model);

/// `budget_for` and `token_counter` together, for a surface to hand on.
[[nodiscard]] TurnBudget turn_budget(const harness::Harness& harness, const std::string& model,
                                     std::optional<std::int64_t> max_tokens);

/// Where the turn in progress starts: the last user message. Everything
/// before it belongs to finished turns. `messages.size()` when there is none.
[[nodiscard]] std::size_t current_turn_start(const std::vector<harness::ChatMessage>& messages);

/// The one line sent in place of a tool result: the call, with its arguments
/// clipped, how much it returned, and `why` it is not here.
[[nodiscard]] std::string tool_result_stub(const std::string& call, std::size_t bytes,
                                           std::string_view why);

/// Messages with the tool results before `before` sent as stubs.
struct Stubbed {
    std::vector<harness::ChatMessage> messages;
    /// How many results were replaced, and their bytes.
    int stubs = 0;
    std::size_t bytes = 0;
};

/// `messages` with every tool result before `before` over `kStubbedAbove`
/// bytes replaced by its stub -- except `ask_user`'s, which are the user's
/// own answers. Pure: `messages` is not touched.
[[nodiscard]] Stubbed stub_tool_results(const std::vector<harness::ChatMessage>& messages,
                                        std::size_t before);

/// An attachment inlined whole (26d): its text rides the user message at
/// `message` in history, in what is sent only -- never in the transcript.
struct InlineAttachment {
    std::size_t message = 0;
    /// Its name, as a trim reports it.
    std::string name;
    /// The block, as it is prepended to the message.
    std::string text;
};

/// One request's messages, as the budget sends them.
struct Assembly {
    std::vector<harness::ChatMessage> messages;
    /// Where the injected context sits in `messages`, for the request's
    /// transient markers; length 0 when none was sent.
    std::size_t transient_start = 0;
    std::size_t transient_length = 0;
    /// The count the last decision was made on; zero with the window unknown.
    TokenCount tokens;
    /// Finished turns' tool results sent as stubs.
    int stubs = 0;
    std::size_t stubbed_bytes = 0;
    /// What overflow cost, one phrase each, for the user.
    std::vector<std::string> trims;
    /// Inlined attachments this request could not carry: their exchange was
    /// dropped, or the window was full even without them being anywhere else.
    std::vector<std::string> inline_dropped;
};

/// Assembles `history` with `pinned` then `injected` spliced in at `at` --
/// `pinned` is never trimmed (the tools' environment note), `injected` is
/// (retrieval, a review note) -- for a request shaped like `shape` (its model
/// and tools; its messages are ignored). `turn_start` is where the turn in
/// progress starts in `history`. See the file comment for the order.
///
/// `inlined` attachments are prepended to the user messages they ride. They
/// are the highest priority the budget trims, so the last: an exchange
/// dropped takes its attachment with it, and past the injected context an
/// overflow strips them, oldest first. Each one not sent is named in
/// `inline_dropped`.
[[nodiscard]] Assembly assemble_request(const TurnBudget& budget, const harness::ChatRequest& shape,
                                        const std::vector<harness::ChatMessage>& history,
                                        const std::vector<harness::ChatMessage>& pinned,
                                        const std::vector<harness::ChatMessage>& injected,
                                        std::size_t at, std::size_t turn_start,
                                        const std::vector<InlineAttachment>& inlined = {});

/// How many of `items` -- each a leading prefix, `render(k)` the text of the
/// first k -- fit `share` tokens as one system message, counted by `budget`.
/// Monotone in k, so found by bisection. `items` when the window is unknown.
[[nodiscard]] std::size_t fitting_prefix(const TurnBudget& budget, std::int64_t share,
                                         std::size_t items,
                                         const std::function<std::string(std::size_t)>& render);

}  // namespace apogee::agentloop
