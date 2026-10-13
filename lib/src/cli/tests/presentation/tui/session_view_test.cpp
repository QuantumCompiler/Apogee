#include "tui/session_view.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <future>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
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

/// The frame's rows, each without its newline.
[[nodiscard]] std::vector<std::string> rows_of(const std::string& frame) {
    std::vector<std::string> rows;
    std::size_t at = 0;
    for (std::size_t end = frame.find('\n'); end != std::string::npos;
         at = end + 1, end = frame.find('\n', at)) {
        rows.push_back(frame.substr(at, end - at));
    }
    return rows;
}

/// The index of the first row holding `text`, or the row count.
[[nodiscard]] std::size_t row_with(const std::vector<std::string>& rows, const std::string& text) {
    std::size_t i = 0;
    while (i < rows.size() && !has(rows.at(i), text)) {
        ++i;
    }
    return i;
}

/// The cells a drawn row takes: a cell a codepoint, the box's edges among
/// them.
[[nodiscard]] std::size_t cells(const std::string& row) {
    std::size_t count = 0;
    for (const char c : row) {
        count += (static_cast<unsigned char>(c) & 0xC0U) != 0x80U ? 1 : 0;
    }
    return count;
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
    // `markdown/` rendered it: no asterisks, bullets drawn, in the answer's box.
    CHECK(has(answered, "│ Here is the answer: │\n"));
    CHECK(has(answered, "│ • one "));
    CHECK(has(answered, "│ • two "));
    CHECK_FALSE(has(answered, "**"));
}

TEST_CASE("the answer is markdown/'s rendering, row for row: one renderer", "[tui][session]") {
    const std::string answer = "# Title\n\nSome *text* with `code`.\n\n1. first\n2. second\n";
    apogee::markdown::StreamRenderer renderer;
    std::vector<apogee::markdown::Row> rendered;
    // The view's margin, and the box's sides: an edge and a column of
    // padding each.
    const std::size_t width = 64 - 2 - 4;
    for (const auto& ops : {renderer.feed(answer, width), renderer.finish(width)}) {
        rendered.insert(rendered.end(), ops.commit.begin(), ops.commit.end());
    }
    // The box holds the rows from the first with text to the last, as wide
    // as the widest.
    while (!rendered.empty() && apogee::markdown::row_width(rendered.back()) == 0) {
        rendered.pop_back();
    }
    std::size_t inner = 0;
    for (const apogee::markdown::Row& row : rendered) {
        inner = std::max(inner, apogee::markdown::row_width(row));
    }
    std::string rule;
    for (std::size_t i = 0; i < inner + 2; ++i) {
        rule += "─";
    }
    std::vector<std::string> expected{" ╭" + rule + "╮"};
    for (const apogee::markdown::Row& row : rendered) {
        expected.push_back(" │ " + apogee::markdown::plain_text(row) +
                           std::string(inner - apogee::markdown::row_width(row), ' ') + " │");
    }
    expected.push_back(" ╰" + rule + "╯");
    Stage stage;
    stage.view.begin_session();
    (void)stage.frame(66, 24);
    stage.view.reporter().on_answer_start();
    stage.view.reporter().on_answer_token(answer);
    stage.view.reporter().on_answer_end();
    const std::vector<std::string> frame = rows_of(stage.frame(66, 24));
    REQUIRE(rendered.size() > 4);
    REQUIRE(apogee::markdown::row_width(rendered.front()) > 0);
    const std::size_t top = row_with(frame, " ╭");
    REQUIRE(top + expected.size() <= frame.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CHECK(frame.at(top + i) == expected.at(i));
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

TEST_CASE("the picker's doors sit between the new chat and the saved ones, each opening itself",
          "[tui][session]") {
    // 37d: the execute door, one per suite -- chosen, it opens; a saved chat
    // below the doors is still picked by its id.
    Stage stage;
    std::vector<std::string> picked;
    std::vector<std::string> opened;
    stage.view.show_picker(
        {{.id = "c1", .name = "planning", .updated = "2026-10-09T10:00:00Z", .turns = 4}},
        [&picked](std::string id) { picked.push_back(std::move(id)); },
        {{.label = "Execute under suite duo (default)",
          .open = [&opened]() { opened.emplace_back("duo"); }},
         {.label = "Execute under suite solo",
          .open = [&opened]() { opened.emplace_back("solo"); }}});
    const std::string frame = stage.frame();
    CHECK(frame.find(" › New chat\n") < frame.find("   Execute under suite duo (default)\n"));
    CHECK(frame.find("   Execute under suite solo\n") < frame.find("   planning  ·"));
    (void)stage.shell.press(Key::named(Key::Name::Down));
    (void)stage.shell.press(Key::named(Key::Name::Down));
    CHECK(has(stage.frame(), " › Execute under suite solo"));
    (void)stage.shell.press(Key::named(Key::Name::Return));
    CHECK(opened == std::vector<std::string>{"solo"});
    CHECK(picked.empty());

    stage.view.show_picker(
        {{.id = "c1", .name = "planning", .updated = "2026-10-09T10:00:00Z", .turns = 4}},
        [&picked](std::string id) { picked.push_back(std::move(id)); },
        {{.label = "Execute under suite duo",
          .open = [&opened]() { opened.emplace_back("duo"); }}});
    (void)stage.frame();
    for (int i = 0; i < 5; ++i) {  // past the end stays on the last
        (void)stage.shell.press(Key::named(Key::Name::Down));
    }
    CHECK(has(stage.frame(), " › planning"));
    (void)stage.shell.press(Key::named(Key::Name::Return));
    CHECK(picked == std::vector<std::string>{"c1"});
}

TEST_CASE("a queued line is the conversation's next, shown as typed; a new one drops it",
          "[tui][session]") {
    Stage stage;
    stage.view.begin_session();
    stage.view.queue_line("/play echo2 hello");
    (void)stage.frame();
    CHECK(has(stage.frame(), "│ /play echo2 hello │"));
    CHECK(stage.view.read_line() == std::optional<std::string>{"/play echo2 hello"});
    // Left for a conversation that never read it: not the next one's.
    stage.view.queue_line("stale");
    stage.view.begin_session();
    stage.view.queue_line("fresh");
    (void)stage.frame();
    CHECK(stage.view.read_line() == std::optional<std::string>{"fresh"});
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
    CHECK(has(frame, "│ early │\n"));
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

namespace {

/// A completer over a fixed table, as chat's offers commands: a name taking
/// an argument ends in a space.
apogee::commands::Suggester table_completer(std::vector<apogee::commands::Suggestion> table) {
    return [table = std::move(table)](std::string_view before) {
        apogee::commands::Suggestions offered;
        if (!before.starts_with('/') || before.find(' ') != std::string_view::npos) {
            return offered;
        }
        for (const apogee::commands::Suggestion& row : table) {
            if (row.label.starts_with(before)) {
                offered.candidates.push_back(row);
            }
        }
        return offered;
    };
}

const std::vector<apogee::commands::Suggestion> kMenuTable{
    {.text = "/model ", .label = "/model", .description = "switch"},
    {.text = "/models", .label = "/models", .description = "list"},
    {.text = "/help", .label = "/help", .description = "commands"}};

}  // namespace

TEST_CASE("the menu marks its choice, which the arrows move and Tab takes", "[tui][session]") {
    Stage stage;
    stage.view.begin_session();
    stage.view.set_completion(table_completer(kMenuTable));
    (void)stage.frame();
    stage.type("/mo");
    const std::string offered = stage.frame();
    INFO(offered);
    // The first chosen, the descriptions in one column.
    CHECK(has(offered, "\n › /model   switch\n"));
    CHECK(has(offered, "\n   /models  list\n"));
    // Down chooses the next, wrapping; Tab takes the choice.
    CHECK(stage.shell.press(Key::named(Key::Name::Down)));
    CHECK(has(stage.frame(), "\n › /models  list\n"));
    (void)stage.shell.press(Key::named(Key::Name::Down));
    CHECK(has(stage.frame(), "\n › /model   switch\n"));
    (void)stage.shell.press(Key::named(Key::Name::Up));
    CHECK(stage.shell.press(Key::named(Key::Name::Tab)));
    CHECK(has(stage.frame(), " › /models"));
    // Esc closes it until the line changes.
    CHECK(has(stage.frame(), "/models  list\n"));
    CHECK(stage.shell.press(Key::named(Key::Name::Escape)));
    CHECK_FALSE(has(stage.frame(), "list\n"));
    (void)stage.shell.press(Key::named(Key::Name::Backspace));
    CHECK(has(stage.frame(), "/models  list\n"));
}

TEST_CASE("the menu scrolls a window over a long list, saying how many more", "[tui][session]") {
    Stage stage;
    stage.view.begin_session();
    std::vector<apogee::commands::Suggestion> many;
    for (int i = 0; i < 12; ++i) {
        const std::string name = "/c" + std::to_string(10 + i);
        many.push_back({.text = name, .label = name, .description = "row"});
    }
    stage.view.set_completion(table_completer(many));
    (void)stage.frame(64, 30);
    stage.type("/c");
    std::string frame = stage.frame(64, 30);
    INFO(frame);
    CHECK(has(frame, " › /c10  row\n"));
    CHECK(has(frame, "   /c17  row\n"));
    CHECK_FALSE(has(frame, "/c18"));
    CHECK(has(frame, "   ↓ 4 more\n"));
    for (int i = 0; i < 9; ++i) {
        (void)stage.shell.press(Key::named(Key::Name::Down));
    }
    frame = stage.frame(64, 30);
    CHECK(has(frame, " › /c19  row\n"));
    CHECK(has(frame, "   ↑ 2 more\n"));
    CHECK(has(frame, "   ↓ 2 more\n"));
    CHECK_FALSE(has(frame, "/c11 "));
}

TEST_CASE(
    "Enter takes the chosen command: run when it is whole, its argument next when it "
    "takes one",
    "[tui][session]") {
    Stage stage;
    stage.view.begin_session();
    stage.view.set_completion(table_completer(kMenuTable));
    (void)stage.frame();
    const auto enter = [&stage]() {
        std::future<std::optional<std::string>> read =
            std::async(std::launch::async, [&stage]() { return stage.view.read_line(); });
        stage.until([&stage]() { return !has(stage.frame(), "working…"); });
        (void)stage.shell.press(Key::named(Key::Name::Return));
        stage.until([&read]() {
            return read.wait_for(std::chrono::milliseconds{0}) == std::future_status::ready;
        });
        return read.get();
    };
    // A command taking an argument stops there for it, the session still
    // waiting: nothing is sent.
    {
        std::future<std::optional<std::string>> read =
            std::async(std::launch::async, [&stage]() { return stage.view.read_line(); });
        stage.until([&stage]() { return !has(stage.frame(), "working…"); });
        stage.type("/mo");
        (void)stage.shell.press(Key::named(Key::Name::Return));
        CHECK(has(stage.frame(), "\n › /model\n"));
        CHECK(read.wait_for(std::chrono::milliseconds{50}) == std::future_status::timeout);
        // Typed whole, it runs as typed.
        (void)stage.shell.press(Key::named(Key::Name::Backspace));
        (void)stage.shell.press(Key::named(Key::Name::Return));
        CHECK(read.get() == std::optional<std::string>{"/model"});
    }
    // A command taking none is taken and run; so is the one the arrows chose.
    stage.type("/he");
    CHECK(enter() == std::optional<std::string>{"/help"});
    stage.type("/mo");
    (void)stage.shell.press(Key::named(Key::Name::Down));
    CHECK(enter() == std::optional<std::string>{"/models"});
}

TEST_CASE("a recalled line opens no menu: the arrows go on through history", "[tui][session]") {
    Stage stage;
    stage.view.begin_session();
    stage.view.set_completion(table_completer(kMenuTable));
    (void)stage.frame();
    for (const char* line : {"/models", "hello"}) {
        std::future<std::optional<std::string>> read =
            std::async(std::launch::async, [&stage]() { return stage.view.read_line(); });
        stage.until([&stage]() { return !has(stage.frame(), "working…"); });
        stage.type(line);
        (void)stage.shell.press(Key::named(Key::Name::Return));
        REQUIRE(read.get() == std::optional<std::string>{line});
    }
    (void)stage.shell.press(Key::named(Key::Name::Up));
    CHECK(has(stage.frame(), " › hello"));
    (void)stage.shell.press(Key::named(Key::Name::Up));
    CHECK(has(stage.frame(), " › /models"));
    CHECK_FALSE(has(stage.frame(), "list\n"));  // no menu on a recalled line
    (void)stage.shell.press(Key::named(Key::Name::Down));
    CHECK(has(stage.frame(), " › hello"));
}

TEST_CASE(
    "a transcript taller than the stage scrolls inside its own region, the input always "
    "on screen",
    "[tui][session]") {
    // Found 2026-10-10 in a long conversation: the shell scrolled the whole
    // view to the transcript's last row and the input fell off the bottom.
    Stage stage;
    stage.view.begin_session();
    for (int i = 0; i < 60; ++i) {
        stage.view.say("line " + std::to_string(i));
    }
    stage.view.set_status("prompt 3475 tokens");
    const std::string frame = stage.frame(64, 20);
    CHECK(has(frame, "\n line 59\n"));               // the newest row shown
    CHECK_FALSE(has(frame, "\n line 0\n"));          // the oldest scrolled away
    CHECK(has(frame, "\n prompt 3475 tokens\n ›"));  // the status, then the input
    CHECK(frame.ends_with(" Ctrl-D quit · Tab next view · F1–F9 a view\n"));
    // Scrolled back, the input stays where it is.
    (void)stage.shell.press(apogee::tui::Key::named(apogee::tui::Key::Name::PageUp));
    CHECK(has(stage.frame(64, 20), "\n ›"));
}

TEST_CASE("the person's lines sit boxed at the right, Apogee's answers boxed at the left",
          "[tui][session]") {
    Stage stage;
    stage.view.begin_session();
    stage.view.say("[apogee] local  ·  chat 1");
    // Drawn once before the answer streams, as the shell always has been:
    // an answer wraps at the width the frame was last drawn at.
    (void)stage.frame(66, 24);
    stage.view.queue_line("Hello, how are you today?");
    stage.view.reporter().on_answer_start();
    stage.view.reporter().on_answer_token(
        "I'm doing well, thank you for asking! How can I help you today? Here is a longer "
        "sentence that wraps past the first row of the answer.\n");
    stage.view.reporter().on_answer_end();
    const std::string frame = stage.frame(66, 24);
    INFO(frame);
    const std::vector<std::string> rows = rows_of(frame);
    // The person's line: no label, a box with a column of padding inside its
    // edges, ending at the right edge with the margin kept.
    CHECK_FALSE(has(frame, "You:"));
    const std::size_t said = row_with(rows, "│ Hello, how are you today? │");
    REQUIRE(said > 0);
    REQUIRE(said + 1 < rows.size());
    const std::string& line = rows.at(said);
    CHECK(cells(line) == 65);  // 66 columns less the right margin
    const std::size_t edge = line.find_first_not_of(' ');
    CHECK(edge > 20);
    CHECK(rows.at(said - 1).find_first_not_of(' ') == edge);
    CHECK(has(rows.at(said - 1), "╭───"));
    CHECK(rows.at(said - 1).ends_with("╮"));
    CHECK(cells(rows.at(said - 1)) == 65);
    CHECK(has(rows.at(said + 1), "╰───"));
    CHECK(rows.at(said + 1).ends_with("╯"));
    // The answer: no label, boxed at the left margin, its rows wrapped
    // inside the box and every edge in one column.
    CHECK_FALSE(has(frame, "Apogee:"));
    const std::size_t answer = row_with(rows, " │ I'm doing well, thank you for asking!");
    REQUIRE(answer > 0);
    REQUIRE(answer + 2 < rows.size());
    CHECK(rows.at(answer - 1).starts_with(" ╭───"));
    const std::size_t width = cells(rows.at(answer - 1));
    CHECK(rows.at(answer + 1).starts_with(" │ "));
    CHECK(rows.at(answer + 1).at(3) != ' ');
    std::size_t bottom = answer + 1;
    while (bottom < rows.size() && !rows.at(bottom).starts_with(" ╰")) {
        CHECK(rows.at(bottom).ends_with(" │"));
        CHECK(cells(rows.at(bottom)) == width);
        ++bottom;
    }
    REQUIRE(bottom < rows.size());
    CHECK(cells(rows.at(bottom)) == width);
}

TEST_CASE("a person's long line wraps within its box's share, set against the right",
          "[tui][session]") {
    Stage stage;
    stage.view.begin_session();
    stage.view.queue_line(
        "What is the current weather in Lehi Utah, and what should I wear to walk the dog "
        "this evening if it keeps raining?");
    const std::string frame = stage.frame(66, 24);
    INFO(frame);
    const std::vector<std::string> rows = rows_of(frame);
    const std::size_t first = row_with(rows, "│ What is the current");
    REQUIRE(first > 0);
    // The box, top to bottom: every row from the same column to the right
    // margin, none wider than its share of the width.
    const std::size_t edge = rows.at(first).find_first_not_of(' ');
    CHECK(has(rows.at(first - 1), "╭"));
    std::size_t row = first - 1;
    for (; row < rows.size() && !has(rows.at(row - 1), "╰"); ++row) {
        CHECK(rows.at(row).find_first_not_of(' ') == edge);
        CHECK(cells(rows.at(row)) == 65);
        CHECK(cells(rows.at(row)) - edge <= 64 * 2 / 3);
    }
    // Wrapped: more than one row of text between the edges.
    CHECK(row - first > 3);
    CHECK(has(rows.at(first + 1), "│ "));
    CHECK(rows.at(first + 1).at(edge + std::string_view{"│ "}.size()) != ' ');
}

TEST_CASE("Shift+Tab hands the conversation's mode on, and the row under the input says it",
          "[tui][session][modes]") {
    // 39b: the session's hooks, called on the shell's thread.
    Stage stage;
    stage.view.begin_session();
    int moved = 0;
    std::string label;
    stage.view.set_mode_switch(
        [&moved, &label]() {
            ++moved;
            label = moved % 2 == 1 ? "\x1b[35m⏵⏵ accept edits on\x1b[0m" : "";
        },
        [&label]() { return label; });
    // Manual: no row, the input the frame's last line before the bar.
    CHECK_FALSE(has(stage.frame(), "shift+tab"));
    CHECK(stage.shell.press(Key::named(Key::Name::BackTab)));
    CHECK(moved == 1);
    const std::string frame = stage.frame();
    INFO(frame);
    CHECK(has(frame, "\n ›\n ⏵⏵ accept edits on (shift+tab to cycle)\n"));
    // Taken by the conversation: the shell's view stays where it is.
    CHECK(has(frame, "[1 Session]"));
    (void)stage.shell.press(Key::named(Key::Name::BackTab));
    CHECK(moved == 2);
    CHECK_FALSE(has(stage.frame(), "shift+tab"));
}

TEST_CASE("the picker leaves Shift+Tab to the shell, and a new conversation drops the old hooks",
          "[tui][session][modes]") {
    Stage stage;
    int moved = 0;
    stage.view.begin_session();
    stage.view.set_mode_switch([&moved]() { ++moved; }, []() { return std::string{"MODE"}; });
    CHECK(has(stage.frame(), "MODE (shift+tab to cycle)"));
    stage.view.show_picker({}, [](const std::string&) {});
    (void)stage.frame();
    (void)stage.shell.press(Key::named(Key::Name::BackTab));
    CHECK(moved == 0);
    stage.view.begin_session();
    CHECK_FALSE(has(stage.frame(), "MODE"));
    (void)stage.shell.press(Key::named(Key::Name::BackTab));
    CHECK(moved == 0);
}

TEST_CASE("summarised, the thinking block draws the latest line in the reasoning's place",
          "[tui][session][summary]") {
    // 43: the shell's painter of the reasoning's one-line summary.
    Stage stage;
    stage.view.begin_session();
    stage.view.set_thinking_summarized(true);
    apogee::agentloop::Reporter& reporter = stage.view.reporter();
    reporter.on_thinking();
    reporter.on_thinking_token("the private chain of thought, word for word");
    std::string frame = stage.frame();
    CHECK(has(frame, "✻ Thinking…"));
    CHECK_FALSE(has(frame, "chain of thought"));
    reporter.on_thinking_summary("comparing the two config paths");
    frame = stage.frame();
    CHECK(has(frame, "\n   comparing the two config paths\n"));
    reporter.on_thinking_summary("testing the fix");
    frame = stage.frame();
    CHECK(has(frame, "testing the fix"));
    CHECK_FALSE(has(frame, "comparing the two"));
    // The fold is as ever.
    reporter.on_answer_start();
    reporter.on_answer_token("Done.");
    reporter.on_answer_end();
    frame = stage.frame();
    CHECK(has(frame, "✻ Thought for "));
    CHECK_FALSE(has(frame, "testing the fix"));
}

TEST_CASE("summarised, a turn shown raw after all draws its reasoning and says why once",
          "[tui][session][summary]") {
    Stage stage;
    stage.view.begin_session();
    stage.view.set_thinking_summarized(true);
    apogee::agentloop::Reporter& reporter = stage.view.reporter();
    reporter.on_thinking();
    reporter.on_thinking_raw("no utility model is set to summarise it");
    reporter.on_thinking_token("the reasoning itself");
    const std::string frame = stage.frame(100, 20);
    CHECK(has(frame, "reasoning shown in full -- no utility model is set to summarise it"));
    CHECK(has(frame, "the reasoning itself"));
}

TEST_CASE("the shell folds the todo above the input, redrawn whole on change",
          "[tui][session][todo]") {
    Stage stage;
    stage.view.begin_session();
    stage.view.reporter().on_todo(
        {{.text = "Read it", .state = apogee::harness::TodoState::Done},
         {.text = "Fix it", .state = apogee::harness::TodoState::Active}});
    std::string frame = stage.frame();
    CHECK(frame.find("todo · 1 of 2 done") != std::string::npos);
    CHECK(frame.find("[>] Fix it") != std::string::npos);
    stage.view.reporter().on_todo({{.text = "Fix it", .state = apogee::harness::TodoState::Done}});
    frame = stage.frame();
    CHECK(frame.find("todo · 1 of 1 done") != std::string::npos);
    CHECK(frame.find("Read it") == std::string::npos);
    stage.view.reporter().on_todo({});
    CHECK(stage.frame().find("todo ·") == std::string::npos);
}
