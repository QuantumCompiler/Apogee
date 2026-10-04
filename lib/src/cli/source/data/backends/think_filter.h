#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// A streaming filter that separates a model's private reasoning from its
/// answer.
///
/// **Built against real output, not a specification.** Qwen 3.6 on this
/// machine answers "What is 2+2?" with:
///
/// ```
/// <think>
///
/// </think>
///
/// 4
/// ```
///
/// Every one of those characters reaches the user today. That is the bug this
/// exists to fix, and it is why the filter is not optional dressing: a reasoning
/// model without one shows its working as though it were the reply.
///
/// ## Streaming, with a hold-back buffer
///
/// Tokens arrive in whatever pieces the model produces, so an opener can be
/// split across two of them — `<thi` then `nk>`. Bytes that could still turn
/// out to be part of a marker are held until a later `write` or `flush`
/// resolves them. The buffer is bounded by the longest marker, so the delay is
/// a few characters and never a sentence.
///
/// ## Two rules earned elsewhere
///
/// **A partial marker at end of stream was never a marker.** `flush` emits it
/// verbatim rather than eating it: a model that legitimately ends a sentence
/// with `<` should not have it silently removed.
///
/// **An empty pair list must not underflow the hold-back.** Ommi's version
/// computed the hold-back from the longest marker and panicked on a negative
/// slice when there were no markers at all. Here the empty case is a
/// pass-through with no arithmetic, and a test pins it.
namespace apogee::backends {

/// One open/close reasoning-block pair.
struct TagPair {
    std::string open;
    std::string close;
};

/// The pairs seen in the wild, and the default when a profile says nothing.
///
/// `<think>` is Qwen and DeepSeek-R1 — **verified here against Qwen 3.6**.
/// `<thinking>` and `<reasoning>` are carried from Ommi's list; they cost
/// nothing to recognise and the permissive-unknown rule says an uncharacterised
/// model should have every format it might emit recognised.
///
/// Channel-marker formats (`<|channel|>analysis<|message|>`) are deliberately
/// NOT here: they are not open/close pairs and shoehorning them in would make
/// this filter wrong about both. They belong to the markup filter.
[[nodiscard]] const std::vector<TagPair>& default_think_pairs();

class ThinkFilter {
public:
    /// Receives reasoning text as it arrives, tags excluded. Optional: a
    /// surface that has nowhere to show reasoning simply drops it.
    using ThinkingSink = std::function<void(std::string_view)>;

    ThinkFilter() = default;

    /// `pairs` empty means "recognise nothing" — a verified-none profile, and a
    /// pure pass-through. Use `default_think_pairs()` for the permissive case.
    explicit ThinkFilter(std::vector<TagPair> pairs) : pairs_{std::move(pairs)} {}

    void on_thinking(ThinkingSink sink) {
        thinking_ = std::move(sink);
    }

    /// Consumes `chunk` and returns the text safe to emit now.
    [[nodiscard]] std::string write(std::string_view chunk);

    /// Returns whatever is still held. End of stream resolves any ambiguity: a
    /// partial marker was never a marker, so it is emitted rather than eaten.
    /// The residue of an unterminated reasoning block goes to the thinking
    /// sink, not to the answer.
    [[nodiscard]] std::string flush();

    /// Whether the filter is currently inside a reasoning block.
    [[nodiscard]] bool inside() const noexcept {
        return !close_.empty();
    }

private:
    [[nodiscard]] std::string drain(bool at_end);
    [[nodiscard]] std::size_t longest_marker() const noexcept;

    std::vector<TagPair> pairs_;
    ThinkingSink thinking_;
    std::string pending_;
    /// The active pair's close marker; empty means "outside a block".
    std::string close_;
};

/// One-shot convenience for text that is already complete.
[[nodiscard]] std::string strip_think_blocks(std::string_view text,
                                             const std::vector<TagPair>& pairs);

}  // namespace apogee::backends
