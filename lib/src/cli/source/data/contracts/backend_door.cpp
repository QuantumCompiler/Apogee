#include "contracts/backend_door.h"

#include <utility>

#include "contracts/config_yaml.h"

namespace apogee::harness {

std::vector<SuggestedCommand> backend_edit_door(
    std::string_view name, const std::function<SuggestedCommand(CommandPart)>& door) {
    if (yaml_plain_key(name)) {
        return {door(operand(name))};
    }
    return {SuggestedCommand{.verbs = {"config", "rename-backend"},
                             .parts = {operand(name), placeholder(kNewBackendName)}},
            door(placeholder(kNewBackendName))};
}

std::vector<SuggestedCommand> set_backend_door(std::string_view name,
                                               const std::vector<CommandPart>& options) {
    return backend_edit_door(name, [&options](CommandPart target) {
        SuggestedCommand command{.verbs = {"config", "set-backend"}, .parts = {std::move(target)}};
        command.parts.insert(command.parts.end(), options.begin(), options.end());
        return command;
    });
}

std::vector<SuggestedCommand> delete_backend_door(std::string_view name) {
    return backend_edit_door(name, [](CommandPart target) {
        return SuggestedCommand{.verbs = {"config", "delete-backend"},
                                .parts = {std::move(target)}};
    });
}

bool renames_first(const std::vector<SuggestedCommand>& commands) {
    return commands.size() > 1;
}

std::string backend_door_remedy(const std::vector<SuggestedCommand>& commands) {
    return render_all(commands, " && ");
}

std::string backend_door_said(const std::vector<SuggestedCommand>& commands) {
    if (!renames_first(commands)) {
        return commands.empty() ? std::string{} : render(commands.front());
    }
    return "rename it first: " + render(commands.front()) + ", then " + render(commands.back());
}

std::string backend_door_with(std::string_view purpose,
                              const std::vector<SuggestedCommand>& commands,
                              std::string_view quote) {
    if (renames_first(commands)) {
        return "to " + std::string{purpose} + ", " + backend_door_said(commands);
    }
    return std::string{purpose} + " with " + std::string{quote} + backend_door_said(commands) +
           std::string{quote};
}

}  // namespace apogee::harness
