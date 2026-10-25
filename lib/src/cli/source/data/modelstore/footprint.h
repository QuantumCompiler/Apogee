#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/config.h"
#include "modelstore/gguf_inspect.h"
#include "modelstore/kv_cache.h"
#include "modelstore/sidecar.h"

/// What a suite takes of the machine's memory, and whether that fits (27e).
///
/// **Composition, not new math.** Every number is one Apogee already states:
/// a local member's weights are the store's own records (`Sidecar::file_size`,
/// the record beside its model file and, when it has one, beside its
/// projector), its attention cache is 26a's arithmetic at the window the suite
/// pins (`local_window` over the header, the entry as run), and the machine's
/// budget is what 26a's check measures a load against, with llama-server's
/// margin held back once for the set. The functions below take those types
/// and nothing else, so a second estimator cannot be written here.
///
/// **Unknown is said, never guessed**, and never the reason a suite is
/// refused: a member with no recorded size, an MLX model, a header that cannot
/// be read -- each is named as unknown, and the rest is still added up.
///
/// **One base, counted once** (53d): members whose entries run the same base
/// (`BaseKey`) are served by one loaded model, so its weights are counted for
/// the first of them and every other adds its own cache and adapters alone.
///
/// **A drafter counts** (M25): a member whose entry drafts (53i's `draft:`)
/// loads the drafter's weights beside its base, so they are counted by their
/// bytes on disk -- in the sum even while the base's own numbers are unknown,
/// since they are a load of their own.
namespace apogee::models {

/// What one loaded llama.cpp base is (53d): its model file, the projector
/// loaded beside it, and the cache its contexts keep -- everything a load is
/// made with but the entry's adapters, which each entry loads over the shared
/// model, and its window, which each entry's own context takes. Two entries
/// whose keys agree are served by one loaded model: the factory shares it by
/// this key (`backends/llama_bases.h`), so the footprint counts it once.
struct BaseKey {
    /// The model file, absolute and normalized -- as the prompt cache's
    /// fingerprint names it.
    std::string model{};
    /// The projector, likewise; empty for none.
    std::string projector{};
    harness::KvCacheType cache_type = kDefaultCacheType;
    /// A named cache type is used or refused; the default gives way to f16
    /// where it cannot be made, so the two are different loads.
    bool cache_type_named = false;

    auto operator<=>(const BaseKey&) const = default;
};

/// The base a load of `model_path` with these settings is.
[[nodiscard]] BaseKey base_key(const std::string& model_path, const std::string& projector_path,
                               harness::KvCacheType cache_type, bool cache_type_named);

/// The base a llamacpp entry runs, as the factory builds it from `as_run`.
[[nodiscard]] BaseKey base_key(const harness::BackendConfig& as_run);

/// What 26a's check holds back from the devices' memory: llama-server's 1 GiB
/// margin. Held back once for a suite, as it is for one model.
inline constexpr std::int64_t kFitMargin = std::int64_t{1024} * 1024 * 1024;

/// The machine's memory for models, as 26a's check reads it: the devices a
/// model offloads to, in all (`backends::offload_memory_total`) -- on Apple
/// silicon, Metal's share of the machine. Unknown in a build without
/// llama.cpp, or where no device says.
struct MachineBudget {
    std::optional<std::int64_t> bytes;
};

/// One backend of a suite, priced.
struct MemberFootprint {
    std::string backend;
    /// The roles it answers for in the suite.
    std::vector<std::string> roles;
    /// Whether its memory is on this machine: a local model. A cloud API or
    /// a vendor CLI's service holds nothing here and costs nothing.
    bool local = false;
    /// Its weights, as the store recorded them: the model file and its
    /// projector. Nullopt when either has no record.
    std::optional<std::int64_t> weights;
    /// Its window and the cache that window allocates (26a), at the window
    /// the suite pins. Nullopt when the header cannot be read.
    std::optional<LocalWindow> window;
    /// The member whose loaded base this one shares (53d) -- the first in the
    /// suite on the same base -- which counts the weights; empty when it
    /// loads its own.
    std::string shares{};
    /// How many adapters it applies over its base, and their bytes: each
    /// file's size, what llama.cpp reads into memory over the base (53a).
    /// Nullopt when a file cannot be read.
    std::size_t adapters = 0;
    std::optional<std::int64_t> adapter_bytes{0};
    /// The drafter its entry loads beside its base (53i, M25): the directory
    /// an mlx entry's `draft:` names, expanded, and its weights by their
    /// bytes on disk -- each `*.safetensors` file's size, what mlx-vlm reads
    /// into memory beside the base, as an adapter is counted by its file.
    /// Empty and 0 for a member that drafts with nothing; nullopt when the
    /// directory's weights cannot be read.
    std::string drafter{};
    std::optional<std::int64_t> drafter_bytes{0};
    /// Why a number is unknown, for a person; empty when none is.
    std::string unknown;

    /// Weights -- unless it shares another member's -- cache, adapters and
    /// drafter: 0 for a member holding nothing here, nullopt when a number is
    /// unknown.
    [[nodiscard]] std::optional<std::int64_t> bytes() const;
    /// What the suite's sum counts of it: `bytes()` when every number is
    /// known; else its drafter's weights where those are known -- a load of
    /// their own beside the base, whatever the base's own numbers (M25) --
    /// and 0 otherwise.
    [[nodiscard]] std::int64_t counted_bytes() const;
};

/// What a member costs beside its own weights and cache (53d), as the suite
/// knows it.
struct MemberExtras {
    /// The member whose loaded base it shares; empty for its own.
    std::string shares{};
    /// Each adapter file's size on disk, in order; nullopt for one that
    /// cannot be read.
    std::vector<std::optional<std::int64_t>> adapters{};
};

/// What a suite's arithmetic says.
enum class Admission : std::uint8_t {
    /// No member holds memory here: nothing to fit.
    NothingHeld,
    /// Every number is known, and the set fits.
    Fits,
    /// What is known fits; some member's size is unknown.
    FitsAsFarAsKnown,
    /// The machine's budget is not known here: the footprint is stated, not
    /// judged.
    BudgetUnknown,
    /// What is known, with the margin, is over the budget.
    OverBudget,
};

/// A suite priced against a machine.
struct SuiteFootprint {
    /// As the file spells it.
    std::string suite;
    /// One per backend (`suite_backends`), in role order.
    std::vector<MemberFootprint> members;
    MachineBudget machine;

    /// The sum of every member's counted bytes (`counted_bytes`): each known
    /// member's, and an unknown one's drafter where its size is known.
    [[nodiscard]] std::int64_t known_bytes() const;
    /// The weights a shared base keeps from being loaded twice (53d): each
    /// sharing member's, where known.
    [[nodiscard]] std::int64_t saved_bytes() const;
    /// Whether any member's memory is on this machine.
    [[nodiscard]] bool holds_local() const;
    /// Whether any member's size is unknown.
    [[nodiscard]] bool has_unknown() const;
    /// What the set needs: the known bytes and the margin, or 0 when nothing
    /// is held here.
    [[nodiscard]] std::int64_t needed() const;
    [[nodiscard]] Admission admission() const;
};

/// One local GGUF member, from the shipped inputs alone: `record` is the
/// store's record beside its model file, `projector_record` its projector's
/// (looked at only when `as_run` names a projector), `header` its model
/// file's header, and `as_run` the entry as it runs under the suite
/// (`harness::backend_as_run`) -- whose window the cache is priced at. With
/// `extras`, the base it shares and its adapters' sizes (53d): a sharing
/// member's weights are stated and not counted, and an unreadable adapter is
/// unknown.
[[nodiscard]] MemberFootprint gguf_member_footprint(const harness::SuiteBackend& member,
                                                    const harness::BackendConfig& as_run,
                                                    const std::optional<Sidecar>& record,
                                                    const std::optional<Sidecar>& projector_record,
                                                    const GgufInfo& header,
                                                    const MemberExtras& extras = {});

/// The suite `suite` of `config` priced against `machine`: each backend its
/// members name, read from the store's records and its header where it is a
/// local GGUF, at the window `suite` pins -- whether or not it is the active
/// suite -- with its adapters' files' sizes, and a base two members share
/// counted for the first (53d); an MLX member's own numbers unknown, and the
/// drafter its entry names counted by its weights' bytes on disk (M25). An
/// empty footprint (no members) when `config` has no such suite.
[[nodiscard]] SuiteFootprint suite_footprint(const harness::Config& config, std::string_view suite,
                                             const MachineBudget& machine);

}  // namespace apogee::models
