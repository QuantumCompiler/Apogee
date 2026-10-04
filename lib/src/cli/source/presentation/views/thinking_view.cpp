#include "views/thinking_view.h"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <utility>

namespace apogee::commands {
namespace {

/// `text` cut to at most `cells` terminal columns, never inside a character.
std::string cut_to(const std::string& text, std::size_t cells) {
    if (ansi::display_width(text) <= cells) {
        return text;
    }
    std::string out;
    std::size_t used = 0;
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t length = std::max<std::size_t>(
            1, ansi::utf8_sequence_length(static_cast<unsigned char>(text[at])));
        const std::size_t width = ansi::codepoint_cells(ansi::decode_utf8(text, at, length));
        if (used + width + 1 > cells) {
            break;
        }
        out.append(text, at, length);
        used += width;
        at += length;
    }
    return out + "…";
}

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
        if (!open_) {
            budget_reached_ = false;
        }
        writer_.write(chunk);
        open_ = true;
        reasoned_ = true;
        return;
    }

    open_block();
    reasoned_ = true;
    append(chunk);
}

void ThinkingView::open_block() {
    if (!open_) {
        open_ = true;
        budget_reached_ = false;
        reasoned_ = false;
        started_ = clock_();
        painted_ = 0;
    }
}

void ThinkingView::side_call(std::string_view label) {
    if (!options_.active || label.empty()) {
        return;
    }
    const std::string line = cut_to(std::string{"· "} + std::string{label}, content_width());
    if (options_.verbose) {
        if (!open_) {
            budget_reached_ = false;
        }
        open_ = true;
        writer_.write(options_.style.dim(line) + "\n");
        return;
    }
    open_block();
    // A line of its own, whatever the reasoning was in the middle of.
    std::string text;
    if (!tail_.empty() && tail_.back() != '\n') {
        text += '\n';
    }
    text += line + "\n";
    append(text);
}

void ThinkingView::side_call_done(std::string_view label, std::string_view suffix) {
    if (!options_.active || suffix.empty() || !open_) {
        return;
    }
    const std::string opened = std::string{"· "} + std::string{label};
    if (options_.verbose) {
        writer_.write(options_.style.dim(cut_to(opened, content_width()) + std::string{suffix}) +
                      "\n");
        return;
    }
    // The latest line the call opened, completed where it stands -- cut so
    // that it still fits with its suffix.
    const std::string started = cut_to(opened, content_width()) + "\n";
    const std::size_t at = tail_.rfind(started);
    if (at == std::string::npos) {
        return;
    }
    const std::size_t suffix_cells = ansi::display_width(suffix);
    const std::size_t room =
        content_width() > suffix_cells + 1 ? content_width() - suffix_cells : 1;
    tail_.replace(at, started.size(), cut_to(opened, room) + std::string{suffix} + "\n");
    writer_.with_lock([this](std::ostream& out) { repaint_locked(out); });
}

bool ThinkingView::print_above(std::string_view line) {
    if (!open_ || !options_.active || options_.verbose || painted_ == 0) {
        return false;
    }
    writer_.with_lock([this, line](std::ostream& out) {
        erase_locked(out);
        out << line << "\n";
        repaint_locked(out);
    });
    return true;
}

void ThinkingView::append(std::string_view text) {
    tail_ += text;
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
        // No summary to carry the note: the transcript ends with it instead.
        writer_.write(budget_reached_ ? "\n" + options_.style.dim("✻ (budget reached)") + "\n"
                                      : std::string{"\n"});
        budget_reached_ = false;
        return;
    }
    if (!options_.active) {
        return;
    }

    const std::int64_t seconds = std::max<std::int64_t>(1, clock_() - started_);
    // A block of side calls alone did no thinking: it worked (26n).
    const std::string summary = options_.style.dim(
        std::string{reasoned_ ? "✻ Thought for " : "✻ Worked for "} + std::to_string(seconds) +
        "s" + (budget_reached_ ? std::string{" (budget reached)"} : std::string{}));
    budget_reached_ = false;
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
