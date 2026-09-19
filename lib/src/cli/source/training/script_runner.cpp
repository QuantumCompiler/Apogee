#include "training/script_runner.h"

#include <algorithm>
#include <thread>

#include "backends/jsonl_framer.h"

namespace apogee::training {
namespace {

constexpr std::chrono::milliseconds kReadTimeout{100};
constexpr std::chrono::milliseconds kExitGrace{30000};
/// stderr is kept as a bounded tail: a chatty driver must not grow memory
/// without bound, and the failure report wants the end, not the start.
constexpr std::size_t kStderrKeep = 16 * 1024;

void keep_tail(std::string& buffer, std::string_view more) {
    buffer.append(more);
    if (buffer.size() > kStderrKeep) {
        buffer.erase(0, buffer.size() - kStderrKeep);
    }
}

void drain_stderr(platform::ChildProcess& child, std::string& buffer) {
    std::string piece;
    while (child.read_stderr(piece, std::chrono::milliseconds{0}) == platform::ReadStatus::Data) {
        keep_tail(buffer, piece);
    }
}

}  // namespace

ScriptEvent classify_script_line(std::string_view line) {
    ScriptEvent event;
    if (!backends::looks_like_json_object(line)) {
        event.kind = ScriptEvent::Kind::Message;
        event.text = std::string{line};
        return event;
    }
    const nlohmann::json parsed = nlohmann::json::parse(line, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        // Opened like an object and did not parse: still a message. The
        // alternative -- dropping it -- is the silence this file exists to
        // end.
        event.kind = ScriptEvent::Kind::Message;
        event.text = std::string{line};
        return event;
    }
    if (const auto it = parsed.find("error"); it != parsed.end() && it->is_string()) {
        event.kind = ScriptEvent::Kind::Error;
        event.text = it->get<std::string>();
        event.record = parsed;
        return event;
    }
    if (const auto it = parsed.find("message"); it != parsed.end() && it->is_string()) {
        event.kind = ScriptEvent::Kind::Message;
        event.text = it->get<std::string>();
        event.record = parsed;
        return event;
    }
    event.kind = ScriptEvent::Kind::Record;
    event.text = std::string{line};
    event.record = parsed;
    return event;
}

Spawner default_spawner() {
    return [](const platform::ChildCommand& command, std::string& error) {
        return platform::start_child(command, error);
    };
}

std::string ScriptOutcome::describe() const {
    if (stderr_tail.empty()) {
        return error;
    }
    return error + " -- stderr: " + stderr_tail;
}

ScriptOutcome run_script(const ScriptRequest& request, const ScriptEventSink& on_event,
                         const harness::CancellationToken& cancellation, const Spawner& spawn) {
    ScriptOutcome outcome;
    platform::ChildCommand command;
    command.program = request.interpreter.string();
    command.arguments.push_back(request.script.string());
    command.arguments.insert(command.arguments.end(), request.arguments.begin(),
                             request.arguments.end());
    command.extra_environment = request.environment;

    std::string spawn_error;
    std::unique_ptr<platform::ChildProcess> child = spawn(command, spawn_error);
    if (child == nullptr) {
        outcome.error = "could not start " + request.script.filename().string() + ": " +
                        (spawn_error.empty() ? std::string{"unknown error"} : spawn_error);
        return outcome;
    }
    child->close_stdin();

    backends::JsonlFramer framer;
    std::string stderr_buffer;
    std::string last_error;
    const auto sink = [&](std::string_view line) {
        if (line.empty()) {
            return;
        }
        ScriptEvent event = classify_script_line(line);
        if (event.kind == ScriptEvent::Kind::Error) {
            last_error = event.text;
        }
        if (on_event) {
            on_event(event);
        }
    };

    bool eof = false;
    while (!eof) {
        if (cancellation.stop_requested()) {
            child->terminate();
            outcome.cancelled = true;
            outcome.error = "cancelled";
            drain_stderr(*child, stderr_buffer);
            outcome.stderr_tail = tail_of(stderr_buffer);
            return outcome;
        }
        std::string chunk;
        const platform::ReadStatus status = child->read_stdout(chunk, kReadTimeout);
        drain_stderr(*child, stderr_buffer);
        switch (status) {
            case platform::ReadStatus::Data:
                framer.feed(chunk, sink);
                break;
            case platform::ReadStatus::Timeout:
                break;
            case platform::ReadStatus::Eof:
            case platform::ReadStatus::Error:
                eof = true;
                break;
        }
    }
    framer.flush(sink);
    outcome.exit_code = child->wait_for_exit(kExitGrace);
    drain_stderr(*child, stderr_buffer);
    outcome.stderr_tail = tail_of(stderr_buffer);

    const bool exited_clean = outcome.exit_code.has_value() && *outcome.exit_code == 0;
    if (!last_error.empty()) {
        outcome.error = last_error;
        outcome.ok = false;
        return outcome;
    }
    if (!exited_clean) {
        outcome.error =
            request.script.filename().string() + " exited with " +
            (outcome.exit_code.has_value() ? "code " + std::to_string(*outcome.exit_code)
                                           : std::string{"no exit status"});
        return outcome;
    }
    outcome.ok = true;
    return outcome;
}

CommandResult run_command(const platform::ChildCommand& command,
                          const harness::CancellationToken& cancellation, const Spawner& spawn) {
    CommandResult result;
    std::string spawn_error;
    std::unique_ptr<platform::ChildProcess> child = spawn(command, spawn_error);
    if (child == nullptr) {
        result.start_error =
            spawn_error.empty() ? "could not start " + command.program : spawn_error;
        return result;
    }
    child->close_stdin();
    bool eof = false;
    while (!eof) {
        if (cancellation.stop_requested()) {
            child->terminate();
            result.cancelled = true;
            return result;
        }
        std::string chunk;
        const platform::ReadStatus status = child->read_stdout(chunk, kReadTimeout);
        std::string err_piece;
        while (child->read_stderr(err_piece, std::chrono::milliseconds{0}) ==
               platform::ReadStatus::Data) {
            keep_tail(result.err, err_piece);
        }
        switch (status) {
            case platform::ReadStatus::Data:
                keep_tail(result.out, chunk);
                break;
            case platform::ReadStatus::Timeout:
                break;
            case platform::ReadStatus::Eof:
            case platform::ReadStatus::Error:
                eof = true;
                break;
        }
    }
    result.exit_code = child->wait_for_exit(kExitGrace);
    std::string err_piece;
    while (child->read_stderr(err_piece, std::chrono::milliseconds{0}) ==
           platform::ReadStatus::Data) {
        keep_tail(result.err, err_piece);
    }
    return result;
}

CommandRunner default_command_runner() {
    return
        [](const platform::ChildCommand& command, const harness::CancellationToken& cancellation) {
            return run_command(command, cancellation, default_spawner());
        };
}

std::string tail_of(std::string_view text, std::size_t lines) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    if (text.empty() || lines == 0) {
        return {};
    }
    std::size_t start = text.size();
    std::size_t seen = 0;
    while (start > 0) {
        --start;
        if (text[start] == '\n') {
            ++seen;
            if (seen == lines) {
                ++start;
                break;
            }
        }
    }
    return std::string{text.substr(start)};
}

}  // namespace apogee::training
