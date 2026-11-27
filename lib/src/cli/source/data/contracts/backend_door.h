#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/suggested_command.h"

/// The door that edits a configured backend (CLI-M50). A backend's name is
/// a key, and since 55f a new one must be one both config formats edit
/// (`yaml_plain_key`); a key outside that set -- written by hand, or kept
/// from a config older than 2026-10-25 -- still loads, but every editor
/// refuses it (57e) and only `config rename-backend` reaches it. So a door
/// naming `config set-backend` or `config delete-backend` on such a name
/// leads with the rename that reaches it, then names the same door on the
/// new name; on any other name it is the door alone, byte for byte.
///
/// One choice, by name, in one place: no door site asks the rule, and the
/// choice reads neither the config nor its format -- one rule in both, as
/// 55f and 57e say it. Every command goes through the one renderer
/// (CLI-M48), so the rename takes its `--` before a dash-led name and its
/// quotes around `a b`, and `<new name>` prints verbatim.
namespace apogee::harness {

/// The placeholder a rename door names the new name with.
inline constexpr std::string_view kNewBackendName = "<new name>";

/// The commands that edit configured backend `name` -- the key as the
/// config stores it, which `rename-backend` matches exactly -- with `door`,
/// a `config set-backend` or `config delete-backend` naming the part it is
/// given, in order: `door(operand(name))` alone when `name` is one both
/// config formats edit; else `config rename-backend <name> <new name>`,
/// then `door(<new name>)`, the new name a placeholder part.
[[nodiscard]] std::vector<SuggestedCommand> backend_edit_door(
    std::string_view name, const std::function<SuggestedCommand(CommandPart)>& door);

/// `config set-backend` on configured backend `name` with `options` after
/// the name, by `backend_edit_door`: the door every site naming that editor
/// builds.
[[nodiscard]] std::vector<SuggestedCommand> set_backend_door(
    std::string_view name, const std::vector<CommandPart>& options);

/// `config delete-backend` on configured backend `name`, the same way.
[[nodiscard]] std::vector<SuggestedCommand> delete_backend_door(std::string_view name);

/// Whether `commands` lead with the rename: a site that wraps one command
/// in its own words (`with '<door>'`) says the two its own way.
[[nodiscard]] bool renames_first(const std::vector<SuggestedCommand>& commands);

/// As one door of a `remedy` field (machine-mode.md): the commands joined
/// by ` && `, each running on the state the one before it leaves.
[[nodiscard]] std::string backend_door_remedy(const std::vector<SuggestedCommand>& commands);

/// As a sentence says it: the command alone, or
/// `rename it first: <rename>, then <door>`.
[[nodiscard]] std::string backend_door_said(const std::vector<SuggestedCommand>& commands);

/// As a sentence that sets one command apart says it after `purpose`
/// ("remove the backend itself"): `<purpose> with '<command>'`, `quote`
/// around the command as the site writes it; or, leading with the rename,
/// `to <purpose>, rename it first: <rename>, then <door>`, which delimits
/// the two itself.
[[nodiscard]] std::string backend_door_with(std::string_view purpose,
                                            const std::vector<SuggestedCommand>& commands,
                                            std::string_view quote = "'");

}  // namespace apogee::harness
