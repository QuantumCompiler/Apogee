#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "commands/command.h"
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
};

/// Builds the plan for deleting `name` -- `owner/repo`, a model directory
/// name, `<model>/<format>/<id>`, or a bare id -- from the store.
///
/// Refuses anything outside the store -- a path elsewhere, a `..` -- because
/// "delete a model by name" must never become "delete a file by path".
[[nodiscard]] DeletePlan plan_delete(const models::StoreRoots& roots, std::string_view name);

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
void bind_model_mutations(CLI::App& models, const std::filesystem::path& models_dir,
                          const RootContext& context);

}  // namespace apogee::commands
