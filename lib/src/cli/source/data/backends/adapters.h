#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "contracts/config.h"

/// An entry's adapters against its base (53a): the file facts a load is
/// refused on before it starts, and the hash an adapter is pinned to its base
/// by.
///
/// **Pinned by the weights' own hash.** An adapter is a delta trained over one
/// exact set of weights; over any other it answers fluently and wrongly, with
/// no error. So `config set-backend --adapters` records the base's sha256
/// beside each adapter, and every load compares it with the base the entry
/// names now -- a mismatch refused by name with the ways out, 49e's `--lora`
/// pattern consumed whole.
///
/// **Read once per change.** A base is gigabytes and its hash seconds to read,
/// so each file's sha256 is remembered by its identity -- path, size and
/// modification time, the prompt cache's own rule (26j) -- in
/// `cache/weight-hashes.json`: a base is read again when it changes, never at
/// every load.
namespace apogee::backends {

/// Reads a file's sha256: `models::file_sha256` in the build, a test's counter
/// otherwise. Empty when the file cannot be read.
using FileHasher = std::function<std::string(const std::filesystem::path&)>;

/// Where the hashes of an entry's weights come from (53a).
struct WeightHashing {
    /// The file they are remembered in -- `weight_hash_cache_path()` in the
    /// build. Empty remembers nothing: every ask reads the weights.
    std::filesystem::path memo{};
    /// Reads one file; `models::file_sha256` when unset.
    FileHasher hasher{};
};

/// The sha256 that pins an adapter to its base (53a). For a file -- a GGUF --
/// the file's own; for a directory -- an MLX model -- a sha256 over
/// `<path>\t<sha256>\n` for every `*.safetensors` shard, sorted by its path
/// within the directory: the store's id rule (`models::snapshot_weight_id`)
/// whole, so its first 12 characters are the store's id. A file is read only
/// when the memo does not hold its identity. Empty when a file cannot be read,
/// or a directory holds no shard.
[[nodiscard]] std::string weights_sha256(const std::filesystem::path& weights,
                                         const WeightHashing& hashing = {});

/// How an entry's adapters are checked against its base (53a).
struct AdapterCheck {
    /// The entry's name, for the command each refusal names.
    std::string backend{};
    /// The weights the adapters apply over: the entry's `model_path`.
    std::filesystem::path base{};
    /// An mlx entry's adapter is a directory, as mlx-lm's training writes one
    /// (`adapter_config.json` beside its weights); a llamacpp entry's is a
    /// GGUF LoRA file.
    bool directories = false;
    WeightHashing hashing{};
};

/// Why `adapters` cannot apply over `check.base`, or empty when they can:
/// file facts, in order -- an adapter not there, or not the kind its runtime
/// reads; one pinned to no base; then the base's weights not the ones it was
/// pinned to -- each naming the adapter and the command that fixes it. The
/// base is hashed only when there is a pin to check. What a load asks before
/// anything is read into memory.
[[nodiscard]] std::string adapter_refusal(const std::vector<harness::AdapterConfig>& adapters,
                                          const AdapterCheck& check);

/// `adapters` pinned to the base at `check.base` as it is now (53a) -- what
/// `config set-backend --adapters` records: each path made absolute (`~` and
/// `${VAR}` expanded), the file facts a load asks, then the base's sha256 set
/// on every one. The refusal, or empty with `adapters` pinned.
[[nodiscard]] std::string pin_adapters(std::vector<harness::AdapterConfig>& adapters,
                                       const AdapterCheck& check);

}  // namespace apogee::backends
