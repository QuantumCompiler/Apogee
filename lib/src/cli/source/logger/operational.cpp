#include "logger/operational.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

#include "harness/layout.h"
#include "harness/paths.h"

namespace apogee::logger {
namespace {

std::atomic<bool> g_enabled{true};
std::mutex g_mutex;

std::tm utc_now() {
    const std::time_t as_time =
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &as_time);
#else
    gmtime_r(&as_time, &utc);
#endif
    return utc;
}

}  // namespace

std::string_view to_string(Level level) noexcept {
    switch (level) {
        case Level::Info:
            return "INFO";
        case Level::Warn:
            return "WARN";
        case Level::Error:
            return "ERROR";
    }
    return "INFO";
}

std::filesystem::path logs_dir() {
    // Forwards to the one declaration of the on-disk contract. This used to
    // spell the directory name itself, which is how three files came to own
    // the layout between them -- see harness/layout.h.
    return harness::logs_dir();
}

std::filesystem::path today_log_path() {
    std::tm utc = utc_now();
    std::ostringstream name;
    name << "apogee-" << std::put_time(&utc, "%Y-%m-%d") << ".log";
    return logs_dir() / name.str();
}

std::string format_line(Level level, std::string_view component, std::string_view message,
                        std::string_view timestamp) {
    std::ostringstream out;
    out << timestamp << " " << to_string(level) << " [" << component << "] " << message;

    // Newlines would break the one-event-per-line contract that makes the file
    // greppable, which is the only thing anyone ever does with it.
    std::string line = out.str();
    for (char& c : line) {
        if (c == '\n' || c == '\r') {
            c = ' ';
        }
    }
    return line + "\n";
}

void set_enabled(bool enabled) noexcept {
    g_enabled.store(enabled);
}

bool enabled() noexcept {
    return g_enabled.load();
}

void log(Level level, std::string_view component, std::string_view message) noexcept {
    if (!g_enabled.load()) {
        return;
    }
    try {
        std::tm utc = utc_now();
        std::ostringstream stamp;
        stamp << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");

        const std::filesystem::path path = today_log_path();

        const std::lock_guard<std::mutex> guard{g_mutex};
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);

        // Append, not rewrite: the operational log is a stream of events, and
        // several processes may be running at once.
        std::ofstream out(path, std::ios::app);
        if (out) {
            out << format_line(level, component, message, stamp.str());
        }
    } catch (...) {
        // Deliberately swallowed. A full disk degrades to a missing log line,
        // never to a failed conversation.
    }
}

}  // namespace apogee::logger
