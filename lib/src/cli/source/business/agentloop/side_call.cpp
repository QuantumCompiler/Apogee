#include "agentloop/side_call.h"

#include <array>
#include <cstdio>
#include <exception>
#include <utility>

namespace apogee::agentloop {

SideCallScope::SideCallScope(SideCallSink sink, std::string role, std::string detail)
    : sink_{std::move(sink)}, began_{std::chrono::steady_clock::now()} {
    call_.role = std::move(role);
    call_.detail = std::move(detail);
    if (sink_) {
        sink_(call_);
    }
}

SideCallScope::~SideCallScope() {
    if (!sink_) {
        return;
    }
    call_.done = true;
    call_.seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - began_).count();
    try {
        sink_(call_);
    } catch (const std::exception&) {
        // Saying it is over is display; it must never throw out of a
        // destructor, and never fail the call it describes.
        return;
    }
}

std::string side_call_suffix(const SideCall& call) {
    std::string out;
    if (call.seconds.has_value()) {
        std::array<char, 32> text{};
        std::snprintf(text.data(), text.size(), "%.1f s", *call.seconds);
        out += std::string{" · "} + text.data();
    }
    if (call.tokens.has_value()) {
        out += " · " + std::to_string(*call.tokens) + " tokens";
    }
    return out;
}

}  // namespace apogee::agentloop
