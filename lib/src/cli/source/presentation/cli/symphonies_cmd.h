#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "cli/command.h"
#include "symphony/definition.h"

/// `apogee symphonies list|show|create|edit|delete|play` (27q) -- named,
/// staged prompt processes over a suite's members, the agents lifecycle for
/// a new kind of definition.
///
/// `list` and `show` read every source -- the shipped starters, the config's
/// `symphonies:` entries, the spec files under `symphonies/` -- and print the
/// human view or, with `--output-format json`, the one document the admin
/// plane serves for the same read. `create` and `edit` go through the shared
/// scaffold core (`scaffold/symphony.h`), the function `POST`/`PUT
/// /v1/admin/symphonies` call too, so an entry written here is byte-identical
/// to one written over HTTP; `delete` is the editor's inverse. `play` runs a
/// definition once -- its input from `--input` or stdin, its image from
/// `--image` -- under the active suite (`--suite` for this run), each stage
/// said as it runs, and prints the output: CLI-only, the training precedent.
namespace apogee::commands {

/// `list`'s table, one line each -- the header, then a row per definition
/// `catalog` holds: its name, its role chain, its source and its description,
/// or that it cannot be played. What an execute session's `/symphonies` says
/// too (27s), so the two read alike.
[[nodiscard]] std::vector<std::string> symphony_list_lines(const symphony::Catalog& catalog);

/// The symphonies `symphonies list` reads (37d): every source, the catalog
/// its table, its document and the shell's Symphonies view are drawn from.
/// Throws `harness::ConfigError` for a config that will not load.
[[nodiscard]] symphony::Catalog read_symphony_catalog(const RootContext& context);

/// `symphonies show <name>` as the command prints it (37d): the Symphonies
/// view's card. Throws std::runtime_error in the command's words for a name
/// that finds nothing or a config that will not load.
[[nodiscard]] std::string symphony_show_text(const RootContext& context, const std::string& name);

class SymphoniesCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
