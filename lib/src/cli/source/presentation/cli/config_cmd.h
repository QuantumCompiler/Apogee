#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "cli/command.h"
#include "contracts/config.h"

namespace apogee::commands {

/// Every dotted key `config get` answers for `config`: the fixed ones, and
/// each backend's, MCP server's and collection's fields by name -- what
/// completion offers for `config get <TAB>`. Beside `lookup`, which it must
/// agree with; a test asks `config get` for every key listed here.
[[nodiscard]] std::vector<std::string> config_keys(const harness::Config& config);

/// The default pointers as the config writes them -- what `config get
/// models.default` and `config get models.default_suite` print, unresolved --
/// for a view that shows them and marks what they name (32d). Empty when
/// unset.
struct WrittenDefaults {
    std::string backend;
    std::string suite;
};

[[nodiscard]] WrittenDefaults written_defaults(const harness::Config& config);

/// Points `models.<field>` at backend `name` in the config at `path`, through
/// the one editor -- what `config set-default` (and each `set-default-*`)
/// runs, and the full-screen shell's "make default" (32d). Refused, naming
/// what is configured, when no backend is called `name`. Returns what was
/// set: `models.default = local`.
[[nodiscard]] std::string point_role(const std::filesystem::path& path, std::string_view field,
                                     const std::string& name);

/// Points `models.default_video` at the pipeline `name` names -- a store
/// handle, id or model holding one pipeline, or a pipeline's path -- or
/// clears it for `off` (49c), through the one editor: what `config
/// set-default-video` runs. Refused, naming why, when `name` resolves to no
/// one pipeline. Returns what was set: `models.default_video = <name>`.
[[nodiscard]] std::string point_default_video(const std::filesystem::path& path,
                                              const std::string& name);

/// Removes backend `name`'s entry from the config at `path` through the one
/// editor -- `config delete-backend`, the shell's remove (32d) and `models
/// delete --and-backends` (M14) -- every role pointer that named it cleared
/// in the same edit (M33, `delete_backend_text`). Returns what was done:
/// `removed backend 'local' from <path>`, then a line per role cleared,
/// `models.default cleared: it named 'local'`.
[[nodiscard]] std::string remove_backend(const std::filesystem::path& path,
                                         const std::string& name);

/// `apogee config` -- inspect and edit the config file.
///
/// Every mutating subcommand routes through harness/config_edit.h rather than
/// writing YAML itself. That is the Core constraint the config engine exists
/// to establish: one mutation path, shared by every surface, so an edit made
/// over HTTP later is byte-identical to the same edit made here. A command
/// that formatted its own YAML would be the first crack in it.
class ConfigCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
