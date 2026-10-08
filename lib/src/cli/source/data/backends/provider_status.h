#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "backends/provider_cache.h"
#include "backends/provider_table.h"
#include "contracts/config.h"

namespace apogee::secrets {
class CredentialStore;
class EnvSnapshot;
}  // namespace apogee::secrets

/// A provider's status without a probe (28a's cheap checks, shared since 28c).
///
/// Everything here is spawn-free: whether a binary is on PATH and what it is,
/// whether an evidence file exists, whether a key resolves -- plus what the
/// last explicit scan cached (a version asked under the same fingerprint, a
/// status command's answer, the verified record). So the doctor and `models
/// list` tell the truth about a configured backend on a machine that never
/// ran a scan, and still never start a vendor's binary to do it: the
/// spawning half is `provider_probe.h`, which `cli.no_provider_probes` keeps
/// to the explicit surfaces.
///
/// **Existence, never contents.** The filesystem is seen through
/// `ExistenceView`, which has no read; `cli.no_vendor_credentials` holds this
/// file, the table and the prober to having no way to read one.
namespace apogee::backends {

/// The filesystem as the detector may see it: existence and identity, never
/// contents.
///
/// There is no read here, on purpose -- a credential file's contents cannot
/// reach the detector by any edit short of changing this interface.
class ExistenceView {
public:
    ExistenceView() = default;
    virtual ~ExistenceView() = default;
    ExistenceView(const ExistenceView&) = delete;
    ExistenceView& operator=(const ExistenceView&) = delete;
    ExistenceView(ExistenceView&&) = delete;
    ExistenceView& operator=(ExistenceView&&) = delete;

    /// `program`'s absolute path when it is executable on PATH -- or, when it
    /// names a path, that path when it is a file -- else empty.
    [[nodiscard]] virtual std::string find_program(std::string_view program) const = 0;
    /// Whether `path` exists.
    [[nodiscard]] virtual bool exists(const std::filesystem::path& path) const = 0;
    /// What a binary is: its real path, symlinks followed, and the time it
    /// last changed -- nullopt when it cannot be read. An updated CLI is a
    /// new file, or a link moved to one, so either half changing means the
    /// version must be asked again.
    [[nodiscard]] virtual std::optional<BinaryFingerprint> fingerprint(
        const std::filesystem::path& path) const = 0;
    /// The user's home directory, or nullopt.
    [[nodiscard]] virtual std::optional<std::filesystem::path> home() const = 0;
};

/// The real filesystem, through the platform seam.
[[nodiscard]] std::unique_ptr<ExistenceView> host_existence_view();

/// Where an API type's key would come from -- `ANTHROPIC_API_KEY`, `the
/// store` -- or nullopt when none resolves. Never the key itself.
using KeyPresence = std::function<std::optional<std::string>(harness::BackendType)>;

/// The real key presence, through the one key resolver: an entry of the type
/// with no `api_key`, so the store and then the environment answer. The key
/// the resolver returns is dropped where it is received.
[[nodiscard]] KeyPresence host_key_presence(const secrets::CredentialStore* store,
                                            const secrets::EnvSnapshot& env);

/// Fills `status`'s credentials tier from the provider's evidence files --
/// tested for existence, never opened -- or says the provider leaves none.
/// Not for a status-command provider, whose answer only a scan can ask.
void credentials_from_evidence(const ProviderFacts& facts, const ExistenceView& view,
                               ProviderStatus& status);

/// `'claude' was not found on PATH` -- the words the use-time error and the
/// doctor share, the remedy (`ProviderFacts::remedy`) after them.
[[nodiscard]] std::string not_on_path(std::string_view program);

/// A provider's tier in words, with its evidence -- `credentials found
/// (~/.claude.json exists); 2.1.289 (Claude Code)`, `verified -- answered a
/// turn on 2026-10-06`, `installed (...); version not asked yet` -- or, not
/// found, why. The one phrasing `check`, `models info` and the rest print
/// (28c); never "authenticated".
[[nodiscard]] std::string describe_status(const ProviderStatus& status);

/// One configured backend's provider status, spawn-free: its own `binary:`
/// (or the provider's) looked for on PATH, its evidence files, its own key
/// through the one resolver -- and from `cache`, the version asked under the
/// same fingerprint, a status command's last answer and the verified record.
/// Nullopt for a type the knowledge table does not cover (local types).
[[nodiscard]] std::optional<ProviderStatus> backend_provider_status(
    const harness::BackendConfig& entry, const ExistenceView& view,
    const secrets::CredentialStore* store, const secrets::EnvSnapshot& env,
    const ProviderCache& cache);

}  // namespace apogee::backends
