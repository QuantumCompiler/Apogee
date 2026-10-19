#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "forge/cli_forge.h"
#include "support/fake_child.h"

/// A scripted `gh` or `glab` for the forge transports' tests (48a): each
/// call one scripted child -- what it prints on each stream and its exit --
/// and every call's argv and stdin recorded byte for byte. No network, no
/// process: the transport's own spawn seam is handed these children.
namespace apogee::testing {

struct CliReply {
    std::string out;
    std::string err;
    int exit = 0;
};

/// Forwards to a FakeChild the test keeps, since the transport owns (and
/// drops) the child it is handed.
class ChildProxy final : public platform::ChildProcess {
public:
    explicit ChildProxy(std::shared_ptr<FakeChild> child) : child_{std::move(child)} {}

    [[nodiscard]] bool write_stdin(std::string_view bytes) override {
        return child_->write_stdin(bytes);
    }

    void close_stdin() override {
        child_->close_stdin();
    }

    [[nodiscard]] platform::ReadStatus read_stdout(std::string& out,
                                                   std::chrono::milliseconds timeout) override {
        return child_->read_stdout(out, timeout);
    }

    [[nodiscard]] platform::ReadStatus read_stderr(std::string& out,
                                                   std::chrono::milliseconds timeout) override {
        return child_->read_stderr(out, timeout);
    }

    [[nodiscard]] bool exited() override {
        return child_->exited();
    }

    [[nodiscard]] std::optional<int> wait_for_exit(std::chrono::milliseconds timeout) override {
        return child_->wait_for_exit(timeout);
    }

    void terminate() override {
        child_->terminate();
    }

private:
    std::shared_ptr<FakeChild> child_;
};

/// The calls a transport made, and the replies it gets in order (the last
/// repeating once the script runs out).
struct ScriptedCli {
    std::vector<CliReply> replies;
    std::vector<platform::ChildCommand> commands;
    std::vector<std::shared_ptr<FakeChild>> children;
    /// What PATH holds: the CLI's path, or empty for not installed.
    std::string on_path = "/usr/local/bin/cli";

    [[nodiscard]] forge::CliTransportOptions options() {
        forge::CliTransportOptions options;
        options.find_on_path = [this](std::string_view) { return on_path; };
        options.spawn = [this](const platform::ChildCommand& command, std::string&) {
            commands.push_back(command);
            auto child = std::make_shared<FakeChild>();
            const CliReply& reply = replies.empty()
                                        ? CliReply{}
                                        : replies[std::min(children.size(), replies.size() - 1)];
            child->stdout_script = reply.out;
            child->stderr_script = reply.err;
            child->exit_status = reply.exit;
            children.push_back(child);
            return std::make_unique<ChildProxy>(child);
        };
        return options;
    }

    /// Call `i`'s program and arguments, as one vector.
    [[nodiscard]] std::vector<std::string> argv(std::size_t i) const {
        std::vector<std::string> out{commands.at(i).program};
        out.insert(out.end(), commands.at(i).arguments.begin(), commands.at(i).arguments.end());
        return out;
    }

    /// What call `i` wrote on the child's stdin.
    [[nodiscard]] std::string input(std::size_t i) const {
        std::string out;
        for (const std::string& piece : children.at(i)->writes) {
            out += piece;
        }
        return out;
    }
};

}  // namespace apogee::testing
