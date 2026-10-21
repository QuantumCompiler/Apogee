#include "backends/adapters.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <fstream>
#include <optional>
#include <sstream>
#include <system_error>
#include <utility>

#include "contracts/config_edit.h"
#include "contracts/sha256.h"
#include "modelstore/sidecar.h"

namespace apogee::backends {
namespace {

namespace fs = std::filesystem;
using nlohmann::json;

/// What a file is, for the memo: the prompt cache's identity (26j) -- not a
/// hash of its contents, which is what it saves reading.
struct Identity {
    std::uintmax_t size = 0;
    std::int64_t modified = 0;

    bool operator==(const Identity&) const = default;
};

[[nodiscard]] std::optional<Identity> identity_of(const fs::path& path) {
    std::error_code code;
    const std::uintmax_t size = fs::file_size(path, code);
    if (code) {
        return std::nullopt;
    }
    const fs::file_time_type modified = fs::last_write_time(path, code);
    if (code) {
        return std::nullopt;
    }
    return Identity{
        .size = size,
        .modified = static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(modified.time_since_epoch())
                .count())};
}

/// The memo's hashes, each file's under its normalized absolute path: one
/// read and at most one write a check, however many files it hashes.
class Memo {
public:
    explicit Memo(const WeightHashing& hashing) : hashing_{hashing} {
        if (hashing_.memo.empty()) {
            return;
        }
        const std::ifstream in{hashing_.memo, std::ios::binary};
        if (!in) {
            return;
        }
        std::ostringstream text;
        text << in.rdbuf();
        const json parsed = json::parse(text.str(), nullptr, false);
        if (parsed.is_object()) {
            files_ = parsed.value("files", json::object());
        }
        if (!files_.is_object()) {
            files_ = json::object();
        }
    }

    /// Writes what was read since the memo was opened, when anything was --
    /// and when asked to remember it.
    void save() {
        if (!changed_ || hashing_.memo.empty() || !hashing_.remember) {
            return;
        }
        try {
            // A file gone is a line nobody will ask about again.
            json kept = json::object();
            std::error_code code;
            for (const auto& [path, entry] : files_.items()) {
                if (fs::exists(path, code)) {
                    kept[path] = entry;
                }
            }
            fs::create_directories(hashing_.memo.parent_path(), code);
            harness::write_file_atomically(hashing_.memo,
                                           json{{"version", 1}, {"files", kept}}.dump(2) + "\n");
        } catch (const std::exception&) {
            // Not remembered is read again at the next check: never a failure.
        }
    }

    /// `file`'s sha256, from the memo while its identity holds; read otherwise.
    [[nodiscard]] std::string sha256_of(const fs::path& file) {
        std::error_code code;
        const std::string key = fs::absolute(file, code).lexically_normal().string();
        const std::optional<Identity> before = identity_of(file);
        if (!before.has_value()) {
            return {};
        }
        if (const auto held = files_.find(key); held != files_.end() && held->is_object()) {
            try {
                const Identity remembered{.size = held->value("size", std::uintmax_t{0}),
                                          .modified = held->value("modified", std::int64_t{0})};
                std::string sha256 = held->value("sha256", std::string{});
                if (remembered == *before && !sha256.empty()) {
                    return sha256;
                }
            } catch (const json::exception&) {
                // A line that is not one of ours is a line not remembered.
            }
        }
        const std::string sha256 =
            hashing_.hasher ? hashing_.hasher(file) : models::file_sha256(file);
        // Remembered only when the file held still while it was read: a hash
        // of bytes that moved underneath is never kept under either identity.
        if (!sha256.empty() && identity_of(file) == before) {
            files_[key] = {
                {"size", before->size}, {"modified", before->modified}, {"sha256", sha256}};
            changed_ = true;
        }
        return sha256;
    }

private:
    const WeightHashing& hashing_;
    json files_ = json::object();
    bool changed_ = false;
};

/// The command that writes an entry's adapters.
[[nodiscard]] std::string retake(const AdapterCheck& check) {
    return "apogee config set-backend " + check.backend + " --adapters";
}

/// Why `adapter` is not the kind of file its runtime reads, or empty: its
/// file facts alone, whatever its pin.
[[nodiscard]] std::string file_refusal(const harness::AdapterConfig& adapter,
                                       const AdapterCheck& check) {
    switch (adapter_files(adapter, check)) {
        case AdapterFiles::Missing:
            return "the adapter " + adapter.path +
                   " is not there -- restore it, or take the adapters off with " + retake(check) +
                   " \"\"";
        case AdapterFiles::WrongKind:
            if (check.directories) {
                return "the adapter " + adapter.path +
                       " is not an mlx-lm adapter directory (it holds no adapter_config.json) -- "
                       "name the directory its training wrote, with " +
                       retake(check) + " <directory>";
            }
            return "the adapter " + adapter.path +
                   " is not a file -- a llamacpp entry applies a GGUF LoRA file, named with " +
                   retake(check) + " <file.gguf>";
        case AdapterFiles::Pinned:
        case AdapterFiles::Unpinned:
            break;
    }
    return {};
}

/// The pin's short form, for a sentence: the store's id length.
[[nodiscard]] std::string short_hash(const std::string& sha256) {
    return sha256.substr(0, 12);
}

/// `weights_sha256` over `memo`, which the caller saves.
[[nodiscard]] std::string hash_weights(const fs::path& weights, Memo& memo) {
    std::error_code code;
    if (!fs::is_directory(weights, code)) {
        return fs::is_regular_file(weights, code) ? memo.sha256_of(weights) : std::string{};
    }
    // An MLX model: every shard by its place in the directory, as the store
    // digests a snapshot's.
    std::vector<std::pair<std::string, fs::path>> shards;
    for (fs::recursive_directory_iterator it{weights, code}, end; !code && it != end;
         it.increment(code)) {
        if (it->is_regular_file(code) && it->path().extension() == ".safetensors") {
            shards.emplace_back(it->path().lexically_relative(weights).generic_string(),
                                it->path());
        }
    }
    if (code || shards.empty()) {
        return {};
    }
    std::ranges::sort(shards, {}, &std::pair<std::string, fs::path>::first);
    models::Sha256 digest;
    for (const auto& [relative, file] : shards) {
        const std::string sha256 = memo.sha256_of(file);
        if (sha256.empty()) {
            return {};
        }
        digest.update(relative + "\t" + sha256 + "\n");
    }
    return digest.hex_digest();
}

}  // namespace

AdapterFiles adapter_files(const harness::AdapterConfig& adapter, const AdapterCheck& check) {
    const fs::path path{adapter.path};
    std::error_code code;
    if (!fs::exists(path, code)) {
        return AdapterFiles::Missing;
    }
    const bool kind = check.directories
                          ? fs::is_directory(path, code) &&
                                fs::is_regular_file(path / "adapter_config.json", code)
                          : fs::is_regular_file(path, code);
    if (!kind) {
        return AdapterFiles::WrongKind;
    }
    return adapter.base_sha256.empty() ? AdapterFiles::Unpinned : AdapterFiles::Pinned;
}

std::string spell_adapters(const std::vector<harness::AdapterConfig>& adapters) {
    std::string out;
    for (const harness::AdapterConfig& adapter : adapters) {
        std::string item = adapter.path;
        if (adapter.scale != 1.0) {
            std::ostringstream scale;
            scale << adapter.scale;
            item += ":" + scale.str();
        }
        out += (out.empty() ? "" : ",") + item;
    }
    if (out.empty()) {
        return "\"\"";
    }
    return out.find_first_of(" '\"") == std::string::npos ? out : "\"" + out + "\"";
}

std::string weights_sha256(const fs::path& weights, const WeightHashing& hashing) {
    Memo memo{hashing};
    std::string sha256 = hash_weights(weights, memo);
    // What was read is kept even when a later shard could not be.
    memo.save();
    return sha256;
}

std::string adapter_refusal(const std::vector<harness::AdapterConfig>& adapters,
                            const AdapterCheck& check) {
    if (adapters.empty()) {
        return {};
    }
    for (const harness::AdapterConfig& adapter : adapters) {
        if (std::string refused = file_refusal(adapter, check); !refused.empty()) {
            return refused;
        }
        if (adapter.base_sha256.empty()) {
            return "the adapter " + adapter.path + " is pinned to no base -- " + retake(check) +
                   " " + spell_adapters(adapters) + " records the base each applies over";
        }
    }
    const std::string base = weights_sha256(check.base, check.hashing);
    if (base.empty()) {
        return "the base weights at " + check.base.string() +
               " could not be read to check the adapters pinned to them -- model_path names "
               "the weights they apply over";
    }
    for (const harness::AdapterConfig& adapter : adapters) {
        if (adapter.base_sha256 != base) {
            return "the adapter " + adapter.path + " was pinned to other base weights (sha256 " +
                   short_hash(adapter.base_sha256) + ") than " + check.base.string() +
                   " holds now (" + short_hash(base) +
                   ") -- an adapter applies only over the weights it was trained on: point "
                   "model_path back at them, or, if these are the ones it was made for, pin it "
                   "again with " +
                   retake(check) + " " + spell_adapters(adapters);
        }
    }
    return {};
}

std::string pin_adapters(std::vector<harness::AdapterConfig>& adapters, const AdapterCheck& check) {
    for (harness::AdapterConfig& adapter : adapters) {
        std::error_code code;
        fs::path absolute = fs::absolute(fs::path{harness::expand_env_and_home(adapter.path)}, code)
                                .lexically_normal();
        if (!absolute.has_filename() && absolute.has_relative_path()) {
            absolute = absolute.parent_path();  // a directory named with its trailing slash
        }
        adapter.path = absolute.string();
        if (std::string refused = file_refusal(adapter, check); !refused.empty()) {
            return refused;
        }
    }
    const std::string base = weights_sha256(check.base, check.hashing);
    if (base.empty()) {
        return "the base weights at " + check.base.string() +
               " could not be read to pin the adapters to -- set model_path to the weights they "
               "apply over first";
    }
    for (harness::AdapterConfig& adapter : adapters) {
        adapter.base_sha256 = base;
    }
    return {};
}

}  // namespace apogee::backends
