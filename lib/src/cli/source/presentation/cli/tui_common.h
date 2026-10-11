#pragma once

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <string>
#include <vector>

#include "cli/command.h"

/// What every shell view composed in `cli/` shares (32d, 37b): where the
/// config is, a document's field as a cell, a command's text as lines.
namespace apogee::commands {

/// The config file `context` names, resolved as every command resolves it.
[[nodiscard]] std::filesystem::path config_file(const RootContext& context);

/// `object[key]` as a cell: a string as itself, anything else as JSON,
/// absent or null as empty.
[[nodiscard]] std::string field(const nlohmann::json& object, const char* key);

/// `text` as its lines, the last newline ending the last.
[[nodiscard]] std::vector<std::string> lines_of(const std::string& text);

}  // namespace apogee::commands
