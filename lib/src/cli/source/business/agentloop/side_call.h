#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

/// A model call a turn makes besides the chat model's own (26n): the embedder
/// reading the question, the utility model restating a follow-up or
/// summarising a tool result, the rerank judge, the knowledge clerk. Said
/// through the Reporter so each surface decides how -- the terminal draws it
/// inside the thinking block -- and never persisted anywhere.
namespace apogee::agentloop {

struct SideCall {
    /// What kind of call: `embedding`, `utility`, `rerank`, `clerk`.
    std::string role;
    /// What it is doing, for a person: `rewriting the follow-up into a
    /// search query`.
    std::string detail;
    /// False when it starts; true when it is over.
    bool done = false;
    /// What it took, each only when truly known -- never estimated.
    std::optional<double> seconds;
    std::optional<std::int64_t> tokens;
};

using SideCallSink = std::function<void(const SideCall&)>;

/// Says a side call when it starts and, on leaving scope, that it is over
/// with how long it took. A null sink says nothing.
class SideCallScope {
public:
    SideCallScope(SideCallSink sink, std::string role, std::string detail);
    ~SideCallScope();

    SideCallScope(const SideCallScope&) = delete;
    SideCallScope& operator=(const SideCallScope&) = delete;
    SideCallScope(SideCallScope&&) = delete;
    SideCallScope& operator=(SideCallScope&&) = delete;

    /// The tokens it produced, when the call reports them.
    void tokens(std::int64_t count) noexcept {
        call_.tokens = count;
    }

private:
    SideCallSink sink_;
    SideCall call_;
    std::chrono::steady_clock::time_point began_;
};

/// The completion's suffix as the terminal shows it: ` · 0.6 s · 120 tokens`,
/// each part only when known; empty when nothing is.
[[nodiscard]] std::string side_call_suffix(const SideCall& call);

}  // namespace apogee::agentloop
