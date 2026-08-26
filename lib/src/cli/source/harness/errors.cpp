#include "harness/errors.h"

#include <utility>

namespace apogee::harness {

NoAvailableBackendError::NoAvailableBackendError(std::string model, const std::string& message)
    : HarnessError(message), model_{std::move(model)} {}

ProviderNotRegisteredError::ProviderNotRegisteredError(std::string name)
    : HarnessError("no provider registered under '" + name + "'"), name_{std::move(name)} {}

ProviderError::ProviderError(std::string backend, const std::string& message)
    : HarnessError(backend + ": " + message), backend_{std::move(backend)} {}

CancelledError::CancelledError() : HarnessError("request cancelled") {}

}  // namespace apogee::harness
