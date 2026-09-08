#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

/// Splitting text into overlapping chunks, by codepoint.
///
/// **By codepoint, not by byte, and that is the whole reason this file
/// exists.** Cutting a UTF-8 string at an arbitrary byte offset splits a
/// multi-byte character in half, and the halves are not text: they corrupt the
/// chunk that ends, corrupt the one that begins, and reach SQLite as invalid
/// UTF-8 where FTS5 tokenises them into nonsense. A corpus of any language but
/// English degrades quietly and nobody can say why.
///
/// ## No dependency for this
///
/// The item's recorded default was utf8proc. That library exists for
/// normalisation, case-folding and character properties — none of which
/// chunking needs. What chunking needs is "advance N codepoints", which is
/// counting bytes that are not continuation bytes: `(b & 0xC0) != 0x80`. That
/// is a handful of lines against a fetched dependency on six targets, and the
/// same trade the GGUF reader and SHA-256 were decided on earlier in this
/// project.
///
/// The deviation is recorded in the item document rather than made silently.
namespace apogee::embedstore {

/// Chunking parameters.
struct ChunkOptions {
    /// Codepoints per chunk. 512 is the recorded default.
    std::size_t size = 512;

    /// Codepoints each chunk repeats from the one before.
    ///
    /// Overlap exists so a passage that straddles a boundary is still findable:
    /// without it, the sentence spanning the cut appears whole in neither
    /// chunk and matches neither well.
    std::size_t overlap = 64;
};

/// Splits `text` into chunks.
///
/// Never returns a chunk that splits a codepoint. Whitespace-only input and
/// text shorter than one chunk both yield sensible results rather than edge
/// cases the caller must handle: empty input gives no chunks, and short input
/// gives exactly one.
///
/// An `overlap` at or above `size` would make no forward progress and is
/// clamped rather than looping forever — a config a user can write must not be
/// a config that hangs.
[[nodiscard]] std::vector<std::string> chunk_text(std::string_view text,
                                                  const ChunkOptions& options = {});

/// Number of Unicode codepoints in `text`, counting invalid bytes as one each.
///
/// Lenient on purpose: this runs over arbitrary user files, and a document with
/// one bad byte should be chunked slightly oddly rather than rejected.
[[nodiscard]] std::size_t codepoint_count(std::string_view text) noexcept;

}  // namespace apogee::embedstore
