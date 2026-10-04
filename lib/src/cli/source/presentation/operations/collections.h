#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

/// Where the collections live, by name (A4: moved out of `cli/embed` -- the
/// command line and the admin plane open them the same way).
namespace apogee::commands {

/// Where a collection's database lives: `~/.apogee/embeddings/<name>.db`.
///
/// One file per collection rather than one shared database. A collection is the
/// unit a user creates, searches and throws away, and `rm` on a single file is
/// a deletion story that cannot half-succeed.
[[nodiscard]] std::filesystem::path collection_path(std::string_view name);

/// Every collection currently on disk, sorted.
[[nodiscard]] std::vector<std::string> collection_names();

}  // namespace apogee::commands
