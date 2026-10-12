#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "contracts/cancellation.h"
#include "contracts/types.h"
#include "harness/harness.h"

/// The reasoning as one line, kept current while it streams (43): the
/// fourth chore beside the query rewrite, the tool summary and the thinking
/// judge.
///
/// A local model's reasoning is its raw chain of thought, verbose where a
/// reader wants the gist. While it streams, the utility model is asked every
/// few seconds for one short present-tense line of what it is doing now
/// (*comparing the two config paths*), and the surfaces draw that line in the
/// raw window's place. **One model call at a time** (the user's call): the
/// summary is written inside the reasoning stream's own callback, so the chat
/// model's stream waits for it -- a local provider is never driven from two
/// threads. The cadence bounds what that pause costs.
///
/// The loop runs it only for a surface that draws it, only on an unmetered
/// utility member that is not the chat model itself, and only for reasoning
/// that is raw -- a vendor's own summary (`ModelBehavior::reasoning_summarized`)
/// is shown as it is.
namespace apogee::agentloop {

/// How long the reasoning runs before its first line is asked for.
inline constexpr std::chrono::milliseconds kSummaryFirst{1500};
/// How long a line stands before the next is asked for, from when it
/// arrived: the chat model's stream waits while a line is written, and this
/// bounds that wait's share of the reasoning.
inline constexpr std::chrono::milliseconds kSummaryEvery{4000};
/// The least new reasoning, in bytes, worth a new line.
inline constexpr std::size_t kSummaryNewReasoning = 120;
/// The most of the reasoning's tail the utility model is shown, in bytes.
inline constexpr std::size_t kSummaryInput = 3000;
/// The line's longest, in characters; past it the line is cut with `…`.
inline constexpr std::size_t kSummaryLine = 80;
/// The reply's budget, in tokens.
inline constexpr std::int64_t kSummaryTokens = 40;

/// The request asking `backend` for the line, over `reasoning`'s tail. A side
/// request with no thinking of its own. Exposed so its shape is testable.
[[nodiscard]] harness::ChatRequest reasoning_summary_request(const std::string& backend,
                                                             std::string_view reasoning);

/// The line a reply gives: its first line with words in it, a list mark,
/// quotes and a closing full stop dropped, spaces collapsed, cut to
/// `kSummaryLine` characters. Empty when the reply says nothing.
[[nodiscard]] std::string summary_line(std::string_view reply);

/// Who summarises a chat model's reasoning (43), decided once -- the loop's
/// rule, and what `/thinking-display` says of it.
struct SummaryRoute {
    /// The member that writes the line; empty when none does.
    std::string model;
    /// The reasoning is the vendor's own summary, shown as it is.
    bool vendor = false;
    /// Why no line can be had, when it cannot and it is not the vendor's.
    std::string why_not;
};

/// The route for `chat_model`'s reasoning, `summary_model` the utility model
/// (empty for none): the vendor's own summary is shown as it is; otherwise
/// the utility model writes the line when it is set, is not the chat model
/// itself, and is unmetered -- never a charge for a display.
[[nodiscard]] SummaryRoute route_summary(const harness::Harness& harness,
                                         std::string_view chat_model,
                                         std::string_view summary_model);

/// One model call's reasoning, summarised as it streams.
class ReasoningSummarizer {
public:
    using Clock = std::function<std::chrono::steady_clock::time_point()>;

    ReasoningSummarizer(const harness::Harness& harness, std::string backend,
                        harness::CancellationToken cancellation, Clock clock = {});

    /// One chunk of the reasoning. When a line is due -- the first once the
    /// reasoning has run `kSummaryFirst`, each next `kSummaryEvery` after the
    /// last arrived, with `kSummaryNewReasoning` new since -- the utility
    /// model is asked for it now, on this thread, and the line returned when
    /// it is new. A failed call ends the summaries for this model call
    /// (`failed()`); a cancelled one ends them quietly, the chat model's own
    /// stream ending the turn.
    [[nodiscard]] std::optional<std::string> feed(std::string_view chunk);

    /// Whether a call failed: no more are asked for.
    [[nodiscard]] bool failed() const noexcept {
        return failed_;
    }

private:
    const harness::Harness& harness_;
    std::string backend_;
    harness::CancellationToken cancellation_;
    Clock clock_;

    /// The reasoning's tail: the most the utility model is shown.
    std::string tail_;
    std::size_t total_ = 0;
    std::size_t summarized_ = 0;
    std::optional<std::chrono::steady_clock::time_point> began_;
    std::optional<std::chrono::steady_clock::time_point> last_;
    std::string line_;
    bool failed_ = false;
    bool stopped_ = false;
};

}  // namespace apogee::agentloop
