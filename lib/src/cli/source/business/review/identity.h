#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// What makes two findings the same finding (48c), and the markers a review
/// leaves in what it posts so the next review can read itself back.
///
/// **Identity** is the file, the location normalized to its first line, and
/// the kind: two lenses flagging line 12 of `a.py` as a correctness defect
/// are one finding, posted once, its lenses both credited.
///
/// **Markers** are HTML comments a forge renders as nothing: each inline
/// comment carries its finding's identity, the summary carries the head the
/// review read. They are hints, never authority: a re-review trusts only
/// those its own login posted, and reads a head only when it is a commit id.
namespace apogee::review {

/// The first line number in `location` (`"12"`, `"12-14"`, `"line 12"`), or
/// nullopt when it names none.
[[nodiscard]] std::optional<std::uint32_t> location_line(std::string_view location);

/// `location` normalized: `L<first line>`, or its text trimmed and lowercased
/// when it names no line.
[[nodiscard]] std::string normalized_location(std::string_view location);

/// The identity: sixteen hex digits of SHA-256 over the file, the
/// normalized location and the kind.
[[nodiscard]] std::string finding_identity(std::string_view file, std::string_view location,
                                           std::string_view kind);

/// `<!-- apogee-review:finding <id> -->`.
[[nodiscard]] std::string finding_marker(std::string_view identity);

/// `<!-- apogee-review:summary head=<sha> -->`.
[[nodiscard]] std::string summary_marker(std::string_view head);

/// The markers in one comment's body.
struct Markers {
    std::vector<std::string> findings;
    /// The head a summary names, only when it is a commit id.
    std::optional<std::string> head;
};

[[nodiscard]] Markers read_markers(std::string_view body);

}  // namespace apogee::review
