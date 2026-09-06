#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "harness/config.h"
#include "harness/provider.h"
#include "platform/child_process.h"

/// OpenAI's subscription path, through the official `codex` CLI.
///
/// The second vendor-CLI backend, and it sits between the other two in what the
/// vendor offers. Established by characterizing `codex-cli` 0.153.4 against a
/// ChatGPT login rather than assumed from Claude's shape:
///
/// **Typed JSONL events, but no deltas.** `codex exec --json` emits
/// `thread.started` / `turn.started` / `item.completed` / `turn.completed`, so
/// the shared framer applies — but a twelve-line answer arrives in a *single*
/// `item.completed`. Streaming is message-level. That is less than Claude's
/// token-level stream, and it is recorded rather than implied.
///
/// **Real usage accounting**, on `turn.completed` — unlike Ollama's, which
/// reports none.
///
/// **Real session continuity.** `thread.started` carries a `thread_id`, and
/// `codex exec resume <id>` continues that thread with history intact
/// (verified). So there is no persistent child — the CLI has no stdin turn
/// stream — but history need not be re-sent either: turn one spawns `exec`,
/// and every later turn spawns `exec resume`.
///
/// **`resume` does not accept `exec`'s flags.** `--color` and `-s` are rejected
/// by the subcommand. One argv builder for both would work on turn one and fail
/// on turn two, which is why `build_arguments` takes the mode explicitly.
///
/// ## ⚠ This CLI executes shell commands
///
/// `codex exec` is an **agent**, not a chat endpoint: `-s/--sandbox` selects a
/// policy for model-generated shell commands it will run on the user's machine.
/// Every other backend Apogee drives only produces text. So the sandbox is
/// **pinned to `read-only` in the argv builder and is not configurable** — a
/// config key able to widen it would turn an ordinary chat turn into arbitrary
/// local execution, which is not a trade a chat backend gets to offer.
namespace apogee::backends {

class CodexCliProvider final : public harness::LLMProvider, public harness::StatusReporting {
public:
    using Spawner = std::function<std::unique_ptr<platform::ChildProcess>(
        const platform::ChildCommand&, std::string&)>;

    /// Which subcommand a turn uses. They have different flag surfaces, so the
    /// distinction is explicit rather than inferred.
    enum class Invocation : std::uint8_t {
        /// `codex exec …` — the first turn of a thread.
        Fresh,
        /// `codex exec resume <thread_id> …` — every later turn.
        Resume,
    };

    struct Options {
        std::string backend_name = "codex-cli";
        std::string binary = "codex";
        std::string model;
        std::chrono::milliseconds turn_timeout{600000};
    };

    CodexCliProvider(Options options, Spawner spawner);

    CodexCliProvider(const CodexCliProvider&) = delete;
    CodexCliProvider& operator=(const CodexCliProvider&) = delete;
    CodexCliProvider(CodexCliProvider&&) = delete;
    CodexCliProvider& operator=(CodexCliProvider&&) = delete;
    ~CodexCliProvider() override = default;

    /// Builds one over the real process seam.
    /// Throws harness::ProviderError when the platform cannot spawn children or
    /// the binary is missing.
    [[nodiscard]] static std::unique_ptr<CodexCliProvider> from_config(
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
    /// Public because two of its rules are load-bearing enough to deserve their
    /// own assertions: the sandbox is always `read-only`, and the `resume`
    /// form omits the flags that subcommand rejects.
    [[nodiscard]] static std::vector<std::string> build_arguments(const Options& options,
                                                                  Invocation invocation,
                                                                  const std::string& thread_id,
                                                                  const std::string& schema_path);

    /// The thread id captured from the last run, if any.
    [[nodiscard]] const std::string& thread_id() const noexcept;

    /// How many children have been started. A per-turn spawn is expected here;
    /// counting makes that visible rather than assumed.
    [[nodiscard]] int spawn_count() const noexcept;

    /// Forgets the thread, so the next turn starts a fresh one.
    void end_session();

private:
    struct Outcome {
        std::string answer;
        std::int64_t input_tokens = 0;
        std::int64_t output_tokens = 0;
        bool is_error = false;
        std::string error_detail;
    };

    [[nodiscard]] Outcome run_turn(const std::string& prompt, const std::string& schema_path,
                                   const harness::StreamOptions& options);

    Options options_;
    Spawner spawner_;
    std::string thread_id_;
    int spawns_ = 0;
};

}  // namespace apogee::backends
