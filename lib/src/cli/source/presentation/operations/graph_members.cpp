#include "operations/graph_members.h"

#include <algorithm>
#include <filesystem>
#include <set>
#include <system_error>

#include "contracts/collection_name.h"
#include "contracts/config_yaml.h"
#include "graph/code_languages.h"
#include "operations/collections.h"
#include "operations/graph_sources.h"

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
        // A member written as a path is never opened (CLI-M45): it maps to
        // null, as a missing one does, and `check` names it.
        if (harness::name_leaves_folder(collection)) {
            out.stores[collection] = nullptr;
            continue;
        }
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
    // The collections' read rule (CLI-M45), in the graph's own words.
    if (harness::name_leaves_folder(name)) {
        out.error = "'" + std::string{name} + "' is not a plain graph name";
        return out;
    }
    // A new graph's name keeps the collections' margin (CLI-M46): its
    // database and build lock sit beside each other under `graphs/`.
    if (config.find_graph(name) == nullptr && name.size() > harness::kNewCollectionNameMax) {
        out.error = "graph name '" + std::string{name} + "' is " + std::to_string(name.size()) +
                    " bytes -- a new name is at most " +
                    std::to_string(harness::kNewCollectionNameMax);
        return out;
    }
    if (graph.collections.empty() && graph.sources.empty()) {
        out.error =
            "at least one member collection or source tree is required (--collections a,b, or "
            "--sources <dir>)";
        return out;
    }
    for (const std::string& collection : graph.collections) {
        if (harness::name_leaves_folder(collection)) {
            out.error = "'" + collection + "' is not a plain collection name";
            return out;
        }
        // A new member is one the config can hold (CLI-M46), unless a store
        // or the config already names it: an ingest could never register it.
        if (const std::string refused = new_collection_error(collection, &config);
            !refused.empty()) {
            out.error = "member " + refused;
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
    // Source trees (27k): absolute, each label once and never a member
    // collection's name -- a label is what a tree's rows are recorded under.
    std::set<std::string> labels;
    for (const std::string& source : graph.sources) {
        if (source.empty() || !std::filesystem::path{source}.is_absolute()) {
            out.error = "source tree '" + source + "' must be an absolute path";
            return out;
        }
        const std::string label = source_member_label(source);
        if (!labels.insert(label).second) {
            out.error = "two source trees are both named '" + label +
                        "' -- a graph's trees need distinct directory names";
            return out;
        }
        if (std::ranges::find(graph.collections, label) != graph.collections.end()) {
            out.error = "source tree '" + source + "' is named like the member collection '" +
                        label + "' -- rename one";
            return out;
        }
    }
    for (const std::string& language : graph.languages) {
        if (graph::code_language_by_name(language) == nullptr) {
            out.error =
                "unknown language '" + language + "' -- one of: " + graph::code_language_names();
            return out;
        }
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
        if (config.find_embedding(collection) != nullptr ||
            file_exists(collection_path(collection))) {
            continue;
        }
        // "Registers it" only of a name the config can hold (CLI-M46): one the
        // config refers to already but cannot hold is written without an entry.
        if (!harness::yaml_plain_key(collection)) {
            const std::string held = harness::collection_create_error(collection, {});
            out.warnings.push_back("member collection '" + collection +
                                   "' is not configured yet, and an ingest writes it without an "
                                   "entry: " +
                                   held);
        } else {
            out.warnings.push_back("member collection '" + collection +
                                   "' is not configured yet (ingest registers it on first use)");
        }
    }
    for (const std::string& source : graph.sources) {
        std::error_code code;
        if (!std::filesystem::is_directory(source, code)) {
            out.warnings.push_back("source tree '" + source + "' is not a directory");
        }
    }
    return out;
}

}  // namespace apogee::commands
