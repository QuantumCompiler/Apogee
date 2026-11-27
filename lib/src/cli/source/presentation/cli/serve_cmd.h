#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "agent/tool.h"
#include "cli/command.h"
#include "operations/retrieval.h"

/// `apogee serve` -- the OpenAI-compatible HTTP server.
///
/// **Server deployments only** (decided 2026-08-24): the executable runs on a
/// server and remote clients -- mobile or desktop apps -- make REST calls to
/// it. A local front-end never talks to this port; it drives the CLI over
/// stdin/stdout instead (see `documentation/reference/cli/machine-mode.md`). That
/// is why a non-loopback bind is this command's *normal* production posture,
/// and why it still fails closed behind an explicit flag.
namespace apogee::commands {

/// What `serve`'s startup line is made from (57n).
struct ServeSummaryInput {
    /// The backends a request may name, and the one it lands on by default.
    std::vector<std::string> served{};
    std::string default_backend{};
    /// The registry `--tools` built -- chat's, whole -- or null without it.
    const agent::ToolRegistry* tools = nullptr;
    /// The checker served requests are gated by: a gated tool it allows is
    /// one that runs unasked.
    agent::PermissionChecker permission{};
    /// Where the file tools are sandboxed (`tools.fs_root`, else the launch
    /// folder).
    std::filesystem::path fs_root{};
    RagChoice rag{};
    int session_ttl_minutes = 60;
};

/// The line `serve` says on startup: what it serves, then -- built from the
/// registry, never a literal -- how many tools it offers and which (`files
/// under <root>`, each native set, the website tools, the rest by name, `MCP:
/// <servers>`), every gated tool the checker lets run unasked (or `none`),
/// retrieval, and how long a session may idle. An operator who trusts it
/// knows what any client of the port can have the model do.
[[nodiscard]] std::string serve_summary(const ServeSummaryInput& input);

class ServeCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
