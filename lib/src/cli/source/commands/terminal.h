#pragma once

#include <mutex>
#include <ostream>
#include <string_view>
#include <utility>

/// The single serialization point for everything written to the terminal.
///
/// **Not optional.** A spinner thread repainting every 120 ms and a token
/// stream arriving from the provider's reader thread will interleave mid-escape
/// -sequence, and the result is a torn line the user cannot read and nobody can
/// reproduce on demand. Ommi hit exactly this collision. One mutex, and every
/// writer goes through it.
///
/// The stream is injected, which is what makes the whole layer testable: the
/// escape sequences are where the bugs live, so tests write to a string buffer
/// and assert on the bytes.
namespace apogee::commands {

class TerminalWriter {
public:
    explicit TerminalWriter(std::ostream& out) : out_{out} {}

    TerminalWriter(const TerminalWriter&) = delete;
    TerminalWriter& operator=(const TerminalWriter&) = delete;
    TerminalWriter(TerminalWriter&&) = delete;
    TerminalWriter& operator=(TerminalWriter&&) = delete;
    ~TerminalWriter() = default;

    /// Writes and flushes, holding the lock for the whole call.
    void write(std::string_view text);

    /// Runs `action` with the lock held, passing it the raw stream.
    ///
    /// For multi-part sequences that must not be interrupted -- an erase
    /// followed by a repaint is one visual operation, and another thread
    /// writing between them leaves the screen in a state neither intended.
    ///
    /// **Do not call any other TerminalWriter method from inside `action`**:
    /// the mutex is not recursive, and doing so deadlocks.
    template <typename Action>
    void with_lock(Action&& action) {
        const std::lock_guard<std::mutex> guard{mutex_};
        std::forward<Action>(action)(out_);
        out_.flush();
    }

private:
    std::ostream& out_;
    std::mutex mutex_;
};

}  // namespace apogee::commands
