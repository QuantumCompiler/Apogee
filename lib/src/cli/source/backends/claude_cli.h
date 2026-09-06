#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "backends/cli_event.h"
#include "backends/jsonl_framer.h"
#include "harness/config.h"
#include "harness/provider.h"
#include "platform/child_process.h"

/// Claude driven through the official `claude` CLI as a long-lived child.
///
/// The **subscription-auth path**, beside the API-key path in
/// `backends/anthropic.h`. It reuses that backend's IR mapping and typed sinks
/// and none of its HTTP: this one speaks JSONL over pipes.
///
/// **One child per session, not per turn.** That is the design's whole point —
/// a per-turn spawn pays process startup and MCP discovery on every message,
/// which is what makes a naive CLI wrapper feel slow. Keeping stdin open lets
/// one process serve the whole conversation.
///
/// **Apogee's transcript stays authoritative.** The CLI's session is an
/// optimisation: it saves re-sending history and preserves server-side prompt
/// caching. When a resume fails, the fallback is to replay our own transcript
/// into a fresh child. A conversation that dies because a session id went
/// stale is a worse failure than one slow turn.
///
/// **Credentials are never read.** Apogee does not open `~/.claude`, implement
/// OAuth, or parse the CLI's session files. The CLI authenticates; Apogee only
/// spawns it. Crash recovery goes through `--resume`, which is exactly why
/// reading anything off disk is never necessary. This is a SPEC principle
/// binding every vendor-CLI backend, not a preference of this one.
namespace apogee::backends {

/// Which credentials the child is allowed to use.
enum class ClaudeCliMode : std::uint8_t {
    /// Whatever the user's CLI is logged into. Hooks, MCP servers and
    /// CLAUDE.md are discovered.
    Subscription,
    /// `--bare`: skips discovery, and **disables subscription auth** — the
    /// child then needs `ANTHROPIC_API_KEY` or an `apiKeyHelper`. The right
    /// mode for CI and reproducible runs.
    Bare,
};

[[nodiscard]] std::string_view to_string(ClaudeCliMode mode) noexcept;
[[nodiscard]] std::optional<ClaudeCliMode> claude_cli_mode_from_string(
    std::string_view name) noexcept;

/// Builds the argument list for a child.
///
/// Pure, and separated from spawning so the flag policy is testable without a
/// process: these flags are load-bearing and easy to get subtly wrong.
/// `--output-format stream-json` REQUIRES `--verbose`, and
/// `--include-partial-messages` is the flag that produces token deltas at all.
struct ClaudeCliInvocation {
    ClaudeCliMode mode = ClaudeCliMode::Subscription;
    std::string model;
    std::string system_prompt;
    /// Non-empty resumes an existing CLI session.
    std::string resume_session_id;
    /// Non-empty constrains the turn to a JSON Schema.
    std::string json_schema;
    /// A one-shot invocation carries its prompt in argv and does not read
    /// stdin as a stream.
    bool streaming_stdin = true;
};

[[nodiscard]] std::vector<std::string> build_arguments(const ClaudeCliInvocation& invocation);

class ClaudeCliProvider final : public harness::LLMProvider, public harness::StatusReporting {
public:
    /// Starts a child. Injected so the whole provider is testable without the
    /// real CLI — no subscription spend, and failure modes (a child that dies
    /// mid-turn, a resume that is refused) that a live CLI will not produce on
    /// demand.
    using Spawner = std::function<std::unique_ptr<platform::ChildProcess>(
        const platform::ChildCommand&, std::string&)>;

    struct Options {
        std::string backend_name = "claude-cli";
        /// Resolved from PATH when it has no separator.
        std::string binary = "claude";
        ClaudeCliMode mode = ClaudeCliMode::Subscription;
        std::string model;
        /// How long to wait for a turn's terminal event.
        std::chrono::milliseconds turn_timeout{600000};
        /// Shutdown budget. The CLI drains its backlog rather than truncating,
        /// scaling its wait with the queue up to roughly 30s; a shorter kill
        /// cuts off tail output under load.
        std::chrono::milliseconds shutdown_timeout{30000};
    };

    ClaudeCliProvider(Options options, Spawner spawner);

    // A live child is owned here, so copying or moving this object would
    // duplicate or orphan a process.
    ClaudeCliProvider(const ClaudeCliProvider&) = delete;
    ClaudeCliProvider& operator=(const ClaudeCliProvider&) = delete;
    ClaudeCliProvider(ClaudeCliProvider&&) = delete;
    ClaudeCliProvider& operator=(ClaudeCliProvider&&) = delete;

    /// Builds one over the real process seam.
    /// Throws harness::ProviderError when the platform cannot spawn children,
    /// or when the binary is not on PATH.
    [[nodiscard]] static std::unique_ptr<ClaudeCliProvider> from_config(
        const std::string& backend_name, const harness::BackendConfig& config);

    ~ClaudeCliProvider() override;

    [[nodiscard]] std::string_view backend_name() const noexcept override;

    [[nodiscard]] harness::ChatResponse chat(
        const harness::ChatRequest& request,
        const harness::CancellationToken& cancellation) override;

    [[nodiscard]] harness::ChatResponse stream_chat(const harness::ChatRequest& request,
                                                    const harness::StreamOptions& options) override;

    [[nodiscard]] std::vector<harness::ModelInfo> list_models(
        const harness::CancellationToken& cancellation) override;

    [[nodiscard]] harness::StatusEvent model_status() const override;

    /// A schema-constrained one-shot, on its own short-lived child.
    ///
    /// A distinct entry point rather than a flag on the conversational path,
    /// because the two have different failure modes and different costs:
    /// `--json-schema` is implemented as a forced tool call AFTER a normal
    /// prose answer, so it budgets an extra generation. The prose is
    /// suppressed from display and held as a fallback — without suppression the
    /// user watches a paragraph stream only to be replaced by a JSON object,
    /// and without the fallback a CLI that stopped populating
    /// `structured_output` would return nothing at all.
    [[nodiscard]] std::string complete_structured(std::string_view prompt,
                                                  std::string_view json_schema,
                                                  const harness::CancellationToken& cancellation);

    /// How many children this provider has started. The persistent-child claim
    /// is only meaningful if it can be counted.
    [[nodiscard]] int spawn_count() const noexcept;

    /// The CLI session id captured from the last terminal event, if any.
    [[nodiscard]] const std::string& session_id() const noexcept;

    /// Whether a session child is currently alive.
    [[nodiscard]] bool has_live_child() const noexcept;

    /// Drops the session child. The next turn starts or resumes one.
    void end_session();

private:
    struct TurnOutcome {
        TurnComplete complete;
        std::string answer;
        bool timed_out = false;
        bool child_died = false;
    };

    /// Starts a session child, resuming when a session id is known.
    [[nodiscard]] bool ensure_child(std::string& error);

    /// Reads events until the turn's terminal event, feeding the sinks.
    [[nodiscard]] TurnOutcome pump_turn(platform::ChildProcess& child,
                                        const harness::StreamOptions& options, bool suppress_text);

    /// Runs a whole one-shot invocation and returns its terminal event.
    [[nodiscard]] TurnOutcome run_one_shot(const ClaudeCliInvocation& invocation,
                                           std::string_view prompt,
                                           const harness::StreamOptions& options,
                                           bool suppress_text, std::string& error);

    Options options_;
    Spawner spawner_;

    std::unique_ptr<platform::ChildProcess> child_;
    JsonlFramer framer_;

    /// Events parsed but not yet consumed by a turn.
    ///
    /// A persistent child's output is one continuous stream, and a single read
    /// can carry a turn's terminal event with more bytes behind it. Applying
    /// everything a feed produced to the CURRENT turn would fold the next
    /// turn's text into this one's answer; dropping the remainder would lose
    /// it. So parsing and consuming are separated, and whatever a turn does not
    /// use stays queued for the next one.
    std::deque<CliEvent> pending_;

    /// The CLI's own session id, captured from every terminal event and used
    /// to resume. Stored by Apogee; never read from the CLI's files.
    std::string session_id_;

    /// How many IR messages have already been delivered to the live child.
    ///
    /// The CLI keeps the conversation, so a turn sends only what is new. If
    /// the caller's history no longer extends what was sent — a compaction, an
    /// edit — the child is restarted rather than fed a contradiction.
    std::size_t delivered_ = 0;
    std::vector<std::string> delivered_digest_;

    int spawns_ = 0;
    std::string stderr_tail_;
};

}  // namespace apogee::backends
