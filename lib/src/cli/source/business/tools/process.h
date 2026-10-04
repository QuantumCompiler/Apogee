#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "platform/child_process.h"

/// Running a child to completion -- what the shell and git toolsets share.
///
/// Both spawn a program, feed it nothing, collect everything it says on both
/// pipes, and want its exit status -- bounded by a deadline, after which the
/// child is killed and the fact is reported rather than the call hanging the
/// turn. That loop is written once here.
namespace apogee::tools {

/// How much of one stream a run keeps: its first `head` bytes and its last
/// `tail` bytes. What falls between is counted, never held -- a command that
/// prints gigabytes costs the kept bytes and a number.
struct OutputLimit {
    std::size_t head = 0;
    std::size_t tail = 256 * 1024;
};

/// One stream's output, kept to an OutputLimit.
class CapturedOutput {
public:
    explicit CapturedOutput(OutputLimit limit = {});

    void append(std::string_view piece);

    /// The first bytes kept -- the whole stream while it fits in the head.
    [[nodiscard]] const std::string& head() const noexcept {
        return head_;
    }

    /// The bytes kept after the head: the stream's last ones once anything
    /// has been dropped.
    [[nodiscard]] const std::string& tail() const noexcept {
        return tail_;
    }

    /// How many bytes were dropped between the head and the tail.
    [[nodiscard]] std::uint64_t omitted() const noexcept {
        return omitted_;
    }

    /// Everything kept, head then tail: the whole stream when `omitted()`
    /// is zero.
    [[nodiscard]] std::string text() const;

    [[nodiscard]] bool empty() const noexcept {
        return head_.empty() && tail_.empty();
    }

private:
    OutputLimit limit_;
    std::string head_;
    std::string tail_;
    std::uint64_t omitted_ = 0;
};

struct ProcessOutcome {
    CapturedOutput out;
    CapturedOutput err;
    /// The exit status; nullopt when the child was killed or never started.
    std::optional<int> exit_code;
    bool timed_out = false;
    /// Why the child could not be started, or empty.
    std::string start_error;
};

/// Runs `command` with stdin closed and drains it until it exits or `timeout`
/// elapses. Each stream is kept to `limit` -- by default its last 256 KiB,
/// since the end of a build log is the part with the error in it.
[[nodiscard]] ProcessOutcome run_to_completion(const platform::ChildCommand& command,
                                               std::chrono::milliseconds timeout,
                                               OutputLimit limit = {});

}  // namespace apogee::tools
