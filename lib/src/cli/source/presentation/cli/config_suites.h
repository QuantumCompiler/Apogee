#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cli/command.h"
#include "contracts/config.h"

namespace CLI {
class App;
}

/// The `config` verbs for suites (27d) -- `add-suite`, `set-suite`,
/// `delete-suite`, `set-default-suite` -- and the suite keys `config get`
/// answers. Every write goes through `contracts/config_edit.h`'s suite
/// transforms, the writer `add-backend` uses: this file formats no YAML.
namespace apogee::commands {

/// One `ROLE=VALUE` argument, as `--context-size utility=4096` and
/// `--toolset utility=fs,git` take it.
struct RoleArgument {
    std::string role;
    std::string value;
};

/// Parses `ROLE=VALUE`, the role one of `harness::suite_role_names()`.
/// Returns why it cannot -- no `=`, or not a role -- or empty, with `out` set.
[[nodiscard]] std::string parse_role_argument(std::string_view text, RoleArgument& out);

/// A `--toolset` value: comma-separated words of
/// `harness::suite_toolset_names()`, `""` for none. Returns why it cannot --
/// a word that names no toolset -- or empty, with `out` set.
[[nodiscard]] std::string parse_toolset(std::string_view text, std::vector<std::string>& out);

/// `config get`'s answer for a suite key -- `models.default_suite`,
/// `suites`, `suites.<name>`, `suites.<name>.description`,
/// `suites.<name>.<role>` -- or nullopt when `key` is not one.
[[nodiscard]] std::optional<std::string> suite_lookup(const harness::Config& config,
                                                      std::string_view key);

/// The active suite in one line -- its name and the backend each member
/// gives its role, `research -- chat root · utility helper` -- or that there
/// is none: what `/suite` says.
[[nodiscard]] std::string active_suite_summary(const harness::Config& config);

/// The suite keys `suite_lookup` answers for `config`, appended to `keys`.
void append_suite_keys(const harness::Config& config, std::vector<std::string>& keys);

/// Adds the suite verbs to `config`.
void bind_suite_verbs(CLI::App& parent, const RootContext& context);

/// Sets `models.default_suite` in the config at `path` to suite `name`, or
/// to none for `off`, through the one editor -- `config set-default-suite`,
/// and the full-screen shell's (32d). Refused, naming the suites there are,
/// when none is called `name`. Returns what was set.
[[nodiscard]] std::string point_default_suite(const std::filesystem::path& path,
                                              const std::string& name);

}  // namespace apogee::commands
