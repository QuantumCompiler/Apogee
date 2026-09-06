#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "backends/ollama_cli_output.h"
#include "harness/config.h"
#include "harness/provider.h"
#include "platform/child_process.h"

/// Ollama's cloud models, driven through the official `ollama` CLI.
///
/// The fourth vendor's subscription path. It is the odd one in the vendor-CLI
/// family and the differences are not cosmetic — they were established by
/// characterizing `ollama` 0.33.2 against a signed-in cloud session rather than
/// assumed from `claude-cli`'s shape:
///
/// **There is no event stream.** `ollama run` has no `--output-format
/// stream-json` and no stdin-fed turn protocol. So there is no persistent child
/// here: each turn is its own invocation, and streaming granularity is whatever
/// falls out of reading its stdout. That is worse than every other backend, and
/// it is recorded rather than disguised.
///
/// **`--nowordwrap` is mandatory.** Without it the CLI writes cursor-control
/// bytes (`ESC[nD ESC[K`) *into stdout* to re-wrap words — even when stdout is
/// a pipe. A consumer that did not pass it would parse terminal escapes as
/// answer text.
///
/// **Thinking is in-band**, between literal markers, and is demultiplexed in
/// `ollama_cli_output.h`.
///
/// ## ⚠ This backend must never cause a server to start
///
/// Every other Apogee backend satisfies *interactive turns never open a
/// listening socket* by construction. This one talks to a CLI that is itself a
/// client of a local HTTP server — and **`ollama run` tries to START that
/// server when it cannot reach one** (`Error: timed out waiting for server to
/// start`). So merely never running `ollama serve` ourselves is not enough:
/// spawning `ollama run` on a machine with no server would make Apogee the
/// cause of a listening socket, one process removed.
///
/// The rule is ownership. A server the user runs is their socket, exactly as an
/// external `url:` backend would be. A server Apogee brings into existence is
/// ours, and violates the invariant.
///
/// So the provider **pre-flights**: it asks whether a server is already
/// reachable, and refuses the turn if not. Connecting outward to check is
/// invariant-safe — the rule forbids listening, not connecting.
namespace apogee::backends {

class OllamaCliProvider final : public harness::LLMProvider, public harness::StatusReporting {
public:
    /// Starts a child. Injected so the provider is testable without the CLI —
    /// and so the "never starts a server" rule can be asserted on argv.
    using Spawner = std::function<std::unique_ptr<platform::ChildProcess>(
        const platform::ChildCommand&, std::string&)>;

    /// Answers whether an Ollama server is already reachable at `host`.
    ///
    /// Injected for the same reason: the refusal path is an acceptance
    /// criterion, and a test must be able to say "no server" without stopping
    /// the developer's real one.
    using ServerProbe = std::function<bool(const std::string& host)>;

    struct Options {
        std::string backend_name = "ollama-cli";
        std::string binary = "ollama";
        /// The cloud model, e.g. `gpt-oss:20b-cloud`.
        std::string model;
        /// Where the CLI's server lives. Mirrors the CLI's own `OLLAMA_HOST`.
        std::string host = "127.0.0.1:11434";
        std::chrono::milliseconds turn_timeout{600000};
    };

    OllamaCliProvider(Options options, Spawner spawner, ServerProbe probe);

    OllamaCliProvider(const OllamaCliProvider&) = delete;
    OllamaCliProvider& operator=(const OllamaCliProvider&) = delete;
    OllamaCliProvider(OllamaCliProvider&&) = delete;
    OllamaCliProvider& operator=(OllamaCliProvider&&) = delete;
    ~OllamaCliProvider() override = default;

    /// Builds one over the real process seam and a real reachability probe.
    /// Throws harness::ProviderError when the platform cannot spawn children,
    /// when the binary is missing, or when no model is configured.
    [[nodiscard]] static std::unique_ptr<OllamaCliProvider> from_config(
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

    /// The argv for one turn. Pure, and exposed because the flag policy carries
    /// two load-bearing rules — `--nowordwrap` present, `serve` never — that
    /// deserve assertions of their own.
    [[nodiscard]] static std::vector<std::string> build_arguments(const Options& options,
                                                                  bool hide_thinking);

    /// Renders a conversation into the single prompt argument the CLI takes.
    ///
    /// There is no turn protocol, so history is flattened. That is a real
    /// fidelity loss and it is stated here rather than buried: this backend
    /// re-sends the whole conversation every turn, and the CLI has no memory of
    /// the last one.
    [[nodiscard]] static std::string flatten_prompt(
        const std::vector<harness::ChatMessage>& messages);

private:
    Options options_;
    Spawner spawner_;
    ServerProbe probe_;
};

/// The real reachability probe: an outbound request to `host`.
[[nodiscard]] bool ollama_server_reachable(const std::string& host);

}  // namespace apogee::backends
