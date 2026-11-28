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
/// row: typing until Enter asks or Esc closes, the last question kept. And
/// CLI-M62: an open question waits through the shell's redraws for a key.
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

    /// What the terminal's pump sends after each closure it runs, three
    /// times, the pump drained between: whether any was handled.
    [[nodiscard]] bool redraws() {
        bool handled = false;
        for (int i = 0; i < 3; ++i) {
            handled = shell.redraw() || handled;
            (void)pump.drain();
        }
        return handled;
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

namespace {

/// The two rows the questions below are asked over.
[[nodiscard]] std::pair<std::vector<std::string>, std::vector<apogee::tui::ListRow>> two_rows() {
    return std::pair{std::vector<std::string>{},
                     std::vector<apogee::tui::ListRow>{{.key = "alpha", .cells = {"alpha"}},
                                                       {.key = "beta", .cells = {"beta"}}}};
}

/// Every key but `y` and `Y`: each answers no.
[[nodiscard]] std::vector<Key> other_keys() {
    return {Key::character("n"), Key::named(Key::Name::Escape), Key::named(Key::Name::Return),
            Key::named(Key::Name::Up)};
}

}  // namespace

TEST_CASE("an open question waits through the shell's redraws, and only a key answers it",
          "[tui][list][M62]") {
    std::vector<std::string> runs;
    apogee::tui::ListOptions options;
    options.title = "Records";
    options.columns = {"NAME"};
    options.load = two_rows;
    options.actions = {apogee::tui::ListAction{
        .key = "x",
        .label = "delete",
        .confirm = [](const apogee::tui::ListRow& row) { return "Delete " + row.key + "?"; },
        .run =
            [&runs](const apogee::tui::ListRow& row) {
                runs.push_back(row.key);
                return "removed " + row.key;
            }}};
    Stage stage{options};
    (void)stage.frame();

    // The pump's redraw is not a key: the question stays open, passed on
    // unanswered.
    stage.press(Key::character("x"));
    CHECK_FALSE(stage.redraws());
    const std::string waiting = stage.frame();
    CHECK(has(waiting, " Delete alpha? [y/N]\n"));
    CHECK_FALSE(has(waiting, "not done"));
    CHECK(runs.empty());
    stage.press(Key::character("y"));
    CHECK(runs == std::vector<std::string>{"alpha"});
    CHECK(has(stage.frame(), " removed alpha\n"));

    // `Y` is yes too, as the line-mode prompts read it.
    stage.press(Key::character("x"));
    stage.press(Key::character("Y"));
    CHECK(runs == std::vector<std::string>{"alpha", "alpha"});

    // Any other key is the question's default, no.
    for (const Key& key : other_keys()) {
        stage.press(Key::character("x"));
        CHECK(has(stage.frame(), "[y/N]"));
        stage.press(key);
        const std::string answered = stage.frame();
        CHECK(has(answered, " not done\n"));
        CHECK_FALSE(has(answered, "[y/N]"));
    }
    CHECK(runs.size() == 2);
}

TEST_CASE("a typed ask's question waits through the shell's redraws, and only a key answers it",
          "[tui][list][M62]") {
    std::vector<std::string> asked;
    apogee::tui::ListOptions options;
    options.title = "Datasets";
    options.columns = {"NAME"};
    options.load = two_rows;
    options.asks = {apogee::tui::ListAsk{
        .key = "p",
        .label = "prepare",
        .ask =
            [&asked](const apogee::tui::ListRow& /*row*/, const std::string& text) {
                asked.push_back(text);
                return "started " + text;
            },
        .prefill = [](const apogee::tui::ListRow& /*row*/) { return std::string{"prepare "}; },
        .confirm = [](const apogee::tui::ListRow& /*row*/,
                      const std::string& text) { return "Start '" + text + "'?"; },
        .here = true}};
    Stage stage{options};
    (void)stage.frame();

    stage.press(Key::character("p"));
    stage.press(Key::character("a"));
    stage.press(Key::named(Key::Name::Return));
    CHECK_FALSE(stage.redraws());
    const std::string waiting = stage.frame();
    CHECK(has(waiting, " Start 'prepare a'? [y/N]\n"));
    CHECK_FALSE(has(waiting, "not done"));
    CHECK(asked.empty());
    stage.press(Key::character("y"));
    CHECK(asked == std::vector<std::string>{"prepare a"});
    CHECK(has(stage.frame(), " started prepare a\n"));

    // Reopened on the last question asked: `Y` asks it again.
    stage.press(Key::character("p"));
    stage.press(Key::named(Key::Name::Return));
    stage.press(Key::character("Y"));
    CHECK(asked == std::vector<std::string>{"prepare a", "prepare a"});

    for (const Key& key : other_keys()) {
        stage.press(Key::character("p"));
        stage.press(Key::named(Key::Name::Return));
        CHECK(has(stage.frame(), "[y/N]"));
        stage.press(key);
        const std::string answered = stage.frame();
        CHECK(has(answered, " not done\n"));
        CHECK_FALSE(has(answered, "[y/N]"));
    }
    CHECK(asked.size() == 2);
}
