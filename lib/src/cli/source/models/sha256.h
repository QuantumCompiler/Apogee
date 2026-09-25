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
/// Adding one across five targets — including two Windows architectures — to
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

/// The block function, both ways -- exposed so a test can hold the fast one
/// to the portable one on the host that has both.
///
/// **Why a second way.** A converted 27B model is a 51 GiB file, and its id
/// is its hash: the portable code does ~320 MB/s, so `models convert` spent
/// three silent minutes on it (2026-09-24) -- read as a hang. ARMv8's SHA-256
/// instructions do ~2 GB/s. Chosen at compile time (`__ARM_FEATURE_SHA2`, on
/// for every Apple Silicon build), so there is nothing to detect at run time
/// and nothing to get wrong on a CPU without them.
namespace sha256_detail {

/// Whether `compress` uses the CPU's SHA-256 instructions in this build.
[[nodiscard]] bool accelerated() noexcept;

/// `count` consecutive 64-byte blocks into `state`, the way this build does it.
void compress(std::array<std::uint32_t, 8>& state, const std::uint8_t* blocks,
              std::size_t count) noexcept;

/// The same, always in portable C++.
void compress_portable(std::array<std::uint32_t, 8>& state, const std::uint8_t* block) noexcept;

}  // namespace sha256_detail

}  // namespace apogee::models
