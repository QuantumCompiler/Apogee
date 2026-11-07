#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

/// The portability seam.
///
/// Apogee ships five targets: linux and windows on x64 and arm64, macOS on
/// arm64 alone (the Intel Mac was dropped 2026-09-19). OS-specific
/// mechanisms belong HERE, behind a first-party interface, rather than as
/// `#ifdef` blocks sprinkled through feature code -- that is a Core constraint
/// of the project skeleton, and it exists because the alternative is what
/// happens by default: a Windows port that has to touch every file.
///
/// What lands here as later backlog items need it: process spawning (the
/// vendor-CLI backends' persistent children), PTY and terminal control (the
/// chat UX layer's `forkpty`/`tcflush` equivalents), file mode enforcement
/// (the 0600 secrets rule), and socket peer checks (`getpeername` on the admin
/// plane). Each arrives as a declaration in this header with one definition
/// per platform -- and where a mechanism has no Windows equivalent, the item
/// records the skip rather than leaving the gap silent.
namespace apogee::platform {

enum class OperatingSystem : std::uint8_t { Linux, MacOS, Windows };

enum class Architecture : std::uint8_t { X64, Arm64 };

/// The OS this binary was compiled for. Resolved at compile time; there is no
/// runtime detection to get wrong.
[[nodiscard]] OperatingSystem host_os() noexcept;

/// The CPU architecture this binary was compiled for.
[[nodiscard]] Architecture host_architecture() noexcept;

[[nodiscard]] std::string_view to_string(OperatingSystem os) noexcept;

[[nodiscard]] std::string_view to_string(Architecture arch) noexcept;

/// This build's release-target name -- one of the exact six strings used by
/// `lib/scripts/cicd.sh --platform`, the CMake presets, and the CI matrix:
/// "linux-x64", "linux-arm64", "macos-arm64", "windows-x64",
/// "windows-arm64". One vocabulary across the build system and the binary, so
/// a target name never means two slightly different things.
[[nodiscard]] std::string host_target();

/// The current user's home directory, or nullopt when it cannot be determined.
///
/// POSIX reads $HOME; Windows prefers %USERPROFILE% and falls back to
/// %HOMEDRIVE%%HOMEPATH%. Returning nullopt rather than a guess is deliberate:
/// every caller here resolves a path it is about to WRITE to, and writing to a
/// wrong-but-plausible directory is worse than a clear error.
///
/// **UTF-8 on every platform** (57j): Windows reads the variables wide
/// (`utf8_environment`), so a profile folder named `C:\Users\José` comes back
/// as the bytes `…Jos\xC3\xA9` whatever the process's code page -- the root
/// a path is built on with `path_from_utf8`. POSIX hands back $HOME's bytes as
/// they are.
///
/// Callers wanting Apogee's data directory should ask
/// `apogee::harness::apogee_home()` instead -- it layers the APOGEE_HOME
/// override on top of this.
[[nodiscard]] std::optional<std::string> home_directory();

/// One environment variable's value as UTF-8, or nullopt when it is not set
/// (57j). Windows reads the process's environment wide
/// (`GetEnvironmentVariableW`) and converts: never the narrow copy, which
/// holds only what the ANSI code page can spell and misses a change made
/// through the Win32 call. POSIX is `getenv`, the bytes as they are.
[[nodiscard]] std::optional<std::string> utf8_environment(std::string_view name);

/// The path `utf8` names (57j): on Windows decoded from UTF-8 to the wide form
/// the filesystem takes, so libstdc++ (which reads a narrow path as UTF-8) and
/// libc++ (which reads it in the ANSI code page) build the same path, and a
/// byte that is no UTF-8 is U+FFFD rather than a thrown "Cannot convert
/// character sequence". POSIX takes the bytes as they are.
[[nodiscard]] std::filesystem::path path_from_utf8(std::string_view utf8);

/// Why this process may read a non-ASCII argument, path or environment value
/// wrongly, or empty when it reads them as UTF-8 (57j). On Windows, whether
/// the process's ANSI code page is UTF-8: the manifest every Apogee
/// executable carries names it (`source/apogee.manifest`), and a Windows
/// older than 10 version 1903 ignores that -- `apogee check` warns, and
/// nothing refuses to run. Always empty elsewhere.
[[nodiscard]] std::string text_encoding_gap();

/// Absolute path of the running executable, or empty when the platform will
/// not say.
///
/// argv[0] is not a substitute: it is whatever the caller passed, so it is
/// bare on a PATH invocation and relative after a `cd`. `apogee check` reports
/// on the binary itself (its writability, and on macOS its quarantine
/// attribute), and `apogee uninstall` has to delete it -- both need the real
/// path, and a wrong one there means uninstall removes the wrong file.
[[nodiscard]] std::filesystem::path executable_path();

/// A day on the local calendar, and the time zone it is in.
struct LocalDate {
    int year = 0;
    /// 1-12.
    int month = 0;
    /// 1-31.
    int day = 0;
    /// 0 is Sunday.
    int weekday = 0;
    /// The zone as the system names it: `MDT` on POSIX, its long name on
    /// Windows.
    std::string zone;
    /// Minutes east of UTC, daylight saving included.
    int utc_offset_minutes = 0;
};

/// `when` on the local calendar, by the system's own time-zone settings
/// (`TZ`, `/etc/localtime`, the Windows zone).
[[nodiscard]] LocalDate local_date(std::chrono::system_clock::time_point when);

/// `when` on the UTC calendar, laid out by `format` (`strftime`'s fields): a
/// record's timestamp, `%Y-%m-%dT%H:%M:%SZ`, or an id's, `%Y%m%d-%H%M%S`.
/// Empty when the layout does not fit 64 bytes.
[[nodiscard]] std::string utc_time(std::chrono::system_clock::time_point when, const char* format);

/// This process's id, for a PID lock file (`train cycle`'s) and nothing
/// else -- a number a person can `kill`, never an identity.
[[nodiscard]] long current_process_id() noexcept;

/// Whether a process with this id is running -- for telling a staging
/// directory a live `models convert` is filling from one an interrupted run
/// left behind. A reused id reads as running, which errs toward keeping.
[[nodiscard]] bool process_running(long pid) noexcept;

/// Creates `path` holding `content` only if it does not exist yet -- one
/// atomic create-or-fail (`O_CREAT|O_EXCL`; `CREATE_NEW` on Windows), so two
/// processes racing for a lock cannot both win. False when the file exists
/// or cannot be made; `exists` says which. Windows names the file wide
/// (`CreateFileW`), so a non-ASCII path is the path asked for (57j).
[[nodiscard]] bool create_exclusive_file(const std::filesystem::path& path,
                                         std::string_view content, bool& exists);

/// Creates `path` -- nothing may be there yet, a link included -- holding
/// `content`, under `mode` (permission bits; the umask takes what it takes)
/// from its first instant: the mode is given to the create itself
/// (`O_CREAT|O_EXCL`), so the file is never on disk under a wider one, not
/// even before its first byte is written (57e: a secret's temporary file was
/// caught at the umask's mode while it was written). False, with `error`
/// saying why, when it cannot be made or written -- and then nothing of this
/// call's is left at `path`. Windows, where modes do not exist, writes it as a
/// stream does and ignores `mode`.
[[nodiscard]] bool write_new_file(const std::filesystem::path& path, std::string_view content,
                                  std::filesystem::perms mode, std::string& error);

/// Creates the directory `path` only if nothing is there yet, readable,
/// writable and searchable by its owner alone (0700) from its first instant
/// -- one atomic create-or-fail (`mkdir`; `CreateDirectory` on Windows,
/// whose temporary directory is the user's own), so a folder another run or
/// another local user made under that name is never taken for this one's
/// (57a: a private chat's attachment index). False when something is there
/// or the directory cannot be made; `exists` says which.
[[nodiscard]] bool create_private_directory(const std::filesystem::path& path, bool& exists);

/// Whether `path` itself -- a link, never what it points at -- belongs to the
/// user this process runs as: what a sweep of a shared temporary directory
/// may remove is this user's leftover alone (57a). False when `path` cannot
/// be read. On Windows, whose temporary directory is the user's own, whether
/// anything is there.
[[nodiscard]] bool owned_by_this_user(const std::filesystem::path& path) noexcept;

/// The three standard streams, for terminal detection.
enum class StandardStream : std::uint8_t { In, Out, Err };

/// Whether `stream` is attached to a terminal, as Apogee reads and writes it:
/// its descriptor is a terminal AND the C++ standard stream over it --
/// `std::cin`, `std::cout`, `std::cerr` -- still uses the buffer it was born
/// with. A process that has given that stream another buffer reads or writes
/// the buffer, not the terminal, and is answered as a pipe would be: every
/// in-process command test feeds input and captures output that way, and
/// asking only the descriptor let a test run from a developer's terminal paint
/// into its captured output and wait at a prompt on the real keyboard
/// (2026-10-07).
///
/// This is the gate for ALL decoration. A piped or redirected run must emit
/// exactly the answer -- no spinner frames, no colour, no status lines -- or
/// `apogee complete ... | jq` produces garbage and the CLI stops being
/// composable. Checking the stream rather than a global flag matters because
/// they differ routinely: stdout redirected to a file while stderr is still a
/// terminal is the normal shape of `apogee complete x > out.txt`.
[[nodiscard]] bool is_terminal(StandardStream stream) noexcept;

/// The terminal's width in columns, or nullopt when it cannot be determined
/// (not a terminal, or the query failed).
///
/// The thinking view needs this to pre-wrap every painted row: painted rows
/// must equal screen rows or the cursor arithmetic that erases them is wrong,
/// and a wrong erase eats the user's prompt. A caller with no answer should
/// assume a conservative default rather than skip wrapping.
[[nodiscard]] std::optional<int> terminal_width() noexcept;

/// The width of the terminal `stream` is attached to. A line painted on
/// stderr is measured on stderr: under `apogee models list | grep x` stdout
/// is the pipe and stderr the terminal the busy line repaints on (M1).
[[nodiscard]] std::optional<int> terminal_width(StandardStream stream) noexcept;

/// The terminal's height in rows, or nullopt when it cannot be determined.
///
/// The answer view needs it to keep its open area on screen: an erase cannot
/// reach a row that has scrolled away, so what it repaints must fit.
[[nodiscard]] std::optional<int> terminal_height() noexcept;

/// Why this process's terminal cannot host a full-screen view (32b), or empty
/// when it can. On POSIX a terminal is asked nothing: `TERM` unset or `dumb`
/// names one that cannot place the cursor. On Windows the console is asked
/// whether it takes virtual-terminal sequences -- what the shell draws with --
/// by turning them on and back off; a console that refuses, or an output that
/// is no console, cannot host it. A refusal is said and the caller falls back
/// to plain output: never a garbled screen.
[[nodiscard]] std::string full_screen_refusal();

/// Whether the output `stream` writes to shows an escape sequence as what it
/// means -- a colour, a cursor move -- rather than as the bytes (57j). Asked of
/// a Windows console alone: one whose virtual-terminal processing is off (a
/// classic console where `ConsoleScope` could not turn it on) prints
/// `←[32m`, so colour resolves off there. Everything else -- a POSIX
/// terminal, a pipe, a file, a Windows console with it on -- answers true, and
/// `is_terminal` stays the gate for decoration. `In` answers true.
[[nodiscard]] bool renders_escape_sequences(StandardStream stream) noexcept;

/// The Windows console set up for the run to read and write as a POSIX
/// terminal does (57j), and put back as it was found when the run ends.
///
/// When any standard stream is a console, the outermost scope saves its
/// input and output code pages and each output's mode, then sets both code
/// pages to UTF-8 -- what the CLI writes, `·`, `…`, `—` and the markdown
/// renderer's rules -- and turns virtual-terminal processing on, so colour
/// and the status line's cursor moves render in a classic console rather than
/// printing as `←[32m`. A console's code page outlives the process in
/// cmd.exe, so all three are put back: by the destructor (after flushing the
/// standard streams, so the last line is read in the page it was written in),
/// and by `restore_console()` on an exit no destructor sees -- the third
/// Ctrl-C's `_Exit`, and the `ExitProcess` Windows takes for a Ctrl-C or
/// Ctrl-Break that nothing handled (a command with no `InterruptScope`). For
/// that last one the first scope that changes the console adds a console
/// routine, once per process, which puts it back and declines the event; it
/// must come before any `signal()` call adds the C runtime's routine, so that
/// it is asked only after the runtime declined -- `RootCommand::run` makes the
/// scope before any command arms a handler. A console that refuses the mode
/// keeps it off, and `renders_escape_sequences` says so. replxx and FTXUI
/// save and restore their own console state inside it, unchanged. Scopes
/// nest: one made while another lives does nothing. A no-op on macOS and
/// Linux, and wherever no standard stream is a console.
class ConsoleScope {
public:
    ConsoleScope() noexcept;
    ~ConsoleScope();

    ConsoleScope(const ConsoleScope&) = delete;
    ConsoleScope& operator=(const ConsoleScope&) = delete;
    ConsoleScope(ConsoleScope&&) = delete;
    ConsoleScope& operator=(ConsoleScope&&) = delete;

    /// Whether this scope changed the console (and will put it back).
    [[nodiscard]] bool changed() const noexcept {
        return changed_;
    }

private:
    bool changed_ = false;
};

/// Puts back the console a live `ConsoleScope` changed, once, from any thread
/// -- for an exit that skips the scope's destructor: the interrupt handler's
/// `_Exit` on the third Ctrl-C, and the scope's console routine before an
/// unhandled event's `ExitProcess`. A no-op when nothing is changed, and
/// outside Windows.
void restore_console() noexcept;

/// Stands `handler` again for `signal` from inside that handler (57j). The
/// Microsoft C runtime puts a signal back to its default action before it
/// calls the handler, so without this a second Ctrl-C ends a Windows process
/// with `STATUS_CONTROL_C_EXIT` where POSIX, whose `signal` keeps the handler,
/// lets it count. A no-op elsewhere. The gap between the runtime's reset and
/// this call is a few instructions.
void rearm_signal(int signal, void (*handler)(int)) noexcept;

/// Discards anything already typed at the terminal but not yet read.
///
/// Chat startup is not instant — every backend is constructed before the first
/// prompt — and the terminal buffers whatever the user types meanwhile. Without
/// this, an impatient keystroke becomes the session's opening message the
/// instant the prompt appears.
///
/// **Discards rather than suppressing echo.** Turning ECHO off for the duration
/// of startup would need restorable terminal state held across a stretch of
/// code with many exit paths, and an exit that skips the restore strands the
/// user's shell with echo off. Discarding needs no state to restore and cannot
/// leave the terminal worse than it found it.
///
/// A no-op when stdin is not a terminal: there the buffered bytes ARE the
/// input, not typeahead. Failures are ignored — a session must never fail to
/// start because a flush was refused.
void discard_pending_input() noexcept;

/// Keeps what the user types during a turn off the screen until the next
/// prompt reads it.
///
/// While a model answers, the terminal is in its ordinary mode and echoes
/// every keystroke into the middle of the answer; the line editor then takes
/// the same keystrokes as type-ahead and shows them again at the prompt --
/// the duplicated question of 2026-09-23. With echo off for the turn, the
/// keystrokes stay queued, unseen, and appear once, at the prompt.
///
/// **The terminal is restored however the process ends.** Echo left off
/// outlives the process -- the user's shell would take input blind -- so
/// Ctrl-C, Ctrl-\, a hang-up and a termination restore it before the
/// signal's own action runs, and Ctrl-Z restores it before stopping and hides
/// it again on resume. Only one guard is live at a time; a second is inert.
/// A no-op when stdin is not a terminal, and on Windows, whose console echoes
/// only while a program is reading.
class TypeaheadGuard {
public:
    TypeaheadGuard() noexcept;
    ~TypeaheadGuard();

    TypeaheadGuard(const TypeaheadGuard&) = delete;
    TypeaheadGuard& operator=(const TypeaheadGuard&) = delete;
    TypeaheadGuard(TypeaheadGuard&&) = delete;
    TypeaheadGuard& operator=(TypeaheadGuard&&) = delete;

    /// Whether this guard turned echo off (and will turn it back on).
    [[nodiscard]] bool active() const noexcept {
        return active_;
    }

private:
    bool active_ = false;
};

/// Echo back on while a turn asks the user something -- a permission, a
/// question -- so the answer is seen as it is typed; hidden again after. A
/// no-op when no `TypeaheadGuard` is live.
class EchoPause {
public:
    EchoPause() noexcept;
    ~EchoPause();

    EchoPause(const EchoPause&) = delete;
    EchoPause& operator=(const EchoPause&) = delete;
    EchoPause(EchoPause&&) = delete;
    EchoPause& operator=(EchoPause&&) = delete;

    /// Whether this pause turned echo back on (and will hide it again).
    [[nodiscard]] bool paused() const noexcept {
        return paused_;
    }

private:
    bool paused_ = false;
};

/// Reads one line from standard input with echo off, for a secret.
///
/// Writes `prompt` to standard error, disables terminal echo for the read,
/// restores it, and prints the newline the user's Enter did not echo. On a
/// pipe -- no terminal to silence -- it reads the line plainly. Returns
/// nullopt at end of input. Never leaves echo off: the terminal state is
/// restored on every path out, which is why the whole read lives here rather
/// than in the command that wants it.
[[nodiscard]] std::optional<std::string> read_hidden_line(std::string_view prompt);

}  // namespace apogee::platform
