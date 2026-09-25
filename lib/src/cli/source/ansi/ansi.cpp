#include "ansi/ansi.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <utility>

#include "platform/platform.h"

namespace apogee::ansi {
namespace {

constexpr std::array<std::pair<Role, std::string_view>, 7> kRoleNames{{
    {Role::Apogee, "apogee"},
    {Role::Tool, "tool"},
    {Role::Rag, "rag"},
    {Role::Permission, "perm"},
    {Role::Warning, "warn"},
    {Role::Error, "error"},
    {Role::Agent, "agent"},
}};

Color color_for(Role role) noexcept {
    switch (role) {
        case Role::Apogee:
            return Color::Cyan;
        case Role::Tool:
            return Color::Yellow;
        case Role::Rag:
            return Color::Blue;
        case Role::Permission:
            return Color::Magenta;
        case Role::Warning:
            return Color::Yellow;
        case Role::Error:
            return Color::Red;
        case Role::Agent:
            return Color::Magenta;
    }
    return Color::Default;
}

}  // namespace

std::string_view to_string(Role role) noexcept {
    for (const auto& [candidate, name] : kRoleNames) {
        if (candidate == role) {
            return name;
        }
    }
    return "apogee";
}

std::string_view color_code(Color color) noexcept {
    switch (color) {
        case Color::Default:
            return {};
        case Color::Red:
            return "\033[31m";
        case Color::Green:
            return "\033[32m";
        case Color::Yellow:
            return "\033[33m";
        case Color::Blue:
            return "\033[34m";
        case Color::Magenta:
            return "\033[35m";
        case Color::Cyan:
            return "\033[36m";
        case Color::White:
            return "\033[37m";
        case Color::BrightBlack:
            return "\033[90m";
    }
    return {};
}

bool resolve_color(ColorMode mode, bool stdout_is_terminal, bool no_color_set) noexcept {
    switch (mode) {
        case ColorMode::Never:
            return false;
        case ColorMode::Always:
            // Explicit intent beats detection -- a caller piping into something
            // that renders escape codes has asked for them.
            return true;
        case ColorMode::Auto:
            break;
    }
    // Per the NO_COLOR convention its PRESENCE disables colour, whatever the
    // value: NO_COLOR=0 still means no colour.
    return stdout_is_terminal && !no_color_set;
}

Style Style::detect(ColorMode mode) {
    const bool no_color_set = std::getenv("NO_COLOR") != nullptr;
    const bool is_terminal = platform::is_terminal(platform::StandardStream::Out);
    return Style{resolve_color(mode, is_terminal, no_color_set)};
}

std::string Style::dim(std::string_view text) const {
    if (!enabled_) {
        return std::string{text};
    }
    return std::string{kDim} + std::string{text} + std::string{kReset};
}

std::string Style::bold(std::string_view text) const {
    if (!enabled_) {
        return std::string{text};
    }
    return std::string{kBold} + std::string{text} + std::string{kReset};
}

std::string Style::colorize(std::string_view text, Color color) const {
    const std::string_view code = color_code(color);
    if (!enabled_ || code.empty()) {
        return std::string{text};
    }
    return std::string{code} + std::string{text} + std::string{kReset};
}

std::string Style::tag(Role role) const {
    return colorize("[" + std::string{to_string(role)} + "]", color_for(role));
}

std::string Style::paint(std::string_view text, const TextAttributes& attributes) const {
    if (!enabled_ || attributes.plain()) {
        return std::string{text};
    }
    std::string codes;
    const auto add = [&codes](std::string_view code) {
        codes += codes.empty() ? "" : ";";
        codes += code;
    };
    if (attributes.bold) {
        add("1");
    }
    if (attributes.dim) {
        add("2");
    }
    if (attributes.italic) {
        add("3");
    }
    if (attributes.underline) {
        add("4");
    }
    if (attributes.strike) {
        add("9");
    }
    if (const std::string_view color = color_code(attributes.color); !color.empty()) {
        // color_code is a whole sequence ("\033[36m"); only its parameter goes
        // into the combined one.
        add(color.substr(2, color.size() - 3));
    }
    return "\033[" + codes + "m" + std::string{text} + std::string{kReset};
}

std::string hyperlink(std::string_view text, std::string_view url) {
    return "\033]8;;" + std::string{url} + "\033\\" + std::string{text} + "\033]8;;\033\\";
}

bool hyperlinks_supported(const EnvLookup& env) {
    const auto value = [&env](std::string_view name) {
        return env ? env(name).value_or(std::string{}) : std::string{};
    };
    static constexpr std::array<std::string_view, 4> kPrograms{"iTerm.app", "WezTerm", "vscode",
                                                               "ghostty"};
    const std::string program = value("TERM_PROGRAM");
    if (std::ranges::find(kPrograms, program) != kPrograms.end()) {
        return true;
    }
    if (!value("KITTY_WINDOW_ID").empty() || value("TERM") == "xterm-kitty" ||
        !value("WT_SESSION").empty()) {
        return true;
    }
    // VTE (GNOME Terminal, Tilix, ...) gained OSC 8 in 0.50, which it
    // announces as VTE_VERSION=5000.
    const std::string vte = value("VTE_VERSION");
    if (!vte.empty() && std::ranges::all_of(vte, [](char c) { return c >= '0' && c <= '9'; })) {
        return vte.size() > 4 || (vte.size() == 4 && vte >= "5000");
    }
    return false;
}

bool hyperlinks_supported() {
    return hyperlinks_supported([](std::string_view name) -> std::optional<std::string> {
        const char* value = std::getenv(std::string{name}.c_str());
        return value == nullptr ? std::nullopt : std::optional<std::string>{value};
    });
}

}  // namespace apogee::ansi
