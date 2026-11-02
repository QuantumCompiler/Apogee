#include "platform/termination.h"

#include <array>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <mutex>
#include <thread>
#include <utility>

#if !defined(_WIN32)
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace apogee::platform {

#if !defined(_WIN32)
namespace {

// The handler's state: plain data, set before a handler can run. One watch at
// a time owns it.
std::atomic<bool> g_watch_live{false};
volatile std::sig_atomic_t g_write_end = -1;
std::atomic<int> g_caught{0};

constexpr char kSignalled = 1;
constexpr char kStopping = 0;

// Async-signal-safe throughout: a lock-free atomic compare-and-store and
// write(2), errno kept as it was.
extern "C" void termination_signal(int signal) {
    const int saved = errno;
    int none = 0;
    (void)g_caught.compare_exchange_strong(none, signal);
    if (g_write_end >= 0) {
        const char byte = kSignalled;
        (void)::write(g_write_end, &byte, 1);
    }
    errno = saved;
}

/// Installs the handler for `signal`, keeping what was there in `previous`
/// -- unless the signal is ignored: a process started to outlive its terminal
/// (`nohup`) keeps SIGHUP ignored, and the watch never sees it.
void install(int signal, struct ::sigaction& previous) {
    ::sigaction(signal, nullptr, &previous);
    if ((previous.sa_flags & SA_SIGINFO) == 0 && previous.sa_handler == SIG_IGN) {
        return;
    }
    struct ::sigaction action{};
    action.sa_handler = termination_signal;
    sigemptyset(&action.sa_mask);
    // Every other thread's system call goes on as it was: only the watch's
    // own thread learns of the signal.
    action.sa_flags = SA_RESTART;
    ::sigaction(signal, &action, nullptr);
}

/// What the session said last reaches its reader before a signal ends the
/// process, which flushes nothing of its own: stdout and stderr, each only
/// when no other thread holds it -- never `fflush(nullptr)`, which waits on
/// stdin's lock, held for as long as a reader thread blocks reading it.
void flush_output() {
    for (std::FILE* stream : {stdout, stderr}) {
        if (::ftrylockfile(stream) == 0) {
            (void)std::fflush(stream);
            ::funlockfile(stream);
        }
    }
}

/// Raises `signal` on the action it had before the watch, as if the watch had
/// never caught it.
void raise_again(int signal, const struct ::sigaction& previous) {
    ::sigaction(signal, &previous, nullptr);
    ::sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, signal);
    ::pthread_sigmask(SIG_UNBLOCK, &set, nullptr);
    (void)std::raise(signal);
}

}  // namespace

struct TerminationWatch::Impl {
    Hooks hooks;
    std::chrono::milliseconds grace{0};
    bool owner = false;
    std::array<int, 2> pipe{-1, -1};
    struct ::sigaction previous_term{};
    struct ::sigaction previous_hup{};
    struct ::sigaction previous_int{};
    std::mutex interrupt_mutex;
    bool interrupt_on = false;
    /// The watch's own thread raised the signal (`force`): its end raises
    /// nothing more.
    std::atomic<bool> raised{false};
    std::thread watcher;

    [[nodiscard]] const struct ::sigaction& previous_of(int signal) const noexcept {
        if (signal == SIGHUP) {
            return previous_hup;
        }
        if (signal == SIGINT) {
            return previous_int;
        }
        return previous_term;
    }

    /// The next byte on the pipe within `timeout` milliseconds (negative: no
    /// bound), or -1 when the time ran out.
    [[nodiscard]] int next_byte(int timeout) const {
        ::pollfd watched{.fd = pipe[0], .events = POLLIN, .revents = 0};
        for (;;) {
            const int ready = ::poll(&watched, 1, timeout);
            if (ready < 0 && errno == EINTR) {
                continue;
            }
            if (ready <= 0) {
                return -1;
            }
            char byte = kStopping;
            const ::ssize_t got = ::read(pipe[0], &byte, 1);
            if (got < 0 && errno == EINTR) {
                continue;
            }
            return got == 1 ? byte : kStopping;
        }
    }

    /// A hook, on the watch's thread: one that throws ends nothing more.
    static void call(const std::function<void()>& hook) noexcept {
        if (!hook) {
            return;
        }
        try {
            hook();
        } catch (...) {
            (void)0;  // the signal is still raised as the watch ends
        }
    }

    void watch() {
        if (next_byte(-1) != kSignalled) {
            return;  // the watch ended with no signal
        }
        call(hooks.end);
        // The session ends on its own, or the grace runs out, or a second
        // signal says not to wait.
        const auto bound = static_cast<int>(grace.count());
        const int next = next_byte(bound < 0 ? 0 : bound);
        if (next == kStopping) {
            return;
        }
        call(hooks.force);
        const int signal = g_caught.load();
        raised.store(true);
        raise_again(signal, previous_of(signal));
        // Still here: the action before was a handler that returned, or
        // ignore. The watch's end raises nothing more.
    }
};

TerminationWatch::TerminationWatch(Hooks hooks, std::chrono::milliseconds grace)
    : impl_{std::make_unique<Impl>()} {
    impl_->hooks = std::move(hooks);
    impl_->grace = grace;
    bool idle = false;
    if (!g_watch_live.compare_exchange_strong(idle, true)) {
        return;  // another watch owns the signals
    }
    if (::pipe(impl_->pipe.data()) != 0) {
        g_watch_live.store(false);
        return;
    }
    for (const int end : impl_->pipe) {
        ::fcntl(end, F_SETFD, FD_CLOEXEC);
    }
    // The handler never waits on a full pipe.
    ::fcntl(impl_->pipe[1], F_SETFL, ::fcntl(impl_->pipe[1], F_GETFL) | O_NONBLOCK);
    impl_->owner = true;
    g_caught.store(0);
    g_write_end = impl_->pipe[1];
    install(SIGTERM, impl_->previous_term);
    install(SIGHUP, impl_->previous_hup);
    impl_->watcher = std::thread([impl = impl_.get()]() { impl->watch(); });
}

TerminationWatch::~TerminationWatch() {
    if (!impl_->owner) {
        return;
    }
    catch_interrupt(false);
    {
        const char byte = kStopping;
        (void)::write(impl_->pipe[1], &byte, 1);
    }
    if (impl_->watcher.joinable()) {
        impl_->watcher.join();
    }
    ::sigaction(SIGTERM, &impl_->previous_term, nullptr);
    ::sigaction(SIGHUP, &impl_->previous_hup, nullptr);
    g_write_end = -1;
    for (const int end : impl_->pipe) {
        ::close(end);
    }
    const int signal = g_caught.exchange(0);
    g_watch_live.store(false);
    if (signal != 0 && !impl_->raised.load()) {
        flush_output();
        raise_again(signal, impl_->previous_of(signal));
    }
}

void TerminationWatch::catch_interrupt(bool on) {
    if (!impl_->owner) {
        return;
    }
    const std::lock_guard lock{impl_->interrupt_mutex};
    if (on == impl_->interrupt_on) {
        return;
    }
    if (on) {
        install(SIGINT, impl_->previous_int);
    } else {
        ::sigaction(SIGINT, &impl_->previous_int, nullptr);
    }
    impl_->interrupt_on = on;
}

int TerminationWatch::caught() const noexcept {
    return impl_->owner ? g_caught.load() : 0;
}

bool TerminationWatch::supported() noexcept {
    return true;
}

bool termination_signal_free() noexcept {
    struct ::sigaction current{};
    if (::sigaction(SIGTERM, nullptr, &current) != 0) {
        return false;
    }
    return (current.sa_flags & SA_SIGINFO) == 0 && current.sa_handler == SIG_DFL;
}

#else

struct TerminationWatch::Impl {};

TerminationWatch::TerminationWatch(Hooks hooks, std::chrono::milliseconds grace)
    : impl_{std::make_unique<Impl>()} {
    (void)hooks;
    (void)grace;
}

TerminationWatch::~TerminationWatch() = default;

void TerminationWatch::catch_interrupt(bool on) {
    (void)on;
}

int TerminationWatch::caught() const noexcept {
    return 0;
}

bool TerminationWatch::supported() noexcept {
    return false;
}

bool termination_signal_free() noexcept {
    return false;
}

#endif

}  // namespace apogee::platform
