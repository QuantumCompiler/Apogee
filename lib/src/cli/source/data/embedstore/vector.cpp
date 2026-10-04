#include "embedstore/vector.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>

namespace apogee::embedstore {

std::string to_blob(const std::vector<float>& vector) {
    std::string out;
    out.resize(vector.size() * sizeof(float));
    for (std::size_t i = 0; i < vector.size(); ++i) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &vector[i], sizeof(bits));
        // Little-endian on the wire regardless of host, so a store copied
        // between machines reads the same vectors.
        out[i * 4] = static_cast<char>(bits & 0xFFU);
        out[(i * 4) + 1] = static_cast<char>((bits >> 8U) & 0xFFU);
        out[(i * 4) + 2] = static_cast<char>((bits >> 16U) & 0xFFU);
        out[(i * 4) + 3] = static_cast<char>((bits >> 24U) & 0xFFU);
    }
    return out;
}

std::vector<float> from_blob(std::string_view bytes) {
    std::vector<float> out(bytes.size() / sizeof(float));
    for (std::size_t i = 0; i < out.size(); ++i) {
        const auto byte = [&](std::size_t at) {
            return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[(i * 4) + at]));
        };
        const std::uint32_t bits = byte(0) | (byte(1) << 8U) | (byte(2) << 16U) | (byte(3) << 24U);
        std::memcpy(&out[i], &bits, sizeof(bits));
    }
    return out;
}

double cosine(const std::vector<float>& a, const std::vector<float>& b) noexcept {
    if (a.empty() || a.size() != b.size()) {
        return 0.0;
    }
    double dot = 0.0;
    double na = 0.0;
    double nb = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const double fa = a[i];
        const double fb = b[i];
        dot += fa * fb;
        na += fa * fa;
        nb += fb * fb;
    }
    if (na == 0.0 || nb == 0.0) {
        return 0.0;
    }
    return dot / (std::sqrt(na) * std::sqrt(nb));
}

std::vector<SearchHit> fuse_rrf(const std::vector<std::vector<SearchHit>>& lists, int limit,
                                int k) {
    struct Fused {
        SearchHit hit;
        double score = 0.0;
    };

    std::map<std::int64_t, Fused> by_id;
    for (const std::vector<SearchHit>& list : lists) {
        for (std::size_t rank = 0; rank < list.size(); ++rank) {
            Fused& entry = by_id[list[rank].chunk.id];
            if (entry.score == 0.0) {
                entry.hit = list[rank];
            }
            // Rank is 1-based; only the position is read, never the score.
            entry.score += 1.0 / static_cast<double>(k + static_cast<int>(rank) + 1);
        }
    }

    std::vector<SearchHit> out;
    out.reserve(by_id.size());
    for (auto& [id, fused] : by_id) {
        fused.hit.score = fused.score;
        fused.hit.retriever = "hybrid";
        out.push_back(std::move(fused.hit));
    }
    // Higher score first; equal scores by chunk id, so the order is a
    // function of the inputs and nothing else.
    std::stable_sort(out.begin(), out.end(), [](const SearchHit& lhs, const SearchHit& rhs) {
        if (lhs.score != rhs.score) {
            return lhs.score > rhs.score;
        }
        return lhs.chunk.id < rhs.chunk.id;
    });
    if (limit > 0 && static_cast<std::size_t>(limit) < out.size()) {
        out.resize(static_cast<std::size_t>(limit));
    }
    return out;
}

}  // namespace apogee::embedstore
