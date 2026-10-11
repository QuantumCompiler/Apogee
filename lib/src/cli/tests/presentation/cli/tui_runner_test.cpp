#include "cli/tui_runner.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "cli/command.h"
#include "support/cli_home.h"
#include "tui/progress.h"
#include "tui/pump.h"
#include "tui/runner_view.h"
#include "tui/shell.h"
#include "tui/view.h"

/// The command runner (37h): a typed line is the command's words -- scoped,
/// split once, refused from the law's table; Tab offers what `__complete`
/// offers for the same words; the shell's `:` and `!` open the line, typing
/// goes to it, Enter runs, Esc closes, Ctrl-C stops only the running command;
/// and, as children of the built binary (skipped where the build made none),
/// a read's output and a mutation's file are the command's on a pipe, a
/// refusal runs nothing, and a slow command is stopped without the shell.
namespace {

namespace fs = std::filesystem;
using apogee::tui::Key;

[[nodiscard]] bool has(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

[[nodiscard]] std::string slurp(const fs::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] fs::path built_binary() {
    const fs::path binary{APOGEE_EXECUTABLE};
    std::error_code missing;
    return fs::is_regular_file(binary, missing) ? binary : fs::path{};
}

[[nodiscard]] std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in{text};
    for (std::string line; std::getline(in, line);) {
        out.push_back(line);
    }
    return out;
}

constexpr const char* kConfig =
    "backends:\n  local:\n    type: mock\n  other:\n    type: mock\nmodels:\n  default: local\n";

/// A shell with a view of a command group, and one that takes typing.
struct ShellStage {
    explicit ShellStage(apogee::tui::ExecLineOptions options) {
        apogee::tui::View models = apogee::tui::text_view("Models", {"the models"});
        models.set_group("models");
        (void)shell.add(std::move(models));
        (void)shell.add(apogee::tui::text_view("Home", {"home"}));
        shell.set_exec_line(std::move(options));
        shell.activate(0);
    }

    [[nodiscard]] std::string frame() {
        (void)pump.drain();
        return shell.render_text(160, 40);
    }

    void type(const std::string& text) {
        for (const char c : text) {
            (void)shell.press(Key::character(std::string(1, c)));
        }
    }

    apogee::tui::ManualPump pump;
    apogee::tui::Shell shell{{.title = "apogee test", .theme = {.color = false}}};
};

}  // namespace

TEST_CASE("a typed line is the command's words: scoped, split once, refused from the law's table",
          "[cli][tui][runner]") {
    using apogee::commands::exec_words;
    using Words = std::vector<std::string>;
    CHECK(exec_words("models", "pull org/repo --quant Q4_K_M").words ==
          Words{"models", "pull", "org/repo", "--quant", "Q4_K_M"});
    CHECK(exec_words("", "config set-default 'two words'").words ==
          Words{"config", "set-default", "two words"});
    CHECK(exec_words("models", "pull 'open").refusal == "not run: a single quote is left open");
    CHECK(exec_words("models", "  ").refusal == "not run: nothing was typed");
    // Each refusal names the law's recorded reason, after any root flag.
    for (const char* refused :
         {"tui", "chat", "execute --suite duo", "reset", "uninstall", "serve --port 1",
          "agents edit reviewer", "symphonies edit x", "--dev chat", "--config /tmp/c.yaml tui"}) {
        INFO(refused);
        CHECK(exec_words("", refused).refusal.starts_with("refused: '"));
    }
    CHECK(has(exec_words("", "tui").refusal, "refused: 'tui' -- "));
    // Neighbours of a refusal run.
    for (const char* runs : {"chats list", "agents list", "symphonies list", "config get x"}) {
        INFO(runs);
        CHECK(exec_words("", runs).refusal.empty());
    }
}

TEST_CASE("Tab offers what __complete offers for the same words", "[cli][tui][runner]") {
    const apogee::testing::CliHome home{kConfig};
    apogee::commands::RootContext context;
    context.config_path = home.config_path().string();

    struct Case {
        std::string scope;
        std::string line;
        std::vector<std::string> words;
    };

    for (const Case& asked :
         std::vector<Case>{{"", "mod", {"mod"}},
                           {"models", "pu", {"models", "pu"}},
                           {"models", "pull --", {"models", "pull", "--"}},
                           {"config", "set-default ", {"config", "set-default", ""}},
                           {"", "chats ", {"chats", ""}}}) {
        INFO(asked.scope << " | " << asked.line);
        std::vector<std::string> args{"__complete"};
        args.insert(args.end(), asked.words.begin(), asked.words.end());
        std::string out;
        std::string err;
        REQUIRE(home.run(args, &out, &err) == 0);
        CHECK(apogee::commands::exec_candidates(context, asked.scope, asked.line) == lines(out));
    }
    CHECK(apogee::commands::exec_candidates(context, "config", "set-default ") ==
          std::vector<std::string>{"local", "other"});
}

TEST_CASE("the shell's : opens the line scoped, ! unscoped; it takes the keys while open",
          "[cli][tui][runner]") {
    std::vector<std::pair<std::string, std::string>> ran;
    apogee::tui::ExecLineOptions options;
    options.complete = [](const std::string& /*scope*/, const std::string& line) {
        return line.ends_with("pu") ? std::vector<std::string>{"pull"}
                                    : std::vector<std::string>{"list", "info", "pull"};
    };
    options.run = [&ran](const std::string& scope, const std::string& line) {
        ran.emplace_back(scope, line);
        return std::string{"ran it"};
    };
    ShellStage stage{options};
    CHECK(has(stage.frame(), ": a command"));
    CHECK(stage.shell.press(Key::character(":")));
    CHECK(has(stage.frame(), " :models ▏"));
    // Typing is the line's: q is a letter, 2 a digit, not a quit or a view.
    stage.type("q2");
    CHECK_FALSE(stage.shell.quit_requested());
    CHECK(stage.shell.active() == 0);
    (void)stage.shell.press(Key::named(Key::Name::Backspace));
    (void)stage.shell.press(Key::named(Key::Name::Backspace));
    stage.type("pu");
    (void)stage.shell.press(Key::named(Key::Name::Tab));
    CHECK(has(stage.frame(), " :models pull ▏"));
    stage.type("org/repo");
    (void)stage.shell.press(Key::named(Key::Name::Return));
    REQUIRE(ran.size() == 1);
    CHECK(ran.front() == std::pair<std::string, std::string>{"models", "pull org/repo"});
    CHECK(has(stage.frame(), " ran it"));
    // Up brings the last line back; Tab with several lists them.
    (void)stage.shell.press(Key::named(Key::Name::Up));
    CHECK(has(stage.frame(), " :models pull org/repo▏"));
    (void)stage.shell.press(Key::named(Key::Name::Escape));
    CHECK_FALSE(has(stage.frame(), ":models"));
    // `!`: unscoped, from any view not typing.
    stage.shell.activate(1);
    (void)stage.shell.press(Key::character("!"));
    CHECK(has(stage.frame(), " :▏"));
    (void)stage.shell.press(Key::named(Key::Name::Tab));
    CHECK(has(stage.frame(), " list  info  pull"));
    // Ctrl-C with nothing running closes the line, and never quits.
    (void)stage.shell.press(Key::named(Key::Name::CtrlC));
    CHECK_FALSE(stage.shell.quit_requested());
    CHECK_FALSE(has(stage.frame(), " :▏"));
}

TEST_CASE("a command from the exec line is the command on a pipe: a read, a mutation, a refusal",
          "[cli][tui][runner][child]") {
    const fs::path binary = built_binary();
    if (binary.empty()) {
        SKIP("this build made no apogee binary to run as the shell's child");
    }
    const apogee::testing::CliHome home{kConfig};
    apogee::commands::RootContext context;
    context.config_path = home.config_path().string();
    apogee::tui::ManualPump pump;
    const auto output = std::make_shared<apogee::tui::Progress>(pump);
    const apogee::tui::ExecLineOptions options =
        apogee::commands::exec_line_options(context, output, binary);
    const auto ended = [&]() {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
        while (output->running()) {
            REQUIRE(std::chrono::steady_clock::now() < deadline);
            (void)pump.drain();
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
        std::string said;
        for (const std::string& line : output->lines()) {
            said += line + "\n";
        }
        return said;
    };

    // A read: its bytes, then its exit line.
    std::string out;
    std::string err;
    REQUIRE(home.run({"config", "get", "models.default"}, &out, &err) == 0);
    CHECK(options.run("config", "get models.default").empty());
    CHECK(ended() == err + out + "exit 0\n");
    CHECK(output->heading() == "apogee config get models.default");

    // A mutation: the file the command leaves.
    const apogee::testing::CliHome twin{kConfig};
    REQUIRE(twin.run({"config", "set-default", "other"}, &out, &err) == 0);
    CHECK(options.run("config", "set-default other").empty());
    CHECK(ended() == err + out + "exit 0\n");
    CHECK(slurp(home.config_path()) == slurp(twin.config_path()));

    // A refusal: said, and nothing run.
    const std::string before = output->heading();
    CHECK(has(options.run("", "tui"), "refused: 'tui' -- "));
    (void)pump.drain();
    CHECK(output->heading() == before);
    CHECK_FALSE(output->running());
}

TEST_CASE("Ctrl-C stops a slow command from the exec line, and the shell goes on",
          "[cli][tui][runner][child]") {
    const fs::path binary = built_binary();
    if (binary.empty()) {
        SKIP("this build made no apogee binary to run as the shell's child");
    }
    // A model that streams for many seconds.
    const apogee::testing::TempDir scripts{"tui-runner-slow"};
    std::ofstream{scripts.path() / "slow.json"}
        << nlohmann::json{{"turns", {{{"text", std::string(400, 'x')}, {"delay_ms", 100}}}}}.dump();
    const apogee::testing::CliHome home{"backends:\n  slow:\n    type: mock\n    model_path: " +
                                        (scripts.path() / "slow.json").string() +
                                        "\nmodels:\n  default: slow\n"};
    apogee::commands::RootContext context;
    context.config_path = home.config_path().string();
    ShellStage stage{apogee::tui::ExecLineOptions{}};
    const auto output = std::make_shared<apogee::tui::Progress>(stage.pump);
    stage.shell.set_exec_line(apogee::commands::exec_line_options(context, output, binary));
    (void)stage.shell.press(Key::character("!"));
    stage.type("complete hello");
    (void)stage.shell.press(Key::named(Key::Name::Return));
    REQUIRE(output->running());
    // A slow child holds back no frame.
    for (int i = 0; i < 20; ++i) {
        const auto drawn_at = std::chrono::steady_clock::now();
        (void)stage.frame();
        CHECK(std::chrono::steady_clock::now() - drawn_at < std::chrono::milliseconds{500});
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    (void)stage.shell.press(Key::named(Key::Name::CtrlC));
    CHECK(has(stage.frame(), "asked the command to stop"));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
    while (output->running()) {
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        (void)stage.frame();
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    REQUIRE_FALSE(output->lines().empty());
    const std::string last = output->lines().back();
    CHECK((last == "exit 130" || last == "stopped by a signal"));
    CHECK_FALSE(stage.shell.quit_requested());
}
