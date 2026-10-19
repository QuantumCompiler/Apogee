#pragma once

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

#include "forge/cli_call.h"
#include "forge/forge.h"

/// What `gh` and `glab` share as transports (48a): the CLI found -- an
/// explicit path, else its name on PATH, never bundled or modified -- each
/// call one bounded child, and the refusal ladder, every rung in the CLI's
/// own words with the fix named. A missing CLI, a logged-out one, a
/// repository the login cannot see and any other API failure are told
/// apart; nothing here reads a token or a credential file, ever.
namespace apogee::forge {

struct CliTransportOptions {
    /// The CLI to run: a path, or a program name found on PATH. Empty means
    /// the CLI's own name (`gh`, `glab`).
    std::string executable;
    Spawn spawn;
    FindOnPath find_on_path;
    std::chrono::milliseconds timeout = kCallTimeout;
};

class CliForgeClient : public ForgeClient {
protected:
    /// Who the CLI is, for its refusals.
    struct Cli {
        /// `gh`.
        std::string_view name;
        /// `the GitHub CLI`.
        std::string_view product;
        /// Where to get it.
        std::string_view install_url;
    };

    CliForgeClient(Cli cli, CliTransportOptions options);

    /// Runs `<cli> api [--hostname <host>] <arguments...>` with `input` on
    /// stdin, and hands back stdout -- or throws the rung of the ladder the
    /// failure is. `--hostname` only for a host that is not the forge's
    /// default, so a plain call stays plain.
    [[nodiscard]] std::string api(const MergeRequestRef& ref, std::vector<std::string> arguments,
                                  std::string input = {}) const;

    /// The forge's own words for "not logged in" and "not found".
    [[nodiscard]] virtual bool logged_out(const CliResult& result) const = 0;
    [[nodiscard]] virtual bool not_found(const CliResult& result) const = 0;

    /// `ref` is this transport's forge, or a `ForgeError` saying it is not.
    void require_forge(const MergeRequestRef& ref) const;

private:
    [[nodiscard]] std::string resolve() const;

    Cli cli_;
    CliTransportOptions options_;
};

/// Whether `haystack` holds `needle`, letters compared without case.
[[nodiscard]] bool contains_folded(std::string_view haystack, std::string_view needle);

}  // namespace apogee::forge
