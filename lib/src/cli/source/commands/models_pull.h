#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "models/sidecar.h"

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
/// Apogee's own models directory** is testable without deleting anything.
struct DeletePlan {
    bool ok = false;
    std::string error;

    /// The model file, and its sidecar when one exists.
    std::filesystem::path model;
    std::filesystem::path sidecar;
    bool has_sidecar = false;

    /// Set when the model came from the user's Ollama store.
    ///
    /// **Ollama's blobs are shared between models**, so removing one by hand
    /// silently corrupts every sibling that referenced it. Apogee deletes its
    /// own copy and tells the user the one supported way to remove the
    /// original: `ollama rm`. It never touches that store itself.
    bool ollama_sourced = false;
    std::string ollama_ref;
};

/// Builds the plan for deleting `name` from `models_dir`.
///
/// Refuses anything that resolves outside `models_dir` — a `..` in a name, or
/// an absolute path — because "delete a model by name" must never become
/// "delete a file by path".
[[nodiscard]] DeletePlan plan_delete(const std::filesystem::path& models_dir,
                                     std::string_view name);

/// The body of `apogee models repair <name>`: re-verify, and say what to do.
///
/// Repair **diagnoses**; the only fix it can safely perform is removing a file
/// that is provably broken so it can be pulled again. It does not attempt to
/// patch bytes, and it never edits config.
[[nodiscard]] std::string render_repair(const std::filesystem::path& models_dir,
                                        std::string_view name);

/// Registers `pull`, `delete`, and `repair` on the `models` subcommand.
///
/// Takes no `Config`: nothing here reads one. A Hugging Face token comes from
/// the environment at the point of use (`HF_TOKEN`), and the models directory
/// is passed explicitly so a test can point it at a temporary tree.
void bind_model_mutations(CLI::App& models, const std::filesystem::path& models_dir);

}  // namespace apogee::commands
