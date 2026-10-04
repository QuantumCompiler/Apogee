#pragma once

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/config.h"
#include "harness/harness.h"
#include "training/datasets.h"
#include "training/kit.h"
#include "training/synth.h"

/// The dataset cores `datasets create` and its admin twin share -- which is
/// what makes a dataset created over HTTP byte-identical to one created on
/// the command line -- and the teacher a synth runs on (A4: moved out of
/// `cli/datasets`).
namespace apogee::commands {

/// What `datasets create` and its twin do.
struct DatasetCreateRequest {
    std::string name;
    training::CreateSource source = training::CreateSource::Template;
    training::SessionFilter filter;
    bool force = false;
    /// Explicit lines override the source (the HTTP body's `lines`).
    std::vector<std::string> lines;
};

struct DatasetCreateResult {
    std::filesystem::path path;
    int examples = 0;
    /// Sessions that contributed, for the `sessions` source.
    int sessions = 0;
    int skipped = 0;
};

/// Creates the dataset in `store`. Throws std::runtime_error on a refusal
/// (a bad name, an existing dataset without force, a bad date).
[[nodiscard]] DatasetCreateResult create_dataset(const training::DatasetStore& store,
                                                 const DatasetCreateRequest& request);

/// The persisted sessions as the miner sees them. `keep` owns the loaded
/// sessions for as long as the views are used.
struct LoadedSessions {
    std::vector<training::SessionView> views;
    struct Owned;
    std::shared_ptr<Owned> keep;
};

[[nodiscard]] LoadedSessions load_session_views();

/// The teacher `name` resolves to: a configured API-billing or local
/// backend, named explicitly. Empty `key` with `error` set on a refusal --
/// an unknown name, or a vendor-CLI entry (the teacher runs inside Apogee's
/// own loop, and the spend rule is satisfied by the naming itself).
struct TeacherResolution {
    std::string key;
    std::string error;
    /// Whether batches may run in parallel: an API backend yes, a local or
    /// scripted one no (one model, one context).
    bool parallel_safe = false;
};

[[nodiscard]] TeacherResolution resolve_teacher(const harness::Config& config,
                                                std::string_view name);

/// The synth core's generate closure over `harness`: one plain generation
/// call per batch, a side request, the provider's failure reported as
/// retryable and a cancellation as final.
[[nodiscard]] training::GenerateFn make_teacher(const harness::Harness& harness, std::string key);

/// `parallel` clamped to what the teacher allows.
[[nodiscard]] int effective_parallel(const TeacherResolution& teacher, int requested) noexcept;

[[nodiscard]] nlohmann::json dataset_json(const training::DatasetInfo& info);
[[nodiscard]] nlohmann::json kit_json(const training::KitSummary& kit);

}  // namespace apogee::commands
