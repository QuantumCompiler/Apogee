#include "cli/interrupt.h"

#include <catch2/catch_test_macros.hpp>

#include <csignal>

#include "cli/helpers.h"
#include "platform/termination.h"

/// SIGTERM joins Ctrl-C (57g): a scope catches it as the same cancel while
/// it is free, a command it cancelled exits 143 where Ctrl-C's exits 130,
/// and what was there before is put back -- while a handler someone else
/// stands keeps SIGTERM, untouched. Each case stands SIGTERM where it needs
/// it and puts back what the test runner had there.
#if !defined(_WIN32)
namespace {

using apogee::commands::InterruptScope;
using apogee::commands::kCancelled;
using apogee::commands::kTerminated;

volatile std::sig_atomic_t g_recorded = 0;

void record_signal(int signal) {
    g_recorded = signal;
}

/// `signal` set to `action` while it lives; what was there before put back.
class Standing {
public:
    Standing(int signal, void (*action)(int)) : signal_{signal} {
        previous_ = std::signal(signal_, action);
    }

    ~Standing() {
        (void)std::signal(signal_, previous_);
    }

    Standing(const Standing&) = delete;
    Standing& operator=(const Standing&) = delete;
    Standing(Standing&&) = delete;
    Standing& operator=(Standing&&) = delete;

private:
    int signal_;
    void (*previous_)(int) = nullptr;
};

/// The action SIGTERM has now, read without changing it.
[[nodiscard]] void (*current_term_action())(int) {
    void (*action)(int) = std::signal(SIGTERM, SIG_DFL);
    (void)std::signal(SIGTERM, action);
    return action;
}

}  // namespace

TEST_CASE("SIGTERM cancels a scope as Ctrl-C does, and the command it cancelled exits 143",
          "[commands][interrupt][sigterm]") {
    // The test runner stands its own handler; a command's process has none.
    const Standing free_term{SIGTERM, SIG_DFL};
    REQUIRE(apogee::platform::termination_signal_free());
    {
        const InterruptScope scope;
        // Armed: raising it now reaches the scope, never the default action.
        REQUIRE(current_term_action() != SIG_DFL);
        CHECK_FALSE(InterruptScope::token().stop_requested());
        (void)std::raise(SIGTERM);
        CHECK(InterruptScope::token().stop_requested());
    }
    // Put back as it was, and the cancel it caused said as 143 -- once.
    CHECK(current_term_action() == SIG_DFL);
    CHECK(InterruptScope::exit_code(kCancelled) == kTerminated);
    CHECK(InterruptScope::exit_code(kCancelled) == kCancelled);
    // A code that is not a cancel is the command's own.
    {
        const InterruptScope scope;
        REQUIRE(current_term_action() != SIG_DFL);
        (void)std::raise(SIGTERM);
    }
    CHECK(InterruptScope::exit_code(2) == 2);
}

TEST_CASE("Ctrl-C still exits 130, and a fresh scope forgets an earlier scope's signal",
          "[commands][interrupt][sigterm]") {
    const Standing free_term{SIGTERM, SIG_DFL};
    {
        const InterruptScope scope;
        REQUIRE(current_term_action() != SIG_DFL);
        (void)std::raise(SIGTERM);
    }
    {
        const InterruptScope scope;
        (void)std::raise(SIGINT);
        CHECK(InterruptScope::token().stop_requested());
    }
    CHECK(InterruptScope::exit_code(kCancelled) == kCancelled);
}

TEST_CASE("a scope made inside a live one joins it: one token, a stop between stages kept",
          "[commands][interrupt][chain]") {
    // 57i: a register chain holds one scope across its stages while each
    // stage's verb keeps its own. A nested scope re-arming the token lost a
    // Ctrl-C made between two stages, and a copy of the outer token missed
    // every signal after a stage's scope had come and gone.
    const Standing free_term{SIGTERM, SIG_DFL};
    void (*const before)(int) = std::signal(SIGINT, SIG_DFL);
    (void)std::signal(SIGINT, before);
    {
        const InterruptScope chain;
        const apogee::harness::CancellationToken outer = InterruptScope::token();
        {
            const InterruptScope stage;
            CHECK_FALSE(InterruptScope::token().stop_requested());
        }
        // The stage's scope gone, the chain's handler still stands.
        void (*const standing)(int) = std::signal(SIGINT, SIG_DFL);
        (void)std::signal(SIGINT, standing);
        CHECK(standing != before);
        // A Ctrl-C between two stages: the next stage's scope starts stopped,
        // and the chain's own copy of the token sees it.
        (void)std::raise(SIGINT);
        {
            const InterruptScope next;
            CHECK(InterruptScope::token().stop_requested());
        }
        CHECK(outer.stop_requested());
    }
    // The outermost scope put back what was there.
    void (*const after)(int) = std::signal(SIGINT, SIG_DFL);
    (void)std::signal(SIGINT, after);
    CHECK(after == before);
    CHECK(InterruptScope::exit_code(kCancelled) == kCancelled);
    // A scope made afterwards arms afresh.
    {
        const InterruptScope fresh;
        CHECK_FALSE(InterruptScope::token().stop_requested());
    }
}

TEST_CASE("a SIGTERM someone else stands -- a session's watch, serve -- is left to them",
          "[commands][interrupt][sigterm]") {
    g_recorded = 0;
    const Standing theirs{SIGTERM, record_signal};
    CHECK_FALSE(apogee::platform::termination_signal_free());
    {
        const InterruptScope scope;
        CHECK(current_term_action() == record_signal);
        (void)std::raise(SIGTERM);
        CHECK(g_recorded == SIGTERM);
        CHECK_FALSE(InterruptScope::token().stop_requested());
    }
    CHECK(current_term_action() == record_signal);
    CHECK(InterruptScope::exit_code(kCancelled) == kCancelled);
}
#endif

/// 57j: the Microsoft C runtime puts SIGINT back to its default action before
/// it calls the handler, so until the handler stood itself again a second
/// Ctrl-C ended a Windows process -- a pull's staging left behind -- where
/// POSIX counts it. Every platform, so CI's Windows rows run it: before the
/// fix, the second raise ended the test binary there.
TEST_CASE("two Ctrl-Cs inside a scope leave the process running with both counted",
          "[commands][interrupt][windows][57j]") {
    using apogee::commands::InterruptScope;
    void (*const before)(int) = std::signal(SIGINT, SIG_DFL);
    (void)std::signal(SIGINT, before);
    {
        const InterruptScope scope;
        CHECK(InterruptScope::signals_caught() == 0);
        (void)std::raise(SIGINT);
        (void)std::raise(SIGINT);
        CHECK(InterruptScope::token().stop_requested());
        CHECK(InterruptScope::signals_caught() == 2);
        // Still standing for the third, which exits.
        void (*const standing)(int) = std::signal(SIGINT, SIG_DFL);
        (void)std::signal(SIGINT, standing);
        CHECK(standing != SIG_DFL);
        CHECK(standing != before);
    }
    // What was there before is back.
    void (*const after)(int) = std::signal(SIGINT, SIG_DFL);
    (void)std::signal(SIGINT, after);
    CHECK(after == before);
    CHECK(InterruptScope::exit_code(apogee::commands::kCancelled) == apogee::commands::kCancelled);
}
