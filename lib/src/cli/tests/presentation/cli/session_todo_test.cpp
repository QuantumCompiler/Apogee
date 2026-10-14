#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "logger/session.h"
#include "support/cli_home.h"
#include "support/env_guard.h"

/// The session todo through the session core (41d), on the real command
/// tree: a scripted model keeps a todo with `todo_write`; the terminal keeps
/// its block, machine mode emits it, the session file holds it, a resume on
/// either door shows it as it was left, and `/todo` prints it.
namespace {

using apogee::testing::CliHome;
using Catch::Matchers::ContainsSubstring;

/// The model plans with `todo_write`, then answers.
constexpr std::string_view kPlans = R"({"turns": [
  {"text": "", "tool_calls": [{"name": "todo_write", "arguments": {"items": [
    {"text": "Read the file", "state": "done"},
    {"text": "Fix the bug", "state": "active"},
    {"text": "Run the tests", "state": "pending"}]}}]},
  {"text": "Planned."}
]})";

struct Home {
    CliHome home{std::string{}};

    explicit Home(std::string_view script = kPlans) {
        const std::filesystem::path path = home.home() / "root.json";
        std::ofstream{path} << script;
        std::ofstream{home.config_path(), std::ios::binary | std::ios::trunc}
            << "backends:\n  root:\n    type: mock\n    model_path: " << path.generic_string()
            << "\nmodels:\n  default: root\nmemory:\n  recall: false\n";
    }

    int run(const std::vector<std::string>& args, std::string* out, std::string* err,
            const std::string& input = {}) const {
        const std::istringstream fed{input};
        std::streambuf* old_in = std::cin.rdbuf(fed.rdbuf());
        const int code = home.run(args, out, err);
        std::cin.rdbuf(old_in);
        std::cin.clear();
        return code;
    }

    [[nodiscard]] apogee::logger::Session newest() const {
        const apogee::testing::EnvGuard guard{"APOGEE_HOME", home.home().string()};
        const std::vector<apogee::logger::Session> all = apogee::logger::list_sessions();
        REQUIRE_FALSE(all.empty());
        return all.front();
    }
};

const std::string kBlock =
    "todo · 1 of 3 done\n  [x] Read the file\n  [>] Fix the bug\n  [ ] Run the tests\n";

/// The `todo` events in a stream.
std::vector<nlohmann::json> todo_events(const std::string& stream) {
    std::vector<nlohmann::json> out;
    std::istringstream lines{stream};
    for (std::string line; std::getline(lines, line);) {
        const nlohmann::json event = nlohmann::json::parse(line);
        if (event.at("type") == "todo") {
            out.push_back(event);
        }
    }
    return out;
}

}  // namespace

TEST_CASE("a tools-on session keeps a todo: drawn, saved, shown again on resume, /todo prints it",
          "[cli][session][todo]") {
    const Home home;
    std::string out;
    std::string err;
    REQUIRE(home.run({"chat", "--tools"}, &out, &err, "plan it\n") == 0);
    CHECK(out == "Planned.\n");
    CHECK_THAT(err, ContainsSubstring(kBlock));
    const apogee::logger::Session saved = home.newest();
    REQUIRE(saved.todo.size() == 3);
    CHECK(saved.todo[1].text == "Fix the bug");
    CHECK(saved.todo[1].state == apogee::harness::TodoState::Active);

    // Resumed: the list as it was left, before any turn, and /todo prints it.
    REQUIRE(home.run({"chat", "-c", "--tools"}, &out, &err, "/todo\n") == 0);
    CHECK_THAT(err, ContainsSubstring(kBlock));
    const std::size_t first = err.find("todo · 1 of 3 done");
    CHECK(err.find("todo · 1 of 3 done", first + 1) != std::string::npos);  // and again for /todo
}

TEST_CASE("machine mode emits the todo, and a resumed stream opens with it",
          "[cli][session][todo][machine]") {
    const Home home;
    std::string out;
    std::string err;
    REQUIRE(home.run({"chat", "--tools", "--output-format", "stream-json"}, &out, &err,
                     "{\"type\":\"user\",\"text\":\"plan it\"}\n") == 0);
    std::vector<nlohmann::json> events = todo_events(out);
    REQUIRE(events.size() == 1);
    CHECK(events.front().at("items") ==
          nlohmann::json::parse(R"([{"text":"Read the file","state":"done"},)"
                                R"({"text":"Fix the bug","state":"active"},)"
                                R"({"text":"Run the tests","state":"pending"}])"));
    CHECK(events.front().at("turn") == 1);

    // The resumed session's first event after `session` is its todo.
    REQUIRE(home.run({"chat", "-c", "--tools", "--output-format", "stream-json"}, &out, &err, "") ==
            0);
    std::istringstream lines{out};
    std::string first;
    std::string second;
    std::getline(lines, first);
    std::getline(lines, second);
    CHECK(nlohmann::json::parse(first).at("type") == "session");
    CHECK(nlohmann::json::parse(second).at("type") == "todo");
}

TEST_CASE("a session without tools has no todo, and /todo says how to have one",
          "[cli][session][todo]") {
    const Home home{R"({"turns": [{"text": "plain"}]})"};
    std::string out;
    std::string err;
    REQUIRE(home.run({"chat"}, &out, &err, "hi\n/todo\n") == 0);
    CHECK_THAT(err, ContainsSubstring("no todo -- a session with --tools keeps one"));
    CHECK(home.newest().todo.empty());
    REQUIRE(home.run({"chat", "--tools"}, &out, &err, "/todo\n") == 0);
    CHECK_THAT(err, ContainsSubstring("no todo yet -- the model keeps one with todo_write"));
}
