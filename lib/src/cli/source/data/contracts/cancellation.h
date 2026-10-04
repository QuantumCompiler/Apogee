#pragma once

#include <atomic>
#include <memory>

/// Cooperative cancellation for in-flight requests.
///
/// Go's `context.Context` threads cancellation through every call in Ommi;
/// this is the C++ equivalent, minus the deadline and value machinery nothing
/// here needs.
///
/// Copies share one flag, so a token handed to a provider and kept by the
/// caller refer to the same cancellation. Cancelling is safe from any thread --
/// which is the point, since the thread that cancels (a signal handler, a UI
/// thread) is never the thread that streams.
///
/// Cancellation is COOPERATIVE. A provider must check `stop_requested()`
/// between chunks; nothing interrupts a blocked read. That is a deliberate
/// limit: the alternative is killing a thread mid-syscall, which leaks
/// whatever it held.
namespace apogee::harness {

class CancellationToken {
public:
    /// A token that can never be cancelled -- the default for callers that do
    /// not offer cancellation. Costs no allocation.
    CancellationToken() = default;

    /// A live token, allocating the shared flag.
    [[nodiscard]] static CancellationToken create();

    /// Requests cancellation. Thread-safe, idempotent, and a no-op on a
    /// never-cancellable token.
    void cancel() const noexcept;

    /// Whether cancellation has been requested. Cheap enough for a per-token
    /// check inside a streaming loop.
    [[nodiscard]] bool stop_requested() const noexcept;

    /// Throws CancelledError when cancellation has been requested.
    /// The idiomatic check inside a provider's stream loop.
    void throw_if_cancelled() const;

private:
    /// Null on a never-cancellable token, which is why every access is guarded.
    std::shared_ptr<std::atomic<bool>> flag_;
};

}  // namespace apogee::harness
