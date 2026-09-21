#pragma once

#include <cstdint>
#include <string>
#include <string_view>

/// Community storage for the knowledge graph's **global layer**: the
/// label-propagation communities `graph/communities` detects, each carrying
/// a generated summary that is ALSO stored as an ordinary chunk -- a
/// **pseudo-chunk** whose source is `graph://community/<id>` -- so a
/// corpus-level summary surfaces through plain retrieval on any retriever
/// with zero new query paths.
///
/// A community's identity is its exact member set, stored verbatim as the
/// sorted member ids joined by commas: any membership change is a new
/// community, so staleness is exact -- the old row and its pseudo-chunk are
/// pruned and a new summary generated, while an unchanged community costs
/// nothing. The key is stored rather than hashed because this package cannot
/// reach the SHA-256 in `models/`, and a second hash implementation would be
/// a second copy of something that already exists once.
///
/// Pseudo-chunks are graph output, never corpus input: excluded from
/// extraction planning, staleness and coverage (a summary must never yield
/// self-referential entities), and they travel with the graph on delete.
/// Detection and summarisation live in `graph/`; this file is storage only.
namespace apogee::embedstore {

/// Tags community-summary pseudo-chunks in the chunks table.
inline constexpr std::string_view kCommunitySourcePrefix = "graph://community/";

/// The pseudo-chunk source for community `id`.
[[nodiscard]] std::string community_source(std::int64_t id);

/// Whether a chunk source is a community summary rather than a document.
[[nodiscard]] bool is_community_source(std::string_view source) noexcept;

/// One detected community and its summary bookkeeping.
struct GraphCommunity {
    std::int64_t id = 0;
    /// The sorted member ids joined by commas -- the identity.
    std::string member_key;
    std::int64_t size = 0;
    std::string summary;
    /// The summariser, informational.
    std::string model;
    /// RFC 3339, UTC.
    std::string summarized_at;
};

}  // namespace apogee::embedstore
