#pragma once

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "harness/cancellation.h"
#include "mcp/transport.h"
#include "mcp/types.h"

/// One connected server: the handshake, the cached tool list, and calls
/// demultiplexed by id by a background read loop.
///
/// **`done` closes from the read loop's error path, not only from `close()`.**
/// The moment the transport reports the far end gone, every pending call is
/// released with "connection closed" -- so a server that dies mid-handshake
/// fails at once instead of holding the caller (and the status line) for the
/// whole connect timeout. Ommi's registry tests dropped from 50 s to 2 s the
/// day that was fixed. `mark_done` is idempotent: a server dying while the
/// caller is closing it must not race.
namespace apogee::mcp {

struct ClientOptions {
    /// Bounds the whole handshake -- connect, initialize, tools/list.
    std::chrono::milliseconds connect_timeout{20000};
    std::chrono::milliseconds call_timeout{60000};
    /// Diagnostics -- a notification, an unexpected id, a bad frame. Null
    /// means dropped. The registry points this at the caller's progress
    /// writer during startup only.
    std::function<void(std::string_view)> log;
    std::string client_version = "0";
};

class Client {
public:
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    Client(Client&&) = delete;
    Client& operator=(Client&&) = delete;

    /// Starts the read loop, runs the handshake, caches the tools. Null with
    /// `error` set on any failure -- the transport is closed and its stderr
    /// tail folded into the message.
    [[nodiscard]] static std::unique_ptr<Client> connect(std::string name,
                                                         std::unique_ptr<Transport> transport,
                                                         const ClientOptions& options,
                                                         std::string& error);

    [[nodiscard]] const std::string& name() const noexcept {
        return name_;
    }

    [[nodiscard]] const std::vector<ToolInfo>& tools() const noexcept {
        return tools_;
    }

    /// What the server answered in `initialize`.
    [[nodiscard]] const std::string& protocol_version() const noexcept {
        return protocol_version_;
    }

    [[nodiscard]] const std::string& server_name() const noexcept {
        return server_name_;
    }

    /// `tools/call`. A transport failure or timeout is an error outcome too:
    /// the model reads it and moves on.
    [[nodiscard]] ToolCallResult call_tool(std::string_view tool, std::string_view arguments_json,
                                           const harness::CancellationToken& cancellation);

    [[nodiscard]] bool done() const noexcept {
        return done_.load();
    }

    [[nodiscard]] std::string stderr_tail() const;

    void close();

private:
    Client(std::string name, std::unique_ptr<Transport> transport, ClientOptions options);

    struct CallOutcome {
        std::optional<IncomingMessage> response;
        std::string error;
    };

    [[nodiscard]] CallOutcome call(std::string_view method, nlohmann::json params,
                                   std::chrono::steady_clock::time_point deadline,
                                   const harness::CancellationToken* cancellation);
    void read_loop();
    void mark_done();
    void log(const std::string& line) const;

    std::string name_;
    std::unique_ptr<Transport> transport_;
    ClientOptions options_;
    std::vector<ToolInfo> tools_;
    std::string protocol_version_;
    std::string server_name_;

    std::atomic<std::int64_t> next_id_{1};
    std::atomic<bool> done_{false};
    std::atomic<bool> closing_{false};
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::map<std::int64_t, std::optional<IncomingMessage>> pending_;
    std::thread reader_;
};

}  // namespace apogee::mcp
