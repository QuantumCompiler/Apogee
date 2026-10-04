#pragma once

#include <functional>
#include <string>
#include <string_view>

/// Separating thinking from the answer in `ollama run`'s output.
///
/// **This is a demultiplexer, and it exists because the CLI gave us no
/// choice.** Every other Apogee backend gets reasoning on a typed channel — a
/// `thinking_delta` event, a `thought`-flagged part. `ollama run` writes
/// thinking to *stdout*, in-band, between two literal markers:
///
/// ```
/// Thinking...
/// <the reasoning>
/// ...done thinking.
///
/// <the answer>
/// ```
///
/// The vendor-CLI design notes warn about exactly this shape: in-band markers
/// cost a filter that must tolerate a marker split across reads, and leave a
/// permanent ambiguity because a model that *writes* "Thinking..." in its
/// answer is indistinguishable from the real thing. Apogee's typed harness
/// avoids that everywhere else. Here it cannot, so the cost is paid in one
/// small, pure, heavily tested place rather than smeared through the provider.
///
/// Two rules follow from the ambiguity being unavoidable:
///
/// **Only a marker at the start of a line counts.** The CLI emits them on their
/// own line; requiring that removes the case where a model's prose merely
/// contains the words.
///
/// **The opening marker is only honoured before any answer text.** Once the
/// answer has begun, a later "Thinking..." is the model's own words. That
/// bounds the damage of a false positive to the very start of a turn, where the
/// real marker actually lives.
namespace apogee::backends {

/// Splits a stream into thinking and answer.
///
/// Fed arbitrary byte chunks — a pipe hands over whatever the kernel felt like,
/// so a marker routinely straddles two reads. Holding back a partial line is
/// what makes that safe, and is the whole reason this is a state machine
/// rather than a pair of `find` calls.
class OllamaOutputDemux {
public:
    using Sink = std::function<void(std::string_view)>;

    /// Feeds `bytes`, routing each piece to `on_answer` or `on_thinking`.
    void feed(std::string_view bytes, const Sink& on_answer, const Sink& on_thinking);

    /// Flushes whatever is held. Call at EOF.
    ///
    /// A turn whose last line has no trailing newline still said something, and
    /// with `ollama run` that final line is often the entire answer.
    void flush(const Sink& on_answer, const Sink& on_thinking);

    /// Whether the stream is currently inside a thinking block.
    [[nodiscard]] bool in_thinking() const noexcept {
        return in_thinking_;
    }

    /// The answer accumulated so far, markers and reasoning removed.
    [[nodiscard]] const std::string& answer() const noexcept {
        return answer_;
    }

    /// The reasoning accumulated so far.
    [[nodiscard]] const std::string& thinking() const noexcept {
        return thinking_;
    }

    /// Resets for a new turn.
    void reset() noexcept;

private:
    void emit_line(std::string_view line, bool had_newline, const Sink& on_answer,
                   const Sink& on_thinking);

    std::string carry_;
    std::string answer_;
    std::string thinking_;
    bool in_thinking_ = false;
    bool answer_started_ = false;

    /// Line separators not yet emitted: the newline ending the previous
    /// content line, plus any blank lines after it.
    ///
    /// Answer lines are **joined** by newlines rather than each terminated by
    /// one, and this is what makes that possible. The CLI ends every turn with
    /// a line terminator and a blank line, both of which are framing — but
    /// neither can be known to be *trailing* until something follows it or the
    /// stream ends. So they are held, released when real content arrives, and
    /// dropped at EOF.
    ///
    /// Holding rather than trimming the final string matters: the streamed
    /// tokens and the accumulated answer must agree, and a caller rendering the
    /// stream live would otherwise show a trailing gap the persisted answer
    /// does not have.
    std::string pending_separator_;
};

/// The marker that opens a thinking block, as the CLI writes it.
inline constexpr std::string_view kOllamaThinkingOpen = "Thinking...";

/// The marker that closes one.
inline constexpr std::string_view kOllamaThinkingClose = "...done thinking.";

}  // namespace apogee::backends
