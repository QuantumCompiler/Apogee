import Darwin
import Foundation

/// One run of the person's login shell, the way `ShellPath` asks it, and
/// nothing else: its input `/dev/null`, its errors dropped, its output kept up
/// to a limit, in a session of its own, and killed with its whole process
/// group once it runs past the deadline.
///
/// Not `ProcessSpawner`, for two reasons. A `Process` cannot start a child in
/// a session or group of its own, and without one there is no group to kill,
/// and an interactive shell could reach for the terminal of an app started
/// from one. And this is the one child whose `PATH` is not laid under it: it
/// is how that `PATH` is learned.
struct LoginShell {
    /// How a run ended.
    enum End: Equatable {
        /// It exited with this status.
        case exited(Int32)
        /// This signal ended it.
        case signalled(Int32)
        /// It was still running at the deadline, and its group was killed.
        case timedOut
        /// It could not be started: the system's error number.
        case couldNotStart(Int32)
    }

    /// A run, to its end or its deadline.
    struct Run: Equatable {
        let end: End
        /// What it wrote to stdout, up to the limit.
        let output: Data
        /// Whether it wrote more than the limit, the rest read and dropped.
        let overflowed: Bool
    }

    let executable: String
    let arguments: [String]
    /// Its whole environment.
    let environment: [String: String]
    /// Its working directory: the home directory, as a terminal starts it.
    let directory: String
    let deadline: Duration
    /// How much of its stdout is kept.
    var outputLimit = 1 << 20

    /// Runs it and waits, blocking the calling thread, until it has exited
    /// and its stdout has closed -- or, when something it left behind holds
    /// stdout open, until it has exited and `finished` says the output holds
    /// what was asked -- or until the deadline, whichever comes first.
    func run(finished: (Data) -> Bool) -> Run {
        var pipeEnds: [Int32] = [-1, -1]
        guard pipe(&pipeEnds) == 0 else {
            return Run(end: .couldNotStart(errno), output: Data(), overflowed: false)
        }
        let (reading, writing) = (pipeEnds[0], pipeEnds[1])
        // Neither end may leak into a child another thread starts meanwhile.
        _ = fcntl(reading, F_SETFD, FD_CLOEXEC)
        _ = fcntl(writing, F_SETFD, FD_CLOEXEC)
        defer { close(reading) }

        var pid: pid_t = 0
        let spawned = spawn(&pid, stdout: writing)
        close(writing)
        guard spawned == 0 else {
            return Run(end: .couldNotStart(spawned), output: Data(), overflowed: false)
        }

        let clock = ContinuousClock()
        let deadlineAt = clock.now.advanced(by: deadline)
        var output = Data()
        var overflowed = false
        var closed = false
        var status: Int32?
        var buffer = [UInt8](repeating: 0, count: 64 << 10)
        var pastDeadline = false

        while true {
            if status == nil {
                var raw: Int32 = 0
                if waitpid(pid, &raw, WNOHANG) == pid { status = raw }
            }
            if status != nil && (closed || finished(output)) { break }
            let remaining = clock.now.duration(to: deadlineAt)
            if remaining <= .zero {
                pastDeadline = true
                break
            }
            // A short slice, so an exit is seen soon even while stdout stays open.
            let slice = Int32(((min(remaining, .milliseconds(50))) / .milliseconds(1)).rounded(.up))
            if closed {
                _ = poll(nil, 0, slice)
                continue
            }
            var watched = pollfd(fd: reading, events: Int16(POLLIN), revents: 0)
            guard poll(&watched, 1, slice) > 0 else { continue }
            let count = read(reading, &buffer, buffer.count)
            if count > 0 {
                if output.count + count <= outputLimit {
                    output.append(contentsOf: buffer[0..<count])
                } else {
                    overflowed = true
                }
            } else if count == 0 || (errno != EINTR && errno != EAGAIN) {
                closed = true
            }
        }

        if pastDeadline {
            // The whole group: the shell, and whatever its profile started in it.
            _ = kill(-pid, SIGKILL)
        }
        guard let status else {
            var raw: Int32 = 0
            while waitpid(pid, &raw, 0) == -1 && errno == EINTR {}
            return Run(end: .timedOut, output: output, overflowed: overflowed)
        }
        let signal = status & 0x7f
        let end: End = signal == 0 ? .exited((status >> 8) & 0xff) : .signalled(signal)
        return Run(end: end, output: output, overflowed: overflowed)
    }

    /// `posix_spawn` with what the ask needs: a new session (so a group of its
    /// own, and no controlling terminal), every signal at its default and none
    /// blocked, no descriptor of the app's but the three it is given, and the
    /// home directory as its folder. The system's error number, or 0.
    private func spawn(_ pid: inout pid_t, stdout: Int32) -> Int32 {
        var attributes: posix_spawnattr_t? = nil
        posix_spawnattr_init(&attributes)
        defer { posix_spawnattr_destroy(&attributes) }
        var everySignal = sigset_t()
        sigfillset(&everySignal)
        var noSignal = sigset_t()
        sigemptyset(&noSignal)
        posix_spawnattr_setsigdefault(&attributes, &everySignal)
        posix_spawnattr_setsigmask(&attributes, &noSignal)
        posix_spawnattr_setflags(
            &attributes,
            Int16(POSIX_SPAWN_SETSID | POSIX_SPAWN_CLOEXEC_DEFAULT | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK))

        var actions: posix_spawn_file_actions_t? = nil
        posix_spawn_file_actions_init(&actions)
        defer { posix_spawn_file_actions_destroy(&actions) }
        posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0)
        posix_spawn_file_actions_adddup2(&actions, stdout, STDOUT_FILENO)
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0)
        posix_spawn_file_actions_addchdir(&actions, directory)

        let argv = ([executable] + arguments).map { strdup($0) } + [nil]
        let envp = environment.map { strdup("\($0.key)=\($0.value)") } + [nil]
        defer {
            argv.forEach { free($0) }
            envp.forEach { free($0) }
        }
        return posix_spawn(&pid, executable, &actions, &attributes, argv, envp)
    }
}
