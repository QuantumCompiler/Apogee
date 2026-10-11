#include "operations/credential_views.h"

#include <nlohmann/json.hpp>

#include <string>
#include <utility>

#include "contracts/config.h"
#include "secrets/store.h"

namespace apogee::operations {

nlohmann::json credentials_document(const std::filesystem::path& config_path,
                                    const secrets::EnvSnapshot& env) {
    const secrets::CredentialStore store{secrets::credentials_path(config_path)};
    nlohmann::json data = nlohmann::json::array();
    for (const secrets::CredentialMetadata& entry : store.list()) {
        // CredentialMetadata has no key field; this cannot serialize one.
        data.push_back(
            nlohmann::json{{"provider", entry.provider}, {"stored_at", entry.stored_at}});
    }
    nlohmann::json backends = nlohmann::json::array();
    try {
        const harness::Config config = harness::load_config(config_path);
        for (const auto& [name, entry] : config.backends) {
            if (!secrets::takes_api_key(entry.type)) {
                continue;
            }
            const secrets::KeyResolution resolution = secrets::resolve_api_key(entry, &store, env);
            nlohmann::json row{{"name", name},
                               {"type", std::string{harness::to_string(entry.type)}},
                               {"source", std::string{secrets::to_string(resolution.source)}}};
            if (!resolution.variable.empty()) {
                row["variable"] = resolution.variable;
            }
            backends.push_back(std::move(row));
        }
    } catch (const harness::ConfigError&) {
        // The store is still listable without a config; the backends column
        // simply has nothing to say.
    }
    nlohmann::json out{
        {"object", "list"}, {"data", std::move(data)}, {"backends", std::move(backends)}};
    if (!store.warning().empty()) {
        out["warning"] = store.warning();
    }
    return out;
}

}  // namespace apogee::operations
