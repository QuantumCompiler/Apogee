#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <istream>
#include <map>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "acp/jsonrpc.h"
#include "acp/session.h"

/// `apogee acp`'s agent (56a): the methods an ACP host calls, on this
/// process's own stdin and stdout -- pipes, never a port.
///
///   - `initialize` answers protocol version 1, `agentInfo`, and what this
///     agent takes: `session/load`, and `session/list`, `session/resume` and
///     `session/close` (56c), text and `resource_link` prompts, no MCP over
///     HTTP or SSE, and no sign-in (`authMethods` is empty: a vendor CLI
///     signs itself in). A host asking for another version is answered 1.
///   - `session/new` starts a session (`acp/session.h`) in the request's
///     `cwd` -- an absolute path to a folder that exists, else `-32602` --
///     and ends the one before it: one live session per process, so one
///     model in memory -- and a replaced session whose child has ended is let
///     go. Its `mcpServers` are that session's (56b): the stdio servers
///     connected by its child for its life, beside the config's and through
///     the same gate; an http or sse one set aside and named in its notice.
///     Its id is the chat's (56c), saved with its first answered turn.
///   - `session/load` and `session/resume` (56c) open a saved chat the same
///     way, its child started with `--resume` -- any saved chat's id, since
///     the store is one, and its id alone: a name, a title or a path, which
///     no chat's id is, is `-32002` as an id no chat has is, naming it. A
///     load replays the chat's turns before it answers. A session of that id
///     this process still holds is ended, and its child waited for, first.
///   - `session/list` (56c) answers the chats an ACP host began, newest
///     first, filtered to an exact `cwd`; one page.
///   - `session/close` (56c) cancels the session's turn and ends it; its
///     chat is already saved. An unknown or closed session answers success.
///   - `session/prompt`, `session/cancel`, `session/set_mode` and
///     `session/set_config_option` reach the session they name; a session a
///     newer one replaced, or the host closed, says so.
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
    /// A session held, and whether it is still the one its id names: a
    /// replaced, closed or reopened one is retired, kept only until its child
    /// has ended and the host holds no question of it.
    struct Held {
        std::unique_ptr<Session> session;
        bool retired = false;
    };

    void dispatch(const Frame& frame);
    void notify(const Frame& frame);
    void initialize(const Frame& frame);
    /// `session/new`, `session/load` and `session/resume`: one opening.
    void open_session(const Frame& frame, OpenAs as);
    void list_sessions(const Frame& frame);
    void close_session(const Frame& frame);
    /// The folder a request names, or empty with its error sent.
    [[nodiscard]] std::string folder(const Frame& frame);
    /// The session a request names, or null with its error sent.
    [[nodiscard]] Session* named(const Frame& frame);
    /// Retires `held`, saying why a request naming it is refused, and ends
    /// its session.
    void retire(Held& held, std::string why);

    AgentSetup setup_;
    Peer peer_;
    Diagnostics diagnostics_;
    HostCaps host_;
    std::vector<Held> sessions_;
    /// Why each retired session's id is refused, until a session reopens it.
    std::map<std::string, std::string, std::less<>> ended_;
    bool finished_ = false;
};

/// The whole verb: `setup`'s agent, reading `in` -- stdin -- line by line
/// until it ends, answering on `out`, diagnostics on `err`. Returns 0 once
/// every session's child has ended.
int serve_acp(AgentSetup setup, std::istream& in, std::ostream& out, std::ostream& err);

}  // namespace apogee::acp
