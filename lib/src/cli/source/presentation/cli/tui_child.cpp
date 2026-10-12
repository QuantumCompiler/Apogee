#include "cli/tui_child.h"

#include <chrono>
#include <memory>
#include <optional>
#include <utility>

#include "cli/line_tokens.h"
#include "contracts/paths.h"

namespace apogee::commands {

namespace {

/// One stream's bytes, split into lines as they come.
struct Lines {
    std::string pending;
    bool open = true;

    void take(std::string bytes, const std::function<void(std::string)>& line) {
        pending += bytes;
        for (std::size_t end = pending.find('\n'); end != std::string::npos;
             end = pending.find('\n')) {
            std::string one = pending.substr(0, end);
            if (!one.empty() && one.back() == '\r') {
                one.pop_back();
            }
            line(std::move(one));
            pending.erase(0, end + 1);
        }
    }

    void finish(const std::function<void(std::string)>& line) {
        if (!pending.empty()) {
            line(std::exchange(pending, std::string{}));
        }
    }
};

}  // namespace

platform::ChildCommand self_command(const RootContext& context, const std::filesystem::path& binary,
                                    const std::vector<std::string>& words) {
    platform::ChildCommand command;
    command.program = binary.string();
    if (!context.config_path.empty()) {
        command.arguments = {"--config", context.config_path};
    }
    command.arguments.insert(command.arguments.end(), words.begin(), words.end());
    // The root this shell runs under, whichever rung chose it.
    try {
        command.extra_environment.emplace_back("APOGEE_HOME", harness::apogee_home().string());
    } catch (const std::exception&) {
        // No root resolved: the child resolves its own, and says why it cannot.
    }
    return command;
}

void ChildStop::stop() {
    const std::lock_guard lock{mutex_};
    stopped_ = true;
    if (child_ != nullptr) {
        child_->interrupt();
    }
}

void ChildStop::attach(platform::ChildProcess* child) {
    const std::lock_guard lock{mutex_};
    child_ = child;
    if (stopped_ && child_ != nullptr) {
        child_->interrupt();
    }
}

void ChildStop::detach() {
    const std::lock_guard lock{mutex_};
    child_ = nullptr;
}

int run_child_lines(const platform::ChildCommand& command,
                    const std::function<void(std::string)>& line, ChildStop* stop) {
    std::string error;
    const std::unique_ptr<platform::ChildProcess> child = platform::start_child(command, error);
    if (child == nullptr) {
        line("could not start " + command.program + ": " + error);
        return -1;
    }
    child->close_stdin();
    if (stop != nullptr) {
        stop->attach(child.get());
    }
    Lines out;
    Lines err;
    constexpr std::chrono::milliseconds kSlice{25};
    // Stderr waited on, stdout only peeked at while stderr is open: a command
    // narrates on stderr as it works and says its outcome on stdout at the
    // end, and two pipes keep no order between them -- so the outcome is
    // never read ahead of narration written before it.
    while (out.open || err.open) {
        std::string bytes;
        if (err.open) {
            const platform::ReadStatus status = child->read_stderr(bytes, kSlice);
            if (status == platform::ReadStatus::Data) {
                err.take(std::move(bytes), line);
                continue;
            }
            if (status == platform::ReadStatus::Eof || status == platform::ReadStatus::Error) {
                err.open = false;
            }
        }
        if (out.open) {
            bytes.clear();
            const platform::ReadStatus status =
                child->read_stdout(bytes, err.open ? std::chrono::milliseconds{0} : kSlice);
            if (status == platform::ReadStatus::Data) {
                out.take(std::move(bytes), line);
            } else if (status == platform::ReadStatus::Eof ||
                       status == platform::ReadStatus::Error) {
                out.open = false;
            }
        }
    }
    out.finish(line);
    err.finish(line);
    std::optional<int> code = child->wait_for_exit(std::chrono::seconds{30});
    if (!code.has_value()) {
        child->terminate();
        code = child->wait_for_exit(std::chrono::seconds{5});
    }
    if (stop != nullptr) {
        stop->detach();
    }
    return code.value_or(-1);
}

ChildOutput run_child_text(const platform::ChildCommand& command) {
    ChildOutput output;
    output.code =
        run_child_lines(command, [&output](std::string line) { output.text += line + "\n"; });
    return output;
}

std::string child_answer(const RootContext& context, const std::filesystem::path& binary,
                         const std::vector<std::string>& words) {
    const ChildOutput output = run_child_text(self_command(context, binary, words));
    return output.code == 0 ? output.text : output.text + exit_line(output.code) + "\n";
}

bool start_child_run(tui::Progress& progress, const RootContext& context,
                     const std::filesystem::path& binary, std::vector<std::string> words,
                     std::string heading) {
    const auto stop = std::make_shared<ChildStop>();
    return progress.start(
        std::move(heading),
        [&context, binary, words = std::move(words), stop](const tui::Progress::Say& say) {
            const int code = run_child_lines(
                self_command(context, binary, words),
                [&say](std::string line) { say(std::move(line)); }, stop.get());
            say(exit_line(code));
        },
        [stop]() { stop->stop(); });
}

std::string run_typed(tui::Progress& progress, const RootContext& context,
                      const std::filesystem::path& binary, const std::string& group,
                      const std::string& line) {
    const LineTokens split = line_tokens(line);
    if (!split.error.empty()) {
        return "not run: " + split.error;
    }
    if (split.words.empty()) {
        return "not run: nothing was typed";
    }
    std::vector<std::string> words{group};
    words.insert(words.end(), split.words.begin(), split.words.end());
    if (!start_child_run(progress, context, binary, std::move(words), group + " " + line)) {
        return "not now: a run is going -- Ctrl-C stops it";
    }
    return "running 'apogee " + group + " " + line + "' -- its narration is below";
}

std::string exit_line(int code) {
    return code < 0 ? std::string{"stopped by a signal"} : "exit " + std::to_string(code);
}

}  // namespace apogee::commands
