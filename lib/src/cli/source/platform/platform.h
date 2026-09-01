#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

/// The portability seam.
///
/// Apogee ships six targets: linux/macos/windows x x64/arm64. OS-specific
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
/// "linux-x64", "linux-arm64", "macos-x64", "macos-arm64", "windows-x64",
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
/// Callers wanting Apogee's data directory should ask
/// `apogee::harness::apogee_home()` instead -- it layers the APOGEE_HOME
/// override on top of this.
[[nodiscard]] std::optional<std::string> home_directory();

/// The three standard streams, for terminal detection.
enum class StandardStream : std::uint8_t { In, Out, Err };

/// Whether `stream` is attached to a terminal.
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

}  // namespace apogee::platform
