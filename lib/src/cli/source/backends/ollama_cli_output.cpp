#include "backends/ollama_cli_output.h"

namespace apogee::backends {
namespace {

/// Trims one trailing `\r`, so a CRLF stream behaves like a LF one.
[[nodiscard]] std::string_view strip_cr(std::string_view line) noexcept {
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    return line;
}

}  // namespace

void OllamaOutputDemux::reset() noexcept {
    carry_.clear();
    answer_.clear();
    thinking_.clear();
    in_thinking_ = false;
    answer_started_ = false;
    pending_separator_.clear();
}

void OllamaOutputDemux::emit_line(std::string_view line, bool had_newline, const Sink& on_answer,
                                  const Sink& on_thinking) {
    const std::string_view trimmed = strip_cr(line);

    // A marker only counts on its own line, and the opener only before any
    // answer text has been seen -- see the header on why both bounds matter.
    if (!in_thinking_ && !answer_started_ && trimmed == kOllamaThinkingOpen) {
        in_thinking_ = true;
        return;
    }
    if (in_thinking_ && trimmed == kOllamaThinkingClose) {
        in_thinking_ = false;
        return;
    }

    if (in_thinking_) {
        // Reasoning is display-only, so it keeps its line breaks as they came.
        std::string piece{trimmed};
        if (had_newline) {
            piece += '\n';
        }
        thinking_ += piece;
        if (on_thinking && !piece.empty()) {
            on_thinking(piece);
        }
        return;
    }

    // The CLI puts a blank line between the closing marker and the answer.
    // Swallowing leading blanks keeps a turn's answer from starting with
    // whitespace the model did not write.
    if (!answer_started_ && trimmed.empty()) {
        return;
    }

    // A blank line may be interior (meaningful) or trailing (framing); which
    // it is only becomes knowable later, so hold it.
    if (trimmed.empty()) {
        pending_separator_ += '\n';
        return;
    }

    // Real content, so whatever was held was interior after all.
    if (!pending_separator_.empty()) {
        answer_ += pending_separator_;
        if (on_answer) {
            on_answer(pending_separator_);
        }
        pending_separator_.clear();
    }

    answer_started_ = true;
    answer_ += trimmed;
    if (on_answer && !trimmed.empty()) {
        on_answer(trimmed);
    }
    // The terminator becomes a pending separator rather than being emitted:
    // if this is the last line, it was framing.
    if (had_newline) {
        pending_separator_ += '\n';
    }
}

void OllamaOutputDemux::feed(std::string_view bytes, const Sink& on_answer,
                             const Sink& on_thinking) {
    // Append first, then scan the joined buffer. Scanning the incoming bytes
    // alone is what loses a marker that straddles two reads -- and with a
    // marker as short as "Thinking..." that is a one-in-eleven chance per turn,
    // not a rare edge.
    carry_.append(bytes);

    std::size_t start = 0;
    for (;;) {
        const std::size_t newline = carry_.find('\n', start);
        if (newline == std::string::npos) {
            break;
        }
        emit_line(std::string_view{carry_}.substr(start, newline - start), true, on_answer,
                  on_thinking);
        start = newline + 1;
    }
    if (start > 0) {
        carry_.erase(0, start);
    }
}

void OllamaOutputDemux::flush(const Sink& on_answer, const Sink& on_thinking) {
    if (!carry_.empty()) {
        emit_line(carry_, false, on_answer, on_thinking);
        carry_.clear();
    }
    // Whatever was held at EOF was trailing framing. Dropping it is what keeps
    // a turn's answer from ending in the CLI's decorative newline.
    pending_separator_.clear();
}

}  // namespace apogee::backends
