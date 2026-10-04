#include "models/lineage.h"

#include <algorithm>
#include <cstddef>
#include <system_error>
#include <utility>

namespace apogee::models {
namespace {

/// Longer than any chain Apogee makes (a quantization of a conversion of a
/// pull is three); a record naming itself cannot loop the walk.
constexpr std::size_t kLongestChain = 8;

[[nodiscard]] std::string handle_of(const StoredGguf& stored) {
    return weights_handle(stored.model, kGgufFormat, stored.id);
}

[[nodiscard]] std::string handle_of(const StoredSnapshot& stored) {
    return weights_handle(stored.model, kSafetensorsFormat, stored.id);
}

}  // namespace

Origin origin_from_record(const std::optional<Sidecar>& record) {
    Origin origin;
    if (!record.has_value()) {
        return origin;
    }
    origin.from = record->ref;
    if (record->source == "convert") {
        origin.kind = OriginKind::Converted;
    } else if (record->source == "quantize") {
        origin.kind = OriginKind::Quantized;
        origin.detail = record->transform_note;
    } else if (record->source == "huggingface" || record->source == "ollama") {
        origin.kind = OriginKind::Pulled;
        origin.detail = record->source;
    } else if (record->source == "train") {
        origin.kind = OriginKind::Trained;
        origin.detail = record->transform_note;
    } else {
        origin.from.clear();
    }
    return origin;
}

Origin origin_from_snapshot(const std::optional<Snapshot>& record) {
    Origin origin;
    if (!record.has_value() || record->ref.empty()) {
        return origin;
    }
    origin.kind = OriginKind::Pulled;
    origin.from = record->ref;
    if (!record->revision.empty() && record->revision != "main") {
        origin.from += "@" + record->revision;
    }
    origin.detail = record->source;
    return origin;
}

Lineage::Lineage(std::vector<RecordedGguf> ggufs, std::vector<RecordedSnapshot> snapshots)
    : ggufs_(std::move(ggufs)), snapshots_(std::move(snapshots)) {}

const RecordedGguf* Lineage::gguf(std::string_view handle) const {
    const auto found = std::ranges::find_if(
        ggufs_, [handle](const RecordedGguf& gguf) { return handle_of(gguf.stored) == handle; });
    return found == ggufs_.end() ? nullptr : &*found;
}

const RecordedSnapshot* Lineage::snapshot(std::string_view handle) const {
    const auto found = std::ranges::find_if(snapshots_, [handle](const RecordedSnapshot& snapshot) {
        return handle_of(snapshot.stored) == handle;
    });
    return found == snapshots_.end() ? nullptr : &*found;
}

const RecordedSnapshot* Lineage::only_snapshot(std::string_view model) const {
    // With two, either could be the parent, and a guess would fold the wrong
    // one -- so only exactly one answers.
    const RecordedSnapshot* only = nullptr;
    for (const RecordedSnapshot& snapshot : snapshots_) {
        if (snapshot.stored.model != model) {
            continue;
        }
        if (only != nullptr) {
            return nullptr;
        }
        only = &snapshot;
    }
    return only;
}

Origin Lineage::stored_origin(const RecordedGguf& gguf) const {
    Origin origin = origin_from_record(gguf.record);
    if (origin.kind != OriginKind::Unknown) {
        return origin;
    }
    // No record says: the model's one snapshot, if it has exactly one.
    if (const RecordedSnapshot* only = only_snapshot(gguf.stored.model)) {
        origin.kind = OriginKind::Converted;
        origin.from = handle_of(only->stored);
        origin.inferred = true;
    }
    return origin;
}

Origin Lineage::origin_of(const std::filesystem::path& file,
                          const std::optional<Sidecar>& record) const {
    const std::filesystem::path normal = file.lexically_normal();
    for (const RecordedGguf& gguf : ggufs_) {
        if (gguf.stored.file.lexically_normal() == normal) {
            return stored_origin(gguf);
        }
    }
    return origin_from_record(record);
}

Origin Lineage::origin_of(std::string_view handle) const {
    if (const RecordedGguf* found = gguf(handle)) {
        return stored_origin(*found);
    }
    if (const RecordedSnapshot* found = snapshot(handle)) {
        return origin_from_snapshot(found->record);
    }
    return {};
}

std::vector<LineageLink> Lineage::chain(const Origin& origin) const {
    std::vector<LineageLink> links;
    Origin at = origin;
    while (links.size() < kLongestChain) {
        if (at.kind == OriginKind::Unknown && !links.empty()) {
            break;  // the records end here; the first link says "unknown" itself
        }
        LineageLink link{.origin = at};
        const bool derived = at.kind == OriginKind::Converted || at.kind == OriginKind::Quantized;
        if (!derived) {
            links.push_back(std::move(link));
            break;
        }
        const std::optional<WeightsHandle> parent = parse_weights_handle(at.from);
        if (!parent.has_value()) {
            // A path outside the store: said as it was recorded, and checked.
            std::error_code code;
            link.missing = !std::filesystem::exists(at.from, code);
            links.push_back(std::move(link));
            break;
        }
        if (at.kind == OriginKind::Quantized) {
            const RecordedGguf* found = gguf(at.from);
            link.missing = found == nullptr;
            links.push_back(std::move(link));
            if (found != nullptr) {
                at = stored_origin(*found);
                continue;
            }
            // The F16 is gone, its record with it -- deleted for its size,
            // most often, once quantized. Its model's one snapshot is where
            // it came from as far as the store can tell, and is said so.
            const RecordedSnapshot* only = only_snapshot(parent->model);
            if (only == nullptr) {
                break;
            }
            at = Origin{
                .kind = OriginKind::Converted, .from = handle_of(only->stored), .inferred = true};
        } else {
            const RecordedSnapshot* found = snapshot(at.from);
            link.missing = found == nullptr;
            links.push_back(std::move(link));
            if (found == nullptr) {
                break;
            }
            at = origin_from_snapshot(found->record);
        }
    }
    return links;
}

bool Lineage::converted(const Origin& origin) const {
    return std::ranges::any_of(chain(origin), [](const LineageLink& link) {
        return link.origin.kind == OriginKind::Converted;
    });
}

std::optional<Consumption> Lineage::consumed(std::string_view handle) const {
    // Each by how many steps it is from the snapshot: the conversion first,
    // what was quantized from it after.
    std::vector<std::pair<std::size_t, std::string>> recorded_by;
    std::vector<std::pair<std::size_t, std::string>> inferred_by;
    for (const RecordedGguf& gguf : ggufs_) {
        bool guessed = false;
        const std::vector<LineageLink> links = chain(stored_origin(gguf));
        for (std::size_t step = 0; step < links.size(); ++step) {
            const Origin& origin = links[step].origin;
            guessed = guessed || origin.inferred;
            if (origin.kind == OriginKind::Converted && origin.from == handle) {
                (guessed ? inferred_by : recorded_by).emplace_back(step, handle_of(gguf.stored));
                break;
            }
        }
    }
    const auto by_step = [](std::vector<std::pair<std::size_t, std::string>> found, bool guessed) {
        std::ranges::stable_sort(found, {}, &std::pair<std::size_t, std::string>::first);
        Consumption consumption{.inferred = guessed};
        for (auto& [step, gguf] : found) {
            consumption.by.push_back(std::move(gguf));
        }
        return consumption;
    };
    const Consumption recorded = by_step(std::move(recorded_by), false);
    const Consumption inferred = by_step(std::move(inferred_by), true);
    if (!recorded.by.empty()) {
        return recorded;
    }
    if (!inferred.by.empty()) {
        return inferred;
    }
    return std::nullopt;
}

Lineage read_lineage(const StoreRoots& roots) {
    std::vector<RecordedGguf> ggufs;
    for (StoredGguf& stored : list_store_ggufs(roots)) {
        std::optional<Sidecar> record = load_sidecar(stored.file);
        ggufs.push_back({.stored = std::move(stored), .record = std::move(record)});
    }
    std::vector<RecordedSnapshot> snapshots;
    for (StoredSnapshot& stored : list_store_snapshots(roots)) {
        std::optional<Snapshot> record = load_snapshot(stored.dir);
        snapshots.push_back({.stored = std::move(stored), .record = std::move(record)});
    }
    return Lineage{std::move(ggufs), std::move(snapshots)};
}

}  // namespace apogee::models
