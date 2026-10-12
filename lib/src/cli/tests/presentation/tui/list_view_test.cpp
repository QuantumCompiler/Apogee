#include "tui/list_view.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "tui/pump.h"
#include "tui/shell.h"

/// The one list view's additions for track 37 (37b): an action over the
/// whole view, runnable with no row; a view's own `r` taking the key from
/// "read again"; and an answer of several lines drawn whole. And 37c's ask
/// row: typing until Enter asks or Esc closes, the last question kept.
namespace {

using apogee::tui::Key;

struct Stage {
    explicit Stage(apogee::tui::ListOptions options)
        : view{pump, apogee::tui::Theme{.color = false}, std::move(options)} {
        shell.add(view.view());
        shell.activate(0);
    }

    [[nodiscard]] std::string frame() {
        for (int i = 0; i < 3; ++i) {
            view.settle();
            (void)pump.drain();
        }
        return shell.render_text(120, 20);
    }

    void press(const Key& key) {
        (void)shell.press(key);
        (void)frame();
    }

    apogee::tui::ManualPump pump;
    apogee::tui::Shell shell{{.title = "apogee test", .theme = {.color = false}}};
    apogee::tui::ListView view;
};

[[nodiscard]] bool has(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

}  // namespace

TEST_CASE("a whole-view action runs with no row, takes r, and answers in several lines",
          "[tui][list]") {
    int reads = 0;
    apogee::tui::ListOptions options;
    options.title = "Bare";
    options.columns = {"NAME"};
    options.load = [&reads]() {
        ++reads;
        return std::pair{std::vector<std::string>{}, std::vector<apogee::tui::ListRow>{}};
    };
    options.actions = {apogee::tui::ListAction{
        .key = "r",
        .label = "run",
        .run = [](const apogee::tui::ListRow& /*row*/) { return std::string{"first\nsecond\n"}; },
        .whole_view = true}};
    Stage stage{options};
    const std::string empty = stage.frame();
    CHECK(has(empty, "nothing here yet"));
    CHECK(has(empty, "r run"));
    CHECK_FALSE(has(empty, "read again"));
    const int before = reads;
    stage.press(Key::character("r"));
    const std::string answered = stage.frame();
    CHECK(has(answered, " first\n"));   // the notice row, and the detail's first line
    CHECK(has(answered, " second\n"));  // the rest drawn whole
    CHECK(has(answered, "Esc closes"));
    CHECK(reads > before);  // read again after the action, as ever
}

TEST_CASE("a page draws its lines plain, with no table", "[tui][list]") {
    apogee::tui::ListOptions options;
    options.title = "Page";
    options.page = true;
    options.load = []() {
        return std::pair{std::vector<std::string>{"CPU  Fake 9000", "MEMORY  16 GiB"},
                         std::vector<apogee::tui::ListRow>{}};
    };
    Stage stage{options};
    const std::string drawn = stage.frame();
    CHECK(has(drawn, " CPU  Fake 9000\n"));
    CHECK(has(drawn, " MEMORY  16 GiB\n"));
    CHECK_FALSE(has(drawn, "nothing here yet"));
    CHECK(has(drawn, "r read again"));
}

TEST_CASE("the ask row takes typing until Enter asks or Esc closes it, the last kept to refine",
          "[tui][list]") {
    std::vector<std::pair<std::string, std::string>> asked;
    apogee::tui::ListOptions options;
    options.title = "Records";
    options.columns = {"NAME"};
    options.load = []() {
        return std::pair{std::vector<std::string>{},
                         std::vector<apogee::tui::ListRow>{{.key = "alpha", .cells = {"alpha"}}}};
    };
    options.asks = {apogee::tui::ListAsk{
        .label = "query",
        .ask =
            [&asked](const apogee::tui::ListRow& row, const std::string& text) {
                asked.emplace_back(row.key, text);
                if (text == "boom") {
                    throw std::runtime_error{"no such thing"};
                }
                return "answered " + text + "\nsecond line\n";
            },
        .needs_row = true}};
    apogee::tui::ManualPump pump;
    apogee::tui::Shell shell{{.title = "apogee test", .theme = {.color = false}}};
    apogee::tui::ListView view{pump, apogee::tui::Theme{.color = false}, std::move(options)};
    shell.add(view.view());
    // A second view: a digit typed into the row must not switch to it.
    shell.add(apogee::tui::text_view("Other", {"elsewhere"}));
    shell.activate(0);
    const auto frame = [&]() {
        for (int i = 0; i < 3; ++i) {
            view.settle();
            (void)pump.drain();
        }
        return shell.render_text(120, 20);
    };
    const auto press = [&](const Key& key) {
        (void)shell.press(key);
        (void)frame();
    };
    CHECK(has(frame(), "/ query"));
    press(Key::character("/"));
    CHECK(view.view().takes_text());
    CHECK(has(frame(), "Enter asks · Esc closes"));
    for (const char* key : {"r", "2", "q", "é"}) {
        press(Key::character(key));
    }
    CHECK(has(frame(), "query: r2qé"));
    CHECK(has(frame(), "Records"));           // still here: the 2 and the q were typing
    press(Key::named(Key::Name::Backspace));  // the whole é, both its bytes
    CHECK(has(frame(), "query: r2q▏"));
    press(Key::named(Key::Name::Return));
    REQUIRE(asked.size() == 1);
    CHECK(asked.front() == std::pair<std::string, std::string>{"alpha", "r2q"});
    CHECK_FALSE(view.view().takes_text());
    CHECK(has(frame(), " answered r2q\n"));
    CHECK(has(frame(), " second line\n"));

    // Reopened: the last question, to refine; Esc closes it unasked.
    press(Key::character("/"));
    CHECK(has(frame(), "query: r2q▏"));
    press(Key::named(Key::Name::Escape));
    CHECK_FALSE(view.view().takes_text());
    CHECK(asked.size() == 1);

    // A throw is said as the reason it could not be asked.
    press(Key::character("/"));
    for (int i = 0; i < 3; ++i) {
        press(Key::named(Key::Name::Backspace));
    }
    for (const char* key : {"b", "o", "o", "m"}) {
        press(Key::character(key));
    }
    press(Key::named(Key::Name::Return));
    CHECK(has(frame(), "could not ask: no such thing"));
}
