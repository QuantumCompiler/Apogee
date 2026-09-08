#include "models/sha256.h"

#include <cstring>
#include <iomanip>
#include <sstream>

namespace apogee::models {
namespace {

/// The first 32 bits of the fractional parts of the cube roots of the first 64
/// primes. FIPS 180-4, §4.2.2.
constexpr std::array<std::uint32_t, 64> kRoundConstants{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

[[nodiscard]] constexpr std::uint32_t rotate_right(std::uint32_t value, unsigned bits) noexcept {
    return (value >> bits) | (value << (32U - bits));
}

}  // namespace

Sha256::Sha256()
    : state_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
             0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19} {}

void Sha256::compress(const std::uint8_t* block) {
    std::array<std::uint32_t, 64> schedule{};
    for (std::size_t i = 0; i < 16; ++i) {
        schedule[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24U) |
                      (static_cast<std::uint32_t>(block[(i * 4) + 1]) << 16U) |
                      (static_cast<std::uint32_t>(block[(i * 4) + 2]) << 8U) |
                      static_cast<std::uint32_t>(block[(i * 4) + 3]);
    }
    for (std::size_t i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotate_right(schedule[i - 15], 7) ^
                                 rotate_right(schedule[i - 15], 18) ^ (schedule[i - 15] >> 3U);
        const std::uint32_t s1 = rotate_right(schedule[i - 2], 17) ^
                                 rotate_right(schedule[i - 2], 19) ^ (schedule[i - 2] >> 10U);
        schedule[i] = schedule[i - 16] + s0 + schedule[i - 7] + s1;
    }

    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];

    for (std::size_t i = 0; i < 64; ++i) {
        const std::uint32_t s1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
        const std::uint32_t choose = (e & f) ^ (~e & g);
        const std::uint32_t temp1 = h + s1 + choose + kRoundConstants[i] + schedule[i];
        const std::uint32_t s0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = s0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::update(std::string_view bytes) {
    if (finished_) {
        return;
    }
    total_bits_ += static_cast<std::uint64_t>(bytes.size()) * 8U;

    std::size_t offset = 0;
    // Top up a partial block first, so a caller feeding one byte at a time and
    // a caller feeding the whole file at once produce the same digest.
    if (buffered_ > 0) {
        const std::size_t take = std::min(buffer_.size() - buffered_, bytes.size());
        std::memcpy(buffer_.data() + buffered_, bytes.data(), take);
        buffered_ += take;
        offset = take;
        if (buffered_ < buffer_.size()) {
            return;
        }
        compress(buffer_.data());
        buffered_ = 0;
    }

    while (bytes.size() - offset >= buffer_.size()) {
        // Reading the same bytes as unsigned octets, which is what the algorithm
        // is defined over. There is no way to do this without a cast, and copying
        // every block into a std::array first would double the work on a 71 GB
        // file for no benefit.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        compress(reinterpret_cast<const std::uint8_t*>(bytes.data()) + offset);
        offset += buffer_.size();
    }

    const std::size_t remaining = bytes.size() - offset;
    if (remaining > 0) {
        std::memcpy(buffer_.data(), bytes.data() + offset, remaining);
        buffered_ = remaining;
    }
}

std::string Sha256::hex_digest() {
    if (!finished_) {
        // Padding: a 0x80 byte, then zeros, then the 64-bit big-endian length.
        // When fewer than 8 bytes remain the length spills into another block,
        // which is the edge a hand-written implementation gets wrong and which
        // the 55/56/57-byte test vectors exist to catch.
        const std::uint64_t length_bits = total_bits_;

        buffer_[buffered_++] = 0x80;
        if (buffered_ > buffer_.size() - 8) {
            while (buffered_ < buffer_.size()) {
                buffer_[buffered_++] = 0;
            }
            compress(buffer_.data());
            buffered_ = 0;
        }
        while (buffered_ < buffer_.size() - 8) {
            buffer_[buffered_++] = 0;
        }
        for (int i = 7; i >= 0; --i) {
            buffer_[buffered_++] =
                static_cast<std::uint8_t>((length_bits >> (static_cast<unsigned>(i) * 8U)) & 0xFFU);
        }
        compress(buffer_.data());
        finished_ = true;
    }

    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const std::uint32_t word : state_) {
        out << std::setw(8) << word;
    }
    return out.str();
}

std::string sha256_hex(std::string_view bytes) {
    Sha256 hash;
    hash.update(bytes);
    return hash.hex_digest();
}

}  // namespace apogee::models
