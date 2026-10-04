#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// The provider credential store: one slot per API-billing provider type.
///
/// A `0600` JSON file beside `config.yaml` (`credentials.json`, following
/// `--config`, exactly where the admin token lives), holding a key per
/// provider *type* -- `anthropic`, `openai`, `google` -- and when it was
/// stored. Keyed by type rather than by backend entry on purpose: two entries
/// for one vendor with different keys is what a per-entry `api_key` in the
/// config is for; the store answers "the key for this vendor", which is the
/// case a user actually has.
///
/// **Unleakable by construction.** The type that holds a key is private to
/// `store.cpp`. What this header hands out is `CredentialMetadata`, which
/// structurally has no key field, and `key_for`, the single function that
/// returns a key -- called by the resolver and by nothing that renders,
/// serializes, or logs. A later contributor who marshals metadata cannot leak
/// a secret, because the secret is not there to marshal.
///
/// Vendor-CLI types (`claude-cli` and its siblings) have no slot: they
/// authenticate through their own CLIs, and Apogee never stores, reads or
/// proxies their credentials (SPEC.md -> *a vendor CLI is spawned, never
/// opened*).
namespace apogee::secrets {

inline constexpr std::string_view kCredentialsFileName = "credentials.json";
inline constexpr int kStoreVersion = 1;

/// Everything a listing, response, or event may carry about a stored key.
/// No key field, by design.
struct CredentialMetadata {
    /// The provider type the slot is for: `anthropic`, `openai`, `google`.
    std::string provider;
    /// RFC 3339 UTC.
    std::string stored_at;
};

/// `<dir of config_path>/credentials.json`.
[[nodiscard]] std::filesystem::path credentials_path(const std::filesystem::path& config_path);

class CredentialStore {
public:
    explicit CredentialStore(std::filesystem::path path);

    /// The stored key for `provider`, or nullopt. **The one function that
    /// returns a key.** The resolver calls it; nothing that renders does.
    [[nodiscard]] std::optional<std::string> key_for(std::string_view provider) const;

    /// Stores `key` for `provider`, replacing any existing slot. Written
    /// `0600` before it is renamed into place. Throws std::runtime_error when
    /// the file exists but cannot be read (a corrupt store is never
    /// overwritten) or cannot be written.
    void put(std::string_view provider, std::string_view key);

    /// Removes the slot. False when there was none. Throws as `put` does.
    bool clear(std::string_view provider);

    /// Every slot, as metadata, sorted by provider.
    [[nodiscard]] std::vector<CredentialMetadata> list() const;

    /// Why the last read treated the store as empty, or empty. A store that
    /// cannot be read degrades to nothing stored, with this explanation --
    /// never a crash.
    [[nodiscard]] const std::string& warning() const noexcept {
        return warning_;
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
    mutable std::string warning_;
};

}  // namespace apogee::secrets
