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

#include "backends/jsonl_framer.h"
#include "mcp/types.h"
#include "platform/child_process.h"

/// How frames reach a server and come back.
///
/// `Transport` is an interface so the client and the registry never know
/// whether a process is behind it: the test fleet is a set of scripted
/// transports, and a remote transport -- out of scope here -- would be one
/// more implementation and nothing else.
///
/// `StdioTransport` is the one that ships: a child on pipes, newline-delimited
/// JSON on stdout, and its stderr **captured, never inherited**. The sink is
/// mandatory in the constructor. Ommi kept an inheriting constructor beside
/// the safe one "for compatibility", and its own postmortem records what an
/// inherited stderr did to the terminal; this API has no such door.
namespace apogee::mcp {

class Transport {
public:
    Transport() = default;
    virtual ~Transport() = default;
    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;
    Transport(Transport&&) = delete;
    Transport& operator=(Transport&&) = delete;

    /// Writes one frame. False when the far end is gone.
    [[nodiscard]] virtual bool send(const nlohmann::json& message) = 0;

    /// The next complete line, or nullopt when the transport is closed or the
    /// far end has gone -- which is the read loop's signal to release every
    /// pending call. Blocks up to `timeout` first; a timeout is an empty
    /// string, so the caller can check for cancellation between waits.
    [[nodiscard]] virtual std::optional<std::string> recv(std::chrono::milliseconds timeout) = 0;

    /// Whatever the far end said on stderr, bounded, for a failure message.
    [[nodiscard]] virtual std::string stderr_tail() const = 0;

    virtual void close() = 0;
};

inline constexpr std::size_t kStderrTailLines = 8;

/// Keeps the last `kStderrTailLines` non-empty lines and tees every byte to
/// an optional sink -- outside its lock, so a slow sink never blocks the
/// reader. What makes discarding a server's stderr safe: the output is
/// dropped while things work and surfaced when they do not.
class StderrTail {
public:
    using Sink = std::function<void(std::string_view)>;

    explicit StderrTail(Sink sink = nullptr);

    void write(std::string_view bytes);

    /// The retained lines joined with ` | `, plus any unterminated partial.
    [[nodiscard]] std::string tail() const;

private:
    Sink sink_;
    mutable std::mutex mutex_;
    std::deque<std::string> lines_;
    std::string partial_;
};

/// ` -- stderr: a | b | c`, or empty when nothing was written.
[[nodiscard]] std::string stderr_note(const std::string& tail);

class StdioTransport final : public Transport {
public:
    /// Takes ownership of a started child. `sink` may be null (discard); the
    /// tail is kept either way.
    StdioTransport(std::unique_ptr<platform::ChildProcess> child, StderrTail::Sink sink);
    ~StdioTransport() override;
    StdioTransport(const StdioTransport&) = delete;
    StdioTransport& operator=(const StdioTransport&) = delete;
    StdioTransport(StdioTransport&&) = delete;
    StdioTransport& operator=(StdioTransport&&) = delete;

    [[nodiscard]] bool send(const nlohmann::json& message) override;
    [[nodiscard]] std::optional<std::string> recv(std::chrono::milliseconds timeout) override;
    [[nodiscard]] std::string stderr_tail() const override;
    void close() override;

private:
    void drain_stderr();

    std::unique_ptr<platform::ChildProcess> child_;
    StderrTail tail_;
    std::mutex write_mutex_;
    backends::JsonlFramer framer_;
    std::deque<std::string> lines_;
    bool closed_ = false;
    bool eof_ = false;
};

/// Spawns `command` with an explicit stderr sink. `error` explains a failure
/// to start. `environment` entries are `KEY=VALUE` and overlay the parent's.
[[nodiscard]] std::unique_ptr<StdioTransport> spawn_stdio(
    const std::string& command, const std::vector<std::string>& arguments,
    const std::vector<std::string>& environment, StderrTail::Sink sink, std::string& error);

}  // namespace apogee::mcp
