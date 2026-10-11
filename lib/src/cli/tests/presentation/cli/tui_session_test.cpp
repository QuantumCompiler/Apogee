#include "cli/tui_session.h"

#include <CLI/CLI.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "backends/model_roster.h"
#include "cli/chat_session.h"
#include "cli/permissions.h"
#include "cli/tui_symphonies.h"
#include "cli/tui_workbench.h"
#include "logger/session.h"
#include "machine/driver_input.h"
#include "machine/json_reporter.h"
#include "support/cli_home.h"
#include "support/env_guard.h"
#include "tui/list_view.h"
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
            apogee::commands::TuiOutput output{view, [this](std::vector<std::string> backends) {
                                                   const std::lock_guard lock{held_mutex};
                                                   held.push_back(std::move(backends));
                                               }};
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
    std::mutex held_mutex;
    /// What the session said it holds, each time it said it (32e).
    std::vector<std::vector<std::string>> held;
};

[[nodiscard]] bool has(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

/// Execute's world (37d), as `execute_test` builds it: members that echo
/// what they were sent, the suite `duo` over them -- the default when
/// `with_default` -- and `echo2`, two stages, the second given the first's
/// answer.
[[nodiscard]] std::string execute_config(const fs::path& scripts, bool with_default = true) {
    fs::create_directories(scripts);
    std::ofstream{scripts / "root.json"} << R"({"turns": [{"text": "ROOT<{{last_user}}>"}]})";
    std::ofstream{scripts / "helper.json"} << R"({"turns": [{"text": "HELPER<{{last_user}}>"}]})";
    nlohmann::json config = {
        {"backends",
         {{"root", {{"type", "mock"}, {"model_path", (scripts / "root.json").generic_string()}}},
          {"helper",
           {{"type", "mock"}, {"model_path", (scripts / "helper.json").generic_string()}}}}},
        {"models", {{"default", "root"}}},
        {"memory", {{"recall", false}}},
        {"suites",
         {{"duo",
           {{"description", "The root and its helper."},
            {"members", {{"chat", "root"}, {"utility", "helper"}}}}}}},
        {"symphonies",
         {{"echo2",
           {{"description", "Two stages, the second given the first's answer."},
            {"input", {{"description", "Any text."}}},
            {"stages",
             {{{"name", "first"}, {"role", "utility"}, {"prompt", "One: {{input}}"}},
              {{"name", "second"}, {"role", "chat"}, {"prompt", "Two: {{first}}"}}}}}}}}};
    if (with_default) {
        config["models"]["default_suite"] = "duo";
    }
    return config.dump(2) + "\n";
}

constexpr std::string_view kPlayed = "ROOT<Two: HELPER<One: hello>>";

/// A saved conversation's messages as `role: text` lines.
[[nodiscard]] std::vector<std::string> transcript(const apogee::logger::Session& session) {
    std::vector<std::string> out;
    for (const apogee::harness::ChatMessage& message : session.messages) {
        out.push_back(std::string{apogee::harness::to_string(message.role)} + ": " +
                      message.content.plain_text());
    }
    return out;
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
    {
        // Said before each line was read, and empty once the session ended.
        const std::lock_guard lock{chat.held_mutex};
        REQUIRE(chat.held.size() >= 3);
        CHECK(chat.held.back().empty());
    }

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

TEST_CASE("/model moves the shell's conversation onto a roster model, its header following",
          "[cli][tui][session][roster]") {
    // 33: the shell has no -m; /model is how a roster model is reached.
    Conversation chat{nlohmann::json{{"turns", {{{"text", "ran on {{model}}"}}}}}};
    apogee::backends::RosterCache cache;
    cache.rosters["mock"] = apogee::backends::ProviderRoster{
        {{"mock-pro", "Pro"}, {"mock-mini", "Mini"}}, "2026-10-09"};
    REQUIRE(apogee::backends::save_roster_cache(cache).empty());
    const std::string before = slurp(chat.home.config_path());
    chat.open();
    // The input offers the roster beside the backends: chat's one completer.
    chat.until([&chat]() { return has(chat.frame(), "\n ›"); }, "the input");
    for (const char c : std::string{"/model mock-"}) {
        (void)chat.shell.press(Key::character(std::string(1, c)));
    }
    chat.until([&chat]() { return has(chat.frame(), "mock's roster"); }, "the roster offered");
    CHECK(has(chat.frame(), "mock-mini"));
    for (int i = 0; i < 12; ++i) {
        (void)chat.shell.press(Key::named(Key::Name::Backspace));
    }
    chat.send("/model mock-pro");
    chat.until(
        [&chat]() {
            return has(chat.frame(), "switched to mock-pro -- mock's roster, on backend 'local'");
        },
        "the switch");
    CHECK(has(chat.frame(), "local (mock-pro)"));  // the header
    chat.send("hello");
    chat.until([&chat]() { return has(chat.frame(), "ran on mock-pro"); }, "the answer");
    chat.finish();
    CHECK(chat.failure.empty());
    CHECK(slurp(chat.home.config_path()) == before);
    const std::vector<apogee::logger::Session> saved = apogee::logger::list_sessions();
    REQUIRE(saved.size() == 1);
    CHECK(saved.front().model == "mock-pro");
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

TEST_CASE("the workbench's choices reach the conversation through its own commands",
          "[cli][tui][session][workbench]") {
    // The driver behind the views (32d): a suite chosen is the session's own
    // `/suite`; a chat chosen ends the open one as `/exit` does, then opens.
    const nlohmann::json script = {{"turns", {{{"text", "answer to {{last_user}}"}}}}};
    Conversation chat{script};
    {
        std::ofstream config{chat.home.config_path(), std::ios::binary};
        config << "{\n  \"backends\": {\"local\": {\"type\": \"mock\", \"model_path\": "
               << nlohmann::json((chat.home.home() / "script.json").generic_string()).dump()
               << "}},\n  \"models\": {\"default\": \"local\"},\n"
                  "  \"suites\": {\"duo\": {\"members\": {\"chat\": \"local\"}}}\n}\n";
    }
    apogee::logger::Session saved;
    saved.chat_id = "20261009-100000-aaaa";
    saved.title = "an earlier chat";
    saved.backend = "local";
    saved.started_at = "2026-10-09T10:00:00Z";
    saved.updated_at = "2026-10-09T10:00:00Z";
    apogee::logger::save(saved);

    apogee::commands::TuiSessionDriver driver{chat.view, chat.pump, chat.context,
                                              []() { return apogee::models::MachineBudget{}; }};
    CHECK(driver.use_suite("duo") == "a new chat under suite duo");
    chat.until([&chat]() { return has(chat.frame(), "suite duo"); }, "the chat under duo");
    chat.until([&chat]() { return chat.view.waiting_for_line(); }, "the chat reading");
    CHECK(driver.use_suite("duo") == "/suite duo sent to the conversation");
    chat.until([&chat]() { return has(chat.frame(), "You: /suite duo"); }, "/suite entered");

    chat.until([&chat]() { return chat.view.waiting_for_line(); }, "the chat reading again");
    CHECK(driver.open_chat("20261009-100000-aaaa") == "opened 20261009-100000-aaaa");
    chat.until([&chat]() { return has(chat.frame(), "chat 20261009-100000-aaaa"); },
               "the chosen chat open");
    chat.send("hello again");
    chat.until([&chat]() { return has(chat.frame(), "answer to hello again"); }, "its answer");
    driver.stop();
    const apogee::logger::LoadedSession loaded = apogee::logger::load("20261009-100000-aaaa", {});
    REQUIRE(loaded.session.messages.size() == 2);
    CHECK(loaded.session.messages.back().content.plain_text() == "answer to hello again");
}

TEST_CASE("the picker's execute door opens execute's session, and a play there is execute's",
          "[cli][tui][session][execute]") {
    // The twin first: the same play driven through machine mode's execute.
    std::vector<std::string> twin_transcript;
    std::optional<std::string> twin_suite;
    {
        apogee::testing::CliHome twin{"{}\n", "config.json"};
        std::ofstream{twin.config_path(), std::ios::binary | std::ios::trunc}
            << execute_config(twin.home() / "scripts");
        const std::istringstream fed{R"({"type":"user","text":"/play echo2 hello"})"
                                     "\n"};
        std::streambuf* old_in = std::cin.rdbuf(fed.rdbuf());
        std::string out;
        std::string err;
        const int code =
            twin.run({"execute", "--suite", "duo", "--output-format", "stream-json"}, &out, &err);
        std::cin.rdbuf(old_in);
        std::cin.clear();
        INFO(out << err);
        REQUIRE(code == 0);
        const apogee::testing::EnvGuard guard{"APOGEE_HOME", twin.home().string()};
        const std::vector<apogee::logger::Session> saved = apogee::logger::list_sessions();
        REQUIRE(saved.size() == 1);
        twin_transcript = transcript(saved.front());
        twin_suite = saved.front().suite;
    }

    const nlohmann::json script = {{"turns", {{{"text", "unused"}}}}};
    Conversation chat{script};
    apogee::commands::TuiSessionDriver driver{chat.view, chat.pump, chat.context,
                                              []() { return apogee::models::MachineBudget{}; }};
    // No suite configured: no door, rather than one that refuses.
    driver.show_picker();
    chat.until([&chat]() { return has(chat.frame(), "New chat"); }, "the picker");
    CHECK_FALSE(has(chat.frame(), "Execute under"));

    std::ofstream{chat.home.config_path(), std::ios::binary | std::ios::trunc}
        << execute_config(chat.home.home() / "scripts");
    driver.show_picker();
    chat.until([&chat]() { return has(chat.frame(), "Execute under suite duo (default)"); },
               "the execute door");
    (void)chat.shell.press(Key::named(Key::Name::Down));
    (void)chat.shell.press(Key::named(Key::Name::Return));
    // Execute's banner, rows and completions: the suite named, what it plays.
    chat.until([&chat]() { return chat.view.waiting_for_line(); }, "the execute session reading");
    const std::string opened = chat.frame();
    CHECK(has(opened, "suite duo"));
    CHECK(has(opened, "symphon"));
    for (const char c : std::string{"/pl"}) {
        (void)chat.shell.press(Key::character(std::string(1, c)));
    }
    chat.until([&chat]() { return has(chat.frame(), "/play"); }, "/play offered");
    for (int i = 0; i < 3; ++i) {
        (void)chat.shell.press(Key::named(Key::Name::Backspace));
    }

    chat.send("/play echo2 hello");
    chat.until([&chat]() { return has(chat.frame(), std::string{kPlayed}); }, "the play's output");
    // The stages' side calls drawn in the turn's thinking block, folded into
    // its summary once the play is over, above the answer -- as the terminal
    // draws them (their words are machine mode's, `execute_test`'s).
    const std::string played = chat.frame();
    CHECK(played.find("✻ Worked for") > played.find("You: /play echo2 hello"));
    CHECK(played.find("✻ Worked for") < played.find(std::string{kPlayed}));
    driver.stop();
    const std::vector<apogee::logger::Session> saved = apogee::logger::list_sessions();
    REQUIRE(saved.size() == 1);
    // Saved as machine mode's execute saves the same play.
    CHECK(transcript(saved.front()) == twin_transcript);
    CHECK(twin_transcript == std::vector<std::string>{"user: /play echo2 hello",
                                                      "assistant: " + std::string{kPlayed}});
    CHECK(saved.front().suite == twin_suite);
    CHECK(saved.front().turns == 1);
}

TEST_CASE("the Symphonies view draws symphonies list's document, and plays through the session",
          "[cli][tui][session][execute]") {
    const nlohmann::json script = {{"turns", {{{"text", "unused"}}}}};
    Conversation chat{script};
    std::ofstream{chat.home.config_path(), std::ios::binary | std::ios::trunc}
        << execute_config(chat.home.home() / "scripts");
    apogee::commands::TuiSessionDriver driver{chat.view, chat.pump, chat.context,
                                              []() { return apogee::models::MachineBudget{}; }};
    // The shell's hook: the session's own /play, the session shown when sent.
    const apogee::commands::WorkbenchHooks hooks{
        .play_symphony = [&driver, &chat](const std::string& symphony, const std::string& input) {
            const apogee::commands::TuiSessionDriver::Handed handed = driver.play(symphony, input);
            if (handed.sent) {
                chat.shell.activate(0);
            }
            return handed.said;
        }};
    const apogee::tui::ListOptions options =
        apogee::commands::symphonies_view_options(chat.context, hooks);

    // The rows: `symphonies list --output-format json`'s, drawn.
    std::string out;
    std::string err;
    REQUIRE(chat.home.run({"symphonies", "list", "--output-format", "json"}, &out, &err) == 0);
    const nlohmann::json document = nlohmann::json::parse(out);
    const auto [heading, rows] = options.load();
    REQUIRE(rows.size() == document["data"].size());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const nlohmann::json& definition = document["data"].at(i);
        INFO(definition.dump());
        CHECK(rows.at(i).key == definition["name"].get<std::string>());
        CHECK(rows.at(i).cells.at(0) == definition["name"].get<std::string>());
        CHECK(rows.at(i).cells.at(2) == definition["source"].get<std::string>());
        if (definition["problems"].empty()) {
            CHECK(rows.at(i).cells.at(3) == definition["description"].get<std::string>());
        }
    }
    const auto echo2 = std::ranges::find_if(
        rows, [](const apogee::tui::ListRow& row) { return row.key == "echo2"; });
    REQUIRE(echo2 != rows.end());
    CHECK(echo2->cells.at(1) == "utility → chat");
    // Enter: `symphonies show`'s card.
    REQUIRE(chat.home.run({"symphonies", "show", "echo2"}, &out, &err) == 0);
    std::string card;
    for (const std::string& line : options.detail(*echo2)) {
        card += line + "\n";
    }
    CHECK(card == out);

    // `p` with no session open: execute's session under the default suite,
    // the play its first line.
    apogee::tui::ListView view{chat.pump, {.color = false}, options};
    (void)chat.shell.add(view.view());
    chat.shell.activate(1);
    const auto settled = [&chat, &view]() {
        view.settle();
        return chat.frame();
    };
    chat.until([&]() { return has(settled(), "echo2"); }, "the symphonies read");
    while (!has(settled(), "› echo2")) {
        (void)chat.shell.press(Key::named(Key::Name::Down));
    }
    CHECK(has(settled(), "p play"));
    (void)chat.shell.press(Key::character("p"));
    for (const char c : std::string{"hello"}) {
        (void)chat.shell.press(Key::character(std::string(1, c)));
    }
    CHECK(has(settled(), "play: hello"));
    (void)chat.shell.press(Key::named(Key::Name::Return));
    // The session shown, the play run there and its output the answer.
    chat.until([&chat]() { return has(chat.frame(), std::string{kPlayed}); }, "the play's output");
    CHECK(has(chat.frame(), "You: /play echo2 hello"));

    // Open and reading: entered there; a play missing its input is refused in
    // the session's own words, and nothing kept.
    chat.until([&chat]() { return chat.view.waiting_for_line(); }, "the session reading");
    const apogee::commands::TuiSessionDriver::Handed bare = driver.play("echo2", "");
    CHECK(bare.sent);
    CHECK(bare.said == "/play echo2 sent to the conversation");
    chat.until(
        [&chat]() {
            return has(chat.frame(), "'echo2' reads its input (Any text.), and none was given");
        },
        "the session's refusal");
    driver.stop();
    const std::vector<apogee::logger::Session> saved = apogee::logger::list_sessions();
    REQUIRE(saved.size() == 1);
    CHECK(
        transcript(saved.front()) ==
        std::vector<std::string>{"user: /play echo2 hello", "assistant: " + std::string{kPlayed}});
}

TEST_CASE("a play is never offered outside execute: a chat open, or no default suite, says why",
          "[cli][tui][session][execute]") {
    const nlohmann::json script = {{"turns", {{{"text", "answer to {{last_user}}"}}}}};
    Conversation chat{script};
    std::ofstream{chat.home.config_path(), std::ios::binary | std::ios::trunc}
        << execute_config(chat.home.home() / "scripts", /*with_default=*/false);
    apogee::commands::TuiSessionDriver driver{chat.view, chat.pump, chat.context,
                                              []() { return apogee::models::MachineBudget{}; }};
    // Nothing open and no suite the default: no session to open under.
    const apogee::commands::TuiSessionDriver::Handed none = driver.play("echo2", "hello");
    CHECK_FALSE(none.sent);
    CHECK(has(none.said, "no suite is the default"));
    // A chat open: /play is not in its table.
    CHECK(driver.use_model("root") == "a new chat on root");
    chat.until([&chat]() { return chat.view.waiting_for_line(); }, "the chat reading");
    const apogee::commands::TuiSessionDriver::Handed in_chat = driver.play("echo2", "hello");
    CHECK_FALSE(in_chat.sent);
    CHECK(has(in_chat.said, "the conversation open is a chat"));
    CHECK_FALSE(has(chat.frame(), "/play"));
    // The execute door from the workbench ends the chat as /exit does and
    // opens execute's session under the suite chosen.
    CHECK(driver.open_execute("duo") == "an execute session under suite duo");
    chat.until([&chat]() { return has(chat.frame(), "suite duo"); }, "execute under duo");
    chat.until([&chat]() { return chat.view.waiting_for_line(); }, "execute reading");
    CHECK(driver.play("echo2", "hello").sent);
    chat.until([&chat]() { return has(chat.frame(), std::string{kPlayed}); }, "the play's output");
    driver.stop();
}
