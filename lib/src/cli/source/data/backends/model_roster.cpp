#include "backends/model_roster.h"

#include <nlohmann/json.hpp>

#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

#include "contracts/config_edit.h"
#include "contracts/layout.h"

namespace apogee::backends {
namespace {

using nlohmann::json;

/// Bumped when the shape changes; another number reads as empty (the
/// provider cache's rule: disposable state never migrates, it rebuilds).
constexpr int kRosterCacheSchema = 1;

}  // namespace

const ProviderRoster* RosterCache::roster_for(std::string_view type) const noexcept {
    const auto found = rosters.find(std::string{type});
    return found == rosters.end() ? nullptr : &found->second;
}

RosterCache load_roster_cache() {
    try {
        std::ifstream in{harness::roster_cache_path(), std::ios::binary};
        if (!in) {
            return {};
        }
        const std::string text{std::istreambuf_iterator<char>{in},
                               std::istreambuf_iterator<char>{}};
        const json document = json::parse(text, nullptr, false);
        if (document.is_discarded() || !document.is_object()) {
            return {};
        }
        const auto schema = document.find("schema");
        if (schema == document.end() || !schema->is_number_integer() ||
            schema->get<int>() != kRosterCacheSchema) {
            return {};
        }
        RosterCache cache;
        if (const auto rosters = document.find("rosters");
            rosters != document.end() && rosters->is_object()) {
            for (const auto& [type, entry] : rosters->items()) {
                if (!entry.is_object()) {
                    continue;
                }
                ProviderRoster roster;
                roster.fetched_at = entry.value("fetched_at", std::string{});
                if (const auto models = entry.find("models");
                    models != entry.end() && models->is_array()) {
                    for (const json& row : *models) {
                        if (!row.is_object()) {
                            continue;
                        }
                        RosterModel model;
                        model.id = row.value("id", std::string{});
                        model.name = row.value("name", model.id);
                        if (!model.id.empty()) {
                            roster.models.push_back(std::move(model));
                        }
                    }
                }
                cache.rosters.emplace(type, std::move(roster));
            }
        }
        return cache;
    } catch (const std::exception&) {
        return {};
    }
}

std::string save_roster_cache(const RosterCache& cache) {
    try {
        json rosters = json::object();
        for (const auto& [type, roster] : cache.rosters) {
            json models = json::array();
            for (const RosterModel& model : roster.models) {
                models.push_back(json{{"id", model.id}, {"name", model.name}});
            }
            rosters[type] = json{{"fetched_at", roster.fetched_at},
                                 {"models", std::move(models)}};
        }
        const json document{{"schema", kRosterCacheSchema}, {"rosters", std::move(rosters)}};
        const std::filesystem::path path = harness::roster_cache_path();
        std::error_code ignored;
        std::filesystem::create_directories(path.parent_path(), ignored);
        harness::write_file_atomically(path, document.dump());
        return {};
    } catch (const std::exception& e) {
        return e.what();
    }
}

}  // namespace apogee::backends
