#pragma once

#include <cstdint>
#include <string>
#include <string_view>

/// ANSI styling, and the one place that decides whether to emit it at all.
///
/// The decision is not a single flag. Three independent inputs can each turn
/// color off — `NO_COLOR` in the environment, an explicit `--no-color`, and
/// stdout not being a terminal — and every one of them has to be honoured
/// separately. Scattering that logic means one output path eventually forgets
/// a case and writes escape codes into a pipe.
namespace apogee::ansi {

enum class ColorMode : std::uint8_t {
    /// Colour when stdout is a terminal and NO_COLOR is unset.
    Auto,
    /// Colour regardless. For a caller piping into something that renders it.
    Always,
    /// Never colour. What `--no-color` sets.
    Never,
};

/// How much operational output a surface emits. Orthogonal to colour: a quiet
/// run on a terminal is still coloured, and a verbose run on a pipe is still
/// plain.
enum class Verbosity : std::uint8_t {
    /// One self-overwriting status line on a terminal; plain lines elsewhere.
    Line,
    /// Every status message as a permanent line. For logs and debugging.
    Verbose,
    /// No status output. **Warnings and errors still appear** -- suppressing
    /// those would make a failed run look like a successful silent one.
    Quiet,
};

enum class Color : std::uint8_t {
    Default,
    Red,
    Green,
    Yellow,
    Blue,
    Magenta,
    Cyan,
    White,
    BrightBlack,
};

/// The roles that get a `[tag]` prefix. Named rather than free strings so the
/// colour mapping is exhaustive and a new role cannot be added without one.
enum class Role : std::uint8_t { Apogee, Tool, Rag, Permission, Warning, Error, Agent };

[[nodiscard]] std::string_view to_string(Role role) noexcept;

/// Resolves whether colour should be emitted.
///
/// `no_color_set` is whether the `NO_COLOR` environment variable is present --
/// per the convention, its *presence* disables colour regardless of value.
[[nodiscard]] bool resolve_color(ColorMode mode, bool stdout_is_terminal,
                                 bool no_color_set) noexcept;

/// Styling with the decision already made.
///
/// When colour is off every method returns its input unchanged, so a caller
/// never branches on it. That is the point: a code path that has to remember
/// to check is a code path that will eventually forget.
class Style {
public:
    Style() = default;

    explicit Style(bool color_enabled) : enabled_{color_enabled} {}

    /// Builds from the environment: reads NO_COLOR and asks the platform
    /// whether stdout is a terminal.
    [[nodiscard]] static Style detect(ColorMode mode);

    [[nodiscard]] bool color_enabled() const noexcept {
        return enabled_;
    }

    [[nodiscard]] std::string dim(std::string_view text) const;
    [[nodiscard]] std::string bold(std::string_view text) const;
    [[nodiscard]] std::string colorize(std::string_view text, Color color) const;

    /// A `[role]` prefix in that role's colour, e.g. `[apogee]`.
    [[nodiscard]] std::string tag(Role role) const;

private:
    bool enabled_ = false;
};

/// The escape sequence for `color`, or empty when Default.
[[nodiscard]] std::string_view color_code(Color color) noexcept;

/// Reset. Exposed so tests can assert on exact byte sequences.
inline constexpr std::string_view kReset = "\033[0m";
inline constexpr std::string_view kDim = "\033[2m";
inline constexpr std::string_view kBold = "\033[1m";

/// Erase the current line and return the cursor to column 0.
inline constexpr std::string_view kEraseLine = "\r\033[2K";
/// Move up one row and erase it.
inline constexpr std::string_view kUpAndErase = "\033[A\033[2K";

}  // namespace apogee::ansi
