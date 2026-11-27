#pragma once

#include <string>
#include <vector>

namespace apogee::testing {

/// The doors a remedy names -- `<command> to <what it does>`, joined by
/// `, or ` (machine-mode.md) -- each cut where its ` to ` begins, both read
/// outside quotes, so a quoted word that holds either never splits a door
/// (CLI-M48). Each door is the command as printed.
[[nodiscard]] std::vector<std::string> remedy_doors(const std::string& remedy);

}  // namespace apogee::testing
