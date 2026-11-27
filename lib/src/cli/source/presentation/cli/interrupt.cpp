#include "cli/interrupt.h"

#include <atomic>
#include <csignal>
#include <cstdlib>

#include "cli/helpers.h"
#include "platform/platform.h"
#include "platform/termination.h"

namespace apogee::commands {
namespace {

harness::CancellationToken g_interrupt_token;
std::atomic<int> g_interrupt_count{0};
/// The signal that first cancelled the live scope's token, or 0.
std::atomic<int> g_first_signal{0};
/// How many scopes are live: the first arms, the rest join it (57i).
std::atomic<int> g_depth{0};

[[nodiscard]] int exit_code_of(int signal) noexcept {
    return signal == SIGTERM ? kTerminated : kCancelled;
}

void on_interrupt(int signal) {
    // Stood again first, while a scope lives: Windows took the handler down
    // to call it, and the next Ctrl-C must be counted here, not end the
    // process (57j). A no-op on POSIX, whose `signal` keeps it.
    if (g_depth.load() > 0) {
        platform::rearm_signal(signal, on_interrupt);
    }
    int none = 0;
    (void)g_first_signal.compare_exchange_strong(none, signal);
    g_interrupt_token.cancel();
    if (g_interrupt_count.fetch_add(1) >= 2) {
        // No destructor runs past `_Exit`: the console goes back as it was
        // found here, or cmd.exe keeps this run's code page.
        platform::restore_console();
        std::_Exit(exit_code_of(g_first_signal.load()));
    }
}

}  // namespace

InterruptScope::InterruptScope() {
    if (g_depth.fetch_add(1) > 0) {
        // Inside a live scope: its token is this one's, so a cancel already
        // there -- a Ctrl-C between two stages -- is never re-armed away.
        nested_ = true;
        return;
    }
    g_interrupt_token = harness::CancellationToken::create();
    g_interrupt_count.store(0);
    g_first_signal.store(0);
    previous_ = std::signal(SIGINT, on_interrupt);
    if (platform::termination_signal_free()) {
        previous_term_ = std::signal(SIGTERM, on_interrupt);
        term_armed_ = true;
    }
}

InterruptScope::~InterruptScope() {
    g_depth.fetch_sub(1);
    if (nested_) {
        return;
    }
    std::signal(SIGINT, previous_);
    if (term_armed_) {
        std::signal(SIGTERM, previous_term_);
    }
}

const harness::CancellationToken& InterruptScope::token() noexcept {
    return g_interrupt_token;
}

int InterruptScope::signals_caught() noexcept {
    return g_interrupt_count.load();
}

int InterruptScope::exit_code(int code) noexcept {
    const int signal = g_first_signal.exchange(0);
    return code == kCancelled ? exit_code_of(signal) : code;
}

}  // namespace apogee::commands
