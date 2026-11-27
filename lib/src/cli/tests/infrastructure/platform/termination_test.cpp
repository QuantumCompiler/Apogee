#include "platform/termination.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>

/// A session asked to end by a signal (57a): the first SIGTERM or SIGHUP
/// calls `end` on the watch's own thread, and the watch's end raises the
/// signal again on the action that was there before; a session that does not
/// end within the grace, or a second signal, calls `force` and raises it at
/// once. Each test stands a recording handler where the default would end the
/// test run, so what the watch raises again is seen rather than obeyed.
#if !defined(_WIN32)
namespace {

using apogee::platform::TerminationWatch;
using namespace std::chrono_literals;

volatile std::sig_atomic_t g_recorded = 0;
volatile std::sig_atomic_t g_recorded_count = 0;

extern "C" void record_signal(int signal) {
    g_recorded = signal;
    g_recorded_count = g_recorded_count + 1;
}

/// `signal` recorded, never obeyed, while it lives; what was there before put
/// back after.
class Recording {
public:
    explicit Recording(int signal) : signal_{signal} {
        g_recorded = 0;
        g_recorded_count = 0;
        previous_ = std::signal(signal_, record_signal);
    }

    ~Recording() {
        (void)std::signal(signal_, previous_);
    }

    Recording(const Recording&) = delete;
    Recording& operator=(const Recording&) = delete;
    Recording(Recording&&) = delete;
    Recording& operator=(Recording&&) = delete;

private:
    int signal_;
    void (*previous_)(int) = nullptr;
};

/// Waits up to five seconds for `done`.
[[nodiscard]] bool within(const std::atomic<bool>& done) {
    for (int i = 0; i < 500 && !done.load(); ++i) {
        std::this_thread::sleep_for(10ms);
    }
    return done.load();
}

}  // namespace

TEST_CASE("a SIGTERM ends the session on the watch's thread, then is raised again",
          "[platform][termination]") {
    REQUIRE(TerminationWatch::supported());
    const Recording recording{SIGTERM};
    std::atomic<bool> ended{false};
    std::atomic<bool> forced{false};
    {
        TerminationWatch watch{
            TerminationWatch::Hooks{.end = [&ended]() { ended.store(true); },
                                    .force = [&forced]() { forced.store(true); }},
            10s};
        (void)std::raise(SIGTERM);
        REQUIRE(within(ended));
        CHECK(watch.caught() == SIGTERM);
        // Caught, not obeyed: nothing reached the action before the watch.
        CHECK(g_recorded == 0);
    }
    // The watch's end raises it on the action it had before.
    CHECK(g_recorded == SIGTERM);
    CHECK(g_recorded_count == 1);
    CHECK_FALSE(forced.load());
}

TEST_CASE("a session that does not end within the grace is forced, and the signal raised at once",
          "[platform][termination]") {
    const Recording recording{SIGHUP};
    std::atomic<bool> forced{false};
    std::atomic<bool> ended{false};
    {
        TerminationWatch watch{
            TerminationWatch::Hooks{.end = [&ended]() { ended.store(true); },
                                    .force = [&forced]() { forced.store(true); }},
            50ms};
        (void)std::raise(SIGHUP);
        REQUIRE(within(forced));
        CHECK(ended.load());
        // Raised on the action before by the watch's own thread...
        for (int i = 0; i < 500 && g_recorded == 0; ++i) {
            std::this_thread::sleep_for(10ms);
        }
        CHECK(g_recorded == SIGHUP);
    }
    // ...and not again as it ends.
    CHECK(g_recorded_count == 1);
}

TEST_CASE("a second signal forces the end without waiting out the grace",
          "[platform][termination]") {
    const Recording recording{SIGTERM};
    std::atomic<bool> ended{false};
    std::atomic<bool> forced{false};
    const auto started = std::chrono::steady_clock::now();
    {
        TerminationWatch watch{
            TerminationWatch::Hooks{.end = [&ended]() { ended.store(true); },
                                    .force = [&forced]() { forced.store(true); }},
            60s};
        (void)std::raise(SIGTERM);
        REQUIRE(within(ended));
        (void)std::raise(SIGTERM);
        REQUIRE(within(forced));
    }
    CHECK(std::chrono::steady_clock::now() - started < 30s);
}

TEST_CASE("a watch that caught nothing leaves the handlers as they were, and raises nothing",
          "[platform][termination]") {
    const Recording recording{SIGTERM};
    std::atomic<bool> ended{false};
    {
        const TerminationWatch watch{
            TerminationWatch::Hooks{.end = [&ended]() { ended.store(true); }, .force = {}}, 1s};
        CHECK(watch.caught() == 0);
    }
    CHECK_FALSE(ended.load());
    CHECK(g_recorded == 0);
    // The recording handler is back: a signal now reaches it.
    (void)std::raise(SIGTERM);
    CHECK(g_recorded == SIGTERM);
}

TEST_CASE("SIGINT is caught only while asked, and one watch at a time catches anything",
          "[platform][termination]") {
    const Recording recording{SIGINT};
    std::atomic<bool> ended{false};
    {
        TerminationWatch watch{
            TerminationWatch::Hooks{.end = [&ended]() { ended.store(true); }, .force = {}}, 10s};
        // A second watch, made while this one lives, watches nothing.
        const TerminationWatch second{TerminationWatch::Hooks{}, 10s};
        CHECK(second.caught() == 0);

        // Not asked: SIGINT does what it did before.
        (void)std::raise(SIGINT);
        CHECK(g_recorded == SIGINT);
        g_recorded = 0;
        g_recorded_count = 0;

        watch.catch_interrupt(true);
        (void)std::raise(SIGINT);
        REQUIRE(within(ended));
        CHECK(watch.caught() == SIGINT);
        CHECK(g_recorded == 0);
        watch.catch_interrupt(false);
    }
    CHECK(g_recorded == SIGINT);
    CHECK(g_recorded_count == 1);
}

TEST_CASE("an ignored signal stays ignored: the watch never catches it",
          "[platform][termination]") {
    // A process started to outlive its terminal (`nohup`) has SIGHUP
    // ignored, and a closed window must not end its session.
    void (*const previous)(int) = std::signal(SIGHUP, SIG_IGN);
    std::atomic<bool> ended{false};
    {
        const TerminationWatch watch{
            TerminationWatch::Hooks{.end = [&ended]() { ended.store(true); }, .force = {}}, 10s};
        (void)std::raise(SIGHUP);
        std::this_thread::sleep_for(100ms);
        CHECK(watch.caught() == 0);
    }
    CHECK_FALSE(ended.load());
    CHECK(std::signal(SIGHUP, previous) == SIG_IGN);
}
#endif
