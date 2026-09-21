#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "embedstore/store.h"
#include "knowledge/record.h"

/// Where records live: a standard collection plus an archive of raw
/// conversations on disk.
///
/// **One record is one chunk**, keyed by the record id as the chunk's source.
/// The chunk's text is `index_text(record)` -- the thin, immutable handle the
/// retrievers see -- and the chunk's metadata is the whole record as JSON.
/// The raw conversation is archived beside the database under `raw/<id>.md`
/// and referenced by `raw_ref`: archive rich, surface thin.
///
/// Because the index is built only from what never changes, an edit to a
/// record's link or status rewrites the metadata and nothing else -- no
/// re-embed, and the stored vector's bytes are provably untouched.
namespace apogee::knowledge {

class Store {
public:
    /// Opens (creating if needed) the collection at `db_path`. `raw_dir` is
    /// where raw conversations are archived; empty disables archiving. The
    /// directory is created on the first archive, private to the user.
    Store(const std::filesystem::path& db_path, std::filesystem::path raw_dir);

    /// The collection underneath, for the shared retriever resolution to
    /// read facts from. Writes go through this class so the metadata and
    /// the archive stay consistent.
    [[nodiscard]] embedstore::Store& chunks() noexcept {
        return chunks_;
    }

    [[nodiscard]] const embedstore::Store& chunks() const noexcept {
        return chunks_;
    }

    /// Where `id`'s raw conversation is (or would be) archived; empty when
    /// archiving is disabled.
    [[nodiscard]] std::filesystem::path raw_path(std::string_view id) const;

    /// Stores a finalized record: `record.id` and `record.timestamp` set, and
    /// `vector` the embedding of its index text (empty for a lexical-only
    /// record). When `raw` is non-empty and archiving is enabled, the
    /// conversation is written first and `record.raw_ref` set to it. Any
    /// prior version of the record is replaced, so re-capture is idempotent.
    void put(Record& record, const std::vector<float>& vector, std::string_view raw);

    /// The record with `id`, or nullopt.
    [[nodiscard]] std::optional<Record> get(std::string_view id) const;

    /// Every record, newest first. Chunks that are not records -- plain
    /// documents ingested into a shared collection -- are skipped.
    [[nodiscard]] std::vector<Record> list() const;

    /// The id of the chunk backing `id` -- a storage handle, the seed a graph
    /// walk expands from later. nullopt when there is no such record.
    [[nodiscard]] std::optional<std::int64_t> chunk_id(std::string_view id) const;

    /// Removes a record and its archived conversation. False when there was
    /// no such record.
    [[nodiscard]] bool remove(std::string_view id);

    /// Metadata-only edits: the new record, or nullopt when `id` is unknown.
    /// None of these touch the index text or the vector.
    [[nodiscard]] std::optional<Record> set_link(std::string_view id, std::string_view link);
    [[nodiscard]] std::optional<Record> set_status(std::string_view id, std::string_view status);
    /// Marks `old_id` superseded. The new record carries `supersedes = old_id`
    /// itself, set at capture.
    [[nodiscard]] std::optional<Record> supersede(std::string_view old_id);

    /// The archived conversation, or nullopt when none is archived or the
    /// file cannot be read.
    [[nodiscard]] std::optional<std::string> read_raw(const Record& record) const;

    /// Whether any record here carries a vector.
    [[nodiscard]] bool has_vectors() const;

    /// How many records here carry no vector -- captured lexically, on
    /// purpose or for want of an embedder. What a whole-collection reindex
    /// discloses before it embeds them.
    [[nodiscard]] std::size_t vectorless_count() const;

    /// Text to vector, wired by the caller to an embedding backend -- the
    /// store is model-free by design. Throws on failure.
    using EmbedText = std::function<std::vector<float>(std::string_view text)>;

    struct ReindexOutcome {
        int reindexed = 0;
        /// Non-empty when it stopped: an unknown id (before anything was
        /// embedded), or an embedder failure naming the record.
        std::string error;
    };

    /// Re-embeds records and rewrites their stored vectors -- every record
    /// when `ids` is empty, else exactly those. The way to refresh the
    /// vectors after an embedding-model change. **Vector-only by design**:
    /// the text index is maintained on every write and needs nothing here.
    /// The archive and `raw_ref` are left exactly as they are.
    [[nodiscard]] ReindexOutcome reindex(const EmbedText& embed,
                                         const std::vector<std::string>& ids);

private:
    [[nodiscard]] std::optional<Record> update(std::string_view id,
                                               const std::function<void(Record&)>& mutate);

    embedstore::Store chunks_;
    std::filesystem::path raw_dir_;
};

}  // namespace apogee::knowledge
