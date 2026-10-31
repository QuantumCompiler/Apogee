#include "contracts/errors.h"

#include <utility>

namespace apogee::harness {

NoAvailableBackendError::NoAvailableBackendError(std::string model, const std::string& message)
    : HarnessError(message), model_{std::move(model)} {}

NoAvailableBackendError::NoAvailableBackendError(std::string model, const std::string& message,
                                                 std::string skipped_backend, std::string reason)
    : HarnessError(message),
      model_{std::move(model)},
      skipped_backend_{std::move(skipped_backend)},
      reason_{std::move(reason)} {}

ProviderNotRegisteredError::ProviderNotRegisteredError(std::string name)
    : HarnessError("no provider registered under '" + name + "'"), name_{std::move(name)} {}

ProviderError::ProviderError(std::string backend, const std::string& message)
    : HarnessError(backend + ": " + message), backend_{std::move(backend)} {}

CancelledError::CancelledError() : HarnessError("request cancelled") {}

}  // namespace apogee::harness
