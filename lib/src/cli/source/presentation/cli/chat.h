#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "agentloop/content.h"
#include "cli/chat_session.h"
#include "cli/command.h"
#include "cli/suite_residency.h"
#include "harness/harness.h"
#include "logger/session.h"

/// `apogee chat` — the interactive multi-turn surface: the session core
/// (`cli/chat_session.h`) as chat, a thin face over it since 27s, when
/// `apogee execute` became its second consumer.
namespace apogee::commands {

/// `apogee chat` — a persistent, resumable conversation.
class ChatCommand final : public Command {
public:
    /// `machine` is where a suite's admission reads the machine's budget
    /// (27e): the devices llama.cpp reports, or a test's fixed number.
    explicit ChatCommand(MachineBudgetSource machine = machine_budget);

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;

private:
    MachineBudgetSource machine_;
};

}  // namespace apogee::commands
