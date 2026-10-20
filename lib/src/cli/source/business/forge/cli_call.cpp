#include "forge/cli_call.h"

#include <algorithm>

namespace apogee::forge {
namespace {

void keep_tail(std::string& tail, std::string_view piece) {
    tail.append(piece);
    if (tail.size() > kStderrTailBytes) {
        tail.erase(0, tail.size() - kStderrTailBytes);
    }
}

[[nodiscard]] std::string trimmed(std::string text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' ||
                             text.back() == '\t')) {
        text.pop_back();
    }
    return text;
}

}  // namespace

CliResult run_cli(const CliCall& call, const Spawn& spawn, std::chrono::milliseconds timeout,
                  std::size_t max_out) {
    CliResult result;
    platform::ChildCommand command;
    command.program = call.program;
    command.arguments = call.arguments;
    std::unique_ptr<platform::ChildProcess> child =
        spawn ? spawn(command, result.start_error)
              : platform::start_child(command, result.start_error);
    if (child == nullptr) {
        if (result.start_error.empty()) {
            result.start_error = "could not start " + call.program;
        }
        return result;
    }
    if (!call.input.empty()) {
        (void)child->write_stdin(call.input);
    }
    child->close_stdin();

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    bool out_open = true;
    bool err_open = true;
    std::string piece;
    while (out_open || err_open) {
        if (std::chrono::steady_clock::now() >= deadline) {
            result.timed_out = true;
            child->terminate();
            result.err = trimmed(result.err);
            return result;
        }
        if (out_open) {
            switch (child->read_stdout(piece, std::chrono::milliseconds{50})) {
                case platform::ReadStatus::Data:
                    result.out.append(piece);
                    if (result.out.size() > max_out) {
                        result.too_large = true;
                        child->terminate();
                        result.err = trimmed(result.err);
                        return result;
                    }
                    break;
                case platform::ReadStatus::Eof:
                case platform::ReadStatus::Error:
                    out_open = false;
                    break;
                case platform::ReadStatus::Timeout:
                    break;
            }
        }
        if (err_open) {
            switch (child->read_stderr(piece, std::chrono::milliseconds{out_open ? 0 : 50})) {
                case platform::ReadStatus::Data:
                    keep_tail(result.err, piece);
                    break;
                case platform::ReadStatus::Eof:
                case platform::ReadStatus::Error:
                    err_open = false;
                    break;
                case platform::ReadStatus::Timeout:
                    break;
            }
        }
    }
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    result.exit_code = child->wait_for_exit(std::max(left, std::chrono::milliseconds{1000}));
    if (!result.exit_code.has_value()) {
        result.timed_out = true;
        child->terminate();
    }
    result.err = trimmed(result.err);
    return result;
}

}  // namespace apogee::forge
