#include "ansi/text_width.h"

#include <algorithm>
#include <array>
#include <utility>

namespace apogee::ansi {

std::size_t utf8_sequence_length(unsigned char lead) noexcept {
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

char32_t decode_utf8(std::string_view text, std::size_t at, std::size_t length) noexcept {
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

std::size_t codepoint_cells(char32_t point) noexcept {
    using Range = std::pair<char32_t, char32_t>;  // first and last, inclusive
    constexpr std::array<Range, 9> kZero{
        Range{0x0300, 0x036F}, Range{0x1AB0, 0x1AFF}, Range{0x1DC0, 0x1DFF},
        Range{0x200B, 0x200F}, Range{0x2060, 0x2064}, Range{0x20D0, 0x20FF},
        Range{0xFE00, 0xFE0F}, Range{0xFE20, 0xFE2F}, Range{0xE0100, 0xE01EF}};
    // East Asian Wide and Fullwidth, and the emoji with default emoji
    // presentation -- which is where ✅, ❌, ⚡, ⭐ and ✨ live, and where a
    // table of model output goes out of line when they are counted as one.
    constexpr std::array<Range, 63> kWide{
        {Range{0x1100, 0x115F},   Range{0x231A, 0x231B},   Range{0x2329, 0x232A},
         Range{0x23E9, 0x23EC},   Range{0x23F0, 0x23F0},   Range{0x23F3, 0x23F3},
         Range{0x25FD, 0x25FE},   Range{0x2614, 0x2615},   Range{0x2648, 0x2653},
         Range{0x267F, 0x267F},   Range{0x2693, 0x2693},   Range{0x26A1, 0x26A1},
         Range{0x26AA, 0x26AB},   Range{0x26BD, 0x26BE},   Range{0x26C4, 0x26C5},
         Range{0x26CE, 0x26CE},   Range{0x26D4, 0x26D4},   Range{0x26EA, 0x26EA},
         Range{0x26F2, 0x26F3},   Range{0x26F5, 0x26F5},   Range{0x26FA, 0x26FA},
         Range{0x26FD, 0x26FD},   Range{0x2705, 0x2705},   Range{0x270A, 0x270B},
         Range{0x2728, 0x2728},   Range{0x274C, 0x274C},   Range{0x274E, 0x274E},
         Range{0x2753, 0x2755},   Range{0x2757, 0x2757},   Range{0x2795, 0x2797},
         Range{0x27B0, 0x27B0},   Range{0x27BF, 0x27BF},   Range{0x2B1B, 0x2B1C},
         Range{0x2B50, 0x2B50},   Range{0x2B55, 0x2B55},   Range{0x2E80, 0x303E},
         Range{0x3041, 0x33FF},   Range{0x3400, 0x4DBF},   Range{0x4E00, 0x9FFF},
         Range{0xA000, 0xA4CF},   Range{0xA960, 0xA97F},   Range{0xAC00, 0xD7A3},
         Range{0xF900, 0xFAFF},   Range{0xFE10, 0xFE19},   Range{0xFE30, 0xFE6F},
         Range{0xFF00, 0xFF60},   Range{0xFFE0, 0xFFE6},   Range{0x16FE0, 0x16FE4},
         Range{0x17000, 0x18CFF}, Range{0x1B000, 0x1B2FF}, Range{0x1F004, 0x1F004},
         Range{0x1F0CF, 0x1F0CF}, Range{0x1F18E, 0x1F18E}, Range{0x1F191, 0x1F19A},
         Range{0x1F200, 0x1F2FF}, Range{0x1F300, 0x1F64F}, Range{0x1F680, 0x1F6FF},
         Range{0x1F7E0, 0x1F7EB}, Range{0x1F7F0, 0x1F7F0}, Range{0x1F90C, 0x1F9FF},
         Range{0x1FA70, 0x1FAFF}, Range{0x20000, 0x2FFFD}, Range{0x30000, 0x3FFFD}}};
    const auto within = [point](const Range& range) {
        return point >= range.first && point <= range.second;
    };
    if (std::ranges::any_of(kZero, within)) {
        return 0;
    }
    return std::ranges::any_of(kWide, within) ? 2 : 1;
}

std::size_t display_width(std::string_view text) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < text.size();) {
        const std::size_t length =
            std::min(utf8_sequence_length(static_cast<unsigned char>(text[i])), text.size() - i);
        count += codepoint_cells(decode_utf8(text, i, length));
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
            std::min(utf8_sequence_length(static_cast<unsigned char>(text[i])), text.size() - i);
        const std::size_t cells_here = codepoint_cells(decode_utf8(text, i, length));
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

}  // namespace apogee::ansi
