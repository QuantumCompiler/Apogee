#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

#include "httpserver/handler.h"
#include "httpserver/http_types.h"
#include "httpserver/mux.h"

/// The listener, and the bind policy in front of it.
///
/// **This is the one translation unit in the whole library that may reference
/// `listen()` and `accept()`.** The no-listen symbol check
/// (`tests/scripts/cmake/no_listen_symbols.cmake`) allow-lists this file's object by name
/// and fails the build if any other object -- ours or a third party's -- ever
/// references either. Only `apogee serve` owns a port; that rule is enforced,
/// and this file is its single, reviewed exception.
namespace apogee::httpserver {

struct BindOptions {
    /// Loopback by default. A non-loopback host exposes the inference plane
    /// -- unauthenticated by design, for OpenAI-client compatibility -- to
    /// the network, and is refused unless `allow_remote` says the operator
    /// meant it. Server deployments always pass it; the default exists so
    /// nobody exposes a model by omitting a flag.
    std::string host = "127.0.0.1";
    /// 0 asks the OS for a free port; the chosen one is reported through
    /// `ServeOptions::on_listening`.
    int port = 8080;
    bool allow_remote = false;
};

/// Why `bind` is refused, or empty when it is permitted. **Fail-closed**: an
/// unrecognisable host is not loopback, and so needs `allow_remote`.
[[nodiscard]] std::string bind_refusal(const BindOptions& bind);

/// Whether the mux's request gate checks `Host` under `bind` (57n): on a
/// loopback bind, unless `allow_remote` says the operator's reverse proxy
/// and access control own the name the server is reached by. A same-host
/// proxy forwards the client's `Host` (`api.example.com`), which only that
/// switch lets through; a non-loopback bind needs the switch anyway. Without the
/// check there is no rebinding protection -- a rebinding page's `Origin` is
/// its own `Host` -- and the `Origin` check alone refuses a page from
/// another site.
[[nodiscard]] bool host_check_applies(const BindOptions& bind);

/// The largest request body accepted: images arrive as data URIs.
inline constexpr std::size_t kMaxPayloadBytes = std::size_t{64} * 1024 * 1024;

struct ServeOptions {
    BindOptions bind;
    std::chrono::minutes session_ttl{60};
    std::chrono::seconds sweep_interval{60};
    /// Called once the port is bound, with the actual port.
    std::function<void(const std::string& host, int port)> on_listening;
    /// One line per request, when set.
    std::function<void(std::string_view line)> on_log;
};

/// Serves until SIGINT/SIGTERM or `stop_serving()`.
///
/// Refuses the bind (throws std::runtime_error) when the policy says no or
/// the address cannot be bound; returns normally on a clean stop. Idle
/// sessions are swept on a background thread for as long as it runs. The
/// bind reaches the mux as one fact, `host_check_applies`, which turns its
/// request gate's `Host` check on (57n).
void run_server(Mux& mux, Handler& handler, const ServeOptions& options);

/// Stops the running server, from any thread. Safe when none is running.
void stop_serving() noexcept;

/// Ctrl-C and SIGTERM stop the server while one of these lives --
/// `run_server` holds one for as long as it serves -- and what was there
/// before is put back when it goes.
///
/// Every signal is a stop, however many arrive: a second Ctrl-C is a second
/// (harmless) stop, and an in-flight turn still finishes before the pool
/// joins. On Windows, whose C runtime puts a signal back to its default
/// action before calling the handler, the handler stands itself again
/// (`platform::rearm_signal`), so the second Ctrl-C no longer kills the
/// server mid-turn (57j).
class StopOnSignals {
public:
    StopOnSignals();
    ~StopOnSignals();

    StopOnSignals(const StopOnSignals&) = delete;
    StopOnSignals& operator=(const StopOnSignals&) = delete;
    StopOnSignals(StopOnSignals&&) = delete;
    StopOnSignals& operator=(StopOnSignals&&) = delete;

private:
    void (*previous_int_)(int) = nullptr;
    void (*previous_term_)(int) = nullptr;
};

}  // namespace apogee::httpserver
