#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "embedstore/store.h"

/// Vectors as bytes, cosine over them, and rank fusion between retrievers.
///
/// ## Scores are never fused, ranks are
///
/// Normalised BM25 and cosine live on incomparable scales -- the fact behind
/// the one-retriever-per-turn rule. Hybrid retrieval therefore fuses by
/// **Reciprocal Rank Fusion**, which reads only each chunk's RANK in each list:
/// `score(c) = Σ_lists 1 / (k + rank_c)`. The result is a third scale,
/// comparable to neither of the others, and every hit says which one it is on.
namespace apogee::embedstore {

/// The RRF constant. 60 is the literature default (Cormack et al., 2009):
/// large enough that consistent mid-list presence in both lists outweighs a
/// single first place.
inline constexpr int kRrfK = 60;

/// How deep each half of a hybrid search fetches before fusion, regardless of
/// the caller's limit. At depth `limit` fusion degenerates toward interleaving
/// two top lists: a chunk just below the cut in BOTH lists could never surface
/// even though its fused score beats every single-list chunk. Depth is nearly
/// free -- cosine scores every chunk before truncating anyway.
inline constexpr int kHybridFetchDepth = 50;

/// Little-endian float32, four bytes per component.
[[nodiscard]] std::string to_blob(const std::vector<float>& vector);
[[nodiscard]] std::vector<float> from_blob(std::string_view bytes);

/// Cosine similarity, or 0 when either vector is empty, zero, or the widths
/// differ. A mismatch answers 0 rather than throwing because the caller has
/// already been told not to query across spaces; this is the last line, not
/// the policy.
[[nodiscard]] double cosine(const std::vector<float>& a, const std::vector<float>& b) noexcept;

/// Fuses ranked lists into one, ordered by RRF score, identity being the chunk
/// id. Ties break by chunk id so fusion is deterministic. `limit <= 0` keeps
/// the whole fused list. Every hit comes back labelled `hybrid`.
[[nodiscard]] std::vector<SearchHit> fuse_rrf(const std::vector<std::vector<SearchHit>>& lists,
                                              int limit, int k = kRrfK);

}  // namespace apogee::embedstore
