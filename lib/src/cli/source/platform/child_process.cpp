#include "platform/child_process.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <system_error>

#if !defined(_WIN32)
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace apogee::platform {
namespace {

#if !defined(_WIN32)

/// A pipe pair, closed exactly once.
///
/// The Code Style rule for C handles, applied to file descriptors: the raw int
/// never escapes a wrapper that owns it, so no error path can leak one.
class Fd {
public:
    Fd() = default;

    explicit Fd(int fd) : fd_{fd} {}

    ~Fd() {
        close();
    }

    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;

    Fd(Fd&& other) noexcept : fd_{other.fd_} {
        other.fd_ = -1;
    }

    Fd& operator=(Fd&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    [[nodiscard]] int get() const noexcept {
        return fd_;
    }

    [[nodiscard]] bool valid() const noexcept {
        return fd_ >= 0;
    }

    void close() noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

private:
    int fd_ = -1;
};

/// Stops a write to a dead child from killing US.
///
/// Found by a test, and it is the failure that matters most here: when a child
/// dies mid-conversation, the very next `write` raises SIGPIPE, whose default
/// disposition terminates the process. Apogee would die trying to talk to a
/// crashed `claude` -- in the exact code path that exists to RECOVER from a
/// crashed `claude`.
///
/// Two mechanisms, because the platforms differ:
///   * BSD/macOS has `F_SETNOSIGPIPE`, which is per-descriptor and therefore
///     the polite answer: nothing outside this pipe changes.
///   * Linux has no such flag for pipes, so the signal is ignored
///     process-wide, once. That is a global change and is called out here
///     rather than buried: the alternative is `pthread_sigmask` around every
///     write plus draining a pending signal, which is a lot of machinery for a
///     signal essentially no program wants delivered.
/// Either way `write` then returns EPIPE, which is an ordinary `false`.
void suppress_sigpipe(int fd) {
#if defined(F_SETNOSIGPIPE)
    ::fcntl(fd, F_SETNOSIGPIPE, 1);
#else
    (void)fd;
    static std::once_flag once;
    std::call_once(once, [] { ::signal(SIGPIPE, SIG_IGN); });
#endif
}

/// Reads what is available on `fd`, waiting up to `timeout`.
[[nodiscard]] ReadStatus poll_and_read(const Fd& fd, std::string& out,
                                       std::chrono::milliseconds timeout) {
    out.clear();
    if (!fd.valid()) {
        return ReadStatus::Eof;
    }

    struct pollfd descriptor{fd.get(), POLLIN, 0};
    const int ready = ::poll(&descriptor, 1, static_cast<int>(timeout.count()));
    if (ready == 0) {
        // Not an error: the timeout is what lets a reader loop notice a
        // cancellation flag instead of blocking forever on a quiet child.
        return ReadStatus::Timeout;
    }
    if (ready < 0) {
        return errno == EINTR ? ReadStatus::Timeout : ReadStatus::Error;
    }

    std::array<char, 16 * 1024> buffer{};
    const ssize_t got = ::read(fd.get(), buffer.data(), buffer.size());
    if (got > 0) {
        out.assign(buffer.data(), static_cast<std::size_t>(got));
        return ReadStatus::Data;
    }
    if (got == 0) {
        return ReadStatus::Eof;
    }
    if (errno == EAGAIN || errno == EINTR) {
        return ReadStatus::Timeout;
    }
    return ReadStatus::Error;
}

class PosixChild final : public ChildProcess {
public:
    PosixChild(const PosixChild&) = delete;
    PosixChild& operator=(const PosixChild&) = delete;
    PosixChild(PosixChild&&) = delete;
    PosixChild& operator=(PosixChild&&) = delete;

    PosixChild(pid_t pid, Fd stdin_write, Fd stdout_read, Fd stderr_read)
        : pid_{pid},
          stdin_{std::move(stdin_write)},
          stdout_{std::move(stdout_read)},
          stderr_{std::move(stderr_read)} {}

    ~PosixChild() override {
        // A destructor must not leave a process running. Close stdin first so
        // the child can finish cleanly, then reap; terminate only if it will
        // not go.
        close_stdin();
        if (!reaped_) {
            if (!wait_for_exit(std::chrono::seconds{2}).has_value()) {
                terminate();
                (void)wait_for_exit(std::chrono::seconds{2});
            }
        }
    }

    [[nodiscard]] bool write_stdin(std::string_view bytes) override {
        if (!stdin_.valid()) {
            return false;
        }
        std::size_t written = 0;
        while (written < bytes.size()) {
            const ssize_t wrote =
                ::write(stdin_.get(), bytes.data() + written, bytes.size() - written);
            if (wrote < 0) {
                if (errno == EINTR) {
                    continue;
                }
                // EPIPE: the child is gone. The caller respawns; it is not an
                // exception, because a dead child mid-conversation is a state
                // this backend is required to recover from.
                return false;
            }
            written += static_cast<std::size_t>(wrote);
        }
        return true;
    }

    void close_stdin() override {
        stdin_.close();
    }

    [[nodiscard]] ReadStatus read_stdout(std::string& out,
                                         std::chrono::milliseconds timeout) override {
        return poll_and_read(stdout_, out, timeout);
    }

    [[nodiscard]] ReadStatus read_stderr(std::string& out,
                                         std::chrono::milliseconds timeout) override {
        return poll_and_read(stderr_, out, timeout);
    }

    [[nodiscard]] bool exited() override {
        if (reaped_) {
            return true;
        }
        int status = 0;
        const pid_t result = ::waitpid(pid_, &status, WNOHANG);
        if (result == pid_) {
            reaped_ = true;
            exit_status_ = status;
        }
        return reaped_;
    }

    [[nodiscard]] std::optional<int> wait_for_exit(std::chrono::milliseconds timeout) override {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        for (;;) {
            if (exited()) {
                return WIFEXITED(exit_status_) ? WEXITSTATUS(exit_status_) : -1;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                return std::nullopt;
            }
            // Polling rather than blocking in waitpid: the caller wants a
            // budget, and SIGCHLD handling would be a process-wide change for
            // one backend's benefit.
            ::usleep(20 * 1000);
        }
    }

    void terminate() override {
        if (!reaped_) {
            ::kill(pid_, SIGTERM);
        }
    }

private:
    pid_t pid_ = -1;
    Fd stdin_;
    Fd stdout_;
    Fd stderr_;
    bool reaped_ = false;
    int exit_status_ = 0;
};

#endif  // !_WIN32

}  // namespace

bool supports_child_processes() noexcept {
#if defined(_WIN32)
    return false;
#else
    return true;
#endif
}

std::string find_on_path(std::string_view program) {
    if (program.empty()) {
        return {};
    }

    const std::filesystem::path candidate{program};
    std::error_code code;
    if (candidate.has_parent_path()) {
        // An explicit path: use it as given, but confirm it is really there so
        // the error names the path rather than an errno.
        return std::filesystem::exists(candidate, code) ? candidate.string() : std::string{};
    }

    const char* path = std::getenv("PATH");
    if (path == nullptr) {
        return {};
    }

    std::string_view remaining{path};
    while (!remaining.empty()) {
        const std::size_t separator = remaining.find(':');
        const std::string_view entry = remaining.substr(0, separator);
        if (!entry.empty()) {
            const std::filesystem::path full = std::filesystem::path{entry} / candidate;
            if (std::filesystem::exists(full, code) &&
                std::filesystem::is_regular_file(full, code)) {
                return full.string();
            }
        }
        if (separator == std::string_view::npos) {
            break;
        }
        remaining.remove_prefix(separator + 1);
    }
    return {};
}

#if defined(_WIN32)

std::unique_ptr<ChildProcess> start_child(const ChildCommand& command, std::string& error) {
    (void)command;
    // A recorded per-item skip, said out loud. The vendor-CLI backends need a
    // CreateProcess implementation with overlapped I/O on the pipes; until it
    // exists, refusing with a message beats a silent gap.
    error =
        "this build cannot spawn child processes: the vendor-CLI backends have no Windows "
        "implementation yet. Use a direct-API backend on this platform";
    return nullptr;
}

#else

std::unique_ptr<ChildProcess> start_child(const ChildCommand& command, std::string& error) {
    const std::string resolved = find_on_path(command.program);
    if (resolved.empty()) {
        error = "'" + command.program + "' was not found on PATH";
        return nullptr;
    }

    // Three pipes: to the child's stdin, from its stdout, from its stderr.
    std::array<int, 2> in{-1, -1};
    std::array<int, 2> out{-1, -1};
    std::array<int, 2> err{-1, -1};
    if (::pipe(in.data()) != 0 || ::pipe(out.data()) != 0 || ::pipe(err.data()) != 0) {
        error = std::string{"could not create pipes: "} + std::strerror(errno);
        return nullptr;
    }

    Fd child_in{in[0]};
    Fd parent_in{in[1]};
    Fd parent_out{out[0]};
    Fd child_out{out[1]};
    Fd parent_err{err[0]};
    Fd child_err{err[1]};

    // CLOEXEC on the ends the parent keeps, so a later spawn from elsewhere in
    // the process does not inherit them and hold a pipe open forever -- which
    // shows up as a reader that never sees EOF.
    ::fcntl(parent_in.get(), F_SETFD, FD_CLOEXEC);
    suppress_sigpipe(parent_in.get());
    ::fcntl(parent_out.get(), F_SETFD, FD_CLOEXEC);
    ::fcntl(parent_err.get(), F_SETFD, FD_CLOEXEC);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, child_in.get(), STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, child_out.get(), STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, child_err.get(), STDERR_FILENO);

    std::vector<std::string> storage;
    storage.push_back(resolved);
    for (const std::string& argument : command.arguments) {
        storage.push_back(argument);
    }
    std::vector<char*> argv;
    argv.reserve(storage.size() + 1);
    for (std::string& value : storage) {
        argv.push_back(value.data());
    }
    argv.push_back(nullptr);

    // The environment is INHERITED, with only what the caller explicitly added
    // layered on. Never synthesised: see ChildCommand::extra_environment.
    std::vector<std::string> env_storage;
    std::vector<char*> envp;
    if (command.extra_environment.empty()) {
        envp.clear();
    } else {
        for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
            env_storage.emplace_back(*entry);
        }
        for (const auto& [name, value] : command.extra_environment) {
            std::string entry = name;
            entry += "=";
            entry += value;
            env_storage.push_back(std::move(entry));
        }
        envp.reserve(env_storage.size() + 1);
        for (std::string& value : env_storage) {
            envp.push_back(value.data());
        }
        envp.push_back(nullptr);
    }

    pid_t pid = -1;
    const int status = ::posix_spawn(&pid, resolved.c_str(), &actions, nullptr, argv.data(),
                                     envp.empty() ? environ : envp.data());
    posix_spawn_file_actions_destroy(&actions);

    if (status != 0) {
        error = "could not start '" + resolved + "': " + std::strerror(status);
        return nullptr;
    }

    // The child's ends belong to the child now.
    child_in.close();
    child_out.close();
    child_err.close();

    return std::make_unique<PosixChild>(pid, std::move(parent_in), std::move(parent_out),
                                        std::move(parent_err));
}

#endif  // _WIN32

}  // namespace apogee::platform
