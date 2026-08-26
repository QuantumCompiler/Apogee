#include "harness/cancellation.h"

#include "harness/errors.h"

namespace apogee::harness {

CancellationToken CancellationToken::create() {
    CancellationToken token;
    token.flag_ = std::make_shared<std::atomic<bool>>(false);
    return token;
}

void CancellationToken::cancel() const noexcept {
    if (flag_) {
        flag_->store(true, std::memory_order_relaxed);
    }
}

bool CancellationToken::stop_requested() const noexcept {
    return flag_ && flag_->load(std::memory_order_relaxed);
}

void CancellationToken::throw_if_cancelled() const {
    if (stop_requested()) {
        throw CancelledError();
    }
}

}  // namespace apogee::harness
