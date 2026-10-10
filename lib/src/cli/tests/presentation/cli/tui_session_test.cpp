#include "cli/tui_session.h"

#include <CLI/CLI.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "cli/chat_session.h"
#include "cli/permissions.h"
#include "logger/session.h"
#include "machine/driver_input.h"
#include "machine/json_reporter.h"
#include "support/cli_home.h"
#include "tui/pump.h"
#include "tui/session_view.h"
#include "tui/shell.h"

/// The shell's conversation end to end (32c): chat's own session core on a
/// thread of its own with the session view as its front-end, driven by key
/// presses on a manual pump -- a tool-using, permission-prompted turn on
/// screen, `always` writing the config byte for byte as the other prompts
/// do, `session` writing nothing, a turn stopped with Ctrl-C and the
/// session going on, and a conversation crossing between the shell and
/// `apogee chat` with nothing lost.
namespace {

using apogee::tui::Key;
namespace fs = std::filesystem;

[[nodiscard]] std::string slurp(const fs::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// A mock backend playing `turns`, with the tools' root in the home.
[[nodiscard]] std::string config_for(const fs::path& script, const fs::path& work) {
    return "{\n  \"backends\": {\"local\": {\"type\": \"mock\", \"model_path\": " +
           nlohmann::json(script.generic_string()).dump() +
           "}},\n  \"models\": {\"default\": \"local\"},\n  \"tools\": {\"fs_root\": " +
           nlohmann::json(work.generic_string()).dump() + "}\n}\n";
}

const nlohmann::json kWriteThenAnswer = {
    {"turns",
     {{{"text", ""},
       {"tool_calls",
        {{{"name", "write_file"}, {"arguments", R"({"path": "note.txt", "content": "hi"})"}}}}},
      {{"text", "I wrote **note.txt** for you."}}}}};

/// One install, a scripted model, and the shell's session view in front of
/// chat's core.
struct Conversation {
    explicit Conversation(const nlohmann::json& script) : home{"{}\n", "config.json"} {
        fs::create_directories(work());
        std::ofstream{home.home() / "script.json", std::ios::binary} << script.dump();
        std::ofstream{home.config_path(), std::ios::binary}
            << config_for(home.home() / "script.json", work());
        context.config_path = home.config_path().string();
        shell.add(view.view());
    }

    Conversation(const Conversation&) = delete;
    Conversation& operator=(const Conversation&) = delete;
    Conversation(Conversation&&) = delete;
    Conversation& operator=(Conversation&&) = delete;

    ~Conversation() {
        view.close();
        if (worker.joinable()) {
            worker.join();
        }
    }

    [[nodiscard]] fs::path work() const {
        return home.home() / "work";
    }

    /// Chat's session, as the shell runs it, resuming `chat_id` if named.
    void open(const std::string& chat_id = {}) {
        ended = false;
        view.begin_session();
        worker = std::thread{[this, chat_id]() {
            apogee::commands::TuiOutput output{view};
            try {
                CLI::App app{"test", "chat"};
                const auto flags = apogee::commands::shell_session_flags(app, chat_id);
                apogee::commands::run_session(
                    context, *flags, []() { return apogee::models::MachineBudget{}; },
                    apogee::commands::SessionMode::Chat, &output);
            } catch (const std::exception& e) {
                failure = e.what();
            }
            ended = true;
        }};
    }

    [[nodiscard]] std::string frame() {
        (void)pump.drain();
        return shell.render_text(90, 40);
    }

    template <typename Done>
    void until(Done done, const char* what) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};
        while (!done()) {
            INFO(what << "\n" << shell.render_text(90, 40));
            REQUIRE(std::chrono::steady_clock::now() < deadline);
            (void)pump.drain();
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
        (void)pump.drain();
    }

    /// Types `line` and enters it once the session is reading: Enter while
    /// it is still busy leaves the line in the input, so it is pressed again
    /// until the transcript shows the line taken.
    void send(const std::string& line) {
        until([this]() { return frame().find("\n ›") != std::string::npos; }, "the input");
        for (const char c : line) {
            (void)shell.press(Key::character(std::string(1, c)));
        }
        const std::size_t sent = count("You: " + line);
        until(
            [this, &line, sent]() {
                (void)shell.press(Key::named(Key::Name::Return));
                (void)pump.drain();
                return count("You: " + line) > sent;
            },
            "the line taken");
    }

    [[nodiscard]] std::size_t count(const std::string& text) {
        const std::string drawn = frame();
        std::size_t found = 0;
        for (std::size_t at = drawn.find(text); at != std::string::npos;
             at = drawn.find(text, at + text.size())) {
            ++found;
        }
        return found;
    }

    void finish() {
        send("/exit");
        until([this]() { return ended.load(); }, "the session ending");
        worker.join();
    }

    apogee::testing::CliHome home;
    apogee::commands::RootContext context;
    apogee::tui::ManualPump pump;
    apogee::tui::Shell shell{{.title = "apogee test", .theme = {.color = false}}};
    apogee::tui::SessionView view{pump, {.color = false}};
    std::thread worker;
    std::atomic<bool> ended{false};
    std::string failure;
};

[[nodiscard]] bool has(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

}  // namespace

TEST_CASE("a tool-using, permission-prompted turn runs to its answer on the shell's screen",
          "[cli][tui][session]") {
    Conversation chat{kWriteThenAnswer};
    const std::string before = slurp(chat.home.config_path());
    chat.open();
    // The input completes from chat's own completer, the one command table:
    // what is offered is what the REPL offers.
    chat.until([&chat]() { return has(chat.frame(), "\n ›"); }, "the input");
    for (const char c : std::string{"/mod"}) {
        (void)chat.shell.press(Key::character(std::string(1, c)));
    }
    chat.until([&chat]() { return has(chat.frame(), "/models"); }, "the completions");
    (void)chat.shell.press(Key::named(Key::Name::Tab));
    CHECK(has(chat.frame(), " › /model"));
    for (int i = 0; i < 12; ++i) {
        (void)chat.shell.press(Key::named(Key::Name::Backspace));
    }
    chat.send("please write a note");
    chat.until([&chat]() { return has(chat.frame(), "Allow?"); }, "the permission prompt");
    const std::string asked = chat.frame();
    CHECK(has(asked, " You: please write a note\n"));
    CHECK(has(asked, "[perm] write_file -> note.txt"));
    CHECK(has(asked, "[y]es / [n]o / [a]lways / [s]ession"));
    (void)chat.shell.press(Key::character("a"));
    chat.until([&chat]() { return has(chat.frame(), "I wrote note.txt for you."); }, "the answer");
    const std::string answered = chat.frame();
    CHECK(has(answered, "permissions.write_file = allow written to"));
    CHECK_FALSE(has(answered, "**"));
    CHECK(slurp(chat.work() / "note.txt") == "hi");
    chat.finish();
    CHECK(chat.failure.empty());

    // `always` through the shell is the config edit every prompt makes: the
    // same answer given machine mode's way, on the same starting file.
    const apogee::testing::TempDir twin{"tui-always-twin"};
    const fs::path twin_config = twin.path() / "config.json";
    std::ofstream{twin_config, std::ios::binary} << before;
    std::ostringstream events;
    apogee::commands::JsonReporter reporter{events};
    std::istringstream answer{R"({"type":"answer","text":"always"})"
                              "\n"};
    apogee::commands::DriverInput driver{answer};
    auto approvals = std::make_shared<apogee::commands::SessionApprovals>();
    REQUIRE(apogee::commands::make_driver_confirm_fn(reporter, driver, twin_config, approvals)(
        apogee::agent::GateRequest{"write_file", "note.txt"}));
    CHECK(slurp(chat.home.config_path()) == slurp(twin_config));

    // The conversation was saved as chat saves one: the exchange, whole.
    const std::vector<apogee::logger::Session> saved = apogee::logger::list_sessions();
    REQUIRE(saved.size() == 1);
    CHECK(saved.front().turns == 1);
}

TEST_CASE("a session grant writes nothing and dies with the session", "[cli][tui][session]") {
    Conversation chat{kWriteThenAnswer};
    const std::string before = slurp(chat.home.config_path());
    chat.open();
    chat.send("please write a note");
    chat.until([&chat]() { return has(chat.frame(), "Allow?"); }, "the permission prompt");
    (void)chat.shell.press(Key::character("s"));
    chat.until([&chat]() { return has(chat.frame(), "I wrote note.txt for you."); }, "the answer");
    chat.finish();
    CHECK(slurp(chat.home.config_path()) == before);
    CHECK(slurp(chat.work() / "note.txt") == "hi");
}

TEST_CASE("Ctrl-C stops the turn running and the session goes on", "[cli][tui][session]") {
    const nlohmann::json slow = {
        {"turns",
         {{{"text",
            "a long answer that streams slowly, one piece at a time, and goes on and on "
            "and on for as long as anyone could want it to before it ever ends"},
           {"delay_ms", 100}},
          {{"text", "the second answer"}}}}};
    Conversation chat{slow};
    chat.open();
    chat.send("tell me something long");
    chat.until([&chat]() { return has(chat.frame(), "a long"); }, "the answer streaming");
    CHECK(chat.shell.press(Key::named(Key::Name::CtrlC)));
    CHECK_FALSE(chat.shell.quit_requested());
    chat.until([&chat]() { return has(chat.frame(), "cancelled"); }, "the turn cancelled");
    chat.send("and now?");
    chat.until([&chat]() { return has(chat.frame(), "the second answer"); }, "the next turn");
    chat.finish();
    CHECK(chat.failure.empty());
}

TEST_CASE("a conversation crosses between the shell and apogee chat with nothing lost",
          "[cli][tui][session]") {
    // One turn, replayed: each run's model answers what it was just asked.
    const nlohmann::json script = {{"turns", {{{"text", "answer to {{last_user}}"}}}}};
    Conversation chat{script};
    chat.open();
    chat.send("question one");
    chat.until([&chat]() { return has(chat.frame(), "answer to question one"); }, "the first");
    chat.finish();
    const std::vector<apogee::logger::Session> saved = apogee::logger::list_sessions();
    REQUIRE(saved.size() == 1);
    const std::string id = saved.front().chat_id;

    // Opened in `apogee chat`, one more turn, piped.
    {
        const std::istringstream fed{"question two\n/exit\n"};
        std::streambuf* old_in = std::cin.rdbuf(fed.rdbuf());
        std::string out;
        std::string err;
        const int code = chat.home.run({"chat", "--resume", id}, &out, &err);
        std::cin.rdbuf(old_in);
        std::cin.clear();
        INFO(out << err);
        REQUIRE(code == 0);
        CHECK(has(out, "answer to question two"));
    }

    // Back in the shell, and the next turn follows them.
    chat.open(id);
    chat.send("question three");
    chat.until([&chat]() { return has(chat.frame(), "answer to question three"); }, "the third");
    chat.finish();
    const apogee::logger::LoadedSession loaded = apogee::logger::load(id, {});
    std::vector<std::string> said;
    for (const apogee::harness::ChatMessage& message : loaded.session.messages) {
        said.push_back(message.content.plain_text());
    }
    CHECK(said == std::vector<std::string>{"question one", "answer to question one", "question two",
                                           "answer to question two", "question three",
                                           "answer to question three"});
    CHECK(loaded.session.turns == 3);
    CHECK(apogee::logger::list_sessions().size() == 1);
}
