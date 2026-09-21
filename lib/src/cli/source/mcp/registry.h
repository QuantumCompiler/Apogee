#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "agent/tool.h"
#include "mcp/client.h"
#include "mcp/transport.h"

/// Every configured server, connected once at startup, and its tools
/// registered into the loop's own registry.
///
/// **A bad server is logged and skipped, never fatal**: a broken entry must
/// not keep `chat` from starting. The connect bound covers connect *and*
/// handshake -- a server that connects and never finishes initializing (a
/// proxy waiting on an OAuth flow, say) would otherwise block startup for
/// ever, and with it the GUI's auto-started harness; the bound is the
/// difference between "tools unavailable" and "the whole app never comes
/// up". `[mcp] connecting: <name>` goes out *before* each dial, so the status
/// line moves rather than sitting on the previous server's result.
namespace apogee::mcp {

/// One configured server. The registry's own shape, so the harness config
/// and this package do not have to know each other's types.
struct ServerSpec {
    std::string name;
    std::string command;
    std::vector<std::string> args;
    /// `KEY=VALUE` entries overlaying the parent's environment.
    std::vector<std::string> env;
    bool enabled = true;
};

struct RegistryOptions {
    /// Progress and connection results: `[mcp] connecting: x`, `[mcp]
    /// connected: x (3 tools)`, `[mcp] warning: x: connect failed (skipped):
    /// ...`. Null means silent.
    std::function<void(std::string_view)> status;
    /// Where a server's raw stderr goes. Null means discarded (the tail is
    /// kept either way); `--verbose` points it at the terminal, `serve` at
    /// its own stderr.
    StderrTail::Sink server_log;
    std::chrono::milliseconds connect_timeout{20000};
    std::chrono::milliseconds call_timeout{60000};
    std::string client_version = "0";
    /// How a transport is made for a spec. Null means `spawn_stdio`; a test
    /// hands in the scripted fleet.
    std::function<std::unique_ptr<Transport>(const ServerSpec&, StderrTail::Sink,
                                             std::string& error)>
        spawn;
};

struct ServerStatus {
    std::string name;
    bool enabled = false;
    bool connected = false;
    /// Why it is not connected, when enabled.
    std::string error;
    std::string protocol_version;
    std::string server_name;
    /// Un-namespaced tool names.
    std::vector<std::string> tools;
};

class Registry {
public:
    Registry() = default;
    ~Registry();
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;
    Registry(Registry&&) = delete;
    Registry& operator=(Registry&&) = delete;

    /// Dials every enabled server in order. Never throws for a bad one.
    void connect_all(const std::vector<ServerSpec>& servers, const RegistryOptions& options);

    [[nodiscard]] std::vector<ServerStatus> status() const;
    [[nodiscard]] std::size_t connected_count() const noexcept;

    /// Adds every connected server's tools as `mcp__<server>__<tool>`. A tool
    /// is `writes` unless its `readOnlyHint` is true: a third-party tool with
    /// no annotation is treated as destructive and goes through the gate.
    void register_into(agent::ToolRegistry& registry) const;

    /// A direct call, for `mcp test`: the same path a dispatched tool takes.
    [[nodiscard]] ToolCallResult call(std::string_view namespaced, std::string_view arguments,
                                      std::string& error) const;

    void close_all();

private:
    std::map<std::string, std::shared_ptr<Client>> clients_;
    std::vector<ServerStatus> statuses_;
};

}  // namespace apogee::mcp
