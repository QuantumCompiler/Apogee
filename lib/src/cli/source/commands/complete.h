#pragma once

#include <string_view>

#include "commands/command.h"

namespace apogee::commands {

/// `apogee complete <prompt>` — one-shot prompt in, answer out.
///
/// The walking skeleton: config → harness → backend → terminal, the shortest
/// path that proves the architecture end to end. It is deliberately the first
/// user-visible command for that reason — a falsified design bet is cheapest to
/// fix here, before the agent loop and the chat REPL are built on top of it.
///
/// Scriptable by construction: on a pipe it emits exactly the answer, so
/// `apogee complete "…" | jq` and friends work. Tool use is not here — it
/// arrives with the agent loop as `complete --tools`.
class CompleteCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
