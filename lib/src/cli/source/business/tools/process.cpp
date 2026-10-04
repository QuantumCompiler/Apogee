#include "tools/process.h"

#include <algorithm>
#include <memory>

namespace apogee::tools {

CapturedOutput::CapturedOutput(OutputLimit limit) : limit_{limit} {}

void CapturedOutput::append(std::string_view piece) {
    // The head fills first and is never touched again; everything after it
    // runs through the tail, whose oldest bytes are counted out as it grows.
    if (omitted_ == 0 && tail_.empty() && head_.size() < limit_.head) {
        const std::size_t room = limit_.head - head_.size();
        head_.append(piece.substr(0, room));
        piece.remove_prefix(std::min(room, piece.size()));
    }
    tail_.append(piece);
    if (tail_.size() > limit_.tail) {
        const std::size_t excess = tail_.size() - limit_.tail;
        tail_.erase(0, excess);
        omitted_ += excess;
    }
}

std::string CapturedOutput::text() const {
    return head_ + tail_;
}

ProcessOutcome run_to_completion(const platform::ChildCommand& command,
                                 std::chrono::milliseconds timeout, OutputLimit limit) {
    ProcessOutcome outcome;
    outcome.out = CapturedOutput{limit};
    outcome.err = CapturedOutput{limit};
    std::string error;
    std::unique_ptr<platform::ChildProcess> child = platform::start_child(command, error);
    if (child == nullptr) {
        outcome.start_error = error.empty() ? "could not start " + command.program : error;
        return outcome;
    }
    child->close_stdin();

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    bool out_open = true;
    bool err_open = true;
    std::string piece;
    while (out_open || err_open) {
        if (std::chrono::steady_clock::now() >= deadline) {
            child->terminate();
            outcome.timed_out = true;
            return outcome;
        }
        if (out_open) {
            switch (child->read_stdout(piece, std::chrono::milliseconds{50})) {
                case platform::ReadStatus::Data:
                    outcome.out.append(piece);
                    break;
                case platform::ReadStatus::Timeout:
                    break;
                case platform::ReadStatus::Eof:
                case platform::ReadStatus::Error:
                    out_open = false;
                    break;
            }
        }
        if (err_open) {
            switch (child->read_stderr(piece, std::chrono::milliseconds{0})) {
                case platform::ReadStatus::Data:
                    outcome.err.append(piece);
                    break;
                case platform::ReadStatus::Timeout:
                    break;
                case platform::ReadStatus::Eof:
                case platform::ReadStatus::Error:
                    err_open = false;
                    break;
            }
        }
    }

    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    outcome.exit_code = child->wait_for_exit(
        remaining > std::chrono::milliseconds{0} ? remaining : std::chrono::milliseconds{0});
    if (!outcome.exit_code.has_value()) {
        child->terminate();
        outcome.timed_out = true;
    }
    return outcome;
}

}  // namespace apogee::tools
