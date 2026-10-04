#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "commands/command.h"

/// `apogee embed` — ingest documents and search them.
///
/// **The whole surface works with no model, no API key, and no network.** That
/// is the point of building the lexical floor first: retrieval is available to
/// someone who has installed Apogee and nothing else. Ommi's recorded history
/// is the argument — its RAG survived an embedding-model freeze only because
/// the lexical path never needed one.
namespace apogee::commands {

/// Where a collection's database lives: `~/.apogee/embeddings/<name>.db`.
///
/// One file per collection rather than one shared database. A collection is the
/// unit a user creates, searches and throws away, and `rm` on a single file is
/// a deletion story that cannot half-succeed.
[[nodiscard]] std::filesystem::path collection_path(std::string_view name);

/// Every collection currently on disk, sorted.
[[nodiscard]] std::vector<std::string> collection_names();

class EmbedCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
