#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
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
    /// Whether a hash read here is written back to `memo`. A load remembers
    /// what it read; the doctor reads the memo and writes nothing (53c:
    /// `check` edits nothing on the machine, a cache included).
    bool remember = true;
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

/// What an mlx-lm adapter directory holds, both read when mlx-lm applies it
/// (55e): its configuration and its weights -- one list for the gate a load
/// passes, the doctor's row and what `train promote --adapter` checks it
/// copied.
inline constexpr std::array<std::string_view, 2> kMlxAdapterFiles{"adapter_config.json",
                                                                  "adapters.safetensors"};

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

/// What one adapter's own files say, before any base is read (53c): the
/// facts `adapter_refusal` refuses a load on, in its order -- the doctor
/// words a row of each, a load the first that is not `Pinned`.
enum class AdapterFiles : std::uint8_t {
    /// There, the kind its runtime reads, and pinned to some base -- whether
    /// that base is the one the entry runs is the weights' to say.
    Pinned,
    /// Not there.
    Missing,
    /// There, but not what its runtime reads: a llamacpp entry applies a
    /// GGUF LoRA file, an mlx entry a directory holding `kMlxAdapterFiles`
    /// -- a directory short of its weights is refused before a load, never
    /// left to fail as mlx-lm's own exception (55e).
    WrongKind,
    /// The kind its runtime reads, pinned to no base: written by hand.
    Unpinned,
};

/// `adapter`'s own facts against the entry `check` describes (53c), its
/// base never read.
[[nodiscard]] AdapterFiles adapter_files(const harness::AdapterConfig& adapter,
                                         const AdapterCheck& check);

/// Why `adapter`, there, is not the kind its runtime reads, as the clause a
/// refusal puts after its path -- "it holds no adapters.safetensors", "it is
/// a file, not a directory" -- or empty when it is that kind or not there
/// at all (55e).
[[nodiscard]] std::string adapter_kind_gap(const harness::AdapterConfig& adapter,
                                           const AdapterCheck& check);

/// `adapters` as `config set-backend --adapters` spells them (53c):
/// `<path>[:<scale>]`, comma-joined, quoted where a shell would split them
/// or read a backslash as an escape (a Windows path), `""` for none -- the whole set a fix names,
/// so it drops nothing it did not mean to.
[[nodiscard]] std::string spell_adapters(const std::vector<harness::AdapterConfig>& adapters);

/// Why `adapters` cannot apply over `check.base`, or empty when they can:
/// file facts, in order -- an adapter not there, or not the kind its runtime
/// reads; one pinned to no base; then the base's weights not the ones it was
/// pinned to -- each naming the adapter and the command that fixes it. The
/// base is hashed only when there is a pin to check. What a load asks before
/// anything is read into memory.
[[nodiscard]] std::string adapter_refusal(const std::vector<harness::AdapterConfig>& adapters,
                                          const AdapterCheck& check);

/// The vector space an entry embeds in through `adapters` (55e): `model`, the
/// name its embedder gives its vectors, alone when there are none -- byte for
/// byte the name every collection bound before 55e recorded -- and otherwise
/// that name and each adapter's own content, ` + adapters <id>[:<scale>],...`,
/// in the order they apply: an id the first 12 characters of a sha256 over
/// what its runtime reads -- a GGUF LoRA file's bytes; an mlx-lm directory's
/// `kMlxAdapterFiles`, each `<name>\t<sha256>\n` -- and a scale other than 1
/// after a colon, as `spell_adapters` writes one. So the same adapter copied
/// elsewhere is the same space, and another adapter, another scale, another
/// order, a re-trained file in place or none at all is another: a collection
/// a store binds under one set falls to lexical under any other, through the
/// one retriever rule, where it used to be queried across the two. An
/// adapter that cannot be read is `unread`, matching no recorded space. The
/// files are hashed once per change of them, through `hashing`'s memo.
[[nodiscard]] std::string embedding_space(const std::string& model,
                                          const std::vector<harness::AdapterConfig>& adapters,
                                          const WeightHashing& hashing = {});

/// `adapters` pinned to the base at `check.base` as it is now (53a) -- what
/// `config set-backend --adapters` records: each path made absolute (`~` and
/// `${VAR}` expanded), the file facts a load asks, then the base's sha256 set
/// on every one. The refusal, or empty with `adapters` pinned.
[[nodiscard]] std::string pin_adapters(std::vector<harness::AdapterConfig>& adapters,
                                       const AdapterCheck& check);

}  // namespace apogee::backends
