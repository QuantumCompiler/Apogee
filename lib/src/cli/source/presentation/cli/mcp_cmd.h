#pragma once

#include <nlohmann/json_fwd.hpp>

#include <string_view>

#include "cli/command.h"

/// `apogee mcp` -- managing the stdio MCP servers the loop connects to.
///
/// `create` scaffolds a runnable Python server (or registers an existing
/// executable) through the shared scaffold core, the same path the admin
/// route takes; `list` shows every configured server's live connection
/// state; `test` invokes one tool through the production client; `enable`
/// and `disable` flip one line of the config. Servers connect at startup, so
/// every change says "restart to take effect".
///
/// `serve` is the other side (54a, `cli/mcp_serve`): Apogee's own read-only
/// tools and status reads over this process's own stdin/stdout, so any MCP
/// host -- or Apogee's own test fleet -- can use them with nothing to
/// install. The hidden `__mcp-tools` is its alias, kept for the hosts
/// registered with it before the verb was public.
namespace apogee::commands {

/// `mcp list --output-format json` (28h): every configured server connected
/// to -- each bounded, its stderr kept off the terminal -- as the control
/// plane serves its entry, with what connecting found; the MCP view's rows
/// (37g). Throws `harness::ConfigError` for a config that will not load.
[[nodiscard]] nlohmann::json mcp_list_document(const RootContext& context);

class McpCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

/// `apogee __mcp-tools`: the hidden alias of `apogee mcp serve` (54a), the
/// same server behind the spelling hosts were registered with before.
class McpToolsServerCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
