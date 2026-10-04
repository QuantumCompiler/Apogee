#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/config.h"

/// A local model's attention cache, kept on disk between processes (26j).
///
/// Two kinds of file, both under `cache/prompt/`:
/// - **the prefix cache**, per model: the state after a conversation's system
///   prompt and tool definitions, named by a hash of those tokens, restored
///   into every new conversation that opens the same way;
/// - **the chat cache**, per chat: the state a chat had reached, saved at a
///   clean exit and after compaction, restored when it is resumed.
///
/// **A cache that cannot be used is discarded, never trusted**: one written
/// for another model file, or one llama.cpp refuses, is removed and the
/// prompt read as before -- the caller says so in one line. Every file is
/// private (`0600`), since a chat's state holds its conversation, and the
/// whole directory is held to a size cap, the least recently used evicted
/// first.
namespace apogee::backends {

/// The most the prompt cache keeps on disk (the user's call, 2026-10-03).
inline constexpr std::uint64_t kPromptCacheCapBytes = 4ULL * 1024 * 1024 * 1024;

/// The shortest system prompt and tool definitions worth a prefix file: a
/// shorter one reads in well under a second, and its file would be churn.
inline constexpr std::size_t kMinPrefixCacheTokens = 512;

/// The shortest chat worth a chat file (the user's call, 2026-10-03): a
/// shorter one re-reads in a second or two.
inline constexpr std::size_t kMinChatCacheTokens = 2000;

/// Which model file a cache was made with: its path, size and modification
/// time. Not a hash of its contents -- reading gigabytes at every load would
/// cost more than the cache saves -- but replacing the file changes it.
struct ModelFingerprint {
    std::string path;
    std::uintmax_t size = 0;
    std::int64_t modified = 0;

    friend bool operator==(const ModelFingerprint&, const ModelFingerprint&) = default;
};

/// `path`'s fingerprint, or nullopt when it cannot be read.
[[nodiscard]] std::optional<ModelFingerprint> fingerprint_of(const std::filesystem::path& path);

/// What a chat file was made with, kept beside it.
struct ChatCacheRecord {
    ModelFingerprint model;
    harness::KvCacheType cache_type = harness::KvCacheType::Q8_0;
    std::int64_t window = 0;
    std::int64_t tokens = 0;
};

/// The directory's files and what they take, for `check`.
struct PromptCacheUsage {
    std::size_t files = 0;
    std::uint64_t bytes = 0;
};

class PromptCache {
public:
    PromptCache(std::filesystem::path root, std::uint64_t cap_bytes = kPromptCacheCapBytes);

    [[nodiscard]] const std::filesystem::path& root() const noexcept {
        return root_;
    }

    /// Claims the prefix directory of the model `fingerprint` names. A
    /// directory made for another file of that path is removed whole, and a
    /// line returned saying so; a missing one is created.
    [[nodiscard]] std::optional<std::string> claim_model(const ModelFingerprint& fingerprint,
                                                         std::string_view name);

    /// The prefix file for `tokens` on that model, its cache type and window.
    [[nodiscard]] std::filesystem::path prefix_path(const ModelFingerprint& fingerprint,
                                                    harness::KvCacheType cache_type,
                                                    std::int64_t window,
                                                    const std::vector<std::int32_t>& tokens) const;

    /// The chat file for `chat_id`, or an empty path for an id that cannot
    /// name a file (a separator, a dot-dot).
    [[nodiscard]] std::filesystem::path chat_path(std::string_view chat_id) const;

    [[nodiscard]] std::optional<ChatCacheRecord> read_chat_record(std::string_view chat_id) const;
    [[nodiscard]] bool write_chat_record(std::string_view chat_id,
                                         const ChatCacheRecord& record) const;
    /// Removes a chat's file and its record.
    void remove_chat(std::string_view chat_id) const;

    /// A state file just written: made private, and moved into place from
    /// `written` (its temporary name) when that differs.
    [[nodiscard]] static bool settle(const std::filesystem::path& written,
                                     const std::filesystem::path& path);

    /// Marks `path` as just used, for the eviction order.
    static void touch(const std::filesystem::path& path);

    /// Removes the least recently used state files until the directory is
    /// within its cap, never `keep`. Returns how many were removed.
    [[nodiscard]] std::size_t evict(const std::filesystem::path& keep = {}) const;

    [[nodiscard]] PromptCacheUsage usage() const;

private:
    [[nodiscard]] std::filesystem::path model_dir(const ModelFingerprint& fingerprint) const;

    std::filesystem::path root_;
    std::uint64_t cap_bytes_;
};

}  // namespace apogee::backends
