#include "cli/registry.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

#include "cli/agents_cmd.h"
#include "cli/analyze.h"
#include "cli/auth_cmd.h"
#include "cli/chat.h"
#include "cli/chat_history.h"
#include "cli/check.h"
#include "cli/complete.h"
#include "cli/complete_protocol.h"
#include "cli/config_cmd.h"
#include "cli/datasets.h"
#include "cli/embed.h"
#include "cli/graph.h"
#include "cli/knowledge.h"
#include "cli/mcp_cmd.h"
#include "cli/models.h"
#include "cli/reset.h"
#include "cli/serve_cmd.h"
#include "cli/train.h"
#include "cli/uninstall.h"
#include "cli/version_command.h"

namespace apogee::commands {

void CommandRegistry::add(std::unique_ptr<Command> command) {
    if (command == nullptr) {
        throw std::invalid_argument("CommandRegistry::add: null command");
    }
    if (find(command->name()) != nullptr) {
        throw std::invalid_argument("CommandRegistry::add: duplicate command name '" +
                                    std::string{command->name()} + "'");
    }
    commands_.push_back(std::move(command));
}

const Command* CommandRegistry::find(std::string_view name) const noexcept {
    const auto match =
        std::ranges::find_if(commands_, [name](const auto& c) { return c->name() == name; });
    return match == commands_.end() ? nullptr : match->get();
}

std::vector<std::string_view> CommandRegistry::names() const {
    std::vector<std::string_view> out;
    out.reserve(commands_.size());
    for (const auto& command : commands_) {
        out.push_back(command->name());
    }
    return out;
}

std::size_t CommandRegistry::size() const noexcept {
    return commands_.size();
}

bool CommandRegistry::empty() const noexcept {
    return commands_.empty();
}

void CommandRegistry::bind_all(CLI::App& root, const RootContext& context) {
    for (const auto& command : commands_) {
        command->bind(root, context);
    }
}

CommandRegistry default_registry() {
    CommandRegistry registry;
    registry.add(std::make_unique<AgentsCommand>());
    registry.add(std::make_unique<AnalyzeCommand>());
    registry.add(std::make_unique<AuthCommand>());
    registry.add(std::make_unique<ChatCommand>());
    registry.add(std::make_unique<CheckCommand>());
    registry.add(std::make_unique<ChatsCommand>());
    registry.add(std::make_unique<CompleteCommand>());
    registry.add(std::make_unique<ConfigCommand>());
    registry.add(std::make_unique<DatasetsCommand>());
    registry.add(std::make_unique<EmbedCommand>());
    registry.add(std::make_unique<GraphCommand>());
    registry.add(std::make_unique<KnowledgeCommand>());
    registry.add(std::make_unique<McpCommand>());
    registry.add(std::make_unique<ModelsCommand>());
    registry.add(std::make_unique<ResetCommand>());
    registry.add(std::make_unique<ServeCommand>());
    registry.add(std::make_unique<UninstallCommand>());
    registry.add(std::make_unique<VersionCommand>());
    // Hidden: the shell-completion protocol, not a user-facing command.
    registry.add(std::make_unique<CompleteProtocolCommand>());
    registry.add(std::make_unique<McpToolsServerCommand>());
    registry.add(std::make_unique<TrainCommand>());
    return registry;
}

}  // namespace apogee::commands
