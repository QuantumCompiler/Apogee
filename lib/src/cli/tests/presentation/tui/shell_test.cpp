#include "tui/shell.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "tui/pump.h"
#include "tui/view.h"

/// The shell (32b), headless: FTXUI renders to text, so the frame, focus
/// moving between the views and every key the shell owns pin as strings --
/// no terminal, through `tui/`'s own API, which names none of FTXUI's types.
namespace {

using apogee::tui::Key;
using apogee::tui::Shell;

[[nodiscard]] apogee::tui::ShellOptions plain() {
    return apogee::tui::ShellOptions{.title = "apogee test", .theme = {.color = false}};
}

void add_views(Shell& shell) {
    shell.add(apogee::tui::text_view("Home", {"first line", "second line", "third line"}));
    shell.add(apogee::tui::text_view("Keys", Shell::key_lines()));
}

/// A view that takes typing: a text body, flagged as one -- what a session's
/// input is (32c), with nothing of its own to type into here.
[[nodiscard]] apogee::tui::View typing_view() {
    const apogee::tui::View page = apogee::tui::text_view("Typing", {"type here"});
    return apogee::tui::View{"Typing", page.body(), /*takes_text=*/true};
}

}  // namespace

TEST_CASE("an empty shell draws its frame and says there is nothing yet", "[tui][shell]") {
    Shell shell{plain()};
    CHECK(shell.size() == 0);
    CHECK(shell.render_text(48, 8) ==
          " apogee test\n"
          "────────────────────────────────────────────────\n"
          " nothing to show yet\n"
          "\n"
          "\n"
          "\n"
          "────────────────────────────────────────────────\n"
          " q quit · Tab next view · 1–9 a view\n");
}

TEST_CASE(
    "the frame: the title, the views numbered with the active one bracketed, the stage, "
    "the hints",
    "[tui][shell]") {
    Shell shell{plain()};
    add_views(shell);
    CHECK(shell.active() == 0);
    CHECK(shell.render_text(48, 8) ==
          " apogee test                   [1 Home] 2 Keys\n"
          "────────────────────────────────────────────────\n"
          " first line\n"
          " second line\n"
          " third line\n"
          "\n"
          "────────────────────────────────────────────────\n"
          " q quit · Tab next view · 1–9 a view\n");
}

TEST_CASE("focus moves through the views with Tab and back with Shift+Tab, wrapping",
          "[tui][shell]") {
    Shell shell{plain()};
    add_views(shell);
    CHECK(shell.press(Key::named(Key::Name::Tab)));
    CHECK(shell.active() == 1);
    CHECK(shell.render_text(72, 12) ==
          " apogee test                                            1 Home [2 Keys]\n"
          "────────────────────────────────────────────────────────────────────────\n"
          " F1–F9, Alt+1–9     show a view by its number\n"
          " 1–9                the same, while the view is not taking typing\n"
          " Tab, Shift+Tab     the next view, the previous one\n"
          " ↑ ↓ Page Up/Down   scroll the view\n"
          " q                  quit, while the view is not taking typing\n"
          " Ctrl-D             quit\n"
          " Ctrl-C             quit, once the view has nothing running to stop\n"
          "\n"
          "────────────────────────────────────────────────────────────────────────\n"
          " q quit · Tab next view · 1–9 a view\n");
    CHECK(shell.press(Key::named(Key::Name::Tab)));
    CHECK(shell.active() == 0);
    CHECK(shell.press(Key::named(Key::Name::BackTab)));
    CHECK(shell.active() == 1);
    CHECK(shell.press(Key::named(Key::Name::BackTab)));
    CHECK(shell.active() == 0);
}

TEST_CASE(
    "a view by its number: F-keys, Alt and a bare digit; a number naming none is "
    "swallowed",
    "[tui][shell]") {
    Shell shell{plain()};
    add_views(shell);
    CHECK(shell.press(Key::function(2)));
    CHECK(shell.active() == 1);
    CHECK(shell.press(Key::alt("1")));
    CHECK(shell.active() == 0);
    CHECK(shell.press(Key::character("2")));
    CHECK(shell.active() == 1);
    CHECK(shell.press(Key::character("9")));
    CHECK(shell.active() == 1);
    CHECK(shell.press(Key::function(1)));
    CHECK(shell.render_text(48, 3).starts_with(" apogee test                   [1 Home] 2 Keys\n"));
}

TEST_CASE("q, Ctrl-C with nothing to stop, and Ctrl-D end the shell through its pump",
          "[tui][shell]") {
    for (const Key& key :
         {Key::character("q"), Key::named(Key::Name::CtrlC), Key::named(Key::Name::CtrlD)}) {
        Shell shell{plain()};
        add_views(shell);
        int exits = 0;
        shell.on_quit([&exits]() { ++exits; });
        CHECK_FALSE(shell.quit_requested());
        CHECK(shell.press(key));
        CHECK(shell.quit_requested());
        CHECK(exits == 1);
    }
}

TEST_CASE("a view that takes typing keeps its digits and q; F-keys still switch", "[tui][shell]") {
    Shell shell{plain()};
    shell.add(typing_view());
    add_views(shell);
    (void)shell.press(Key::character("2"));
    (void)shell.press(Key::character("q"));
    CHECK(shell.active() == 0);
    CHECK_FALSE(shell.quit_requested());
    CHECK(shell.render_text(48, 6).ends_with(" Ctrl-D quit · Tab next view · F1–F9 a view\n"));
    CHECK(shell.press(Key::function(3)));
    CHECK(shell.active() == 2);
    CHECK(shell.render_text(48, 6).ends_with(" q quit · Tab next view · 1–9 a view\n"));
}

TEST_CASE("a notice takes its own row above the bottom bar, the latest one alone", "[tui][shell]") {
    Shell shell{plain()};
    add_views(shell);
    shell.notice("the first thing said");
    shell.notice("a library said something");
    CHECK(shell.render_text(48, 8) ==
          " apogee test                   [1 Home] 2 Keys\n"
          "────────────────────────────────────────────────\n"
          " first line\n"
          " second line\n"
          " third line\n"
          " a library said something\n"
          "────────────────────────────────────────────────\n"
          " q quit · Tab next view · 1–9 a view\n");
}

TEST_CASE("a text view scrolls with the arrows and Page keys, never past its ends",
          "[tui][shell]") {
    Shell shell{plain()};
    add_views(shell);
    CHECK(shell.press(Key::named(Key::Name::Down)));
    CHECK(shell.render_text(48, 8).find(" second line\n third line\n") != std::string::npos);
    CHECK(shell.render_text(48, 8).find("first line") == std::string::npos);
    CHECK(shell.press(Key::named(Key::Name::PageDown)));
    CHECK(shell.render_text(48, 8).find(" third line\n") != std::string::npos);
    CHECK(shell.render_text(48, 8).find("second line") == std::string::npos);
    CHECK(shell.press(Key::named(Key::Name::PageUp)));
    CHECK(shell.render_text(48, 8).find(" first line\n") != std::string::npos);
}

TEST_CASE("colour changes nothing the frame says", "[tui][shell]") {
    Shell plain_shell{plain()};
    Shell coloured{apogee::tui::ShellOptions{.title = "apogee test", .theme = {.color = true}}};
    add_views(plain_shell);
    add_views(coloured);
    CHECK(plain_shell.render_text(60, 10) == coloured.render_text(60, 10));
}

TEST_CASE("a manual pump runs what was posted, in order, when drained", "[tui][pump]") {
    apogee::tui::ManualPump pump;
    std::vector<int> ran;
    pump.post([&ran]() { ran.push_back(1); });
    pump.post([&ran, &pump]() {
        ran.push_back(2);
        pump.post([&ran]() { ran.push_back(3); });
    });
    CHECK(ran.empty());
    CHECK(pump.drain() == 3);
    CHECK(ran == std::vector<int>{1, 2, 3});
    CHECK_FALSE(pump.exited());
    pump.exit();
    CHECK(pump.exited());
}
