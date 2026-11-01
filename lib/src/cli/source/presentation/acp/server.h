#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <istream>
#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "acp/jsonrpc.h"
#include "acp/session.h"

/// `apogee acp`'s agent (56a): the methods an ACP host calls, on this
/// process's own stdin and stdout -- pipes, never a port.
///
///   - `initialize` answers protocol version 1, `agentInfo`, and what this
///     agent takes: no `session/load`, text and `resource_link` prompts, no
///     MCP over HTTP or SSE, and no sign-in (`authMethods` is empty: a vendor
///     CLI signs itself in). A host asking for another version is answered 1.
///   - `session/new` starts a session (`acp/session.h`) in the request's
///     `cwd` -- an absolute path to a folder that exists, else `-32602` --
///     and ends the one before it: one live session per process, so one
///     model in memory -- and a replaced session whose child has ended is let
///     go. Its `mcpServers` are accepted and left unused until Xcode's tools
///     join a session (56b), said on stderr.
///   - `session/prompt`, `session/cancel`, `session/set_mode` and
///     `session/set_config_option` reach the session they name; a session a
///     newer one replaced says so.
///   - Any other method is `-32601`; a line that is not a message is
///     `-32700` or `-32600`; and the process lives on.
namespace apogee::acp {

class Agent {
public:
    Agent(AgentSetup setup, std::ostream& out, std::ostream& err);
    /// Ends every session and waits for their children, if `finish` did not.
    ~Agent();

    Agent(const Agent&) = delete;
    Agent& operator=(const Agent&) = delete;
    Agent(Agent&&) = delete;
    Agent& operator=(Agent&&) = delete;

    /// Handles one line from the host.
    void handle(std::string_view line);

    /// The host closed stdin: every session's turn is cancelled and its
    /// child's stdin closed; returns once they have ended.
    void finish();

    /// The sessions still held: the live one, and each replaced one whose
    /// child has not ended or whose question the host still holds -- the
    /// rest are let go at the next `session/new`.
    [[nodiscard]] std::size_t sessions_held() const noexcept {
        return sessions_.size();
    }

private:
    void dispatch(const Frame& frame);
    void notify(const Frame& frame);
    void initialize(const Frame& frame);
    void open_session(const Frame& frame);
    /// The session a request names, or null with its error sent.
    [[nodiscard]] Session* named(const Frame& frame);

    AgentSetup setup_;
    Peer peer_;
    Diagnostics diagnostics_;
    HostCaps host_;
    std::vector<std::unique_ptr<Session>> sessions_;
    std::set<std::string, std::less<>> replaced_;
    bool finished_ = false;
};

/// The whole verb: `setup`'s agent, reading `in` -- stdin -- line by line
/// until it ends, answering on `out`, diagnostics on `err`. Returns 0 once
/// every session's child has ended.
int serve_acp(AgentSetup setup, std::istream& in, std::ostream& out, std::ostream& err);

}  // namespace apogee::acp
