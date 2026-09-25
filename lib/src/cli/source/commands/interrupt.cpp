#include "commands/interrupt.h"

#include <atomic>
#include <csignal>
#include <cstdlib>

#include "commands/helpers.h"

namespace apogee::commands {
namespace {

harness::CancellationToken g_interrupt_token;
std::atomic<int> g_interrupt_count{0};

void on_interrupt(int /*signal*/) {
    g_interrupt_token.cancel();
    if (g_interrupt_count.fetch_add(1) >= 2) {
        std::_Exit(kCancelled);
    }
}

}  // namespace

InterruptScope::InterruptScope() {
    g_interrupt_token = harness::CancellationToken::create();
    g_interrupt_count.store(0);
    previous_ = std::signal(SIGINT, on_interrupt);
}

InterruptScope::~InterruptScope() {
    std::signal(SIGINT, previous_);
}

const harness::CancellationToken& InterruptScope::token() noexcept {
    return g_interrupt_token;
}

}  // namespace apogee::commands
