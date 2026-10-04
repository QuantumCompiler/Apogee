#pragma once

/// The one flag a long-lived stream watches.
///
/// A signal handler cannot take a mutex, so `stop_serving()` cannot close the
/// event bus's subscribers directly; it sets this flag (an atomic, signal-safe)
/// and closes the listening socket. A handler that would otherwise block
/// forever -- the `/v1/admin/events` stream waiting for the next event -- polls
/// it between waits and returns, which is what lets the listener's thread pool
/// join and the process exit on Ctrl-C.
namespace apogee::httpserver {

void request_shutdown() noexcept;
void clear_shutdown() noexcept;
[[nodiscard]] bool shutdown_requested() noexcept;

}  // namespace apogee::httpserver
