#include "httpserver/shutdown.h"

#include <atomic>

namespace apogee::httpserver {
namespace {

std::atomic<bool> requested{false};

}  // namespace

void request_shutdown() noexcept {
    requested.store(true);
}

void clear_shutdown() noexcept {
    requested.store(false);
}

bool shutdown_requested() noexcept {
    return requested.load();
}

}  // namespace apogee::httpserver
