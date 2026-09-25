#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "harness/config.h"
#include "secrets/store.h"

/// The one key resolver.
///
/// **This file exists for the same reason `harness/roles.h` does.** A
/// precedence chain written inline where a backend is built gets copied the
/// day a second surface builds one, and the copies drift -- and a CLI and a
/// server using different keys for the same entry is a bug nobody notices
/// until a bill arrives. So the chain lives here, once, and
/// `cli.one_key_resolver` fails any other file that reads an entry's
/// `api_key` or names a conventional variable.
///
/// ## The chain (decided 2026-08-24; confirmed by the user 2026-09-13)
///
/// ```
/// the entry's api_key (already ${ENV}-expanded by the loader)
///   > the stored slot for the entry's provider type
///     > the ambient conventional variable, from a snapshot taken once
/// ```
///
/// The config entry stays first on purpose: a `${ENV_VAR}` reference there is
/// the documented, portable way, and lets two entries for one vendor carry
/// different keys. The store is for keys kept out of config and environment
/// entirely.
///
/// ## The snapshot
///
/// The environment is read **once** and never again. Two writers on one
/// process race (Ommi's OMMI-9 lesson: a token injected into the environment
/// by one path changed what another path resolved, mid-run); a snapshot taken
/// before anything resolves cannot move.
namespace apogee::secrets {

/// Which rung answered.
enum class KeySource : std::uint8_t { None, Config, Store, Environment };

[[nodiscard]] std::string_view to_string(KeySource source) noexcept;

/// A resolved key and where it came from. Only the resolver and the factory
/// ever hold one; everything that reports reads `source` and `variable`.
struct KeyResolution {
    std::string key;
    KeySource source = KeySource::None;
    /// The environment variable that answered, when `source` is Environment.
    std::string variable;

    [[nodiscard]] bool found() const noexcept {
        return source != KeySource::None;
    }
};

/// Whether `type` is an API-billing provider that takes a key at all --
/// `anthropic`, `openai`, `google`. Vendor-CLI types and local ones do not.
[[nodiscard]] bool takes_api_key(harness::BackendType type) noexcept;

/// The conventional variables for `type`, in the order they are consulted:
/// `ANTHROPIC_API_KEY`; `OPENAI_API_KEY`; `GEMINI_API_KEY` then
/// `GOOGLE_API_KEY`. Empty for a type that takes no key.
[[nodiscard]] std::span<const std::string_view> conventional_variables(
    harness::BackendType type) noexcept;

/// The provider spelling a slot is keyed by (`anthropic`, `openai`, `google`),
/// or nullopt for a type that takes no key.
[[nodiscard]] std::optional<std::string> slot_name(harness::BackendType type);

/// The slot names `slot_type` accepts, for completion.
[[nodiscard]] std::span<const std::string_view> slot_names();

/// Parses a slot name. Returns the type for `anthropic`/`openai`/`google`;
/// nullopt otherwise -- including for a vendor-CLI type, which is a real
/// backend type but never a slot.
[[nodiscard]] std::optional<harness::BackendType> slot_type(std::string_view name) noexcept;

/// The environment as it was when this was taken.
class EnvSnapshot {
public:
    using Lookup = std::function<std::string(std::string_view)>;

    /// An empty snapshot: nothing set. What a test uses to say "no
    /// environment", and what a caller passes to opt out of the third rung.
    EnvSnapshot() = default;

    /// Captures every conventional variable through `lookup`, now.
    [[nodiscard]] static EnvSnapshot capture(const Lookup& lookup);

    /// The process-wide snapshot: captured from the real environment the
    /// first time it is asked for, then never refreshed.
    [[nodiscard]] static const EnvSnapshot& process();

    /// The captured value, or empty.
    [[nodiscard]] std::string get(std::string_view name) const;

private:
    std::map<std::string, std::string, std::less<>> values_;
};

/// Resolves the key for `entry` through the chain. `store` may be null
/// (no store: the second rung is skipped).
[[nodiscard]] KeyResolution resolve_api_key(const harness::BackendConfig& entry,
                                            const CredentialStore* store, const EnvSnapshot& env);

/// The message for an entry that resolved nothing: names the entry's own
/// variable, `apogee auth add`, and the config field -- and never a key.
[[nodiscard]] std::string no_key_message(std::string_view backend_name, harness::BackendType type);

}  // namespace apogee::secrets
