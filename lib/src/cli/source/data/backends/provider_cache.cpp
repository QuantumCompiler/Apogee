#include "backends/provider_cache.h"

#include <nlohmann/json.hpp>

#include <array>
#include <exception>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>
#include <utility>

#include "backends/provider_table.h"
#include "contracts/config_edit.h"
#include "contracts/layout.h"

namespace apogee::backends {

namespace {

using nlohmann::json;

constexpr std::array<std::pair<CredentialState, std::string_view>, 3> kCredentialNames{{
    {CredentialState::Unknown, "unknown"},
    {CredentialState::Found, "found"},
    {CredentialState::NotFound, "not found"},
}};

[[nodiscard]] CredentialState credential_state_from(std::string_view name) noexcept {
    for (const auto& [state, spelling] : kCredentialNames) {
        if (spelling == name) {
            return state;
        }
    }
    return CredentialState::Unknown;
}

/// A string field, or empty when absent or not a string -- the cache is read
/// leniently field by field, so one odd value costs that value, not the file.
[[nodiscard]] std::string string_field(const json& object, const char* key) {
    const auto found = object.find(key);
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

[[nodiscard]] bool bool_field(const json& object, const char* key) {
    const auto found = object.find(key);
    return found != object.end() && found->is_boolean() && found->get<bool>();
}

[[nodiscard]] std::optional<harness::BackendType> type_of(std::string_view name) {
    return harness::backend_type_from_string(name);
}

[[nodiscard]] ProviderStatus status_from(const std::string& id, const json& row) {
    ProviderStatus status;
    status.id = id;
    if (const auto type = type_of(string_field(row, "type"))) {
        status.type = *type;
    }
    status.installed = bool_field(row, "installed");
    status.binary = string_field(row, "binary");
    if (const auto found = row.find("fingerprint"); found != row.end() && found->is_object()) {
        const auto modified = found->find("modified");
        if (modified != found->end() && modified->is_number_integer()) {
            status.fingerprint =
                BinaryFingerprint{string_field(*found, "path"), modified->get<std::int64_t>()};
        }
    }
    status.version_probed = bool_field(row, "version_probed");
    status.version = string_field(row, "version");
    status.installed_evidence = string_field(row, "installed_evidence");
    status.credentials = credential_state_from(string_field(row, "credentials"));
    status.credential_evidence = string_field(row, "credential_evidence");
    return status;
}

[[nodiscard]] json row_for(const ProviderStatus& status) {
    json row = json::object();
    row["type"] = std::string{harness::to_string(status.type)};
    row["installed"] = status.installed;
    row["binary"] = status.binary;
    if (status.fingerprint) {
        row["fingerprint"] = {{"path", status.fingerprint->path},
                              {"modified", status.fingerprint->modified}};
    }
    row["version_probed"] = status.version_probed;
    row["version"] = status.version;
    row["installed_evidence"] = status.installed_evidence;
    row["credentials"] = std::string{to_string(status.credentials)};
    row["credential_evidence"] = status.credential_evidence;
    return row;
}

}  // namespace

std::string_view to_string(CredentialState state) noexcept {
    for (const auto& [value, spelling] : kCredentialNames) {
        if (value == state) {
            return spelling;
        }
    }
    return "unknown";
}

std::string_view to_string(ProviderTier tier) noexcept {
    switch (tier) {
        case ProviderTier::NotFound:
            return "not found";
        case ProviderTier::Installed:
            return "installed";
        case ProviderTier::CredentialsFound:
            return "credentials found";
        case ProviderTier::Verified:
            return "verified";
    }
    return "not found";
}

ProviderTier ProviderStatus::tier() const noexcept {
    if (!installed) {
        return ProviderTier::NotFound;
    }
    if (verified) {
        return ProviderTier::Verified;
    }
    if (credentials == CredentialState::Found) {
        return ProviderTier::CredentialsFound;
    }
    return ProviderTier::Installed;
}

ProviderCache parse_provider_cache(std::string_view text) noexcept {
    try {
        const json document = json::parse(text);
        if (!document.is_object()) {
            return {};
        }
        const auto schema = document.find("schema");
        if (schema == document.end() || !schema->is_number_integer() ||
            schema->get<int>() != kProviderCacheSchema) {
            return {};
        }
        ProviderCache cache;
        cache.scanned_at = string_field(document, "scanned_at");
        if (const auto rows = document.find("providers");
            rows != document.end() && rows->is_object()) {
            for (const auto& [id, row] : rows->items()) {
                if (row.is_object()) {
                    cache.providers.emplace(id, status_from(id, row));
                }
            }
        }
        if (const auto records = document.find("verified");
            records != document.end() && records->is_object()) {
            for (const auto& [id, record] : records->items()) {
                if (!record.is_object()) {
                    continue;
                }
                VerifiedRecord verified{string_field(record, "date"),
                                        string_field(record, "backend")};
                if (!verified.date.empty()) {
                    cache.verified.emplace(id, std::move(verified));
                }
            }
        }
        for (auto& [id, status] : cache.providers) {
            if (const auto found = cache.verified.find(id); found != cache.verified.end()) {
                status.verified = found->second;
            }
        }
        return cache;
    } catch (const std::exception&) {
        return {};
    }
}

std::string render_provider_cache(const ProviderCache& cache) {
    json document = json::object();
    document["schema"] = kProviderCacheSchema;
    document["scanned_at"] = cache.scanned_at;
    json rows = json::object();
    for (const auto& [id, status] : cache.providers) {
        rows[id] = row_for(status);
    }
    document["providers"] = std::move(rows);
    json records = json::object();
    for (const auto& [id, record] : cache.verified) {
        records[id] = {{"date", record.date}, {"backend", record.backend}};
    }
    document["verified"] = std::move(records);
    return document.dump(2, ' ', false, json::error_handler_t::replace) + "\n";
}

ProviderCache load_provider_cache(const std::filesystem::path& path) noexcept {
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return {};
        }
        const std::string text{std::istreambuf_iterator<char>(in),
                               std::istreambuf_iterator<char>()};
        return parse_provider_cache(text);
    } catch (const std::exception&) {
        return {};
    }
}

ProviderCache load_provider_cache() noexcept {
    try {
        return load_provider_cache(harness::provider_cache_path());
    } catch (const std::exception&) {
        return {};
    }
}

bool store_provider_cache(const std::filesystem::path& path, const ProviderCache& cache) noexcept {
    try {
        std::error_code ignored;
        std::filesystem::create_directories(path.parent_path(), ignored);
        harness::write_file_atomically(path, render_provider_cache(cache));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void record_verified_turn(const std::filesystem::path& path, harness::BackendType type,
                          std::string_view backend, std::string_view date) noexcept {
    try {
        const ProviderFacts* facts = provider_for_type(type);
        if (facts == nullptr) {
            return;
        }
        const std::string_view id = facts->id;
        ProviderCache cache = load_provider_cache(path);
        VerifiedRecord record{std::string{date}, std::string{backend}};
        const auto existing = cache.verified.find(id);
        if (existing != cache.verified.end() && existing->second == record) {
            return;
        }
        cache.verified.insert_or_assign(std::string{id}, record);
        if (const auto row = cache.providers.find(id); row != cache.providers.end()) {
            row->second.verified = std::move(record);
        }
        (void)store_provider_cache(path, cache);
    } catch (const std::exception&) {
        // Disposable state: a turn that succeeded stays a success.
    }
}

}  // namespace apogee::backends
