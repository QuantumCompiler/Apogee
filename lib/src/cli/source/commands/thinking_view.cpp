#include "commands/thinking_view.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <sstream>
#include <utility>

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

/// The codepoint starting at `text[at]`, decoded; `length` is its byte count.
/// A malformed sequence decodes as its lead byte.
char32_t decode_at(std::string_view text, std::size_t at, std::size_t length) noexcept {
    const auto lead = static_cast<unsigned char>(text[at]);
    if (length == 1) {
        return lead;
    }
    char32_t point = 0;
    switch (length) {
        case 2:
            point = lead & 0x1FU;
            break;
        case 3:
            point = lead & 0x0FU;
            break;
        default:
            point = lead & 0x07U;
            break;
    }
    for (std::size_t i = 1; i < length; ++i) {
        const auto next = static_cast<unsigned char>(text[at + i]);
        if ((next & 0xC0U) != 0x80U) {
            return lead;
        }
        point = (point << 6U) | (next & 0x3FU);
    }
    return point;
}

/// Cells a terminal gives `point`: see `display_width`.
std::size_t cells_of(char32_t point) noexcept {
    using Range = std::pair<char32_t, char32_t>;  // first and last, inclusive
    constexpr std::array<Range, 9> kZero{
        Range{0x0300, 0x036F}, Range{0x1AB0, 0x1AFF}, Range{0x1DC0, 0x1DFF},
        Range{0x200B, 0x200F}, Range{0x2060, 0x2064}, Range{0x20D0, 0x20FF},
        Range{0xFE00, 0xFE0F}, Range{0xFE20, 0xFE2F}, Range{0xE0100, 0xE01EF}};
    constexpr std::array<Range, 15> kWide{
        Range{0x1100, 0x115F},   Range{0x2E80, 0x303E},   Range{0x3041, 0x33FF},
        Range{0x3400, 0x4DBF},   Range{0x4E00, 0x9FFF},   Range{0xA000, 0xA4CF},
        Range{0xAC00, 0xD7A3},   Range{0xF900, 0xFAFF},   Range{0xFE30, 0xFE4F},
        Range{0xFF00, 0xFF60},   Range{0xFFE0, 0xFFE6},   Range{0x1F300, 0x1F64F},
        Range{0x1F900, 0x1F9FF}, Range{0x20000, 0x2FFFD}, Range{0x30000, 0x3FFFD}};
    const auto within = [point](const Range& range) {
        return point >= range.first && point <= range.second;
    };
    if (std::ranges::any_of(kZero, within)) {
        return 0;
    }
    return std::ranges::any_of(kWide, within) ? 2 : 1;
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
        const std::size_t length =
            std::min(sequence_length(static_cast<unsigned char>(text[i])), text.size() - i);
        count += cells_of(decode_at(text, i, length));
        i += length;
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
        const std::size_t cells_here = cells_of(decode_at(text, i, length));
        if (cells > 0 && cells + cells_here > width) {
            flush_row();  // a wide character that would straddle the edge starts the next row
        }
        current.append(text, i, length);
        i += length;
        cells += cells_here;

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
