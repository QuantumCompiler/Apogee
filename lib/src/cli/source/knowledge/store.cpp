#include "knowledge/store.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

#include "harness/config_edit.h"

namespace apogee::knowledge {

Store::Store(const std::filesystem::path& db_path, std::filesystem::path raw_dir)
    : chunks_{db_path}, raw_dir_{std::move(raw_dir)} {}

std::filesystem::path Store::raw_path(std::string_view id) const {
    if (raw_dir_.empty()) {
        return {};
    }
    return raw_dir_ / (std::string{id} + ".md");
}

void Store::put(Record& record, const std::vector<float>& vector, std::string_view raw) {
    if (record.id.empty()) {
        throw std::invalid_argument("knowledge: a record needs an id before it is stored");
    }
    if (!raw.empty() && !raw_dir_.empty()) {
        // The archive first, so a record never points at a file that is not
        // there. Private: a raw conversation may hold anything the user typed.
        std::error_code code;
        std::filesystem::create_directories(raw_dir_, code);
        if (code) {
            throw std::runtime_error("knowledge: could not create " + raw_dir_.string() + ": " +
                                     code.message());
        }
#if !defined(_WIN32)
        std::filesystem::permissions(raw_dir_, std::filesystem::perms::owner_all,
                                     std::filesystem::perm_options::replace, code);
#endif
        const std::filesystem::path path = raw_path(record.id);
        harness::write_file_atomically(path, raw, /*private_mode=*/true);
        record.raw_ref = path.string();
    }
    const nlohmann::json metadata = record;
    // Replace-by-source: the chunk store deletes the record's prior chunk and
    // inserts the new one in a single transaction, so re-capture is
    // idempotent and never leaves two versions of one record.
    std::vector<std::vector<float>> vectors;
    if (!vector.empty()) {
        vectors.push_back(vector);
    }
    chunks_.replace_source(record.id, {index_text(record)}, vectors, {metadata.dump()});
}

std::optional<Record> Store::get(std::string_view id) const {
    for (const embedstore::Chunk& chunk : chunks_.chunks_with_metadata()) {
        if (chunk.source != id) {
            continue;
        }
        std::string error;
        return record_from_metadata(chunk.source, chunk.metadata, error);
    }
    return std::nullopt;
}

std::vector<Record> Store::list() const {
    std::vector<Record> records;
    for (const embedstore::Chunk& chunk : chunks_.chunks_with_metadata()) {
        std::string error;
        if (std::optional<Record> record =
                record_from_metadata(chunk.source, chunk.metadata, error);
            record.has_value()) {
            records.push_back(std::move(*record));
        }
    }
    // Newest first. Timestamps are fixed-width RFC 3339, so the string order
    // is the time order; the id breaks a tie the same way.
    std::stable_sort(records.begin(), records.end(), [](const Record& lhs, const Record& rhs) {
        if (lhs.timestamp != rhs.timestamp) {
            return lhs.timestamp > rhs.timestamp;
        }
        return lhs.id > rhs.id;
    });
    return records;
}

std::optional<std::int64_t> Store::chunk_id(std::string_view id) const {
    for (const embedstore::Chunk& chunk : chunks_.chunks_with_metadata()) {
        if (chunk.source == id) {
            return chunk.id;
        }
    }
    return std::nullopt;
}

bool Store::remove(std::string_view id) {
    const std::optional<Record> record = get(id);
    if (!record.has_value()) {
        return false;
    }
    (void)chunks_.delete_source(id);
    if (!record->raw_ref.empty()) {
        // Best-effort: the record is gone whatever the file's fate.
        std::error_code code;
        std::filesystem::remove(record->raw_ref, code);
    }
    return true;
}

std::optional<Record> Store::update(std::string_view id,
                                    const std::function<void(Record&)>& mutate) {
    std::optional<Record> record = get(id);
    if (!record.has_value()) {
        return std::nullopt;
    }
    mutate(*record);
    const nlohmann::json metadata = *record;
    // The metadata alone: the index text is a retrieval handle over the
    // immutable fields, and nothing here changes one of those.
    (void)chunks_.update_metadata(id, metadata.dump());
    return record;
}

std::optional<Record> Store::set_link(std::string_view id, std::string_view link) {
    return update(id, [link](Record& record) {
        record.downstream_link = std::string{link};
        normalize(record);
    });
}

std::optional<Record> Store::set_status(std::string_view id, std::string_view status) {
    return update(id, [status](Record& record) { record.status = std::string{status}; });
}

std::optional<Record> Store::supersede(std::string_view old_id) {
    return update(old_id, [](Record& record) { record.status = std::string{kStatusSuperseded}; });
}

std::optional<std::string> Store::read_raw(const Record& record) const {
    if (record.raw_ref.empty()) {
        return std::nullopt;
    }
    std::ifstream in{record.raw_ref, std::ios::binary};
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool Store::has_vectors() const {
    return chunks_.stats().dimension > 0;
}

}  // namespace apogee::knowledge
