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
    CHECK(shell.render_text(72, 15) ==
          " apogee test                                            1 Home [2 Keys]\n"
          "────────────────────────────────────────────────────────────────────────\n"
          " Ctrl-G             go to any view: categories and their views\n"
          " F1–F9, Alt+1–9     a view of this category by its number\n"
          " 1–9                the same, while the view is not taking typing\n"
          " Tab, Shift+Tab     the next view of this category, the previous one\n"
          " ↑ ↓ Page Up/Down   scroll the view\n"
          " q                  quit, while the view is not taking typing\n"
          " Ctrl-D             quit\n"
          " Ctrl-C             quit, once the view has nothing running to stop\n"
          " :                  a command, scoped to the view (:pull in Models)\n"
          " !                  a command, unscoped; Tab completes, Esc closes\n"
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

TEST_CASE("the tab strip fits any width, the shown view always named", "[tui][shell]") {
    // 37b: track 37's views outgrow an 80-column strip. Past the width the
    // views not shown become their numbers; past that, a window around the
    // shown one, an ellipsis where the strip goes on.
    Shell shell{{.title = "apogee test", .theme = {.color = false}}};
    for (const char* title : {"Session", "Models", "Chats", "Suites", "Config", "Check",
                              "Providers", "System", "Home", "Keys"}) {
        shell.add(apogee::tui::text_view(title, {std::string{title} + " page"}));
    }
    const auto strip = [&shell](int width) {
        const std::string frame = shell.render_text(width, 6);
        return frame.substr(0, frame.find('\n'));
    };
    const std::string wide = strip(160);
    CHECK(wide.find("apogee test") != std::string::npos);
    CHECK(wide.find("[1 Session]") != std::string::npos);
    CHECK(wide.find(" 7 Providers ") != std::string::npos);

    const std::string narrow = strip(80);
    CHECK(narrow.find("[1 Session]") != std::string::npos);
    CHECK(narrow.find(" 7 ") != std::string::npos);
    CHECK(narrow.find("Providers") == std::string::npos);

    shell.activate(6);
    const std::string tight = strip(30);
    CHECK(tight.find("[7 Providers]") != std::string::npos);
    CHECK(tight.find("…") != std::string::npos);
    CHECK(tight.find("apogee test") == std::string::npos);
}

namespace {

/// Views in three categories, a typing one first: what the shell's own
/// registration (38) looks like, smaller.
void add_categorized(Shell& shell) {
    apogee::tui::View session = typing_view();
    session.set_category("Modes");
    shell.add(std::move(session));
    for (const auto& [title, category] :
         std::vector<std::pair<std::string, std::string>>{{"Chats", "Modes"},
                                                          {"Suites", "Studio"},
                                                          {"Agents", "Studio"},
                                                          {"Config", "Setup"},
                                                          {"Keys", "Setup"}}) {
        apogee::tui::View view = apogee::tui::text_view(title, {title + " page"});
        view.set_category(category);
        shell.add(std::move(view));
    }
}

}  // namespace

TEST_CASE("the strip shows the shown view's category and its views, numbered within it",
          "[tui][shell][categories]") {
    Shell shell{plain()};
    add_categorized(shell);
    shell.activate(2);  // Suites
    const std::string studio = shell.render_text(80, 8);
    INFO(studio);
    CHECK(studio.starts_with(" apogee test"));
    CHECK(studio.find(" Studio ▾  [1 Suites] 2 Agents\n") != std::string::npos);
    CHECK(studio.find("Chats") == std::string::npos);
    CHECK(studio.find("Config") == std::string::npos);
    CHECK(studio.find("Ctrl-G go to") != std::string::npos);
    // A digit and Tab stay inside the category.
    CHECK(shell.press(Key::character("2")));
    CHECK(shell.active() == 3);
    CHECK(shell.press(Key::named(Key::Name::Tab)));
    CHECK(shell.active() == 2);
    CHECK(shell.press(Key::named(Key::Name::BackTab)));
    CHECK(shell.active() == 3);
    CHECK(shell.press(Key::character("3")));  // no third in Studio
    CHECK(shell.active() == 3);
    CHECK(shell.press(Key::function(1)));
    CHECK(shell.active() == 2);
}

TEST_CASE("Ctrl-G opens the Go-to menu from any view, a typing one too",
          "[tui][shell][categories]") {
    Shell shell{plain()};
    add_categorized(shell);
    CHECK(shell.active() == 0);  // the typing view
    CHECK(shell.press(Key::named(Key::Name::CtrlG)));
    const std::string menu = shell.render_text(80, 24);
    INFO(menu);
    CHECK(menu.find(" Go to ") != std::string::npos);
    CHECK(menu.find(" Modes   › Typing ") != std::string::npos);
    CHECK(menu.find(" Studio    Suites ") != std::string::npos);
    CHECK(menu.find(" Setup     Config ") != std::string::npos);
    CHECK(menu.find("Enter go · Esc close") != std::string::npos);
    // ↓ moves, → jumps a category, ← back, a letter finds a view.
    CHECK(shell.press(Key::named(Key::Name::Down)));
    CHECK(shell.render_text(80, 24).find("› Chats") != std::string::npos);
    CHECK(shell.press(Key::named(Key::Name::Right)));
    CHECK(shell.render_text(80, 24).find("› Suites") != std::string::npos);
    CHECK(shell.press(Key::named(Key::Name::Right)));
    CHECK(shell.render_text(80, 24).find("› Config") != std::string::npos);
    CHECK(shell.press(Key::named(Key::Name::Left)));
    CHECK(shell.render_text(80, 24).find("› Suites") != std::string::npos);
    CHECK(shell.press(Key::character("k")));
    CHECK(shell.render_text(80, 24).find("› Keys") != std::string::npos);
    // q is the menu's while it is open, never a quit.
    CHECK(shell.press(Key::character("q")));
    CHECK_FALSE(shell.quit_requested());
    CHECK(shell.press(Key::named(Key::Name::Return)));
    CHECK(shell.active() == 5);
    CHECK(shell.render_text(80, 24).find(" Go to ") == std::string::npos);
    const std::string setup = shell.render_text(80, 8);
    INFO(setup);
    CHECK(setup.find(" Setup ▾   1 Config [2 Keys]\n") != std::string::npos);
    // Esc closes it where it was.
    CHECK(shell.press(Key::named(Key::Name::CtrlG)));
    CHECK(shell.press(Key::named(Key::Name::Up)));
    CHECK(shell.press(Key::named(Key::Name::Escape)));
    CHECK(shell.active() == 5);
    CHECK(shell.render_text(80, 24).find(" Go to ") == std::string::npos);
}

TEST_CASE("a shell whose views name no category draws as it always has",
          "[tui][shell][categories]") {
    Shell shell{plain()};
    add_views(shell);
    const std::string frame = shell.render_text(48, 8);
    CHECK(frame.find("▾") == std::string::npos);
    CHECK(frame.find("Ctrl-G") == std::string::npos);
    // The menu still lists them.
    CHECK(shell.press(Key::named(Key::Name::CtrlG)));
    CHECK(shell.render_text(48, 12).find("› Home") != std::string::npos);
}
