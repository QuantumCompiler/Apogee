#include "embedstore/chunk.h"

#include <algorithm>

namespace apogee::embedstore {
namespace {

/// Whether `byte` starts a codepoint.
///
/// UTF-8 continuation bytes are `10xxxxxx`; everything else — ASCII and the
/// lead byte of a multi-byte sequence — begins one. That single test is the
/// entire dependency this file was going to take on utf8proc for.
[[nodiscard]] bool starts_codepoint(unsigned char byte) noexcept {
    return (byte & 0xC0U) != 0x80U;
}

/// Byte offsets of every codepoint start in `text`, plus a final `size()`
/// sentinel so a chunk's end is always addressable.
[[nodiscard]] std::vector<std::size_t> codepoint_offsets(std::string_view text) {
    std::vector<std::size_t> offsets;
    offsets.reserve((text.size() / 2) + 1);
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (starts_codepoint(static_cast<unsigned char>(text[index]))) {
            offsets.push_back(index);
        }
    }
    offsets.push_back(text.size());
    return offsets;
}

}  // namespace

std::size_t codepoint_count(std::string_view text) noexcept {
    std::size_t count = 0;
    for (const char c : text) {
        if (starts_codepoint(static_cast<unsigned char>(c))) {
            ++count;
        }
    }
    return count;
}

std::vector<std::string> chunk_text(std::string_view text, const ChunkOptions& options) {
    std::vector<std::string> chunks;
    if (text.empty()) {
        return chunks;
    }

    const std::size_t size = std::max<std::size_t>(options.size, 1);
    // Clamped rather than trusted. An overlap at or above the chunk size makes
    // no forward progress, and a value a user can put in a config must never be
    // a value that hangs the process.
    const std::size_t overlap = std::min(options.overlap, size - 1);
    const std::size_t stride = size - overlap;

    const std::vector<std::size_t> offsets = codepoint_offsets(text);
    // The last entry is the end sentinel, so this is the codepoint count.
    const std::size_t total = offsets.size() - 1;

    for (std::size_t start = 0; start < total; start += stride) {
        const std::size_t end = std::min(start + size, total);
        // Both bounds land on codepoint starts, so no chunk can split one.
        chunks.emplace_back(text.substr(offsets[start], offsets[end] - offsets[start]));
        if (end == total) {
            break;
        }
    }
    return chunks;
}

}  // namespace apogee::embedstore
