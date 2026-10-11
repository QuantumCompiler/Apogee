#include "tui/list_view.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

#include "tui/pump.h"
#include "tui/shell.h"

/// The one list view's additions for track 37 (37b): an action over the
/// whole view, runnable with no row; a view's own `r` taking the key from
/// "read again"; and an answer of several lines drawn whole.
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
