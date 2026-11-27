#pragma once

#include <optional>
#include <string>
#include <vector>

namespace apogee::testing {

/// The doors a remedy names -- `<command> to <what it does>`, joined by
/// `, or ` (machine-mode.md) -- each cut where its ` to ` begins, both read
/// outside quotes, so a quoted word that holds either never splits a door
/// (CLI-M48). Each door is the command as printed.
[[nodiscard]] std::vector<std::string> remedy_doors(const std::string& remedy);

/// The commands one door chains with ` && ` (CLI-M50: a backend the
/// editors refuse is renamed first), read outside quotes, in order. A door
/// that chains nothing is its one command.
[[nodiscard]] std::vector<std::string> door_steps(const std::string& door);

/// The words `/bin/sh` reads from `command` pasted -- the argv it would
/// run, through `printf '%s\0'`, nothing executed -- with the environment
/// cleared; nullopt where this host has no `/bin/sh` or runs no child.
[[nodiscard]] std::optional<std::vector<std::string>> sh_words(const std::string& command);

}  // namespace apogee::testing
