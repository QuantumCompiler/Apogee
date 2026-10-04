#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

/// The daily operational log.
///
/// Separate from a session file and answering a different question. A session
/// records **what was said**; this records **what the program did** — which
/// backend was constructed, which tool ran, what failed. When a user reports
/// "it hung", the session file shows a conversation that looks fine and this
/// shows the request that never came back.
///
/// It is deliberately not a general logging framework. One file per day, append
/// only, one line per event, no levels beyond a tag. Anything more is a
/// dependency and a configuration surface for something nobody reads until
/// something breaks.
namespace apogee::logger {

enum class Level : std::uint8_t { Info, Warn, Error };

[[nodiscard]] std::string_view to_string(Level level) noexcept;

/// `<APOGEE_HOME>/logs`.
[[nodiscard]] std::filesystem::path logs_dir();

/// Today's log file: `<APOGEE_HOME>/logs/apogee-YYYY-MM-DD.log`.
[[nodiscard]] std::filesystem::path today_log_path();

/// Formats one line. Exposed so tests assert the format without a filesystem.
[[nodiscard]] std::string format_line(Level level, std::string_view component,
                                      std::string_view message, std::string_view timestamp);

/// Appends one event.
///
/// **Never throws.** A logging failure must not take down the command that was
/// being logged — a full disk should degrade to a missing log line, not a
/// failed conversation.
void log(Level level, std::string_view component, std::string_view message) noexcept;

/// Whether logging is enabled for this process. Off in tests by default so a
/// suite does not write into a real home directory.
void set_enabled(bool enabled) noexcept;
[[nodiscard]] bool enabled() noexcept;

}  // namespace apogee::logger
