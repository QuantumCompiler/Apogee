#include "ansi/ansi.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using apogee::ansi::Color;
using apogee::ansi::ColorMode;
using apogee::ansi::resolve_color;
using apogee::ansi::Role;
using apogee::ansi::Style;

TEST_CASE("each of the three inputs disables colour independently", "[ux][ansi]") {
    // Three separate switches, and every one has to be honoured on its own.
    // Scattering this logic is how one output path eventually forgets a case
    // and writes escape codes into a pipe.
    struct Case {
        ColorMode mode;
        bool is_terminal;
        bool no_color_set;
        bool expected;
        const char* why;
    };

    const Case cases[] = {
        {ColorMode::Auto, true, false, true, "a terminal with nothing set"},
        {ColorMode::Auto, false, false, false, "not a terminal (a pipe)"},
        {ColorMode::Auto, true, true, false, "NO_COLOR is present"},
        {ColorMode::Auto, false, true, false, "both"},
        {ColorMode::Never, true, false, false, "--no-color beats a terminal"},
        {ColorMode::Never, true, true, false, "--no-color with NO_COLOR"},
        {ColorMode::Always, false, false, true, "--color beats a pipe"},
        {ColorMode::Always, false, true, true, "explicit intent beats NO_COLOR"},
    };

    for (const Case& c : cases) {
        INFO(c.why);
        CHECK(resolve_color(c.mode, c.is_terminal, c.no_color_set) == c.expected);
    }
}

TEST_CASE("NO_COLOR disables colour by its presence, whatever the value", "[ux][ansi]") {
    // Per the convention: NO_COLOR=0 still means no colour. Reading the value
    // is the mistake this pins.
    CHECK_FALSE(resolve_color(ColorMode::Auto, true, true));
}

TEST_CASE("a disabled style returns its input unchanged", "[ux][ansi]") {
    // So no caller has to branch on colour. A code path that has to remember
    // to check is one that will eventually forget.
    const Style plain{false};

    CHECK(plain.dim("text") == "text");
    CHECK(plain.bold("text") == "text");
    CHECK(plain.colorize("text", Color::Red) == "text");
    CHECK(plain.tag(Role::Tool) == "[tool]");
    CHECK_FALSE(plain.color_enabled());
}

TEST_CASE("an enabled style wraps in escape codes and always resets", "[ux][ansi]") {
    // An unreset sequence bleeds into every later line the terminal prints,
    // including the user's shell prompt.
    const Style colored{true};

    const std::string dim = colored.dim("text");
    CHECK(dim.find(apogee::ansi::kDim) != std::string::npos);
    CHECK(dim.find("text") != std::string::npos);
    CHECK(dim.find(apogee::ansi::kReset) != std::string::npos);

    const std::string red = colored.colorize("text", Color::Red);
    CHECK(red.find("\033[31m") != std::string::npos);
    CHECK(red.rfind(apogee::ansi::kReset) == red.size() - apogee::ansi::kReset.size());
}

TEST_CASE("Color::Default emits no escape code", "[ux][ansi]") {
    const Style colored{true};
    CHECK(colored.colorize("text", Color::Default) == "text");
    CHECK(apogee::ansi::color_code(Color::Default).empty());
}

TEST_CASE("every role has a name and a colour", "[ux][ansi]") {
    const Style colored{true};
    for (const Role role : {Role::Apogee, Role::Tool, Role::Rag, Role::Permission, Role::Warning,
                            Role::Error, Role::Agent}) {
        const std::string name{apogee::ansi::to_string(role)};
        CHECK_FALSE(name.empty());
        const std::string tag = colored.tag(role);
        CHECK(tag.find("[" + name + "]") != std::string::npos);
        // Every role is coloured -- a role with no colour would be a silent
        // gap in the mapping.
        CHECK(tag.find("\033[") != std::string::npos);
    }
}
