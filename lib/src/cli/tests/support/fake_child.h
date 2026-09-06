#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "platform/child_process.h"

namespace apogee::testing {

/// A scripted child process.
///
/// The seam that makes the vendor-CLI backends testable at all. A live `claude`
/// costs the user's subscription on every run, needs a login the CI machine
/// does not have, and — decisively — will not produce the failures that matter
/// on demand: a child that dies mid-turn, a `--resume` that is refused, a
/// stream that stops between two bytes of an object. Those are exactly the
/// paths this backend exists to survive.
class FakeChild final : public platform::ChildProcess {
public:
    /// Bytes this child will emit on stdout, handed over in `chunk_size`
    /// pieces. Zero means all at once.
    std::string stdout_script;
    std::size_t chunk_size = 0;

    /// Bytes emitted on stderr.
    std::string stderr_script;

    /// When set, the child reports EOF after this many stdout reads —
    /// simulating a crash mid-turn.
    int die_after_reads = -1;

    /// Everything written to its stdin, in order.
    std::vector<std::string> writes;

    bool stdin_closed = false;
    bool terminated = false;
    int exit_status = 0;

    [[nodiscard]] bool write_stdin(std::string_view bytes) override {
        if (stdin_closed || dead_) {
            return false;
        }
        writes.emplace_back(bytes);
        return true;
    }

    void close_stdin() override {
        stdin_closed = true;
    }

    [[nodiscard]] platform::ReadStatus read_stdout(std::string& out,
                                                   std::chrono::milliseconds) override {
        out.clear();
        ++reads_;
        if (die_after_reads >= 0 && reads_ > die_after_reads) {
            dead_ = true;
            return platform::ReadStatus::Eof;
        }
        if (offset_ >= stdout_script.size()) {
            return platform::ReadStatus::Eof;
        }
        const std::size_t take = chunk_size == 0
                                     ? stdout_script.size() - offset_
                                     : std::min(chunk_size, stdout_script.size() - offset_);
        out = stdout_script.substr(offset_, take);
        offset_ += take;
        return platform::ReadStatus::Data;
    }

    [[nodiscard]] platform::ReadStatus read_stderr(std::string& out,
                                                   std::chrono::milliseconds) override {
        out.clear();
        if (stderr_offset_ >= stderr_script.size()) {
            return platform::ReadStatus::Eof;
        }
        out = stderr_script.substr(stderr_offset_);
        stderr_offset_ = stderr_script.size();
        return platform::ReadStatus::Data;
    }

    [[nodiscard]] bool exited() override {
        return dead_ || (stdin_closed && offset_ >= stdout_script.size());
    }

    [[nodiscard]] std::optional<int> wait_for_exit(std::chrono::milliseconds) override {
        dead_ = true;
        return exit_status;
    }

    void terminate() override {
        terminated = true;
        dead_ = true;
    }

    /// Marks the child dead without it having been asked to stop.
    void kill_now() {
        dead_ = true;
    }

private:
    std::size_t offset_ = 0;
    std::size_t stderr_offset_ = 0;
    int reads_ = 0;
    bool dead_ = false;
};

/// Records every spawn and hands out scripted children in order.
class FakeSpawner {
public:
    /// One entry per expected spawn. When the script runs out, the last entry
    /// repeats — so a test that only cares about the first turn need not
    /// enumerate every later one.
    std::vector<std::string> stdout_scripts;

    /// Spawns that should FAIL, by index. Used for the refused-resume path.
    std::vector<int> failing_spawns;

    /// The argument list of every spawn, in order.
    std::vector<std::vector<std::string>> commands;

    /// Children handed out, kept alive so a test can inspect them afterwards.
    std::vector<std::shared_ptr<FakeChild>> children;

    std::size_t chunk_size = 0;

    [[nodiscard]] std::unique_ptr<platform::ChildProcess> operator()(
        const platform::ChildCommand& command, std::string& error) {
        const int index = static_cast<int>(commands.size());
        commands.push_back(command.arguments);

        if (std::find(failing_spawns.begin(), failing_spawns.end(), index) !=
            failing_spawns.end()) {
            error = "scripted spawn failure";
            return nullptr;
        }

        auto state = std::make_shared<FakeChild>();
        if (!stdout_scripts.empty()) {
            const std::size_t which =
                std::min(static_cast<std::size_t>(index), stdout_scripts.size() - 1);
            state->stdout_script = stdout_scripts[which];
        }
        state->chunk_size = chunk_size;
        children.push_back(state);
        return std::make_unique<Handle>(state);
    }

private:
    /// The provider owns its child and destroys it; the spawner keeps the
    /// state alive so post-run assertions do not read freed memory.
    class Handle final : public platform::ChildProcess {
    public:
        explicit Handle(std::shared_ptr<FakeChild> state) : state_{std::move(state)} {}

        [[nodiscard]] bool write_stdin(std::string_view bytes) override {
            return state_->write_stdin(bytes);
        }

        void close_stdin() override {
            state_->close_stdin();
        }

        [[nodiscard]] platform::ReadStatus read_stdout(std::string& out,
                                                       std::chrono::milliseconds timeout) override {
            return state_->read_stdout(out, timeout);
        }

        [[nodiscard]] platform::ReadStatus read_stderr(std::string& out,
                                                       std::chrono::milliseconds timeout) override {
            return state_->read_stderr(out, timeout);
        }

        [[nodiscard]] bool exited() override {
            return state_->exited();
        }

        [[nodiscard]] std::optional<int> wait_for_exit(std::chrono::milliseconds timeout) override {
            return state_->wait_for_exit(timeout);
        }

        void terminate() override {
            state_->terminate();
        }

    private:
        std::shared_ptr<FakeChild> state_;
    };
};

}  // namespace apogee::testing
