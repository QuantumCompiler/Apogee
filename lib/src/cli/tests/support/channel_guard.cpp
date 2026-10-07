#include "support/channel_guard.h"

namespace apogee::testing {

BakedChannelGuard::BakedChannelGuard(harness::Channel channel) noexcept
    : previous_{harness::baked_channel()} {
    harness::set_baked_channel(channel);
}

BakedChannelGuard::~BakedChannelGuard() {
    harness::set_baked_channel(previous_);
}

}  // namespace apogee::testing
