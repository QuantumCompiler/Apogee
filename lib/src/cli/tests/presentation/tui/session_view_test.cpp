#include "tui/session_view.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "agentloop/side_call.h"
#include "contracts/errors.h"
#include "markdown/layout.h"
#include "markdown/stream_renderer.h"
#include "tui/pump.h"
#include "tui/shell.h"
#include "tui/styled_text.h"

/// The session view (32c), headless: a scripted turn through the fourth
/// Reporter adapter, the picker, the prompt modal answered and cancelled,
/// the input handed to the session's thread -- all on a manual pump, drawn
/// through the shell's own text rendering.
namespace {

using apogee::tui::Key;

struct Stage {
    apogee::tui::ManualPump pump;
    apogee::tui::Shell shell{{.title = "apogee test", .theme = {.color = false}}};
    apogee::tui::SessionView view{pump, {.color = false}};

    Stage() {
        shell.add(view.view());
    }

    [[nodiscard]] std::string frame(int width = 64, int height = 20) {
        (void)pump.drain();
        return shell.render_text(width, height);
    }

    void type(const std::string& text) {
        for (const char c : text) {
            (void)shell.press(Key::character(std::string(1, c)));
        }
    }

    /// Until `done`, draining the pump between looks -- a session thread
    /// posts as it goes.
    template <typename Done>
    void until(Done done) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
        while (!done()) {
            (void)pump.drain();
            REQUIRE(std::chrono::steady_clock::now() < deadline);
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }
        (void)pump.drain();
    }
};

[[nodiscard]] bool has(const std::string& frame, const std::string& text) {
    return frame.find(text) != std::string::npos;
}

}  // namespace

TEST_CASE("a terminal's styled line reads back as the same looks", "[tui][styled]") {
    const apogee::ansi::Style style{true};
    const apogee::markdown::Row row = apogee::tui::spans_from_sgr(
        style.tag(apogee::ansi::Role::Warning) + " careful " + style.dim("(quietly)"));
    REQUIRE(row.size() == 3);
    CHECK(row.at(0).text == "[warn]");
    CHECK(row.at(0).attributes.color == apogee::ansi::Color::Yellow);
    CHECK(row.at(1).text == " careful ");
    CHECK(row.at(1).attributes.plain());
    CHECK(row.at(2).text == "(quietly)");
    CHECK(row.at(2).attributes.dim);
    // A link is kept; any other control is dropped, never drawn.
    const apogee::markdown::Row linked = apogee::tui::spans_from_sgr(
        apogee::ansi::hyperlink("docs", "https://example.com") + "\a\x01 and \033[2Kmore");
    REQUIRE_FALSE(linked.empty());
    CHECK(linked.front().text == "docs");
    CHECK(linked.front().link == "https://example.com");
    CHECK(apogee::markdown::plain_text(linked) == "docs and more");
}

TEST_CASE("a scripted turn renders every Reporter callback, side calls included",
          "[tui][session]") {
    Stage stage;
    stage.view.begin_session();
    stage.view.set_header("local  ·  chat 1");
    stage.view.say("\033[36m[apogee]\033[0m local  ·  chat 1");
    apogee::agentloop::Reporter& reporter = stage.view.reporter();
    reporter.on_thinking();
    reporter.on_side_call({.role = "utility", .detail = "rewriting the follow-up"});
    reporter.on_side_call(
        {.role = "utility", .detail = "rewriting the follow-up", .done = true, .seconds = 1.2});
    reporter.on_thinking_token("Let me think about the answer.");
    reporter.on_tool_status("read_file notes.txt");
    const std::string thinking = stage.frame();
    CHECK(has(thinking, " local  ·  chat 1\n"));
    CHECK(has(thinking, "✻ Thinking…"));
    CHECK(has(thinking, "utility — rewriting the follow-up · 1.2 s"));
    CHECK(has(thinking, "[tool] read_file notes.txt"));
    CHECK(has(thinking, "Let me think about the answer."));
    // The status line says what the turn is doing now: the tool.
    CHECK(has(thinking, "\n [tool] read_file notes.txt\n ›"));

    reporter.on_clear_status();
    reporter.on_answer_start();
    reporter.on_answer_token("Here is **the** answer:\n\n- one\n");
    reporter.on_answer_token("- two\n");
    reporter.on_answer_end();
    const std::string answered = stage.frame();
    // The thinking block folds to its summary; side calls are never kept.
    CHECK(has(answered, "✻ Thought for "));
    CHECK_FALSE(has(answered, "rewriting the follow-up"));
    CHECK_FALSE(has(answered, "Let me think"));
    // `markdown/` rendered it: no asterisks, bullets drawn.
    CHECK(has(answered, " Here is the answer:\n"));
    CHECK(has(answered, " • one\n"));
    CHECK(has(answered, " • two\n"));
    CHECK_FALSE(has(answered, "**"));
}

TEST_CASE("the answer is markdown/'s rendering, row for row: one renderer", "[tui][session]") {
    const std::string answer = "# Title\n\nSome *text* with `code`.\n\n1. first\n2. second\n";
    apogee::markdown::StreamRenderer renderer;
    std::vector<std::string> expected;
    const std::size_t width = 64 - 2;  // the view's margin
    for (const auto& ops : {renderer.feed(answer, width), renderer.finish(width)}) {
        for (const apogee::markdown::Row& row : ops.commit) {
            expected.push_back(" " + apogee::markdown::plain_text(row));
        }
    }
    Stage stage;
    stage.view.begin_session();
    stage.view.reporter().on_answer_start();
    stage.view.reporter().on_answer_token(answer);
    stage.view.reporter().on_answer_end();
    const std::string frame = stage.frame(66, 24);
    REQUIRE(expected.size() > 4);
    for (const std::string& row : expected) {
        std::string trimmed = row;
        trimmed.erase(trimmed.find_last_not_of(' ') + 1);
        CHECK(has(frame, "\n" + trimmed + "\n"));
    }
}

TEST_CASE("the picker offers a new chat first, then the saved ones, and opens the one chosen",
          "[tui][session]") {
    Stage stage;
    std::vector<std::string> picked;
    stage.view.show_picker(
        {{.id = "c1", .name = "planning", .updated = "2026-10-09T10:00:00Z", .turns = 4},
         {.id = "c2", .name = "debugging", .updated = "2026-10-08T09:00:00Z", .turns = 1}},
        [&picked](std::string id) { picked.push_back(std::move(id)); });
    const std::string frame = stage.frame();
    CHECK(has(frame, " › New chat\n"));
    CHECK(has(frame, "   planning  ·  2026-10-09T10:00:00Z  ·  4 turns\n"));
    CHECK(has(frame, "   debugging  ·  2026-10-08T09:00:00Z  ·  1 turn\n"));
    // Not typing in the picker: a number still switches, q still quits.
    (void)stage.shell.press(Key::named(Key::Name::Down));
    CHECK(has(stage.frame(), " › planning"));
    (void)stage.shell.press(Key::named(Key::Name::Return));
    CHECK(picked == std::vector<std::string>{"c1"});

    stage.view.show_picker({}, [&picked](std::string id) { picked.push_back(std::move(id)); });
    (void)stage.frame();
    (void)stage.shell.press(Key::named(Key::Name::Return));
    CHECK(picked == std::vector<std::string>{"c1", ""});
    stage.view.show_picker({}, {});
    (void)stage.frame();
    CHECK(stage.shell.press(Key::character("q")));
    CHECK(stage.shell.quit_requested());
}

TEST_CASE("a line typed reaches the session's thread, and only while it waits for one",
          "[tui][session]") {
    Stage stage;
    stage.view.begin_session();
    (void)stage.frame();
    // Nothing is waiting: Enter keeps the line where it is.
    stage.type("early");
    (void)stage.shell.press(Key::named(Key::Name::Return));
    CHECK(has(stage.frame(), " › early"));
    std::future<std::optional<std::string>> read =
        std::async(std::launch::async, [&stage]() { return stage.view.read_line(); });
    stage.until([&stage]() { return !has(stage.frame(), "working…"); });
    (void)stage.shell.press(Key::named(Key::Name::Return));
    REQUIRE(read.get() == std::optional<std::string>{"early"});
    const std::string frame = stage.frame();
    CHECK(has(frame, " You: early\n"));
    CHECK(has(frame, "\n ›\n"));
}

TEST_CASE("a permission prompt is answered by its key; Esc answers no", "[tui][session]") {
    Stage stage;
    stage.view.begin_session();
    const apogee::tui::Prompt prompt{.lines = {"[perm] write_file -> note.txt", "Allow?"},
                                     .choices = {{.key = "y", .label = "yes"},
                                                 {.key = "n", .label = "no"},
                                                 {.key = "a", .label = "always"},
                                                 {.key = "s", .label = "session"}}};
    for (const auto& [key, expected] : std::vector<std::pair<Key, std::string>>{
             {Key::character("a"), "a"}, {Key::named(Key::Name::Escape), ""}}) {
        std::future<std::string> asked =
            std::async(std::launch::async, [&stage, &prompt]() { return stage.view.ask(prompt); });
        stage.until([&stage]() { return has(stage.frame(), "Allow?"); });
        const std::string frame = stage.frame();
        CHECK(has(frame, "[y]es / [n]o / [a]lways / [s]ession"));
        // A key that answers nothing is swallowed, never typed.
        (void)stage.shell.press(Key::character("x"));
        (void)stage.shell.press(key);
        CHECK(asked.get() == expected);
        CHECK_FALSE(has(stage.frame(), "Allow?"));
    }
}

TEST_CASE("a question takes a number or free text", "[tui][session]") {
    Stage stage;
    stage.view.begin_session();
    const apogee::tui::Prompt prompt{
        .lines = {"[perm] Which colour?"},
        .choices = {{.key = "1", .label = "red"}, {.key = "2", .label = "blue"}},
        .free_text = true};
    std::future<std::string> asked =
        std::async(std::launch::async, [&stage, &prompt]() { return stage.view.ask(prompt); });
    stage.until([&stage]() { return has(stage.frame(), "Which colour?"); });
    const std::string frame = stage.frame();
    CHECK(has(frame, "1) red"));
    CHECK(has(frame, "Choose a number, or type your own answer:"));
    stage.type("green, please");
    (void)stage.shell.press(Key::named(Key::Name::Return));
    CHECK(asked.get() == "green, please");
}

TEST_CASE(
    "Ctrl-C stops the turn running -- a prompt open in it too -- and with none, is the "
    "shell's",
    "[tui][session]") {
    Stage stage;
    stage.view.begin_session();
    (void)stage.frame();
    (void)stage.shell.press(Key::named(Key::Name::CtrlC));
    CHECK(stage.shell.quit_requested());  // nothing ran: the shell's quit

    Stage running;
    running.view.begin_session();
    const apogee::harness::CancellationToken token = apogee::harness::CancellationToken::create();
    running.view.begin_turn(token);
    std::future<std::string> asked = std::async(std::launch::async, [&running]() {
        return running.view.ask({.lines = {"Allow?"}, .choices = {{.key = "y", .label = "yes"}}});
    });
    running.until([&running]() { return has(running.frame(), "Allow?"); });
    CHECK(has(running.frame(), "Ctrl-C stops the turn"));
    CHECK(running.shell.press(Key::named(Key::Name::CtrlC)));
    CHECK_FALSE(running.shell.quit_requested());
    CHECK(token.stop_requested());
    CHECK_THROWS_AS(asked.get(), apogee::harness::CancelledError);
    running.view.end_turn();
    CHECK_FALSE(has(running.frame(), "Allow?"));
}

TEST_CASE("an answer cut off keeps what came, and the turn's end says the rest", "[tui][session]") {
    Stage stage;
    stage.view.begin_session();
    stage.view.begin_turn(apogee::harness::CancellationToken::create());
    stage.view.reporter().on_answer_start();
    stage.view.reporter().on_answer_token("The first half of a sentence that was cu");
    stage.view.say("\033[33m[warn]\033[0m cancelled");
    stage.view.end_turn();
    const std::string frame = stage.frame();
    CHECK(has(frame, "The first half of a sentence that was cu"));
    CHECK(has(frame, "[warn] cancelled"));
}

TEST_CASE("closing the view ends a waiting read and a waiting prompt", "[tui][session]") {
    Stage stage;
    stage.view.begin_session();
    std::future<std::optional<std::string>> read =
        std::async(std::launch::async, [&stage]() { return stage.view.read_line(); });
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    stage.view.close();
    CHECK_FALSE(read.get().has_value());
    CHECK(stage.view.closed());
    CHECK_THROWS_AS(stage.view.ask({.lines = {"Allow?"}}), std::runtime_error);
}

TEST_CASE("completion offers the session's completer and Tab takes the first", "[tui][session]") {
    Stage stage;
    stage.view.begin_session();
    stage.view.set_completion([](std::string_view before) {
        apogee::commands::Suggestions offered;
        if (before.starts_with("/mo")) {
            offered.from = 0;
            offered.candidates = {{.text = "/model", .label = "/model", .description = "switch"},
                                  {.text = "/models", .label = "/models", .description = "list"}};
        }
        return offered;
    });
    (void)stage.frame();
    stage.type("/mo");
    const std::string offered = stage.frame();
    CHECK(has(offered, "/model  switch"));
    CHECK(has(offered, "/models  list"));
    CHECK(stage.shell.press(Key::named(Key::Name::Tab)));
    CHECK(has(stage.frame(), " › /model"));
}
