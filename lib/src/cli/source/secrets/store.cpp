#include "secrets/store.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "harness/config_edit.h"

namespace apogee::secrets {
namespace {

/// The stored record. PRIVATE to this file: nothing outside it can name a
/// type that carries a key, so nothing outside it can serialize one.
struct Credential {
    std::string key;
    std::string stored_at;
};

using Slots = std::map<std::string, Credential>;

std::string utc_now() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

/// Reads the file. `ok` is false when it exists but cannot be used; `slots`
/// is then empty and `warning` says why.
struct Loaded {
    Slots slots;
    bool exists = false;
    bool ok = true;
    std::string warning;
};

Loaded load(const std::filesystem::path& path) {
    Loaded loaded;
    std::error_code code;
    if (!std::filesystem::exists(path, code)) {
        return loaded;
    }
    loaded.exists = true;
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        loaded.ok = false;
        loaded.warning = "the credential store at " + path.string() + " could not be read";
        return loaded;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    const nlohmann::json root = nlohmann::json::parse(buffer.str(), nullptr, false);
    const auto credentials = root.is_object() ? root.find("credentials") : root.end();
    if (root.is_discarded() || !root.is_object() || credentials == root.end() ||
        !credentials->is_object()) {
        loaded.ok = false;
        loaded.warning = "the credential store at " + path.string() +
                         " is not the expected JSON; treating it as empty (nothing is "
                         "overwritten until it is repaired or removed)";
        return loaded;
    }
    for (const auto& [provider, entry] : credentials->items()) {
        if (!entry.is_object() || !entry.contains("key") || !entry["key"].is_string()) {
            continue;
        }
        Credential credential;
        credential.key = entry["key"].get<std::string>();
        credential.stored_at = entry.value("stored_at", std::string{});
        loaded.slots[provider] = std::move(credential);
    }
    return loaded;
}

void save(const std::filesystem::path& path, const Slots& slots) {
    nlohmann::json credentials = nlohmann::json::object();
    for (const auto& [provider, credential] : slots) {
        credentials[provider] = {{"key", credential.key}, {"stored_at", credential.stored_at}};
    }
    const nlohmann::json root{{"version", kStoreVersion}, {"credentials", credentials}};
    // 0600 on the temporary file before the rename: the secret is never on
    // disk under the umask's mode.
    harness::write_file_atomically(path, root.dump(2) + "\n", /*private_mode=*/true);
}

}  // namespace

std::filesystem::path credentials_path(const std::filesystem::path& config_path) {
    return config_path.parent_path() / std::string{kCredentialsFileName};
}

CredentialStore::CredentialStore(std::filesystem::path path) : path_{std::move(path)} {}

std::optional<std::string> CredentialStore::key_for(std::string_view provider) const {
    const Loaded loaded = load(path_);
    warning_ = loaded.warning;
    const auto it = loaded.slots.find(std::string{provider});
    if (it == loaded.slots.end()) {
        return std::nullopt;
    }
    return it->second.key;
}

void CredentialStore::put(std::string_view provider, std::string_view key) {
    Loaded loaded = load(path_);
    warning_ = loaded.warning;
    if (!loaded.ok) {
        // Never clobber a store that could not be read: whatever is in it may
        // be someone's only copy of a key.
        throw std::runtime_error(loaded.warning);
    }
    Credential credential;
    credential.key = std::string{key};
    credential.stored_at = utc_now();
    loaded.slots[std::string{provider}] = std::move(credential);
    try {
        save(path_, loaded.slots);
    } catch (const harness::ConfigEditError& e) {
        throw std::runtime_error(std::string{"could not write the credential store: "} + e.what());
    }
}

bool CredentialStore::clear(std::string_view provider) {
    Loaded loaded = load(path_);
    warning_ = loaded.warning;
    if (!loaded.ok) {
        throw std::runtime_error(loaded.warning);
    }
    const auto it = loaded.slots.find(std::string{provider});
    if (it == loaded.slots.end()) {
        return false;
    }
    loaded.slots.erase(it);
    try {
        save(path_, loaded.slots);
    } catch (const harness::ConfigEditError& e) {
        throw std::runtime_error(std::string{"could not write the credential store: "} + e.what());
    }
    return true;
}

std::vector<CredentialMetadata> CredentialStore::list() const {
    const Loaded loaded = load(path_);
    warning_ = loaded.warning;
    std::vector<CredentialMetadata> out;
    out.reserve(loaded.slots.size());
    for (const auto& [provider, credential] : loaded.slots) {
        out.push_back(CredentialMetadata{provider, credential.stored_at});
    }
    return out;  // a std::map iterates sorted
}

}  // namespace apogee::secrets
