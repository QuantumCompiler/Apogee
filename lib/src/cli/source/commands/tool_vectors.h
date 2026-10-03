#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "agentloop/tool_selection.h"

/// The tool-vector cache (26g): each tool definition's vector, under the key
/// `agentloop::ToolEmbedding` names it with -- the embedding model and the
/// definition's hash -- in one file under `cache/`.
///
/// Safe to delete, as that layout row promises: a missing or unreadable file
/// is an empty cache, and the next ranking embeds the tools again. A changed
/// definition is a new key, so its old vector is never read.
namespace apogee::commands {

/// `<data directory>/cache/tool-vectors.json`, the data directory being the
/// one `config_path` lives in -- so a test's temporary config means a
/// temporary cache.
[[nodiscard]] std::filesystem::path tool_vector_cache_path(
    const std::filesystem::path& config_path);

class ToolVectorCache {
public:
    explicit ToolVectorCache(std::filesystem::path file);

    /// The vector cached under `key`, read from the file on first use.
    [[nodiscard]] std::optional<std::vector<float>> load(const std::string& key);

    /// Adds `made` to what the file holds now -- another process may have
    /// written it since -- and writes it whole, atomically. Never throws: a
    /// cache that cannot be written is one that embeds again next time.
    void store(const agentloop::ToolVectors& made);

private:
    void read();

    std::filesystem::path file_;
    bool read_ = false;
    agentloop::ToolVectors vectors_;
};

}  // namespace apogee::commands
