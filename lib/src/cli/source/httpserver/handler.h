#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "agent/tool.h"
#include "agentloop/reporter.h"
#include "commands/helpers.h"
#include "harness/cancellation.h"
#include "harness/config.h"
#include "harness/harness.h"
#include "httpserver/http_types.h"
#include "httpserver/session.h"

/// The OpenAI-compatible inference plane, one handler per route.
///
/// Every chat request -- streamed or not, with tools or without -- runs the
/// same `agentloop::run` every other surface runs, behind a Reporter: a
/// `NullReporter` when the client wants one JSON object back, an
/// `SseReporter` when it wants frames. That is the whole design. There is no
/// second loop for HTTP, so a tool, a retriever, a reasoning filter or a
/// compaction rule reaches a remote client the day it reaches the terminal.
///
/// **One turn at a time.** Inference requests serialise behind one mutex; the
/// read-only routes answer concurrently. A local model has one context, and
/// the cloud clients were never audited for concurrent use -- a second request
/// waiting beats a first one corrupted.
namespace apogee::httpserver {

/// Which tools a request may use: its `tool_mode` field.
enum class ToolMode : std::uint8_t {
    /// Every tool the server was started with (`--tools`). The default.
    All,
    /// None -- the request goes straight to the model.
    None,
};

[[nodiscard]] std::optional<ToolMode> tool_mode_from_string(std::string_view name) noexcept;

/// A finish reason in the OpenAI vocabulary: `stop`, `length`, or
/// `content_filter`. A turn that ended with tool calls the loop could not run
/// (the iteration limit) still produced an answer, and a cancelled or otherwise
/// ended one has nothing better to say than `stop` -- the two words a client
/// acts on are `length` (cut short) and `content_filter`.
[[nodiscard]] std::string_view openai_finish_reason(harness::FinishReason reason) noexcept;

/// Whether `type` is a vendor-CLI backend -- a personal subscription login
/// driven as a child process. **Serve refuses these by type**, even when the
/// provider was built: re-serving a subscription to remote REST clients is the
/// "third-party product routes subscription credentials" line the vendor-CLI
/// design notes draw, and likely hostile to the vendors' terms.
[[nodiscard]] bool is_vendor_cli(harness::BackendType type) noexcept;

class SseReporter;

struct HandlerOptions {
    /// Backend keys a request may name. Computed by the serve command: the
    /// default (or `-m`) alone, or every constructed non-CLI backend under
    /// `--all-backends`.
    std::vector<std::string> served;

    /// The backend a request without a `model` lands on.
    std::string default_backend;

    /// Configured backends that failed to construct, with the reason, so a
    /// request naming one gets a 503 that says why rather than a 400 that
    /// says "not served".
    std::map<std::string, std::string> unavailable;

    /// Retrieval, fixed for the server's lifetime. Empty means none.
    std::string rag_collection;
    commands::RagSource rag_source = commands::RagSource::None;
    int rag_limit = 4;

    /// Server-wide `--retriever` / `--rerank`. A request's `?retriever=` /
    /// `?rerank=` query parameter overrides them for that turn, through the
    /// same resolver.
    std::string retriever;
    std::string rerank;

    std::chrono::minutes session_ttl{60};
};

class Handler {
public:
    /// `tools` may be null: no tool is ever advertised, whatever a request's
    /// `tool_mode` says. Non-owning; both outlive the handler.
    Handler(const harness::Harness& harness, HandlerOptions options,
            const agent::ToolRegistry* tools);

    [[nodiscard]] HttpResponse chat_completions(const HttpRequest& request);
    [[nodiscard]] HttpResponse completions(const HttpRequest& request);
    [[nodiscard]] HttpResponse list_models(const HttpRequest& request);
    [[nodiscard]] HttpResponse model_status(const HttpRequest& request);
    [[nodiscard]] HttpResponse health(const HttpRequest& request);
    [[nodiscard]] HttpResponse list_sessions(const HttpRequest& request);
    [[nodiscard]] HttpResponse get_session(const HttpRequest& request, std::string_view id);
    [[nodiscard]] HttpResponse delete_session(const HttpRequest& request, std::string_view id);

    [[nodiscard]] SessionStore& sessions() noexcept {
        return sessions_;
    }

    [[nodiscard]] const HandlerOptions& options() const noexcept {
        return options_;
    }

    /// Resolves a request's `model` to a served backend key, or throws the
    /// error the request should get. Public so the rule is testable on its
    /// own: unknown → 400 naming what is served; vendor CLI → 400 saying why;
    /// configured but unbuilt → 503 with the backend's own reason.
    [[nodiscard]] std::string resolve_served(std::string_view model) const;

private:
    struct ChatCall;
    struct TurnPlan;
    struct TurnOutcome;

    [[nodiscard]] TurnPlan plan_turn(const ChatCall& call);
    [[nodiscard]] TurnOutcome run_turn(TurnPlan& plan, agentloop::Reporter& reporter,
                                       SseReporter* sse,
                                       const harness::CancellationToken& cancellation);
    void stream_turn(TurnPlan& plan, const WriteFn& write);

    const harness::Harness* harness_;
    HandlerOptions options_;
    const agent::ToolRegistry* tools_;
    SessionStore sessions_;
    std::mutex turn_mutex_;
};

}  // namespace apogee::httpserver
