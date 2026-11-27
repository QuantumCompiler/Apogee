#pragma once

#include <utility>

/// A value put back as it was found when a scope ends, however it ends.
///
/// The vendor-CLI backends' resume handles (57b): a side request is not a turn
/// of the conversation, so the conversation's session id or thread id must be
/// exactly what it was before the side request ran -- on a normal return, and
/// equally on a cancellation, a timeout or a failed spawn. A catch-and-rethrow
/// covers the throw sites that exist on the day it is written; a destructor
/// covers the ones added after it too.
///
/// `keep()` lets a scope's own value stand: a conversation's first Gemini turn
/// keeps the session id it minted only once the turn has succeeded, and a
/// failure on any path puts back the empty handle it found.
///
/// Header-only as a template (CLAUDE.md -> Code Style).
namespace apogee::backends {

template <typename T>
class RestoreOnExit {
public:
    /// Remembers `value` as it is now.
    explicit RestoreOnExit(T& value) : value_{value}, saved_{value} {}

    /// Remembers `value` as it is now and puts `replacement` in its place for
    /// the scope -- moved, so state that is swapped rather than shared (a
    /// framer's carry, a queue of parsed events) is never copied.
    RestoreOnExit(T& value, T replacement)
        : value_{value}, saved_{std::exchange(value, std::move(replacement))} {}

    ~RestoreOnExit() {
        if (armed_) {
            value_ = std::move(saved_);
        }
    }

    RestoreOnExit(const RestoreOnExit&) = delete;
    RestoreOnExit& operator=(const RestoreOnExit&) = delete;
    RestoreOnExit(RestoreOnExit&&) = delete;
    RestoreOnExit& operator=(RestoreOnExit&&) = delete;

    /// The scope's value stands: nothing is put back.
    void keep() noexcept {
        armed_ = false;
    }

private:
    T& value_;
    T saved_;
    bool armed_ = true;
};

}  // namespace apogee::backends
