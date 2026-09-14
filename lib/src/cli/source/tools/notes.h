#pragma once

#include <filesystem>
#include <string_view>

#include "agent/tool.h"

/// The notes toolset: a scratchpad the model keeps for the user, one text file
/// per key under the `notes/` layout directory.
///
/// Ported from Ommi's `ommi-mcp-notes`. Keys are `[A-Za-z0-9_-]{1,128}` so a
/// key can never be a path; `write_note` and `delete_note` declare `writes`.
namespace apogee::tools {

[[nodiscard]] bool valid_note_key(std::string_view key) noexcept;

void register_notes_tools(agent::ToolRegistry& registry, const std::filesystem::path& notes_dir);

}  // namespace apogee::tools
