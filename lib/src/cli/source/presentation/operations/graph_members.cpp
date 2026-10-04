#include "operations/graph_members.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

#include "operations/collections.h"

namespace apogee::commands {

namespace {

[[nodiscard]] bool file_exists(const std::filesystem::path& path) {
    std::error_code code;
    return std::filesystem::exists(path, code);
}

}  // namespace

embedstore::MemberStores GraphMembers::views() const {
    embedstore::MemberStores out;
    for (const auto& [collection, store] : stores) {
        out[collection] = store.get();
    }
    return out;
}

std::vector<graph::Member> GraphMembers::members(const harness::NamedGraphConfig& named) const {
    std::vector<graph::Member> out;
    for (const std::string& collection : named.collections) {
        const auto it = stores.find(collection);
        out.push_back(graph::Member{.collection = collection,
                                    .store = it == stores.end() ? nullptr : it->second.get()});
    }
    return out;
}

bool GraphMembers::any_data() const {
    return std::ranges::any_of(stores, [](const auto& entry) {
        return entry.second != nullptr && entry.second->chunk_count() > 0;
    });
}

GraphMembers open_graph_members(const harness::NamedGraphConfig& named) {
    GraphMembers out;
    for (const std::string& collection : named.collections) {
        const std::filesystem::path path = collection_path(collection);
        out.stores[collection] =
            file_exists(path) ? std::make_unique<embedstore::Store>(path) : nullptr;
    }
    return out;
}

NamedGraphValidation validate_named_graph(const harness::Config& config, std::string_view name,
                                          const harness::NamedGraphConfig& graph) {
    NamedGraphValidation out;
    if (name.empty()) {
        out.error = "a graph needs a name";
        return out;
    }
    if (name.find("..") != std::string_view::npos || name.find('/') != std::string_view::npos ||
        name.find('\\') != std::string_view::npos) {
        out.error = "'" + std::string{name} + "' is not a plain graph name";
        return out;
    }
    if (graph.collections.empty()) {
        out.error = "at least one member collection is required (--collections a,b)";
        return out;
    }
    for (const std::string& collection : graph.collections) {
        if (collection.empty() || collection.find("..") != std::string::npos ||
            collection.find('/') != std::string::npos ||
            collection.find('\\') != std::string::npos) {
            out.error = "'" + collection + "' is not a plain collection name";
            return out;
        }
    }
    // The collision ban: resolution is graphs-first, so a graph sharing a
    // collection's name would make the collection's own graph unreachable.
    const bool on_disk = std::ranges::any_of(collection_names(), [&](const std::string& existing) {
        return harness::CaseInsensitiveLess{}(existing, name) ==
                   harness::CaseInsensitiveLess{}(name, existing) &&
               !harness::CaseInsensitiveLess{}(existing, name);
    });
    if (config.find_embedding(name) != nullptr || on_disk) {
        out.error = "'" + std::string{name} +
                    "' is already a collection name -- graph names must not collide with "
                    "collection names";
        return out;
    }
    if (!graph.extract_backend.empty() && config.find_backend(graph.extract_backend) == nullptr) {
        out.error = "extraction backend '" + graph.extract_backend +
                    "' is not configured (add it with 'apogee config add-backend')";
        return out;
    }
    if (graph.hops < 1 || graph.hops > 2) {
        out.error = "hops must be 1 or 2";
        return out;
    }
    if (graph.max_entities < 1) {
        out.error = "max_entities must be at least 1";
        return out;
    }
    for (const std::string& collection : graph.collections) {
        if (config.find_embedding(collection) == nullptr &&
            !file_exists(collection_path(collection))) {
            out.warnings.push_back("member collection '" + collection +
                                   "' is not configured yet (ingest registers it on first use)");
        }
    }
    return out;
}

}  // namespace apogee::commands
