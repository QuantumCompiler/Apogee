#pragma once

#include <nlohmann/json.hpp>

#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mcp/registry.h"
#include "mcp/transport.h"

/// The scripted server fleet: `Transport`s that behave like the servers the
/// client must survive, with no process behind them.
///
/// Each personality answers the handshake its own way -- `chatty` narrates
/// on stderr and then behaves, `slow` never answers, `dying` says one thing
/// on stderr and hangs up mid-handshake, `malformed` interleaves junk frames
/// with valid ones, `erroring` answers every call with `isError`. A `well`
/// server does everything right and records what it was sent.
namespace apogee::testing {

class FakeMcpTransport final : public mcp::Transport {
public:
    enum class Personality { Well, Chatty, Slow, Dying, Malformed, Erroring, SlowCalls };

    explicit FakeMcpTransport(Personality personality, std::string name = "fake");

    [[nodiscard]] bool send(const nlohmann::json& message) override;
    [[nodiscard]] std::optional<std::string> recv(std::chrono::milliseconds timeout) override;
    [[nodiscard]] std::string stderr_tail() const override;
    void close() override;

    /// Every frame the client sent, in order.
    [[nodiscard]] std::vector<nlohmann::json> sent() const;
    [[nodiscard]] bool closed() const;

    /// The tools this server advertises: `echo` (read-only) and `write`
    /// (no annotation). Shared so tests can assert names.
    [[nodiscard]] static nlohmann::json tools_list();

private:
    void reply(const nlohmann::json& request);

    Personality personality_;
    std::string name_;
    mutable std::mutex mutex_;
    std::deque<std::string> outbox_;
    std::vector<nlohmann::json> sent_;
    mcp::StderrTail tail_;
    bool closed_ = false;
    bool hung_up_ = false;
};

/// A `RegistryOptions::spawn` that hands out fakes by server name:
/// the spec's `command` names the personality (`well`, `chatty`, `slow`,
/// `dying`, `malformed`, `erroring`, or `missing` for a spawn failure).
[[nodiscard]] std::function<std::unique_ptr<mcp::Transport>(const mcp::ServerSpec&,
                                                            mcp::StderrTail::Sink, std::string&)>
fake_fleet(std::vector<std::shared_ptr<FakeMcpTransport>>* made = nullptr);

}  // namespace apogee::testing
