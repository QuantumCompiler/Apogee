#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>

/// JSON-RPC 2.0 over newline-delimited stdio (56a): the Agent Client
/// Protocol's transport, both ways.
///
/// **One writer owns stdout.** Every frame `apogee acp` sends -- a response, a
/// session update, a request of its own -- goes through `Peer::send`, one JSON
/// object per line, flushed, under one lock: the reader of each session's
/// child and the thread reading the host write from different threads, and
/// stdout carries nothing else. Diagnostics go to stderr.
///
/// **Requests go both ways, with ids.** The host asks (`initialize`,
/// `session/prompt`); this side asks too -- `session/request_permission`,
/// `elicitation/create` -- and `Peer::ask` routes the host's answer back to
/// whoever asked, by id.
///
/// **Tolerant of what it does not know.** An unknown field is ignored; a line
/// that is not a message is answered with an error and the process lives on.
namespace apogee::acp {

/// The JSON-RPC error codes ACP uses.
inline constexpr int kParseError = -32700;
inline constexpr int kInvalidRequest = -32600;
inline constexpr int kMethodNotFound = -32601;
inline constexpr int kInvalidParams = -32602;
inline constexpr int kInternalError = -32603;

/// One line from the host, classified.
struct Frame {
    enum class Kind : std::uint8_t {
        /// A method with an id: answered with a response.
        Request,
        /// A method with no id: never answered.
        Notification,
        /// An answer to a request this side sent: `result` or `error`.
        Response,
        /// JSON, but no message: answered with `kInvalidRequest`.
        Invalid,
        /// Not JSON at all: answered with `kParseError`.
        Unparsable,
    };

    Kind kind = Kind::Unparsable;
    /// The request's or response's id, as sent; null when there is none.
    nlohmann::json id = nullptr;
    std::string method{};
    /// The params as sent -- an object for every ACP method; null when absent.
    nlohmann::json params = nullptr;
    nlohmann::json result = nullptr;
    /// A response's error object, or null.
    nlohmann::json error = nullptr;
    /// Why an Invalid or Unparsable line is not a message, in words.
    std::string problem{};
};

/// Classifies one line. Never throws: anything that is not a message is a
/// frame saying why.
[[nodiscard]] Frame parse_frame(std::string_view line);

/// A frame as one line, without its newline -- never a throw for its bytes:
/// what is not UTF-8 is said as U+FFFD, as machine mode's writer does.
[[nodiscard]] std::string frame_text(const nlohmann::json& frame);

[[nodiscard]] nlohmann::json response(const nlohmann::json& id, nlohmann::json result);
[[nodiscard]] nlohmann::json error_response(const nlohmann::json& id, int code,
                                            std::string_view message);
[[nodiscard]] nlohmann::json notification(std::string_view method, nlohmann::json params);
[[nodiscard]] nlohmann::json request(std::int64_t id, std::string_view method,
                                     nlohmann::json params);

/// The host on the other end of stdio: the one writer of stdout, and the
/// requests this side has sent it that wait for an answer.
class Peer {
public:
    /// The host's answer to a request: its `result`, or its `error`, or
    /// neither when it never answered -- it closed stdin first. Called once.
    using Answer = std::function<void(const nlohmann::json* result, const nlohmann::json* error)>;

    /// `out` is stdout in practice, and must carry nothing else.
    explicit Peer(std::ostream& out);

    Peer(const Peer&) = delete;
    Peer& operator=(const Peer&) = delete;
    Peer(Peer&&) = delete;
    Peer& operator=(Peer&&) = delete;
    ~Peer() = default;

    /// Writes `frame` as one line and flushes it.
    void send(const nlohmann::json& frame);

    /// Sends `method` as a request with an id of this side's own, and keeps
    /// `answered` until the host's response with that id arrives -- or the
    /// host goes, which answers it with neither.
    void ask(std::string_view method, nlohmann::json params, Answer answered);

    /// Hands a response to the request it answers, on the calling thread.
    /// False when this side sent no request with its id.
    bool deliver(const Frame& response);

    /// The host is gone: every request still waiting is answered with
    /// neither, and a later `ask` is answered so at once.
    void close();

private:
    std::mutex write_mutex_;
    std::ostream* out_;
    std::mutex pending_mutex_;
    std::map<std::int64_t, Answer> pending_;
    std::int64_t next_id_ = 0;
    bool closed_ = false;
};

}  // namespace apogee::acp
