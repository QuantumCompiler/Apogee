#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "contracts/config.h"

/// The provider cache (28a): what the last explicit scan found, kept so the
/// answer is free at runtime.
///
/// Probing a provider costs seconds (`provider_probe.h`), so nothing that
/// starts a turn ever does it -- it reads this instead. The file is
/// `cache/providers.json` (`harness::provider_cache_path()`), and it is
/// **disposable state**: absent, truncated, corrupt or from another schema
/// reads as empty, the next scan rebuilds it silently, and every consumer
/// behaves sanely with no cache at all. Reading it never throws.
///
/// One thing here is not a scan result: the **verified** slot -- the date a
/// provider last answered a real turn, and the backend that earned it --
/// written passively when a turn succeeds (provider-surfacing, 28c), never
/// by a probe turn spent on Apogee's initiative.
namespace apogee::backends {

/// A binary's identity for the fingerprint rule: its real path, symlinks
/// followed, and when it last changed. An update replaces the file or moves
/// the link, so either half differing means the version must be asked again.
struct BinaryFingerprint {
    std::string path;
    std::int64_t modified = 0;

    bool operator==(const BinaryFingerprint&) const = default;
};

/// What the detector can say about a login.
enum class CredentialState : std::uint8_t {
    /// Nothing it can see answers the question.
    Unknown,
    /// Evidence was found -- a file exists, or a status command said so.
    Found,
    /// The evidence was looked for and is not there.
    NotFound,
};

[[nodiscard]] std::string_view to_string(CredentialState state) noexcept;

/// The honesty tiers, lowest first. Each is a claim the detector can back:
/// `Installed` with proof, `CredentialsFound` with evidence it names,
/// `Verified` with a recorded successful turn. Never "authenticated".
enum class ProviderTier : std::uint8_t { NotFound, Installed, CredentialsFound, Verified };

/// The tier's words, as every surface prints them: `not found`, `installed`,
/// `credentials found`, `verified`.
[[nodiscard]] std::string_view to_string(ProviderTier tier) noexcept;

/// A provider's last successful real turn.
struct VerifiedRecord {
    /// The local calendar day, `YYYY-MM-DD`.
    std::string date;
    /// The configured backend whose turn it was.
    std::string backend;

    bool operator==(const VerifiedRecord&) const = default;
};

/// One provider, as the last scan found it.
struct ProviderStatus {
    /// The knowledge table's id.
    std::string id;
    harness::BackendType type = harness::BackendType::Mock;

    /// Proof: the binary on PATH, or for an API type a key source resolving.
    bool installed = false;
    /// Where PATH found the binary; empty for an API type or when absent.
    std::string binary;
    /// The binary's identity when its version was last asked.
    std::optional<BinaryFingerprint> fingerprint;
    /// Whether the version was asked for that fingerprint -- answered or not.
    /// A probe that hung is recorded, and asked again only when the binary
    /// changes or a scan refreshes.
    bool version_probed = false;
    /// The version's first line, or empty when unknown.
    std::string version;
    /// The installed tier's evidence, for display: where the binary is and
    /// its version, or which source a key resolves from -- or why not.
    std::string installed_evidence;

    CredentialState credentials = CredentialState::Unknown;
    /// The credentials tier's evidence, named: which file exists, which
    /// command said so -- or what was looked for.
    std::string credential_evidence;

    /// The last successful turn, when one was recorded.
    std::optional<VerifiedRecord> verified;

    /// The highest tier the evidence backs. A verified record counts only
    /// while the provider is still installed: a removed binary is not found,
    /// whatever it once did.
    [[nodiscard]] ProviderTier tier() const noexcept;
};

/// The whole cache file.
struct ProviderCache {
    /// When the providers were last scanned, `YYYY-MM-DDTHH:MM:SSZ`; empty
    /// when never.
    std::string scanned_at;
    /// The last scan's result per provider id.
    std::map<std::string, ProviderStatus, std::less<>> providers;
    /// The verified slot: one record per provider id, the latest.
    std::map<std::string, VerifiedRecord, std::less<>> verified;
    /// The first-launch registration offer (28b), once answered: `accepted`
    /// or `declined`, and the day. Asked once ever -- kept here rather than in
    /// the config, which is the user's file; deleting the cache forgets it.
    std::string offer_answer;
    std::string offer_date;

    /// Whether a scan has ever been recorded.
    [[nodiscard]] bool scanned() const noexcept {
        return !scanned_at.empty();
    }
};

/// The schema this build reads and writes. A file carrying another is read
/// as empty and rewritten by the next scan.
inline constexpr int kProviderCacheSchema = 1;

/// Parses a cache file's text. Anything unreadable -- not JSON, the wrong
/// shape, another schema -- is an empty cache, never an exception; unknown
/// fields are ignored.
[[nodiscard]] ProviderCache parse_provider_cache(std::string_view text) noexcept;

/// The cache as the text `parse_provider_cache` reads back.
[[nodiscard]] std::string render_provider_cache(const ProviderCache& cache);

/// Reads the cache at `path`; absent or unreadable is empty.
[[nodiscard]] ProviderCache load_provider_cache(const std::filesystem::path& path) noexcept;

/// Reads the cache at its layout path.
[[nodiscard]] ProviderCache load_provider_cache() noexcept;

/// Writes `cache` to `path` atomically, creating `cache/` if it is missing.
/// False when it could not be written -- disposable state is never worth
/// failing the command that refreshed it.
bool store_provider_cache(const std::filesystem::path& path, const ProviderCache& cache) noexcept;

/// `when` as the local calendar day, `YYYY-MM-DD` -- the date the verified
/// slot and the offer's answer are recorded with.
[[nodiscard]] std::string cache_day(std::chrono::system_clock::time_point when);

/// Records the registration offer's answer (`accepted` or `declined`) and the
/// day in the cache at `path`. False when it could not be written.
bool record_offer_answer(const std::filesystem::path& path, std::string_view answer,
                         std::string_view date) noexcept;

/// Records that `backend` -- a backend of `type` -- answered a real turn on
/// `date`, in the cache at `path`. A no-op for a type the knowledge table
/// does not cover. Never throws and never fails the turn that earned it.
void record_verified_turn(const std::filesystem::path& path, harness::BackendType type,
                          std::string_view backend, std::string_view date) noexcept;

}  // namespace apogee::backends
