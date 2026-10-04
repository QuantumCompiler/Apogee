#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "commands/command.h"
#include "harness/config.h"
#include "models/quantize.h"
#include "models/sidecar.h"
#include "models/store.h"

namespace CLI {
class App;
}

/// The mutating half of `apogee models`: `pull`, `delete`, `repair`.
///
/// Split from `models.h` on purpose. The listing surface is safe to run at any
/// time; these three change what is on disk, and keeping them in their own
/// translation unit makes "what can this command destroy?" a question with a
/// short answer.
///
/// **These land before the admin plane exists.** Per the parity discipline,
/// they go into the admin plane's documented-skip list until their HTTP twins
/// are backfilled — enumerated there, never silently skipped.
namespace apogee::commands {

/// What `models delete` would do, computed before anything is removed.
///
/// A plan rather than a direct deletion so the command can show exactly what it
/// will touch and, crucially, so the rule that it touches **nothing outside
/// the model store** is testable without deleting anything.
struct DeletePlan {
    bool ok = false;
    std::string error;

    /// What the name resolved to: a whole model, one format, or one set of
    /// weights.
    models::StoreTarget target;

    /// Every weights directory that goes -- whole `<id>` directories only.
    std::vector<std::filesystem::path> removes;

    /// Set when a GGUF being removed came from the user's Ollama store.
    ///
    /// **Ollama's blobs are shared between models**, so removing one by hand
    /// silently corrupts every sibling that referenced it. Apogee deletes its
    /// own copy and tells the user the one supported way to remove the
    /// original: `ollama rm`. It never touches that store itself.
    bool ollama_sourced = false;
    std::string ollama_ref;

    /// The backend the name was taken as (M7): its model file is the stored
    /// GGUF this plan removes. Empty when the name meant the store.
    std::string backend;
    /// A backend shares the name the store took, and its model is not what
    /// goes -- said before the plan, since a user may have meant it (M7).
    std::string also_backend;
    /// That backend's model, by its handle.
    std::string also_backend_weights;
};

/// Builds the plan for deleting `name` -- `owner/repo`, a model directory
/// name, `<model>/<format>/<id>`, or a bare id -- from the store.
///
/// Refuses anything outside the store -- a path elsewhere, a `..` -- because
/// "delete a model by name" must never become "delete a file by path".
[[nodiscard]] DeletePlan plan_delete(const models::StoreRoots& roots, std::string_view name);

/// `plan_delete`, a backend's name taken too (M7). A name the store knows
/// means what it always did -- `also_backend` saying when a backend has it --
/// else a backend whose model file is a stored GGUF means that GGUF's
/// weights, planned exactly as its handle would be. A backend with no stored
/// file is refused, saying why.
[[nodiscard]] DeletePlan plan_delete(const models::StoreRoots& roots, const harness::Config& config,
                                     std::string_view name);

/// The GGUF half of `apogee models repair <name>`: re-verify each stored GGUF
/// `name` covers against its record, and say what to do. Empty when `name`
/// covers no stored GGUF.
///
/// A GGUF repair **diagnoses**; the only safe fix for bytes that no longer
/// match is a fresh pull. (A SafeTensors set is repaired in place, file by
/// file, against its snapshot record -- `models::repair_snapshot`.)
[[nodiscard]] std::string render_repair(const models::StoreRoots& roots, std::string_view name);

/// Which SafeTensors set `models convert` and `train` read.
struct SnapshotChoice {
    std::filesystem::path path;
    /// The model it belongs to -- where a conversion's GGUF goes.
    std::string model;
    std::string id;
    /// Why none. Set exactly when `path` is empty.
    std::string error;
};

/// `given` as a model (its newest set, or `from_id`), as one set, or as a
/// snapshot directory outside the store (its model named after the
/// directory). Something only the old layout has is refused with the
/// migration named.
[[nodiscard]] SnapshotChoice choose_snapshot(const models::StoreRoots& roots,
                                             std::string_view given, std::string_view from_id = {});

/// Which GGUF `models quantize` reads.
struct GgufChoice {
    std::filesystem::path file;
    /// Its projector, when one sits beside it -- carried into the quantized
    /// copy's directory, since quantizing the model leaves it unchanged.
    std::filesystem::path projector;
    std::string model;
    std::string id;
    std::string error;
};

/// `given` as a model -- its newest UNQUANTIZED GGUF (F32, F16, BF16), or
/// `from_id`, since quantizing what is already quantized loses quality twice
/// -- as one stored GGUF, or as a `.gguf` file outside the store (its model
/// named after the file).
[[nodiscard]] GgufChoice choose_gguf(const models::StoreRoots& roots, std::string_view given,
                                     std::string_view from_id = {});

/// The GGUF `models convert` already made of the SafeTensors set `ref` at
/// `out_type` -- its record says so -- or nullopt. A conversion is recognised
/// before it runs, as a pull is before it downloads: redoing it would write
/// the same bytes again.
[[nodiscard]] std::optional<models::StoredGguf> find_conversion(const models::StoreRoots& roots,
                                                                std::string_view model,
                                                                std::string_view ref,
                                                                std::string_view out_type);

/// Finds what an older `models pull --safetensors` damaged in the snapshot at
/// `dir` and fixes it in place -- fetching each damaged file again from where
/// its record says it came, checked against the recorded size and sha256 --
/// saying what it did on stdout. False when it could not.
[[nodiscard]] bool repair_snapshot_in_place(const std::filesystem::path& dir);

/// Where a SafeTensors snapshot lands and is looked for: `paths.hf_dir`
/// when the config sets it, else the models directory. The template
/// reserves `hf_dir` for exactly these directories.
[[nodiscard]] std::filesystem::path snapshot_root(const std::filesystem::path& models_dir,
                                                  const std::string& config_flag);

/// The model store's roots: the models directory, and `paths.hf_dir` for
/// SafeTensors sets when the config sets it.
[[nodiscard]] models::StoreRoots store_roots(const std::filesystem::path& models_dir,
                                             const std::string& config_flag);

/// Registers `pull`, `delete`, `quantize`, `convert`, `repair` and `migrate`
/// on the `models` subcommand.
///
/// Takes no `Config`: nothing here reads one. A Hugging Face token comes from
/// the environment at the point of use (`HF_TOKEN`), and the models directory
/// is passed explicitly so a test can point it at a temporary tree.
/// What makes a quantization: `models::quantize`, or a test's stand-in.
using Quantizer = std::function<models::QuantizeResult(
    const std::filesystem::path& input, const std::filesystem::path& output, std::string_view level,
    const models::QuantizeProgress& progress)>;

[[nodiscard]] Quantizer default_quantizer();

/// The levels `--register-with` names, in the table's spelling and each once;
/// F16 dropped, since it is always made and registered. Fails on an unknown
/// level, naming the accepted ones -- before anything runs.
[[nodiscard]] std::vector<std::string> register_levels(const std::vector<std::string>& given);

/// One command from SafeTensors to runnable backends (M3).
struct RegisterChainRequest {
    models::StoreRoots roots;
    /// The config the backends are registered in.
    std::filesystem::path config_path;
    /// Set: the chain begins by pulling this Hugging Face repository's full
    /// weights (`models pull <ref> --safetensors --register...`).
    std::string pull_ref;
    /// Without a pull: the SafeTensors set to start from, as `convert` takes
    /// it (`models convert <model> --register...`) -- the resume command.
    std::string snapshot;
    std::string snapshot_from;
    /// The quantization levels, each its own backend beside the F16's.
    std::vector<std::string> levels;
    /// What every backend's name begins with (`--base-name`); empty: the
    /// model's own name. `Gemma4-E2B` registers `Gemma4-E2B-F16` and
    /// `Gemma4-E2B-Q4KM`.
    std::string base_name;
};

/// What the chain makes things with, so a test can stand in for llama.cpp.
struct ChainTools {
    Quantizer quantize = default_quantizer();
};

/// Pull (when asked), convert to F16 with its projector, quantize to each
/// level, and register a backend for every one of them -- `<base>-F16`,
/// `<base>-Q4KM`, the level without its underscores the way backends are
/// named by hand -- each stage the standalone verb's own core. Refuses
/// before the first stage what would refuse at the last (no config, a name
/// taken by another model, a name the config cannot hold). A failure says
/// where it stopped and the command that resumes it; every earlier stage's
/// output stays in the store, and the resumed chain finds it rather than
/// making it again.
void run_register_chain(const RegisterChainRequest& request, const ChainTools& tools = {});

void bind_model_mutations(CLI::App& models, const std::filesystem::path& models_dir,
                          const RootContext& context);

}  // namespace apogee::commands
