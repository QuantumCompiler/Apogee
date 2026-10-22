#pragma once

#include <stdexcept>
#include <string>

/// Typed errors for the harness layer.
///
/// Typed rather than a bare string because callers act differently on them: an
/// unknown backend is a user's typo to report with the list of known names, a
/// provider failure is something a surface may retry or degrade around, and a
/// cancellation is not an error at all in the user's eyes. Collapsing them into
/// one exception type means every call site re-parses a message to tell them
/// apart, which is how error handling rots.
namespace apogee::harness {

/// Base for everything the harness layer throws.
class HarnessError : public std::runtime_error {
public:
    explicit HarnessError(const std::string& message) : std::runtime_error(message) {}
};

/// No backend could serve the requested model.
///
/// Carries the requested name so the surface can name it back to the user
/// alongside what IS configured -- "no backend for 'sonnet'" is only useful
/// with the list of names that would have worked. When the refusal is about a
/// configured backend that could not be started (M26), it carries that
/// backend and the factory's reason too, for a caller that wants them apart
/// from the message.
class NoAvailableBackendError : public HarnessError {
public:
    explicit NoAvailableBackendError(std::string model, const std::string& message);
    NoAvailableBackendError(std::string model, const std::string& message,
                            std::string skipped_backend, std::string reason);

    [[nodiscard]] const std::string& model() const noexcept {
        return model_;
    }

    /// The configured backend the refusal is about, when it could not be
    /// started: the one asked for, or else the default the request fell to.
    /// Empty when neither is one.
    [[nodiscard]] const std::string& skipped_backend() const noexcept {
        return skipped_backend_;
    }

    /// Why `skipped_backend()` could not be started, in the factory's words.
    /// Empty with it.
    [[nodiscard]] const std::string& reason() const noexcept {
        return reason_;
    }

private:
    std::string model_;
    std::string skipped_backend_;
    std::string reason_;
};

/// A backend name was used that is not registered with the harness.
class ProviderNotRegisteredError : public HarnessError {
public:
    explicit ProviderNotRegisteredError(std::string name);

    [[nodiscard]] const std::string& name() const noexcept {
        return name_;
    }

private:
    std::string name_;
};

/// The provider itself failed -- transport error, API rejection, bad response.
///
/// `backend` names the entry that failed, not the model: when two entries share
/// a model name, knowing which one broke is the whole diagnostic.
class ProviderError : public HarnessError {
public:
    ProviderError(std::string backend, const std::string& message);

    [[nodiscard]] const std::string& backend() const noexcept {
        return backend_;
    }

private:
    std::string backend_;
};

/// A request was cancelled through its CancellationToken.
///
/// Distinct from ProviderError on purpose: the user pressed Ctrl-C, and a
/// surface should return quietly rather than report a failure.
class CancelledError : public HarnessError {
public:
    CancelledError();
};

/// The IR could not be built from the given input (bad role, malformed JSON
/// content shape, missing required field).
class InvalidRequestError : public HarnessError {
public:
    explicit InvalidRequestError(const std::string& message) : HarnessError(message) {}
};

}  // namespace apogee::harness
