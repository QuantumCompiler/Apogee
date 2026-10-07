#include "contracts/utf8.h"

#include <cstddef>
#include <cstdint>

namespace apogee::harness {
namespace {

constexpr std::string_view kReplacement = "\xEF\xBF\xBD";  // U+FFFD

/// What the bytes at one position of a text are.
struct Sequence {
    enum class Kind : std::uint8_t {
        /// A whole character.
        Whole,
        /// The start of a character, well-formed so far, cut off by the end
        /// of the text: a later piece may finish it.
        Unfinished,
        /// A maximal subpart of an ill-formed sequence: one U+FFFD.
        Broken,
    };
    Kind kind = Kind::Whole;
    std::size_t length = 0;
};

/// The sequence that begins at `text[at]`.
///
/// The well-formed sequences are the Unicode Standard's table 3-7: a lead
/// byte fixes the length, and the second byte's range is narrowed after
/// `E0` (no overlong form), `ED` (no surrogate), `F0` (no overlong form) and
/// `F4` (nothing past U+10FFFF). A byte out of its range ends a broken
/// sequence without belonging to it -- it is read again as a first byte, as
/// the standard's policy requires.
[[nodiscard]] Sequence sequence_at(std::string_view text, std::size_t at) {
    const auto lead = static_cast<unsigned char>(text[at]);
    if (lead < 0x80U) {
        return {.kind = Sequence::Kind::Whole, .length = 1};
    }
    std::size_t length = 0;
    unsigned char low = 0x80U;
    unsigned char high = 0xBFU;
    if (lead >= 0xC2U && lead <= 0xDFU) {
        length = 2;
    } else if (lead >= 0xE0U && lead <= 0xEFU) {
        length = 3;
        low = lead == 0xE0U ? 0xA0U : low;
        high = lead == 0xEDU ? 0x9FU : high;
    } else if (lead >= 0xF0U && lead <= 0xF4U) {
        length = 4;
        low = lead == 0xF0U ? 0x90U : low;
        high = lead == 0xF4U ? 0x8FU : high;
    } else {
        return {.kind = Sequence::Kind::Broken, .length = 1};  // begins no character at all
    }
    std::size_t seen = 1;
    while (seen < length) {
        if (at + seen == text.size()) {
            return {.kind = Sequence::Kind::Unfinished, .length = seen};
        }
        const auto next = static_cast<unsigned char>(text[at + seen]);
        if (next < low || next > high) {
            return {.kind = Sequence::Kind::Broken, .length = seen};
        }
        low = 0x80U;
        high = 0xBFU;
        ++seen;
    }
    return {.kind = Sequence::Kind::Whole, .length = length};
}

/// Appends `text` to `out` as UTF-8, each maximal subpart of an ill-formed
/// sequence one U+FFFD. An unfinished character at the end is not appended:
/// returns its length, for the caller to hold or replace.
std::size_t append_valid(std::string_view text, std::string& out) {
    std::size_t clean = 0;  // where the well-formed bytes not yet appended begin
    std::size_t at = 0;
    while (at < text.size()) {
        const Sequence sequence = sequence_at(text, at);
        if (sequence.kind == Sequence::Kind::Whole) {
            at += sequence.length;
            continue;
        }
        out.append(text.substr(clean, at - clean));
        if (sequence.kind == Sequence::Kind::Unfinished) {
            return sequence.length;
        }
        out.append(kReplacement);
        at += sequence.length;
        clean = at;
    }
    out.append(text.substr(clean));
    return 0;
}

}  // namespace

std::string Utf8Stream::feed(std::string_view piece) {
    std::string out;
    if (held_.empty()) {
        const std::size_t unfinished = append_valid(piece, out);
        held_.assign(piece.substr(piece.size() - unfinished));
        return out;
    }
    // A character begun in an earlier piece: read again with this one, which
    // finishes it, breaks it, or continues it still unfinished.
    const std::string joined = held_ + std::string{piece};
    const std::size_t unfinished = append_valid(joined, out);
    held_ = joined.substr(joined.size() - unfinished);
    return out;
}

std::string Utf8Stream::flush() {
    if (held_.empty()) {
        return {};
    }
    held_.clear();
    return std::string{kReplacement};
}

std::string valid_utf8(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    if (append_valid(text, out) != 0) {
        out.append(kReplacement);
    }
    return out;
}

bool is_valid_utf8(std::string_view text) noexcept {
    for (std::size_t at = 0; at < text.size();) {
        const Sequence sequence = sequence_at(text, at);
        if (sequence.kind != Sequence::Kind::Whole) {
            return false;
        }
        at += sequence.length;
    }
    return true;
}

}  // namespace apogee::harness
