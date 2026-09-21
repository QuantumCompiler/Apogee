#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "commands/command.h"
#include "harness/config.h"
#include "secrets/resolve.h"
#include "secrets/store.h"

/// `apogee auth` -- store, list, and clear provider API keys.
///
/// A key is never taken on the command line: an argument lands in shell
/// history and process listings. `auth add` reads it from a hidden prompt, from
/// standard input with `--stdin`, or copies the conventional environment
/// variable with `--from-env`. `auth list` shows metadata only -- the type it
/// renders has no key field -- and says which source answers for each
/// configured cloud backend, so "why is it using that key?" has an answer
/// that is not the key.
namespace apogee::commands {

/// What `auth list` renders, gathered so the rendering is testable -- and so
/// the leak test can render it with a known secret in the store.
struct AuthListing {
    std::vector<secrets::CredentialMetadata> stored;

    /// Which rung answers for each configured backend that takes a key.
    struct BackendKey {
        std::string name;
        std::string type;
        secrets::KeySource source = secrets::KeySource::None;
        std::string variable;
    };

    std::vector<BackendKey> backends;
    /// A store that could not be read, explained.
    std::string warning;
};

[[nodiscard]] AuthListing gather_auth_listing(const harness::Config& config,
                                              const secrets::CredentialStore& store,
                                              const secrets::EnvSnapshot& env);

[[nodiscard]] std::string render_auth_listing(const AuthListing& listing);

class AuthCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
