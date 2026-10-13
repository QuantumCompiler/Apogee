#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "logger/session.h"
#include "support/cli_home.h"
#include "support/env_guard.h"

/// The delegate through the session core (41e), on the real command tree
/// over a scripted model: a delegation's answer returned as the tool's
/// result and its account kept in the parent's record, never a chat of its
/// own; a pipe's denial the same with and without nesting; and a delegated
/// prompt reaching the parent's driver, whose `always` writes the config
/// once -- the narration said in the events machine mode already has.
namespace {

using apogee::testing::CliHome;
using Catch::Matchers::ContainsSubstring;

struct Home {
    CliHome home{std::string{}};
    std::filesystem::path work = home.home() / "work";

    explicit Home(std::string_view script) {
        std::filesystem::create_directories(work);
        const std::filesystem::path path = home.home() / "root.json";
        std::ofstream{path} << script;
        std::ofstream{home.config_path(), std::ios::binary | std::ios::trunc}
            << "backends:\n  root:\n    type: mock\n    model_path: " << path.generic_string()
            << "\nmodels:\n  default: root\nmemory:\n  recall: false\npermissions:\n"
               "  write_file: ask\ntools:\n  fs_root: "
            << work.generic_string() << "\n";
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

    [[nodiscard]] std::vector<apogee::logger::Session> sessions() const {
        const apogee::testing::EnvGuard guard{"APOGEE_HOME", home.home().string()};
        return apogee::logger::list_sessions();
    }

    [[nodiscard]] std::string config() const {
        std::ifstream in{home.config_path()};
        return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    }
};

/// The parent delegates; the delegate (the same scripted model, a fresh
/// conversation) answers; the parent answers with what came back.
constexpr std::string_view kDelegates = R"({"turns": [
  {"text": "", "tool_calls": [{"name": "delegate", "arguments": {"brief": "Find the version"}}]},
  {"text": "SUB ANSWER"},
  {"text": "Done: {{last_tool_result}}"}
]})";

}  // namespace

TEST_CASE("a delegation returns its answer and account, kept only in the parent's record",
          "[cli][session][delegate]") {
    const Home home{kDelegates};
    std::string out;
    std::string err;
    REQUIRE(home.run({"chat", "--tools"}, &out, &err, "go\n") == 0);
    CHECK_THAT(out, Catch::Matchers::StartsWith("Done: SUB ANSWER\n\n[delegate on root: 1 model "
                                                "call, no tools, "));
    // One chat: the sub-session is the tool call's detail, never its own.
    const std::vector<apogee::logger::Session> saved = home.sessions();
    REQUIRE(saved.size() == 1);
    bool kept = false;
    for (const apogee::harness::ChatMessage& message : saved.front().messages) {
        kept = kept || (message.role == apogee::harness::Role::Tool &&
                        message.content.plain_text().find("SUB ANSWER\n\n[delegate on root:") !=
                            std::string::npos);
    }
    CHECK(kept);
}

TEST_CASE("on a pipe a delegated write is denied exactly as the parent's own would be",
          "[cli][session][delegate][gate]") {
    // The parent's own call.
    const Home direct{R"({"turns": [
      {"text": "", "tool_calls": [{"name": "write_file", "arguments": {"path": "x.txt", "content": "x"}}]},
      {"text": "DIRECT<{{last_tool_result}}>"}]})"};
    std::string direct_out;
    std::string err;
    REQUIRE(direct.run({"chat", "--tools"}, &direct_out, &err, "go\n") == 0);
    // The same call, made by a delegate, whose answer quotes what it got.
    const Home nested{R"({"turns": [
      {"text": "", "tool_calls": [{"name": "delegate", "arguments": {"brief": "Write x.txt"}}]},
      {"text": "", "tool_calls": [{"name": "write_file", "arguments": {"path": "x.txt", "content": "x"}}]},
      {"text": "DIRECT<{{last_tool_result}}>"},
      {"text": "PARENT<{{last_tool_result}}>"}]})"};
    std::string nested_out;
    REQUIRE(nested.run({"chat", "--tools"}, &nested_out, &err, "go\n") == 0);
    const std::string denial = direct_out.substr(0, direct_out.find('>') + 1);
    REQUIRE_THAT(denial, Catch::Matchers::StartsWith("DIRECT<"));
    CHECK_THAT(nested_out, ContainsSubstring(denial));
    CHECK_FALSE(std::filesystem::exists(direct.work / "x.txt"));
    CHECK_FALSE(std::filesystem::exists(nested.work / "x.txt"));
}

TEST_CASE("a delegated prompt reaches the parent's driver, and its always is written once",
          "[cli][session][delegate][gate][machine]") {
    const Home home{R"({"turns": [
      {"text": "", "tool_calls": [{"name": "delegate", "arguments": {"brief": "Write x.txt"}}]},
      {"text": "", "tool_calls": [{"name": "write_file", "arguments": {"path": "x.txt", "content": "x"}}]},
      {"text": "Wrote it."},
      {"text": "Done."}]})"};
    const std::string before = home.config();
    std::string out;
    std::string err;
    REQUIRE(home.run({"chat", "--tools", "--output-format", "stream-json"}, &out, &err,
                     "{\"type\":\"user\",\"text\":\"go\"}\n"
                     "{\"type\":\"answer\",\"text\":\"always\"}\n") == 0);
    std::vector<nlohmann::json> events;
    std::istringstream lines{out};
    for (std::string line; std::getline(lines, line);) {
        events.push_back(nlohmann::json::parse(line));
    }
    int questions = 0;
    bool narrated = false;
    for (const nlohmann::json& event : events) {
        questions += event.at("type") == "question" ? 1 : 0;
        narrated = narrated || (event.at("type") == "tool_status" &&
                                event.at("text").get<std::string>().starts_with(
                                    "delegate — a sub-session on root: Write x.txt"));
    }
    CHECK(questions == 1);
    CHECK(narrated);
    CHECK(std::filesystem::exists(home.work / "x.txt"));
    // Exactly the one line the answer names, through the one editor.
    std::string expected = before;
    const std::size_t at = expected.find("write_file: ask");
    REQUIRE(at != std::string::npos);
    expected.replace(at, std::string_view{"write_file: ask"}.size(), "write_file: allow");
    CHECK(home.config() == expected);
}
