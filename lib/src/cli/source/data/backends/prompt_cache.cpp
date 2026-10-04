#include "backends/prompt_cache.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

#include "contracts/config_edit.h"
#include "contracts/sha256.h"

namespace apogee::backends {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view kStateExtension = ".state";
constexpr std::string_view kModelRecord = "model.json";

[[nodiscard]] nlohmann::json to_json(const ModelFingerprint& model) {
    return {{"path", model.path}, {"size", model.size}, {"modified", model.modified}};
}

[[nodiscard]] std::optional<ModelFingerprint> fingerprint_from(const nlohmann::json& json) {
    if (!json.is_object() || !json.contains("path") || !json.contains("size") ||
        !json.contains("modified")) {
        return std::nullopt;
    }
    try {
        return ModelFingerprint{.path = json.at("path").get<std::string>(),
                                .size = json.at("size").get<std::uintmax_t>(),
                                .modified = json.at("modified").get<std::int64_t>()};
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<nlohmann::json> read_json(const fs::path& path) {
    const std::ifstream in{path, std::ios::binary};
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream text;
    text << in.rdbuf();
    nlohmann::json parsed = nlohmann::json::parse(text.str(), nullptr, false);
    if (parsed.is_discarded()) {
        return std::nullopt;
    }
    return parsed;
}

[[nodiscard]] bool write_private(const fs::path& path, const std::string& content) {
    try {
        harness::write_file_atomically(path, content, true);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

/// A directory only its owner can enter: a chat's state is its conversation.
void make_private_dir(const fs::path& path) {
    std::error_code code;
    fs::create_directories(path, code);
    fs::permissions(path, fs::perms::owner_all, fs::perm_options::replace, code);
}

[[nodiscard]] std::string hash_of(std::string_view bytes) {
    return models::sha256_hex(bytes).substr(0, 16);
}

}  // namespace

std::optional<ModelFingerprint> fingerprint_of(const fs::path& path) {
    std::error_code code;
    const fs::path absolute = fs::absolute(path, code);
    if (code) {
        return std::nullopt;
    }
    const std::uintmax_t size = fs::file_size(absolute, code);
    if (code) {
        return std::nullopt;
    }
    const fs::file_time_type modified = fs::last_write_time(absolute, code);
    if (code) {
        return std::nullopt;
    }
    return ModelFingerprint{
        .path = absolute.lexically_normal().string(),
        .size = size,
        .modified = static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(modified.time_since_epoch())
                .count())};
}

PromptCache::PromptCache(fs::path root, std::uint64_t cap_bytes)
    : root_{std::move(root)}, cap_bytes_{cap_bytes} {}

fs::path PromptCache::model_dir(const ModelFingerprint& fingerprint) const {
    return root_ / "models" / hash_of(fingerprint.path);
}

std::optional<std::string> PromptCache::claim_model(const ModelFingerprint& fingerprint,
                                                    std::string_view name) {
    const fs::path dir = model_dir(fingerprint);
    const fs::path record = dir / kModelRecord;
    std::optional<std::string> said;
    if (const std::optional<nlohmann::json> json = read_json(record); json.has_value()) {
        if (fingerprint_from(*json) == fingerprint) {
            return std::nullopt;
        }
        std::error_code code;
        fs::remove_all(dir, code);
        said = "the prompt cache for " + std::string{name} +
               " was made with a different model file; cleared";
    }
    make_private_dir(root_);
    make_private_dir(root_ / "models");
    make_private_dir(dir);
    (void)write_private(record, to_json(fingerprint).dump());
    return said;
}

fs::path PromptCache::prefix_path(const ModelFingerprint& fingerprint,
                                  harness::KvCacheType cache_type, std::int64_t window,
                                  const std::vector<std::int32_t>& tokens) const {
    // The tokens as text: a name that changes with any of them.
    std::string bytes;
    for (const std::int32_t token : tokens) {
        bytes += std::to_string(token);
        bytes += ',';
    }
    return model_dir(fingerprint) /
           ("prefix-" + std::string{harness::to_string(cache_type)} + "-" + std::to_string(window) +
            "-" + hash_of(bytes) + std::string{kStateExtension});
}

fs::path PromptCache::chat_path(std::string_view chat_id) const {
    const bool plain =
        !chat_id.empty() && chat_id != "." && chat_id != ".." &&
        std::ranges::all_of(chat_id, [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_';
        });
    if (!plain) {
        return {};
    }
    return root_ / "chats" / (std::string{chat_id} + std::string{kStateExtension});
}

std::optional<ChatCacheRecord> PromptCache::read_chat_record(std::string_view chat_id) const {
    const fs::path state = chat_path(chat_id);
    if (state.empty()) {
        return std::nullopt;
    }
    const std::optional<nlohmann::json> json =
        read_json(fs::path{state}.replace_extension(".json"));
    if (!json.has_value()) {
        return std::nullopt;
    }
    const std::optional<ModelFingerprint> model =
        fingerprint_from(json->value("model", nlohmann::json{}));
    const std::optional<harness::KvCacheType> cache_type =
        harness::cache_type_from_string(json->value("cache_type", std::string{}));
    if (!model.has_value() || !cache_type.has_value()) {
        return std::nullopt;
    }
    return ChatCacheRecord{.model = *model,
                           .cache_type = *cache_type,
                           .window = json->value("window", std::int64_t{0}),
                           .tokens = json->value("tokens", std::int64_t{0})};
}

bool PromptCache::write_chat_record(std::string_view chat_id, const ChatCacheRecord& record) const {
    const fs::path state = chat_path(chat_id);
    if (state.empty()) {
        return false;
    }
    const nlohmann::json json{{"model", to_json(record.model)},
                              {"cache_type", harness::to_string(record.cache_type)},
                              {"window", record.window},
                              {"tokens", record.tokens}};
    return write_private(fs::path{state}.replace_extension(".json"), json.dump());
}

void PromptCache::remove_chat(std::string_view chat_id) const {
    const fs::path state = chat_path(chat_id);
    if (state.empty()) {
        return;
    }
    std::error_code code;
    fs::remove(state, code);
    fs::remove(fs::path{state}.replace_extension(".json"), code);
}

bool PromptCache::settle(const fs::path& written, const fs::path& path) {
    std::error_code code;
    fs::permissions(written, fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace, code);
    if (code) {
        fs::remove(written, code);
        return false;
    }
    if (written != path) {
        fs::rename(written, path, code);
        if (code) {
            fs::remove(written, code);
            return false;
        }
    }
    return true;
}

void PromptCache::touch(const fs::path& path) {
    std::error_code code;
    fs::last_write_time(path, fs::file_time_type::clock::now(), code);
}

std::size_t PromptCache::evict(const fs::path& keep) const {
    struct Held {
        fs::path path;
        fs::file_time_type used;
        std::uint64_t bytes = 0;
    };

    std::vector<Held> held;
    std::uint64_t total = 0;
    std::error_code code;
    for (fs::recursive_directory_iterator it{root_, code}, end; !code && it != end;
         it.increment(code)) {
        if (!it->is_regular_file(code) || it->path().extension() != kStateExtension) {
            continue;
        }
        const std::uint64_t bytes = it->file_size(code);
        total += bytes;
        held.push_back({.path = it->path(), .used = it->last_write_time(code), .bytes = bytes});
    }
    std::ranges::sort(held, [](const Held& a, const Held& b) { return a.used < b.used; });
    std::size_t removed = 0;
    for (const Held& file : held) {
        if (total <= cap_bytes_) {
            break;
        }
        if (!keep.empty() && file.path == keep) {
            continue;
        }
        if (fs::remove(file.path, code)) {
            total -= file.bytes;
            ++removed;
            // A chat's record goes with its state.
            if (file.path.parent_path().filename() == "chats") {
                fs::remove(fs::path{file.path}.replace_extension(".json"), code);
            }
        }
    }
    return removed;
}

PromptCacheUsage PromptCache::usage() const {
    PromptCacheUsage usage;
    std::error_code code;
    for (fs::recursive_directory_iterator it{root_, code}, end; !code && it != end;
         it.increment(code)) {
        if (it->is_regular_file(code) && it->path().extension() == kStateExtension) {
            ++usage.files;
            usage.bytes += it->file_size(code);
        }
    }
    return usage;
}

}  // namespace apogee::backends
