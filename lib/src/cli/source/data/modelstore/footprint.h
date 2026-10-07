#pragma once

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
namespace apogee::models {

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
    /// Why a number is unknown, for a person; empty when none is.
    std::string unknown;

    /// Weights and cache: 0 for a member holding nothing here, nullopt when
    /// a number is unknown.
    [[nodiscard]] std::optional<std::int64_t> bytes() const;
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

    /// The sum of every known member's bytes.
    [[nodiscard]] std::int64_t known_bytes() const;
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
/// (`harness::backend_as_run`) -- whose window the cache is priced at.
[[nodiscard]] MemberFootprint gguf_member_footprint(const harness::SuiteBackend& member,
                                                    const harness::BackendConfig& as_run,
                                                    const std::optional<Sidecar>& record,
                                                    const std::optional<Sidecar>& projector_record,
                                                    const GgufInfo& header);

/// The suite `suite` of `config` priced against `machine`: each backend its
/// members name, read from the store's records and its header where it is a
/// local GGUF, at the window `suite` pins -- whether or not it is the active
/// suite. An empty footprint (no members) when `config` has no such suite.
[[nodiscard]] SuiteFootprint suite_footprint(const harness::Config& config, std::string_view suite,
                                             const MachineBudget& machine);

}  // namespace apogee::models
