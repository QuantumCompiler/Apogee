#pragma once

#include <nlohmann/json_fwd.hpp>

#include <chrono>
#include <optional>
#include <span>
#include <string>
#include <string_view>

/// The canonical knowledge record -- the one schema everything downstream
/// conforms to.
///
/// The knowledge layer captures the *why* behind a decision at the moment the
/// idea is formed, before it evaporates into a ticket or a commit with the
/// rationale stripped out. Three principles from Ommi's originating brief
/// shape this type, and each is enforced by a function here rather than by a
/// convention:
///
///   - **Write-time linking.** `downstream_link` is recorded when ideation
///     becomes an artifact, not reconstructed weeks later.
///   - **Archive rich, surface thin.** The full record is archived; what the
///     collection indexes is `index_text()` -- the immutable reasoning alone.
///   - **Anonymize attribution, preserve the chain.** Who said it lives in a
///     field of its own, so `anonymize()` strips names without severing
///     `source`, `downstream_link`, `supersedes` or `raw_ref`.
namespace apogee::knowledge {

/// The branch marker. A brainstorm is mostly roads not taken, and without it
/// the layer would surface an abandoned idea's rationale as the reason
/// something exists -- the single most important guard in a brainstorm-based
/// corpus.
inline constexpr std::string_view kStatusShipped = "shipped";
inline constexpr std::string_view kStatusRejected = "rejected";
inline constexpr std::string_view kStatusSuperseded = "superseded";

/// `provenance.source` when neither the clerk nor the user named one.
inline constexpr std::string_view kDefaultSource = "manual";

/// The closed set of `status` values.
[[nodiscard]] std::span<const std::string_view> valid_statuses() noexcept;

/// The disciplines the capture clerk targets. Open by convention; these are
/// the canonical values.
[[nodiscard]] std::span<const std::string_view> valid_disciplines() noexcept;

[[nodiscard]] bool is_valid_status(std::string_view status) noexcept;

/// Lower-cases and trims a status and maps common synonyms (`live`, `done`,
/// `abandoned`, `replaced`, ...) onto the canonical set, so the clerk's
/// output is forgiving without being lax. An unknown word comes back
/// lower-cased and trimmed, for `validate` to refuse.
[[nodiscard]] std::string normalize_status(std::string_view status);

/// Where a record came from: the source surface (kept) and, separately, the
/// attribution (strippable). "Semi-anonymized" means names off, chain on,
/// which is only possible because these are two fields.
struct Provenance {
    /// The surface: `chat`, `manual`, `meeting`, `claude-code`, ...
    std::string source;
    /// Who said it. Removed by `anonymize`; never part of `index_text`.
    std::string attribution;
};

struct Record {
    /// `kr-YYYYMMDDTHHMMSSZ-<6hex>`, assigned by the system at store time.
    std::string id;
    /// The why -- preserved in the participants' words, never pre-summarised.
    /// The load-bearing field: the one thing that evaporates.
    std::string intent;
    /// What was actually chosen, distinct from what was discussed.
    std::string decision;
    /// `shipped`, `rejected`, or `superseded`.
    std::string status;
    /// `program`, `product`, `project`, `ux`, or `eng`.
    std::string discipline;
    /// The artifact this decision produced: a ticket, a component, a commit.
    std::string downstream_link;
    Provenance provenance;
    /// Where the raw conversation is archived; machine-local.
    std::string raw_ref;
    /// RFC 3339 capture time, UTC; what listings sort by.
    std::string timestamp;
    /// The id of the record this one replaces.
    std::string supersedes;
};

/// A sortable, collision-resistant id: `kr-<UTC timestamp>-<6 hex>`. The
/// timestamp prefix keeps ids lexically ordered; the random suffix tells
/// apart two records captured in the same second.
[[nodiscard]] std::string new_id(std::chrono::system_clock::time_point now);

/// `now` as the RFC 3339 UTC timestamp a record carries, microseconds
/// included, so ordering by it is ordering by capture time.
[[nodiscard]] std::string timestamp_for(std::chrono::system_clock::time_point now);

/// Cleans a freshly captured record in place: trims every field,
/// canonicalises `status` and `discipline`, and defaults the provenance
/// source. Assigns nothing -- id, timestamp and raw_ref are the store's.
void normalize(Record& record);

/// Why `record` cannot be stored, or empty when it can: the intent is
/// required (the why is the point), and the status must be canonical.
[[nodiscard]] std::string validate(const Record& record);

/// The "surface thin" handle the collection embeds and indexes: the intent,
/// then `\n\nDecision: <decision>` when there is one. Built ONLY from the
/// immutable reasoning -- never attribution (so a query can never surface a
/// name) and never the mutable link or status (metadata you filter over, not
/// content you search for). Because nothing here changes after capture, an
/// edit can never make the stored vector stale.
[[nodiscard]] std::string index_text(const Record& record);

/// A copy with the attribution stripped and every chain field kept.
[[nodiscard]] Record anonymize(Record record);

/// JSON, found by ADL. The shape is Ommi's, so an export reads the same:
/// `attribution`, `raw_ref` and `supersedes` are omitted when empty.
void to_json(nlohmann::json& out, const Record& record);
void from_json(const nlohmann::json& in, Record& record);

/// The ONE decoder of a chunk's record: the JSON a knowledge chunk stores as
/// its metadata, keyed by the chunk's source (the record id).
///
/// Strict, never guessing: nullopt with `error` set for empty metadata,
/// metadata that is not record JSON, a record whose id does not match its
/// source (a record's chunk is always keyed by its own id), or one with no
/// intent. Shared by the store's readers and, later, by the graph build's
/// record recognition.
[[nodiscard]] std::optional<Record> record_from_metadata(std::string_view source,
                                                         std::string_view metadata,
                                                         std::string& error);

}  // namespace apogee::knowledge
