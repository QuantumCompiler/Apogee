#include "backends/model_roster.h"

#include <nlohmann/json.hpp>

#include <array>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
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

/// One model a built-in roster names: its id, as the CLI's `--model` takes
/// it, and how it is said.
struct BuiltInModel {
    std::string_view id;
    std::string_view name;
};

// ADR 0009: these lists are kept as current as their vendors', by the change
// that finds them stale -- a vendor shipping, renaming or retiring a model, a
// change touching the backends, rosters or model selection, every release
// roll. Each table names its source; each `...Reviewed` date moves whenever
// the table is checked, rows changed or not, and users see it. The recipe is
// DEVELOPER.md -> Updating a carried model list.

/// Claude Code prints no model list, so Apogee carries Anthropic's: every
/// model its models overview listed as current or still available on
/// 2026-10-10 (platform.claude.com/docs/en/models/overview), by Claude API
/// id -- the 4.6 generation and later dateless, the 4.5 models by the aliases
/// the API keeps for their dated snapshots. A new model is a row here, a
/// retired one a deletion; the CLI hands any id to the API, which is the
/// judge.
constexpr std::string_view kClaudeCodeReviewed{"2026-10-10"};
constexpr std::array kClaudeCodeModels{
    BuiltInModel{"claude-fable-5-1", "Claude Fable 5.1"},
    BuiltInModel{"claude-opus-5-5", "Claude Opus 5.5"},
    BuiltInModel{"claude-sonnet-5-5", "Claude Sonnet 5.5"},
    BuiltInModel{"claude-haiku-5-5", "Claude Haiku 5.5"},
    BuiltInModel{"claude-fable-5", "Claude Fable 5"},
    BuiltInModel{"claude-opus-5", "Claude Opus 5"},
    BuiltInModel{"claude-sonnet-5", "Claude Sonnet 5"},
    BuiltInModel{"claude-opus-4-8", "Claude Opus 4.8"},
    BuiltInModel{"claude-opus-4-7", "Claude Opus 4.7"},
    BuiltInModel{"claude-opus-4-6", "Claude Opus 4.6"},
    BuiltInModel{"claude-sonnet-4-6", "Claude Sonnet 4.6"},
    BuiltInModel{"claude-opus-4-5", "Claude Opus 4.5"},
    BuiltInModel{"claude-haiku-4-5", "Claude Haiku 4.5"},
};

/// The Gemini CLI prints no model list either; it has aliases it maps to its
/// current models itself (`GEMINI_MODEL_ALIAS_*`, Gemini CLI 0.46.0). `auto`
/// is the CLI with no `--model` at all -- the entry as configured -- so it is
/// not listed.
constexpr std::string_view kGeminiCliReviewed{"2026-10-10"};
constexpr std::array kGeminiCliModels{
    BuiltInModel{"pro", "Gemini CLI's pro alias"},
    BuiltInModel{"flash", "Gemini CLI's flash alias"},
    BuiltInModel{"flash-lite", "Gemini CLI's flash-lite alias"},
};

template <std::size_t N>
[[nodiscard]] ProviderRoster built_in(const std::array<BuiltInModel, N>& models,
                                      std::string_view reviewed) {
    ProviderRoster roster;
    roster.built_in = true;
    roster.fetched_at = std::string{reviewed};
    for (const BuiltInModel& model : models) {
        roster.models.push_back(RosterModel{std::string{model.id}, std::string{model.name}});
    }
    return roster;
}

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
            if (roster.built_in) {
                continue;  // the binary's, read beside the cache, never kept
            }
            json models = json::array();
            for (const RosterModel& model : roster.models) {
                models.push_back(json{{"id", model.id}, {"name", model.name}});
            }
            rosters[type] = json{{"fetched_at", roster.fetched_at}, {"models", std::move(models)}};
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

RosterCache built_in_rosters() {
    RosterCache cache;
    cache.rosters.emplace("claude-cli", built_in(kClaudeCodeModels, kClaudeCodeReviewed));
    cache.rosters.emplace("gemini-cli", built_in(kGeminiCliModels, kGeminiCliReviewed));
    return cache;
}

RosterCache known_rosters() {
    RosterCache known = load_roster_cache();
    for (auto& [type, roster] : built_in_rosters().rosters) {
        known.rosters.try_emplace(type, std::move(roster));  // a fetched one wins
    }
    return known;
}

}  // namespace apogee::backends
