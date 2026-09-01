#include "commands/thinking_view.h"

#include <algorithm>
#include <chrono>
#include <sstream>

namespace apogee::commands {
namespace {

/// Length in bytes of the UTF-8 sequence starting at `lead`.
std::size_t sequence_length(unsigned char lead) noexcept {
    if ((lead & 0x80U) == 0) {
        return 1;
    }
    if ((lead & 0xE0U) == 0xC0U) {
        return 2;
    }
    if ((lead & 0xF0U) == 0xE0U) {
        return 3;
    }
    if ((lead & 0xF8U) == 0xF0U) {
        return 4;
    }
    return 1;  // a stray continuation byte: treat as one, never advance by zero
}

std::int64_t now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

std::size_t display_width(std::string_view text) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < text.size();) {
        i += sequence_length(static_cast<unsigned char>(text[i]));
        ++count;
    }
    return count;
}

std::vector<std::string> wrap_tail(std::string_view text, std::size_t width,
                                   std::size_t max_lines) {
    if (width == 0 || max_lines == 0) {
        return {};
    }

    std::vector<std::string> rows;
    std::string current;
    std::size_t cells = 0;

    auto flush_row = [&rows, &current, &cells]() {
        // Blank rows are dropped: a paragraph break inside the reasoning would
        // otherwise spend one of only two precious rows painting nothing.
        if (!current.empty()) {
            rows.push_back(current);
        }
        current.clear();
        cells = 0;
    };

    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '\n') {
            flush_row();
            ++i;
            continue;
        }
        if (text[i] == '\r') {
            ++i;
            continue;
        }

        // Advance a whole codepoint. Splitting one across rows corrupts the
        // output, and a `std::string` makes that mistake easy to reach for.
        const std::size_t length =
            std::min(sequence_length(static_cast<unsigned char>(text[i])), text.size() - i);
        current.append(text, i, length);
        i += length;
        ++cells;

        if (cells >= width) {
            flush_row();
        }
    }
    flush_row();

    if (rows.size() > max_lines) {
        rows.erase(rows.begin(),
                   rows.begin() + static_cast<std::ptrdiff_t>(rows.size() - max_lines));
    }
    return rows;
}

ThinkingView::ThinkingView(TerminalWriter& writer, Options options)
    : writer_{writer}, options_{std::move(options)}, clock_{now_seconds} {}

std::size_t ThinkingView::content_width() const noexcept {
    // Two columns of indent under the header.
    constexpr std::size_t kIndent = 2;
    return options_.width > kIndent + 1 ? options_.width - kIndent : 1;
}

void ThinkingView::write(std::string_view chunk) {
    if (!options_.active || chunk.empty()) {
        // An empty chunk opens NOTHING. Redacted-thinking models emit thinking
        // events with an empty payload, and opening on one renders an empty
        // reasoning block on every such turn.
        return;
    }

    if (options_.verbose) {
        // A permanent transcript: no cursor movement, no collapse.
        writer_.write(chunk);
        open_ = true;
        return;
    }

    if (!open_) {
        open_ = true;
        started_ = clock_();
        painted_ = 0;
    }

    tail_ += chunk;
    if (tail_.size() > kMaxRetainedTail) {
        // Trim from the front, then advance to the next codepoint boundary so
        // the retained text never starts mid-sequence.
        std::size_t cut = tail_.size() - kMaxRetainedTail;
        while (cut < tail_.size() && (static_cast<unsigned char>(tail_[cut]) & 0xC0U) == 0x80U) {
            ++cut;
        }
        tail_.erase(0, cut);
    }

    writer_.with_lock([this](std::ostream& out) { repaint_locked(out); });
}

void ThinkingView::repaint_locked(std::ostream& out) {
    const std::vector<std::string> rows = wrap_tail(tail_, content_width(), kTailLines);

    erase_locked(out);

    out << "✻ Thinking…";
    for (const std::string& row : rows) {
        out << "\n  " << options_.style.dim(row);
    }
    // Deliberately NO trailing newline: erase_locked's row count depends on the
    // cursor still sitting on the last painted row.
    painted_ = 1 + rows.size();
}

void ThinkingView::erase_locked(std::ostream& out) {
    if (painted_ == 0) {
        return;
    }
    out << ansi::kEraseLine;
    for (std::size_t i = 1; i < painted_; ++i) {
        out << ansi::kUpAndErase;
    }
    painted_ = 0;
}

void ThinkingView::finish() {
    if (!open_) {
        return;  // idempotent: the end-of-turn path calls this unconditionally
    }
    open_ = false;

    if (options_.verbose) {
        writer_.write("\n");
        return;
    }
    if (!options_.active) {
        return;
    }

    const std::int64_t seconds = std::max<std::int64_t>(1, clock_() - started_);
    const std::string summary =
        options_.style.dim("✻ Thought for " + std::to_string(seconds) + "s");
    tail_.clear();

    writer_.with_lock([this, &summary](std::ostream& out) {
        erase_locked(out);
        // The single line that survives in scrollback.
        out << summary << "\n\n";
    });
}

void ThinkingView::abandon() {
    if (!open_ && painted_ == 0) {
        return;
    }
    open_ = false;
    tail_.clear();
    if (!options_.active || options_.verbose) {
        return;
    }
    writer_.with_lock([this](std::ostream& out) { erase_locked(out); });
}

}  // namespace apogee::commands
