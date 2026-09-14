#include "tools/process.h"

#include <memory>

namespace apogee::tools {
namespace {

void append_capped(std::string& sink, const std::string& piece, std::size_t cap) {
    sink += piece;
    if (sink.size() > cap) {
        sink.erase(0, sink.size() - cap);
    }
}

}  // namespace

ProcessOutcome run_to_completion(const platform::ChildCommand& command,
                                 std::chrono::milliseconds timeout, std::size_t max_output) {
    ProcessOutcome outcome;
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
                    append_capped(outcome.out, piece, max_output);
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
                    append_capped(outcome.err, piece, max_output);
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
