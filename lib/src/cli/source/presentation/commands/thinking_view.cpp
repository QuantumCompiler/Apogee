#include "commands/thinking_view.h"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <utility>

namespace apogee::commands {
namespace {

std::int64_t now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

ThinkingView::ThinkingView(TerminalWriter& writer, Options options)
    : writer_{writer}, options_{std::move(options)}, clock_{now_seconds} {}

std::size_t ThinkingView::content_width() const {
    // Two columns of indent under the header, and the terminal's last column
    // never written. A row that fills it is where terminals disagree: most
    // hold the cursor at the edge until the next character, some wrap at
    // once -- and on those the newline after a full row lands a line lower
    // than counted, so the erase misses the header and every repaint leaves
    // a line behind (found live, 2026-09-23).
    constexpr std::size_t kIndent = 2;
    constexpr std::size_t kMargin = 1;
    const std::size_t width = options_.measure ? options_.measure() : options_.width;
    return width > kIndent + kMargin + 1 ? width - kIndent - kMargin : 1;
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
    const std::vector<std::string> rows = ansi::wrap_tail(tail_, content_width(), kTailLines);

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
