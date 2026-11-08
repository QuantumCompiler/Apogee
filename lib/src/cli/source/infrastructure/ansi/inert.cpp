#include "ansi/inert.h"

#include <algorithm>
#include <cstdint>

#include "ansi/text_width.h"

namespace apogee::ansi {
namespace {

/// How a scan treats the control characters it meets.
enum class Form : std::uint8_t {
    /// Every control drawn as U+FFFD.
    Line,
    /// LF kept, TAB expanded, CR dropped, every other control U+FFFD.
    Text,
    /// Every control dropped.
    Dropped,
};

/// The one walk every form takes. A byte is a control when it is below 0x20
/// or DEL, when it is C1 written as UTF-8 (`C2` then `80`..`9F`), or when it
/// is a lone byte in `80`..`9F` -- C1's 8-bit form, which no character that
/// is text begins with. Continuation bytes are counted after their lead, so
/// a character's own bytes are never mistaken for one.
std::string neutralise(std::string_view raw, Form form, std::size_t& column) {
    std::string out;
    out.reserve(raw.size());
    const auto control = [&out, &column, form]() {
        if (form != Form::Dropped) {
            out += kReplacementCharacter;
            ++column;
        }
    };
    std::size_t continuation = 0;  // bytes still owed to the character a lead opened
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const auto byte = static_cast<unsigned char>(raw[i]);
        if (byte >= 0x80 && byte <= 0xBF) {
            if (continuation > 0) {
                --continuation;
                out += raw[i];
            } else if (byte <= 0x9F) {
                control();  // a lone C1 byte
            } else {
                out += raw[i];  // a stray continuation byte: one cell, as display_width counts it
                ++column;
            }
            continue;
        }
        continuation = 0;
        if (byte == 0xC2 && i + 1 < raw.size()) {
            const auto next = static_cast<unsigned char>(raw[i + 1]);
            if (next >= 0x80 && next <= 0x9F) {
                control();  // C1 as UTF-8
                ++i;
                continue;
            }
        }
        if (byte < 0x20 || byte == 0x7F) {
            if (form == Form::Text && byte == '\n') {
                out += '\n';
                column = 0;
            } else if (form == Form::Text && byte == '\r') {
                // dropped: a CR would return to column 0 and draw over the row
            } else if (form == Form::Text && byte == '\t') {
                const std::size_t spaces = 4 - (column % 4);
                out.append(spaces, ' ');
                column += spaces;
            } else {
                control();
            }
            continue;
        }
        if (byte >= 0xC0) {
            continuation = utf8_sequence_length(byte) - 1;
            const std::size_t length = std::min(continuation + 1, raw.size() - i);
            column += codepoint_cells(decode_utf8(raw, i, length));
        } else {
            ++column;
        }
        out += raw[i];
    }
    return out;
}

}  // namespace

std::string inert_line(std::string_view raw) {
    std::size_t column = 0;
    return neutralise(raw, Form::Line, column);
}

std::string inert_text(std::string_view raw) {
    std::size_t column = 0;
    return neutralise(raw, Form::Text, column);
}

std::string inert_text(std::string_view raw, std::size_t& column) {
    return neutralise(raw, Form::Text, column);
}

std::string without_controls(std::string_view raw) {
    std::size_t column = 0;
    return neutralise(raw, Form::Dropped, column);
}

}  // namespace apogee::ansi
