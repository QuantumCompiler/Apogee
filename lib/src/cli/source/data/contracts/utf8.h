#pragma once

#include <string>
#include <string_view>

/// Text that is UTF-8, whatever bytes arrived.
///
/// **Why it exists.** Every event, frame, session file, ledger and request
/// body Apogee writes is a strict JSON dump, and a strict dump throws on a
/// string that is not UTF-8 (`type_error.316`). The bytes arriving are not
/// always text: a backend streams its answer in pieces cut wherever a token
/// or a pipe read ends -- llama.cpp says a byte-fallback token one byte at a
/// time -- so a valid character can arrive split across two pieces, and a
/// stream stopped at its token limit can end inside one; a tool returns a
/// Latin-1 file or a command's raw output as it found them. Each crashed the
/// turn at its first dump. What a surface writes is made text here, at the
/// seams it enters by, rather than tolerated at every dump.
///
/// **The replacement policy is the standard one:** each maximal subpart of an
/// ill-formed sequence becomes one U+FFFD -- the Unicode Standard's
/// "substitution of maximal subparts" (chapter 3, U+FFFD substitution), which
/// the WHATWG Encoding Standard's UTF-8 decoder implements. A maximal subpart
/// is the longest run of bytes that begins a well-formed sequence and cannot
/// finish it, or a single byte that cannot begin one: `F0 9F 98` cut off is
/// one U+FFFD, the overlong `C0 80` two, the surrogate `ED A0 80` three.
/// Well-formed text passes through byte for byte.
namespace apogee::harness {

/// One stream of text arriving in pieces -- an answer, or its reasoning; one
/// object each.
///
/// Each piece is decoded as it comes and the longest well-formed text is
/// handed back at once, so a character split across pieces arrives whole in
/// the piece that completes it: the bytes that begin a character and have not
/// yet finished it are held (three at most) for the next piece. A byte that
/// cannot be mended by what follows is replaced where it stands, never held.
/// The pieces handed back add up to exactly `valid_utf8` of the whole stream.
class Utf8Stream {
public:
    /// The text `piece` completes: what was held, then the piece, up to any
    /// character it leaves unfinished. Empty when the piece only began one.
    [[nodiscard]] std::string feed(std::string_view piece);

    /// The end of the stream: a character left unfinished, as one U+FFFD;
    /// empty when nothing is held. The stream is then empty, ready for reuse.
    [[nodiscard]] std::string flush();

private:
    std::string held_;
};

/// `text` as UTF-8: each maximal subpart of an ill-formed or unfinished
/// sequence one U+FFFD, everything else as it was.
[[nodiscard]] std::string valid_utf8(std::string_view text);

/// Whether `text` is UTF-8 already -- `valid_utf8` would hand it back
/// unchanged -- asked without copying it.
[[nodiscard]] bool is_valid_utf8(std::string_view text) noexcept;

}  // namespace apogee::harness
