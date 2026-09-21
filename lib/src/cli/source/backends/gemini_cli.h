#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "harness/config.h"
#include "harness/provider.h"
#include "platform/child_process.h"

/// Google's subscription path, through the official `gemini` CLI.
///
/// The fourth and last vendor-CLI backend, completing SPEC's dual-path claim:
/// every cloud vendor now works under both a subscription plan and an API
/// billing plan, chosen per backend entry — so one config can hold a `gem`
/// (API key, `backends/google.h`) and a `gem-sub` (this) at once.
///
/// Established by characterizing `gemini` 0.46.0 against a **Google login**,
/// not assumed from any sibling's shape. Where the family template and this
/// CLI disagree, the CLI wins and the divergence is recorded.
///
/// **Real streaming deltas.** `-o stream-json` emits assistant `message` events
/// with `delta:true`, and a twenty-line answer arrived in three of them —
/// splitting *inside* a two-digit number at one boundary. Better granularity
/// than codex (message-level) and far better than ollama (none at all).
///
/// **Apogee chooses the session id.** `--session-id <uuid>` starts a session
/// with an id this backend generates, and `--resume <uuid>` continues it with
/// history intact (verified). So there is no id to capture from the stream and
/// no session file to read — the cleanest continuity story in the family, and
/// the reason the credential rule costs nothing here.
///
/// **No persistent child.** The CLI takes one prompt per invocation via `-p`
/// and offers no stdin turn stream, so turn one spawns with `--session-id` and
/// every later turn spawns with `--resume`. A per-turn spawn is not a listening
/// socket, so the interactive-never-listens invariant stays satisfied by
/// construction.
///
/// ## ⚠ This CLI is an agent, and its safety pin was verified
///
/// `gemini` runs tools while answering. Its `--approval-mode` takes `default`,
/// `auto_edit`, `yolo`, and `plan` (read-only), and `-y/--yolo` approves
/// everything. **`plan` is pinned in the argv and is not configurable**, for
/// the same reason codex's sandbox is: a config key able to widen it would turn
/// an ordinary chat turn into arbitrary local modification.
///
/// The pin needed verifying rather than trusting, because this CLI was observed
/// **silently overriding `plan` back to `default`** in an untrusted folder. The
/// answer is that `--skip-trust` suppresses that override, and the pair was
/// tested against a canary: asked to write a file, the child refused and wrote
/// nothing. Both flags are therefore load-bearing together, which is why
/// `build_arguments` is public and asserted.
///
/// **`--skip-trust` is also mandatory for a different reason:** without it a
/// headless invocation in an untrusted directory refuses to run at all. Apogee
/// spawns this child in whatever directory the user's shell happens to be in,
/// which is usually untrusted, so omitting it would make the backend fail for
/// most users while working on the developer's machine.
///
/// **`--raw-output` is never passed.** It disables sanitisation of model output
/// and the CLI itself calls it a security risk; sanitised is the default and
/// the right one.
namespace apogee::backends {

class GeminiCliProvider final : public harness::LLMProvider, public harness::StatusReporting {
public:
    using Spawner = std::function<std::unique_ptr<platform::ChildProcess>(
        const platform::ChildCommand&, std::string&)>;

    /// Generates the session id for a fresh session. Injectable so a test can
    /// pin it; the real one is a UUID v4.
    using IdFactory = std::function<std::string()>;

    /// Which form a turn uses. They carry different flags, so the distinction
    /// is explicit rather than inferred.
    enum class Invocation : std::uint8_t {
        /// `gemini --session-id <uuid> …` — the first turn of a session.
        Fresh,
        /// `gemini --resume <uuid> …` — every later turn.
        Resume,
    };

    struct Options {
        std::string backend_name = "gemini-cli";
        std::string binary = "gemini";
        std::string model;
        std::chrono::milliseconds turn_timeout{600000};
    };

    GeminiCliProvider(Options options, Spawner spawner, IdFactory id_factory);

    GeminiCliProvider(const GeminiCliProvider&) = delete;
    GeminiCliProvider& operator=(const GeminiCliProvider&) = delete;
    GeminiCliProvider(GeminiCliProvider&&) = delete;
    GeminiCliProvider& operator=(GeminiCliProvider&&) = delete;
    ~GeminiCliProvider() override = default;

    /// Builds one over the real process seam.
    /// Throws harness::ProviderError when the platform cannot spawn children or
    /// the binary is missing.
    [[nodiscard]] static std::unique_ptr<GeminiCliProvider> from_config(
        const std::string& backend_name, const harness::BackendConfig& config);

    [[nodiscard]] std::string_view backend_name() const noexcept override;

    [[nodiscard]] harness::ChatResponse chat(
        const harness::ChatRequest& request,
        const harness::CancellationToken& cancellation) override;

    [[nodiscard]] harness::ChatResponse stream_chat(const harness::ChatRequest& request,
                                                    const harness::StreamOptions& options) override;

    [[nodiscard]] std::vector<harness::ModelInfo> list_models(
        const harness::CancellationToken& cancellation) override;

    [[nodiscard]] harness::StatusEvent model_status() const override;

    /// The argv for one turn.
    ///
    /// Public because three of its rules are load-bearing enough to deserve
    /// their own assertions: `--approval-mode plan` and `--skip-trust` are
    /// always both present, `--raw-output` is never present, and the resume
    /// form addresses the session by the id Apogee generated.
    [[nodiscard]] static std::vector<std::string> build_arguments(const Options& options,
                                                                  Invocation invocation,
                                                                  const std::string& session_id);

    /// The session id in use, if any. Generated by Apogee, never read off disk.
    [[nodiscard]] const std::string& session_id() const noexcept;

    /// How many children have been started. A per-turn spawn is expected here;
    /// counting makes that visible rather than assumed.
    [[nodiscard]] int spawn_count() const noexcept;

    /// Forgets the session, so the next turn starts a fresh one.
    void end_session();

private:
    struct Outcome {
        std::string answer;
        std::string model;
        std::int64_t input_tokens = 0;
        std::int64_t output_tokens = 0;
        bool is_error = false;
        std::string error_detail;
    };

    [[nodiscard]] Outcome run_turn(const std::string& prompt,
                                   const harness::StreamOptions& options);

    Options options_;
    Spawner spawner_;
    IdFactory id_factory_;
    std::string session_id_;
    int spawns_ = 0;
};

}  // namespace apogee::backends
