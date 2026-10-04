#pragma once

#include <filesystem>

#include "commands/command.h"

namespace CLI {
class App;
}

/// `apogee models migrate`: the flat model layout moved into the model store
/// (`models/store.h`), and everything that pointed into it repointed.
///
/// **A command, not a `check --fix` step.** Moving a model means rewriting
/// the `model_path` that points at it, and `check` never edits the config
/// (CLAUDE.md -> One layout declaration). So `check` reports what is still in
/// the old layout and names this command; this command shows everything it
/// will do and does it only on `--yes`.
///
/// What moves with a model, so nothing breaks:
///   - the GGUF's record and its vision projector, into the same directory
///   - every backend's `model_path` and `mmproj_path` that named an old path
///   - every vector collection whose recorded embedding model WAS that path --
///     a llamacpp backend with no `model:` is identified by its `model_path`,
///     and a collection that recorded the old one would otherwise read as a
///     model mismatch and fall back to lexical search
///   - promoted training versions: the GGUF under the model it was trained
///     from, its ledger entry, and the backend serving it
///   - run and pipeline manifests that name a moved snapshot as their base
/// A snapshot an older `models pull --safetensors` damaged is repaired in
/// place once it has moved.
namespace apogee::commands {

void bind_model_migrate(CLI::App& models, const std::filesystem::path& models_dir,
                        const RootContext& context);

}  // namespace apogee::commands
