#pragma once

#include <cstdint>
#include <functional>
#include <optional>
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

/// What a run of text looks like: the attributes one SGR sequence can carry.
///
/// Composed rather than nested: `**bold _and italic_**` is one span with two
/// attributes, painted as one sequence and one reset. Nesting `Style::bold`
/// inside `Style::dim` would reset both at the inner reset and leave the rest
/// of the span plain.
struct TextAttributes {
    bool bold = false;
    bool dim = false;
    bool italic = false;
    bool underline = false;
    bool strike = false;
    Color color = Color::Default;

    [[nodiscard]] bool plain() const noexcept {
        return !bold && !dim && !italic && !underline && !strike && color == Color::Default;
    }

    bool operator==(const TextAttributes&) const = default;
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

    /// `text` in one SGR sequence for `attributes`, then a reset; `text`
    /// unchanged when colour is off or the attributes are plain.
    [[nodiscard]] std::string paint(std::string_view text, const TextAttributes& attributes) const;

private:
    bool enabled_ = false;
};

/// The escape sequence for `color`, or empty when Default.
[[nodiscard]] std::string_view color_code(Color color) noexcept;

/// Reset. Exposed so tests can assert on exact byte sequences.
inline constexpr std::string_view kReset = "\033[0m";
inline constexpr std::string_view kDim = "\033[2m";
inline constexpr std::string_view kBold = "\033[1m";

/// `text` as an OSC 8 hyperlink to `url`. The escapes take no cells, so the
/// text's display width is unchanged. A caller emits one only where
/// `hyperlinks_supported` says the terminal understands it.
[[nodiscard]] std::string hyperlink(std::string_view text, std::string_view url);

/// Reads one environment variable; nullopt when unset.
using EnvLookup = std::function<std::optional<std::string>(std::string_view name)>;

/// Whether the terminal is known to turn OSC 8 into a clickable link.
///
/// **An allow-list, not a guess.** There is no query a program can make, and
/// a terminal that does not understand the sequence may print it: so only the
/// terminals that announce themselves and are known to support it get links
/// (iTerm2, WezTerm, VS Code, Ghostty, kitty, Windows Terminal, VTE 0.50+).
/// Everything else gets `text (url)`, which works everywhere.
[[nodiscard]] bool hyperlinks_supported(const EnvLookup& env);

/// The same, asked of the process environment.
[[nodiscard]] bool hyperlinks_supported();

/// Erase the current line and return the cursor to column 0.
inline constexpr std::string_view kEraseLine = "\r\033[2K";
/// Move up one row and erase it.
inline constexpr std::string_view kUpAndErase = "\033[A\033[2K";

}  // namespace apogee::ansi
