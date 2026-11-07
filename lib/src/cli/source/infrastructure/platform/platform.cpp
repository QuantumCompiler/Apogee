#include "platform/platform.h"

#include <array>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#elif defined(__APPLE__)
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#endif

// The only place in Apogee where a platform `#ifdef` is expected. Everything
// else asks this header instead. An unrecognized platform is a hard compile
// error on purpose: silently degrading to "Unknown" would let an unsupported
// target build and then misbehave at runtime, which is exactly the failure the
// five-target matrix exists to prevent.

namespace apogee::platform {

#if defined(_WIN32)
namespace {

#if !defined(ENABLE_VIRTUAL_TERMINAL_PROCESSING)
constexpr DWORD ENABLE_VIRTUAL_TERMINAL_PROCESSING = 0x0004;
#endif

/// `text` (UTF-16, as Windows answers) in UTF-8, as everything above the
/// seam reads it (57j). An unpaired surrogate becomes U+FFFD.
[[nodiscard]] std::string utf8_from_wide(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int length = static_cast<int>(text.size());
    const int size =
        WideCharToMultiByte(CP_UTF8, 0, text.data(), length, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(size), '\0');
    (void)WideCharToMultiByte(CP_UTF8, 0, text.data(), length, out.data(), size, nullptr, nullptr);
    return out;
}

/// `text` (UTF-8) in the wide form Windows takes. A byte that is no UTF-8
/// becomes U+FFFD, never an error.
[[nodiscard]] std::wstring wide_from_utf8(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int length = static_cast<int>(text.size());
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), length, nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    (void)MultiByteToWideChar(CP_UTF8, 0, text.data(), length, out.data(), size);
    return out;
}

/// The Win32 handle of a standard stream.
[[nodiscard]] DWORD standard_handle(StandardStream stream) noexcept {
    switch (stream) {
        case StandardStream::In:
            return STD_INPUT_HANDLE;
        case StandardStream::Out:
            return STD_OUTPUT_HANDLE;
        case StandardStream::Err:
            return STD_ERROR_HANDLE;
    }
    return STD_OUTPUT_HANDLE;
}

/// Whether `handle` is a console (input or screen buffer).
[[nodiscard]] bool is_console(HANDLE handle) noexcept {
    DWORD mode = 0;
    return handle != nullptr && handle != INVALID_HANDLE_VALUE &&
           GetConsoleMode(handle, &mode) != 0;
}

}  // namespace
#endif

OperatingSystem host_os() noexcept {
#if defined(__linux__)
    return OperatingSystem::Linux;
#elif defined(__APPLE__)
    return OperatingSystem::MacOS;
#elif defined(_WIN32)
    return OperatingSystem::Windows;
#else
#error "Apogee supports Linux, macOS, and Windows only -- see CLAUDE.md -> Stack & environment"
#endif
}

Architecture host_architecture() noexcept {
#if defined(__aarch64__) || defined(_M_ARM64)
    return Architecture::Arm64;
#elif defined(__x86_64__) || defined(_M_X64)
    return Architecture::X64;
#else
#error "Apogee supports x86_64 and arm64 only -- see CLAUDE.md -> Stack & environment"
#endif
}

std::string_view to_string(OperatingSystem os) noexcept {
    switch (os) {
        case OperatingSystem::Linux:
            return "linux";
        case OperatingSystem::MacOS:
            return "macos";
        case OperatingSystem::Windows:
            return "windows";
    }
    return "unknown";
}

std::string_view to_string(Architecture arch) noexcept {
    switch (arch) {
        case Architecture::X64:
            return "x64";
        case Architecture::Arm64:
            return "arm64";
    }
    return "unknown";
}

std::filesystem::path executable_path() {
    // Three genuinely different mechanisms, which is why this lives behind the
    // seam rather than in the one caller that wants it.
#if defined(_WIN32)
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD written =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0) {
            return {};
        }
        if (written < buffer.size()) {
            buffer.resize(written);
            return std::filesystem::path{buffer};
        }
        buffer.resize(buffer.size() * 2);  // truncated: ask again with more room
    }
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);  // returns -1 and sets the size needed
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        return {};
    }
    buffer.resize(std::strlen(buffer.c_str()));
    std::error_code code;
    // Resolve symlinks: a Homebrew-style install is a link into a cellar, and
    // `apogee uninstall` must act on what it is really deleting.
    const std::filesystem::path resolved = std::filesystem::weakly_canonical(buffer, code);
    return code ? std::filesystem::path{buffer} : resolved;
#else
    std::error_code code;
    const std::filesystem::path resolved = std::filesystem::read_symlink("/proc/self/exe", code);
    if (code) {
        return {};
    }
    return resolved;
#endif
}

LocalDate local_date(std::chrono::system_clock::time_point when) {
    const std::time_t seconds = std::chrono::system_clock::to_time_t(when);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &seconds);
    // The offset is how far the local calendar reads ahead of UTC: the
    // local fields read back as if they were UTC, less the moment itself.
    std::tm as_utc = local;
    const long offset_seconds = static_cast<long>(_mkgmtime(&as_utc) - seconds);
#else
    localtime_r(&seconds, &local);
    const long offset_seconds = local.tm_gmtoff;
#endif
    std::array<char, 64> zone{};
    if (std::strftime(zone.data(), zone.size(), "%Z", &local) == 0) {
        zone[0] = '\0';
    }
    LocalDate date;
    date.year = local.tm_year + 1900;
    date.month = local.tm_mon + 1;
    date.day = local.tm_mday;
    date.weekday = local.tm_wday;
    date.zone = zone.data();
    date.utc_offset_minutes = static_cast<int>(offset_seconds / 60);
    return date;
}

std::string utc_time(std::chrono::system_clock::time_point when, const char* format) {
    const std::time_t seconds = std::chrono::system_clock::to_time_t(when);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    std::array<char, 64> buffer{};
    if (std::strftime(buffer.data(), buffer.size(), format, &utc) == 0) {
        return {};
    }
    return buffer.data();
}

long current_process_id() noexcept {
#if defined(_WIN32)
    return static_cast<long>(GetCurrentProcessId());
#else
    return static_cast<long>(getpid());
#endif
}

bool process_running(long pid) noexcept {
    if (pid <= 0) {
        return false;
    }
#if defined(_WIN32)
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (process == nullptr) {
        // No such process -- or one this user may not look at, which exists.
        return GetLastError() == ERROR_ACCESS_DENIED;
    }
    DWORD code = 0;
    const bool running = GetExitCodeProcess(process, &code) != 0 && code == STILL_ACTIVE;
    CloseHandle(process);
    return running;
#else
    // Signal 0 checks without sending; EPERM means it exists, owned by another.
    return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
#endif
}

bool create_exclusive_file(const std::filesystem::path& path, std::string_view content,
                           bool& exists) {
    exists = false;
#if defined(_WIN32)
    // Wide, as `file_lock.cpp` names its file: `CreateFileA` read the
    // narrow name in the ANSI code page, so a non-ASCII path named another
    // file (57j).
    const HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        exists = GetLastError() == ERROR_FILE_EXISTS;
        return false;
    }
    DWORD written = 0;
    const bool ok = WriteFile(handle, content.data(), static_cast<DWORD>(content.size()), &written,
                              nullptr) != 0;
    CloseHandle(handle);
    return ok;
#else
    const int fd = ::open(path.string().c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) {
        exists = errno == EEXIST;
        return false;
    }
    const ssize_t written = ::write(fd, content.data(), content.size());
    ::close(fd);
    return written == static_cast<ssize_t>(content.size());
#endif
}

bool write_new_file(const std::filesystem::path& path, std::string_view content,
                    std::filesystem::perms mode, std::string& error) {
#if defined(_WIN32)
    (void)mode;
    std::error_code code;
    if (std::filesystem::exists(std::filesystem::symlink_status(path, code))) {
        error = "something is already there";
        return false;
    }
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "cannot create it";
            return false;
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        if (out) {
            return true;
        }
    }
    error = "cannot write it";
    std::filesystem::remove(path, code);
    return false;
#else
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                          static_cast<mode_t>(mode & std::filesystem::perms::mask));
    if (fd < 0) {
        error = std::error_code{errno, std::generic_category()}.message();
        return false;
    }
    std::size_t written = 0;
    while (written < content.size()) {
        const ssize_t wrote = ::write(fd, content.data() + written, content.size() - written);
        if (wrote < 0 && errno == EINTR) {
            continue;
        }
        if (wrote <= 0) {
            error = std::error_code{wrote < 0 ? errno : EIO, std::generic_category()}.message();
            (void)::close(fd);
            (void)::unlink(path.c_str());
            return false;
        }
        written += static_cast<std::size_t>(wrote);
    }
    if (::close(fd) != 0) {
        error = std::error_code{errno, std::generic_category()}.message();
        (void)::unlink(path.c_str());
        return false;
    }
    return true;
#endif
}

bool create_private_directory(const std::filesystem::path& path, bool& exists) {
    exists = false;
#if defined(_WIN32)
    if (CreateDirectoryW(path.c_str(), nullptr) == 0) {
        exists = GetLastError() == ERROR_ALREADY_EXISTS;
        return false;
    }
    return true;
#else
    if (::mkdir(path.c_str(), S_IRWXU) != 0) {
        exists = errno == EEXIST;
        return false;
    }
    // The mode exactly, whatever the umask took from it: the folder is
    // this process's, and only ever its owner's.
    if (::chmod(path.c_str(), S_IRWXU) != 0) {
        (void)::rmdir(path.c_str());
        return false;
    }
    return true;
#endif
}

bool owned_by_this_user(const std::filesystem::path& path) noexcept {
#if defined(_WIN32)
    // The temporary directory is the user's own: whatever is there is.
    std::error_code code;
    return std::filesystem::exists(std::filesystem::symlink_status(path, code));
#else
    struct ::stat info{};
    if (::lstat(path.c_str(), &info) != 0) {
        return false;
    }
    return info.st_uid == ::geteuid();
#endif
}

std::optional<std::string> home_directory() {
#if defined(_WIN32)
    // Wide, then UTF-8 (57j): the narrow `getenv` handed back the ANSI code
    // page's bytes, which libstdc++ then failed to read as UTF-8 -- every
    // command under `C:\Users\José` exited 1 before it began.
    if (std::optional<std::string> profile = utf8_environment("USERPROFILE");
        profile.has_value() && !profile->empty()) {
        return profile;
    }
    const std::optional<std::string> drive = utf8_environment("HOMEDRIVE");
    const std::optional<std::string> path = utf8_environment("HOMEPATH");
    if (drive.has_value() && !drive->empty() && path.has_value() && !path->empty()) {
        return *drive + *path;
    }
    return std::nullopt;
#else
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::string{home};
    }
    return std::nullopt;
#endif
}

std::optional<std::string> utf8_environment(std::string_view name) {
#if defined(_WIN32)
    const std::wstring wide_name = wide_from_utf8(name);
    std::wstring value(64, L'\0');
    for (;;) {
        SetLastError(ERROR_SUCCESS);
        const DWORD got = GetEnvironmentVariableW(wide_name.c_str(), value.data(),
                                                  static_cast<DWORD>(value.size()));
        if (got == 0) {
            // Not there, or there and empty.
            if (GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
                return std::nullopt;
            }
            return std::string{};
        }
        if (got < value.size()) {
            value.resize(got);
            return utf8_from_wide(value);
        }
        // Too small: `got` is the size it needs, the terminator included --
        // asked again, since another thread may have changed it meanwhile.
        value.assign(got, L'\0');
    }
#else
    const std::string key{name};
    const char* value = std::getenv(key.c_str());  // NOLINT(concurrency-mt-unsafe): read once
    if (value == nullptr) {
        return std::nullopt;
    }
    return std::string{value};
#endif
}

std::filesystem::path path_from_utf8(std::string_view utf8) {
#if defined(_WIN32)
    return std::filesystem::path{wide_from_utf8(utf8)};
#else
    return std::filesystem::path{std::string{utf8}};
#endif
}

std::string text_encoding_gap() {
#if defined(_WIN32)
    const UINT page = GetACP();
    if (page == CP_UTF8) {
        return {};
    }
    return "Windows reads this process's text in code page " + std::to_string(page) +
           ", not UTF-8: this Windows predates 10 version 1903 and ignores the UTF-8 code page "
           "Apogee's manifest names, so a non-ASCII argument, path or environment value may not "
           "arrive as typed";
#else
    return {};
#endif
}

namespace {

// The buffers the standard streams were born with, captured at static
// initialization -- before anything can have swapped one -- through a
// noexcept function: rdbuf() is a plain accessor the standard merely forgot
// to mark so, and a static initializer must not be able to throw.
[[nodiscard]] std::streambuf* born_with(const std::ios& stream) noexcept {
    return stream.rdbuf();
}

std::streambuf* const kStdinBuffer = born_with(std::cin);
std::streambuf* const kStdoutBuffer = born_with(std::cout);
std::streambuf* const kStderrBuffer = born_with(std::cerr);

/// Whether the C++ stream over `stream` still reads or writes its descriptor.
[[nodiscard]] bool reaches_descriptor(StandardStream stream) noexcept {
    switch (stream) {
        case StandardStream::In:
            return std::cin.rdbuf() == kStdinBuffer;
        case StandardStream::Out:
            return std::cout.rdbuf() == kStdoutBuffer;
        case StandardStream::Err:
            return std::cerr.rdbuf() == kStderrBuffer;
    }
    return false;
}

}  // namespace

bool is_terminal(StandardStream stream) noexcept {
    if (!reaches_descriptor(stream)) {
        return false;
    }
#if defined(_WIN32)
    int descriptor = 0;
    switch (stream) {
        case StandardStream::In:
            descriptor = 0;
            break;
        case StandardStream::Out:
            descriptor = 1;
            break;
        case StandardStream::Err:
            descriptor = 2;
            break;
    }
    return _isatty(descriptor) != 0;
#else
    int descriptor = STDIN_FILENO;
    switch (stream) {
        case StandardStream::In:
            descriptor = STDIN_FILENO;
            break;
        case StandardStream::Out:
            descriptor = STDOUT_FILENO;
            break;
        case StandardStream::Err:
            descriptor = STDERR_FILENO;
            break;
    }
    return ::isatty(descriptor) != 0;
#endif
}

std::optional<int> terminal_width() noexcept {
    return terminal_width(StandardStream::Out);
}

std::optional<int> terminal_width(StandardStream stream) noexcept {
#if defined(_WIN32)
    DWORD handle = STD_OUTPUT_HANDLE;
    switch (stream) {
        case StandardStream::In:
            handle = STD_INPUT_HANDLE;
            break;
        case StandardStream::Out:
            handle = STD_OUTPUT_HANDLE;
            break;
        case StandardStream::Err:
            handle = STD_ERROR_HANDLE;
            break;
    }
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (GetConsoleScreenBufferInfo(GetStdHandle(handle), &info) != 0) {
        const int width = info.srWindow.Right - info.srWindow.Left + 1;
        return width > 0 ? std::optional<int>{width} : std::nullopt;
    }
    return std::nullopt;
#else
    int descriptor = STDOUT_FILENO;
    switch (stream) {
        case StandardStream::In:
            descriptor = STDIN_FILENO;
            break;
        case StandardStream::Out:
            descriptor = STDOUT_FILENO;
            break;
        case StandardStream::Err:
            descriptor = STDERR_FILENO;
            break;
    }
    ::winsize size{};
    if (::ioctl(descriptor, TIOCGWINSZ, &size) == 0 && size.ws_col > 0) {
        return static_cast<int>(size.ws_col);
    }
    return std::nullopt;
#endif
}

std::optional<int> terminal_height() noexcept {
#if defined(_WIN32)
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info) != 0) {
        const int height = info.srWindow.Bottom - info.srWindow.Top + 1;
        return height > 0 ? std::optional<int>{height} : std::nullopt;
    }
    return std::nullopt;
#else
    ::winsize size{};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_row > 0) {
        return static_cast<int>(size.ws_row);
    }
    return std::nullopt;
#endif
}

std::string full_screen_refusal() {
#if defined(_WIN32)
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (out == INVALID_HANDLE_VALUE || GetConsoleMode(out, &mode) == 0) {
        return "the output is not a Windows console";
    }
    if ((mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0) {
        return {};
    }
    if (SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) == 0) {
        return "this console does not take virtual-terminal sequences, which a full screen is "
               "drawn with";
    }
    SetConsoleMode(out, mode);  // put back: the shell turns them on itself
    return {};
#else
    const char* term = std::getenv("TERM");  // NOLINT(concurrency-mt-unsafe): read once, at entry
    if (term == nullptr || *term == '\0') {
        return "TERM is not set, so nothing says this terminal can place the cursor";
    }
    if (std::string_view{term} == "dumb") {
        return "this terminal (TERM=dumb) cannot place the cursor";
    }
    return {};
#endif
}

bool renders_escape_sequences(StandardStream stream) noexcept {
#if defined(_WIN32)
    if (stream == StandardStream::In) {
        return true;
    }
    const HANDLE handle = GetStdHandle(standard_handle(stream));
    DWORD mode = 0;
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE || GetConsoleMode(handle, &mode) == 0) {
        // No console -- a pipe, a file, a terminal emulator's own pipe:
        // decoration is `is_terminal`'s call, as it always was.
        return true;
    }
    return (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
    (void)stream;
    return true;
#endif
}

#if defined(_WIN32)
namespace {

/// What the outermost `ConsoleScope` found, to put back. Written before
/// `console_changed` is set and read after it is taken, so the interrupt
/// handler's thread sees it whole.
struct SavedConsole {
    UINT input_page = 0;
    UINT output_page = 0;
    /// Standard output's and standard error's handles, and the mode each had
    /// -- `has_mode` false where the stream is no console.
    std::array<HANDLE, 2> outputs{};
    std::array<DWORD, 2> modes{};
    std::array<bool, 2> has_mode{};
};

SavedConsole saved_console;
std::atomic<bool> console_changed{false};
std::atomic<int> console_depth{0};
/// Whether `restore_on_unhandled_event` is in the process's handler list.
std::atomic<bool> event_routine_added{false};

/// The console put back on the exit Windows takes for an event nothing
/// handled (57j). Windows asks a process's console routines newest first and
/// ends it through `ExitProcess` -- no destructor, no `_Exit` path -- when
/// every one declines. Added before any `signal()` adds the C runtime's own
/// routine, this one is asked only after the runtime declined: a Ctrl-C or
/// Ctrl-Break to a command that stands no handler (`check`, `config`), or one
/// in the instant between the runtime's reset and the handler's re-arm. It
/// declines too, so the process ends as it would have.
BOOL WINAPI restore_on_unhandled_event(DWORD /*event*/) {
    restore_console();
    return FALSE;
}

}  // namespace
#endif

ConsoleScope::ConsoleScope() noexcept {
#if defined(_WIN32)
    if (console_depth.fetch_add(1) > 0) {
        return;  // inside a live scope: the outermost one owns the console
    }
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    const std::array<HANDLE, 2> outputs{GetStdHandle(STD_OUTPUT_HANDLE),
                                        GetStdHandle(STD_ERROR_HANDLE)};
    if (!is_console(input) && !is_console(outputs[0]) && !is_console(outputs[1])) {
        return;
    }
    SavedConsole saved;
    saved.input_page = GetConsoleCP();
    saved.output_page = GetConsoleOutputCP();
    // Both modes read before either changes: two handles on one screen
    // buffer share its mode, and the second read would see the first change.
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        saved.outputs[i] = outputs[i];
        saved.has_mode[i] = outputs[i] != nullptr && outputs[i] != INVALID_HANDLE_VALUE &&
                            GetConsoleMode(outputs[i], &saved.modes[i]) != 0;
    }
    saved_console = saved;
    // Once per process, and first: `RootCommand::run` makes this scope before
    // any command arms a signal, so the routine sits beneath the runtime's.
    if (!event_routine_added.exchange(true)) {
        (void)SetConsoleCtrlHandler(restore_on_unhandled_event, TRUE);
    }
    // Set before anything changes, so a Ctrl-C from here on puts back what
    // was there.
    console_changed.store(true);
    changed_ = true;
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        if (saved.has_mode[i]) {
            // Refused by a console older than virtual-terminal sequences:
            // the mode stays as it was, and `renders_escape_sequences` says so.
            (void)SetConsoleMode(outputs[i], saved.modes[i] | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
    }
    (void)SetConsoleCP(CP_UTF8);
    (void)SetConsoleOutputCP(CP_UTF8);
#endif
}

ConsoleScope::~ConsoleScope() {
#if defined(_WIN32)
    console_depth.fetch_sub(1);
    if (!changed_) {
        return;
    }
    // What the run wrote in UTF-8 reaches the console while it reads UTF-8.
    std::cout.flush();
    std::cerr.flush();
    restore_console();
#endif
}

void restore_console() noexcept {
#if defined(_WIN32)
    if (!console_changed.exchange(false)) {
        return;
    }
    const SavedConsole& saved = saved_console;
    for (std::size_t i = saved.outputs.size(); i-- > 0;) {
        if (saved.has_mode[i]) {
            (void)SetConsoleMode(saved.outputs[i], saved.modes[i]);
        }
    }
    (void)SetConsoleCP(saved.input_page);
    (void)SetConsoleOutputCP(saved.output_page);
#endif
}

void rearm_signal(int signal, void (*handler)(int)) noexcept {
#if defined(_WIN32)
    (void)std::signal(signal, handler);
#else
    (void)signal;
    (void)handler;
#endif
}

#if !defined(_WIN32)
namespace {

// The typeahead guard's state, read by its signal handlers: plain data, set
// before a handler can run. One guard at a time owns it.
volatile std::sig_atomic_t typeahead_live = 0;
::termios typeahead_saved{};
::termios typeahead_quiet{};

constexpr std::array<int, 5> kRestoringSignals{SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGTSTP};
std::array<struct ::sigaction, kRestoringSignals.size()> typeahead_previous{};

[[nodiscard]] std::size_t signal_slot(int signal) noexcept {
    for (std::size_t i = 0; i < kRestoringSignals.size(); ++i) {
        if (kRestoringSignals[i] == signal) {
            return i;
        }
    }
    return 0;
}

void unblock(int signal) noexcept {
    ::sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, signal);
    ::sigprocmask(SIG_UNBLOCK, &set, nullptr);
}

extern "C" void typeahead_signal(int signal);

void install_typeahead_handler(int signal) noexcept {
    struct ::sigaction action{};
    action.sa_handler = typeahead_signal;
    sigemptyset(&action.sa_mask);
    ::sigaction(signal, &action, nullptr);
}

// Async-signal-safe throughout: tcsetattr, sigaction, sigprocmask and raise
// are all on POSIX's list.
extern "C" void typeahead_signal(int signal) {
    ::tcsetattr(STDIN_FILENO, TCSANOW, &typeahead_saved);
    if (signal == SIGTSTP) {
        // Stop as the shell expects, with echo back on; hide again on resume.
        struct ::sigaction stop{};
        stop.sa_handler = SIG_DFL;
        sigemptyset(&stop.sa_mask);
        ::sigaction(SIGTSTP, &stop, nullptr);
        unblock(SIGTSTP);
        ::raise(SIGTSTP);
        ::tcsetattr(STDIN_FILENO, TCSANOW, &typeahead_quiet);
        install_typeahead_handler(SIGTSTP);
        return;
    }
    // Whatever the process did with this signal before, it does now.
    ::sigaction(signal, &typeahead_previous[signal_slot(signal)], nullptr);
    unblock(signal);
    ::raise(signal);
    // Still here: the previous action was a handler that returned, or ignore.
    if (typeahead_live != 0) {
        ::tcsetattr(STDIN_FILENO, TCSANOW, &typeahead_quiet);
        install_typeahead_handler(signal);
    }
}

}  // namespace
#endif

TypeaheadGuard::TypeaheadGuard() noexcept {
#if !defined(_WIN32)
    if (typeahead_live != 0 || !is_terminal(StandardStream::In) ||
        ::tcgetattr(STDIN_FILENO, &typeahead_saved) != 0) {
        return;
    }
    typeahead_quiet = typeahead_saved;
    typeahead_quiet.c_lflag &= ~static_cast<::tcflag_t>(ECHO | ECHONL);
    for (std::size_t i = 0; i < kRestoringSignals.size(); ++i) {
        ::sigaction(kRestoringSignals[i], nullptr, &typeahead_previous[i]);
    }
    typeahead_live = 1;
    for (const int signal : kRestoringSignals) {
        install_typeahead_handler(signal);
    }
    if (::tcsetattr(STDIN_FILENO, TCSANOW, &typeahead_quiet) != 0) {
        for (std::size_t i = 0; i < kRestoringSignals.size(); ++i) {
            ::sigaction(kRestoringSignals[i], &typeahead_previous[i], nullptr);
        }
        typeahead_live = 0;
        return;
    }
    active_ = true;
#endif
}

TypeaheadGuard::~TypeaheadGuard() {
#if !defined(_WIN32)
    if (!active_) {
        return;
    }
    ::tcsetattr(STDIN_FILENO, TCSANOW, &typeahead_saved);
    for (std::size_t i = 0; i < kRestoringSignals.size(); ++i) {
        ::sigaction(kRestoringSignals[i], &typeahead_previous[i], nullptr);
    }
    typeahead_live = 0;
#endif
}

EchoPause::EchoPause() noexcept {
#if !defined(_WIN32)
    if (typeahead_live != 0 && ::tcsetattr(STDIN_FILENO, TCSANOW, &typeahead_saved) == 0) {
        paused_ = true;
    }
#endif
}

EchoPause::~EchoPause() {
#if !defined(_WIN32)
    if (paused_ && typeahead_live != 0) {
        ::tcsetattr(STDIN_FILENO, TCSANOW, &typeahead_quiet);
    }
#endif
}

void discard_pending_input() noexcept {
    if (!is_terminal(StandardStream::In)) {
        // A pipe or a heredoc: those bytes are the input, not typeahead.
        return;
    }
#if defined(_WIN32)
    FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
#else
    // Ignored on failure: this is an ergonomic nicety, not a precondition.
    static_cast<void>(::tcflush(STDIN_FILENO, TCIFLUSH));
#endif
}

std::optional<std::string> read_hidden_line(std::string_view prompt) {
    std::cerr << prompt << std::flush;
    std::string line;
    if (!is_terminal(StandardStream::In)) {
        if (!std::getline(std::cin, line)) {
            return std::nullopt;
        }
        return line;
    }
#if defined(_WIN32)
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    const bool had_mode = GetConsoleMode(input, &mode) != 0;
    if (had_mode) {
        SetConsoleMode(input, mode & ~static_cast<DWORD>(ENABLE_ECHO_INPUT));
    }
    const bool got = static_cast<bool>(std::getline(std::cin, line));
    if (had_mode) {
        SetConsoleMode(input, mode);
    }
#else
    ::termios saved{};
    const bool had_mode = ::tcgetattr(STDIN_FILENO, &saved) == 0;
    if (had_mode) {
        ::termios quiet = saved;
        quiet.c_lflag &= static_cast<tcflag_t>(~ECHO);
        static_cast<void>(::tcsetattr(STDIN_FILENO, TCSAFLUSH, &quiet));
    }
    const bool got = static_cast<bool>(std::getline(std::cin, line));
    if (had_mode) {
        static_cast<void>(::tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved));
    }
#endif
    std::cerr << "\n";
    if (!got) {
        return std::nullopt;
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return line;
}

std::string host_target() {
    std::string target{to_string(host_os())};
    target += "-";
    target += to_string(host_architecture());
    return target;
}

}  // namespace apogee::platform
