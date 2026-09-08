#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

/// SHA-256, streaming.
///
/// **Why ours rather than a library.** Nothing already in the build hashes:
/// libcurl exposes no portable digest API, and OpenSSL is not a dependency.
/// Adding one across six targets — including two Windows architectures — to
/// gain a single function is a worse trade than ~120 lines of a fully specified
/// algorithm with published test vectors. `sha256_test.cpp` checks it against
/// those vectors, including the multi-block and length-padding edges where a
/// hand-written implementation actually goes wrong.
///
/// **Streaming, not one-shot.** The files this hashes are measured in tens of
/// gigabytes; an interface taking a `std::string` would require reading one
/// into memory to hash it.
///
/// It lives under `models/` because acquisition is its only caller. If a second
/// one appears — a cache key, a config fingerprint — this moves somewhere
/// shared rather than being copied.
namespace apogee::models {

class Sha256 {
public:
    Sha256();

    /// Feeds more bytes. May be called any number of times with any sizes; the
    /// result depends only on the concatenation, which is what the chunk-size
    /// test asserts.
    void update(std::string_view bytes);

    /// Finishes and returns the lowercase hex digest. The object must not be
    /// updated afterwards.
    [[nodiscard]] std::string hex_digest();

private:
    void compress(const std::uint8_t* block);

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffered_ = 0;
    std::uint64_t total_bits_ = 0;
    bool finished_ = false;
};

/// One-shot over an in-memory string. For tests and short values.
[[nodiscard]] std::string sha256_hex(std::string_view bytes);

}  // namespace apogee::models
