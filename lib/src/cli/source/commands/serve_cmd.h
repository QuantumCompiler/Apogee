#pragma once

#include <string_view>

#include "commands/command.h"

/// `apogee serve` -- the OpenAI-compatible HTTP server.
///
/// **Server deployments only** (decided 2026-08-24): the executable runs on a
/// server and remote clients -- mobile or desktop apps -- make REST calls to
/// it. A local front-end never talks to this port; it drives the CLI over
/// stdin/stdout instead (see `documentation/reference/machine-mode.md`). That
/// is why a non-loopback bind is this command's *normal* production posture,
/// and why it still fails closed behind an explicit flag.
namespace apogee::commands {

class ServeCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
