#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "commands/command.h"
#include "harness/config.h"
#include "models/gguf_inspect.h"

/// `apogee models` — what this machine has, and which backend each role uses.
///
/// The read-only half of model management. Nothing here downloads, deletes, or
/// repairs anything; acquisition is its own item, and keeping the boundary at a
/// command level means the destructive verbs arrive as a reviewable addition
/// rather than as flags that grew on a listing command.
///
/// **Reporting is honest about uncertainty.** Under Apogee's open-model policy
/// there is no allowlist making any model a promise, so this display is the
/// only place a user learns what Apogee actually knows about a file: an
/// unprofiled model says *unprofiled*, an unreadable header says *why*, and a
/// check that could not run says *skipped* rather than passing.
namespace apogee::commands {

/// One row of `apogee models list`.
struct ModelRow {
    /// The backend key from `backends:`.
    std::string backend;
    /// Its `type:` — "llamacpp", "anthropic", "gemini-cli", ….
    std::string type;
    /// The model name or the GGUF's basename, whichever the entry carries.
    std::string model;
    /// Which roles point at this backend, e.g. "chat, embedding". Empty when
    /// none do.
    std::string roles;
    /// Where the file came from, when it is a local one Apogee can see.
    /// "local" for a user-supplied path; "-" for a cloud backend.
    std::string provenance;
    /// `general.architecture` from the GGUF header — "llama", "qwen35". A
    /// **fact read out of the file**, and "-" for a backend with no local file.
    std::string architecture;

    /// The resolved behaviour profile.
    ///
    /// Deliberately NOT the architecture, though it is tempting to show one as
    /// the other: an architecture is what the file says it is, a profile is
    /// what Apogee knows about how that family behaves — its markers, its
    /// reasoning wrappers, its tool dialect. Until the model-profiles item
    /// lands there is no registry, so this reads "unprofiled" for everything,
    /// which is true and useful rather than an empty column. Showing the
    /// architecture here instead would claim knowledge Apogee does not have.
    std::string profile;
    /// Header state for a local model: "ok", "unreadable", or "missing".
    /// Cloud backends report "-".
    std::string state;

    /// What acquisition checked, from the model's sidecar — "size ok, digest
    /// ok, header ok", or "no digest published, header ok", and so on.
    ///
    /// "no record" for a model a user placed by hand: legitimate, and simply
    /// not something integrity can be rechecked against. Never the bare word
    /// "verified"; what was checked is the information.
    std::string verified;
    /// An advisory note — a missing file, a combined multimodal blob, an
    /// unreadable header's reason. Never fatal on its own.
    std::string note;
};

/// Builds the listing: every configured backend, plus every model on disk.
///
/// **Both halves are needed and neither is enough.** A configured backend may
/// point anywhere on the filesystem, so scanning the models directory alone
/// would miss it. And a freshly pulled model is not in the config at all — a
/// listing built only from `backends:` reported "no backends configured"
/// immediately after a 460 MB download, which is how this was found.
///
/// Pure with respect to everything but reading headers and sidecars, so a test
/// drives it with a temp directory rather than the developer's installation.
/// `models_dir` empty skips the on-disk half.
[[nodiscard]] std::vector<ModelRow> build_model_rows(const harness::Config& config,
                                                     const std::filesystem::path& models_dir = {});

/// Renders rows as an aligned table. Empty input yields a single explanatory
/// line, never a bare header with nothing under it.
[[nodiscard]] std::string render_model_table(const std::vector<ModelRow>& rows);

/// Renders the listing as one JSON object per line, for `--output-format
/// stream-json`. A GUI listing models is the first consumer of machine mode
/// beyond chat, and the flag already exists.
[[nodiscard]] std::string render_model_jsonl(const std::vector<ModelRow>& rows);

/// The body of `apogee models info <backend>`.
///
/// `info` is where a header read is reported in full, including the failure
/// reason when it did not parse — an unparseable header must never render as
/// an empty field, which is the reporting bug this whole surface exists to
/// avoid.
[[nodiscard]] std::string render_model_info(const harness::Config& config,
                                            std::string_view backend);

/// The body of `apogee models status` — which backend each role resolves to,
/// and whether that backend is actually configured.
///
/// Every line here comes from `harness::resolve_backend_key`, never from a
/// local reimplementation of the chain. This command is the visible proof that
/// the resolver answers the same way the run path does.
[[nodiscard]] std::string render_role_status(const harness::Config& config);

class ModelsCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
