#pragma once

#include "contracts/paths.h"

namespace apogee::testing {

/// Bakes `channel` into the library for the life of the object, restoring the
/// previous one on destruction -- a test's way to be another channel's build
/// (M10). The executable bakes its channel once, in main(); a test that
/// changed it and leaked the change would turn every test after it into a
/// dev build's.
class BakedChannelGuard {
public:
    explicit BakedChannelGuard(harness::Channel channel) noexcept;
    ~BakedChannelGuard();

    BakedChannelGuard(const BakedChannelGuard&) = delete;
    BakedChannelGuard& operator=(const BakedChannelGuard&) = delete;
    BakedChannelGuard(BakedChannelGuard&&) = delete;
    BakedChannelGuard& operator=(BakedChannelGuard&&) = delete;

private:
    harness::Channel previous_;
};

}  // namespace apogee::testing
