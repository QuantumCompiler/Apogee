#pragma once

#include <string_view>

#include "commands/command.h"

/// `apogee mcp` -- managing the stdio MCP servers the loop connects to.
///
/// `create` scaffolds a runnable Python server (or registers an existing
/// executable) through the shared scaffold core, the same path the admin
/// route takes; `list` shows every configured server's live connection
/// state; `test` invokes one tool through the production client; `enable`
/// and `disable` flip one line of the config. Servers connect at startup, so
/// every change says "restart to take effect".
///
/// The hidden `__mcp-tools` subcommand is the other side: it serves the
/// read-only native toolsets over this process's own stdin/stdout, so any
/// MCP client -- or Apogee's own test fleet -- can use them with nothing to
/// install.
namespace apogee::commands {

class McpCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

/// `apogee __mcp-tools`: an MCP server over stdio exposing the read-only
/// native toolsets.
class McpToolsServerCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
