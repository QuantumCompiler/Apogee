#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "agentloop/member_call.h"
#include "agentloop/reporter.h"
#include "backends/mock.h"
#include "cli/chat_completer.h"
#include "cli/chat_play.h"
#include "cli/chat_session.h"
#include "cli/chat_turn.h"
#include "contracts/config.h"
#include "harness/harness.h"
#include "logger/session.h"
#include "support/cli_home.h"
#include "support/env_guard.h"
#include "symphony/tools.h"

/// `apogee execute` (27s): chat's session core opened with a suite.
///
/// In process, over scripted members that echo the brief they were sent:
/// the suite-first refusals and the default suite; bare input conversing
/// with the root exactly as `chat --suite` does; `/play` running a symphony
/// whose stages are the members' calls, its output the session's answer and
/// its history -- resumed whole, and read by the root on the next question;
/// a play that stops leaving the session as it was; `/symphonies`; `/suite`
/// switching and never turning the suite off; machine mode carrying a play
/// as an ordinary turn in the events it already has; the command table's
/// execute rows, `/help`, and completion in the REPL and the shell. The
/// whole surface is pinned by a golden written by this item and reviewed.
namespace {

using apogee::testing::CliHome;
namespace c = apogee::commands;

/// Members that echo what they were sent, a two-stage symphony over them,
/// and suites: `duo` (root answers, helper helps), `fast` (helper answers),
/// and `strict` (an extraction member whose answer is no JSON).
std::string config_with(const std::filesystem::path& scripts, const std::string& models) {
    std::filesystem::create_directories(scripts);
    std::ofstream{scripts / "root.json"} << R"({"turns": [{"text": "ROOT<{{last_user}}>"}]})";
    std::ofstream{scripts / "helper.json"} << R"({"turns": [{"text": "HELPER<{{last_user}}>"}]})";
    return "backends:\n"
           "  root:\n    type: mock\n    model_path: " +
           (scripts / "root.json").generic_string() +
           "\n  helper:\n    type: mock\n    model_path: " +
           (scripts / "helper.json").generic_string() + "\nmodels:\n  default: root\n" + models +
           "memory:\n  recall: false\n"
           "suites:\n"
           "  duo:\n    description: The root and its helper.\n    members:\n"
           "      chat: root\n      utility: helper\n"
           "  fast:\n    members:\n      chat: helper\n      utility: helper\n"
           "  strict:\n    members:\n      chat: root\n      utility: helper\n"
           "      extraction: helper\n"
           "symphonies:\n"
           "  echo2:\n    description: Two stages, the second given the first's answer.\n"
           "    input:\n      description: Any text.\n"
           "    stages:\n"
           "      - {name: first, role: utility, prompt: 'One: {{input}}'}\n"
           "      - {name: second, role: chat, prompt: 'Two: {{first}}'}\n";
}

/// A throwaway install of that config; `models` lands under `models:`.
struct Home {
    CliHome home{std::string{}};

    explicit Home(const std::string& models = {}) {
        std::ofstream{home.config_path(), std::ios::binary | std::ios::trunc}
            << config_with(home.home() / "scripts", models);
    }

    /// `apogee --config <config> <args>` with `input` on stdin.
    int run(const std::vector<std::string>& args, std::string* out, std::string* err,
            const std::string& input = {}) const {
        const std::istringstream fed{input};
        std::streambuf* old_in = std::cin.rdbuf(fed.rdbuf());
        const int code = home.run(args, out, err);
        std::cin.rdbuf(old_in);
        std::cin.clear();
        return code;
    }

    /// The sessions saved here, newest first.
    [[nodiscard]] std::vector<apogee::logger::Session> sessions() const {
        const apogee::testing::EnvGuard guard{"APOGEE_HOME", home.home().string()};
        return apogee::logger::list_sessions();
    }
};

/// The messages of `session` as `role: text` lines.
std::vector<std::string> transcript(const apogee::logger::Session& session) {
    std::vector<std::string> out;
    for (const apogee::harness::ChatMessage& message : session.messages) {
        out.push_back(std::string{apogee::harness::to_string(message.role)} + ": " +
                      message.content.plain_text());
    }
    return out;
}

/// `text` with every occurrence of `from` replaced by `to`.
std::string replaced(std::string text, const std::string& from, const std::string& to) {
    if (from.empty()) {
        return text;
    }
    for (std::size_t at = text.find(from); at != std::string::npos;
         at = text.find(from, at + to.size())) {
        text.replace(at, from.size(), to);
    }
    return text;
}

/// The types of the JSONL events in `stream`, in order.
std::vector<std::string> event_types(const std::string& stream) {
    std::vector<std::string> out;
    std::istringstream lines{stream};
    for (std::string line; std::getline(lines, line);) {
        out.push_back(nlohmann::json::parse(line).at("type").get<std::string>());
    }
    return out;
}

constexpr std::string_view kPlayed = "ROOT<Two: HELPER<One: hello>>";

}  // namespace

// --- suite-first -------------------------------------------------------------

TEST_CASE("execute with no suite named and none the default refuses, naming the way to one",
          "[execute][suites]") {
    {
        const Home home;
        std::string out;
        std::string err;
        CHECK(home.run({"execute"}, &out, &err, "hi\n") == 1);
        CHECK(out.empty());
        CHECK(err ==
              "apogee execute: execute opens a session with a suite, and none is named or the "
              "default -- run one with --suite <name> (configured: duo, fast, strict), or make "
              "one the default: apogee config set-default-suite <name>\n");
        CHECK(home.sessions().empty());
    }
    {
        // With no suite at all: how to add one -- never a plain chat.
        CliHome bare{"backends:\n  root:\n    type: mock\nmodels:\n  default: root\n"};
        std::string out;
        std::string err;
        CHECK(bare.run({"execute"}, &out, &err) == 1);
        CHECK(out.empty());
        CHECK(err ==
              "apogee execute: execute opens a session with a suite, and none is configured -- "
              "add one: apogee config add-suite <name> --chat <backend> [--utility <backend> "
              "...], then run it with --suite <name>, or make it the default: apogee config "
              "set-default-suite <name>\n");
    }
}

TEST_CASE("execute never runs under no suite: --suite off and a missing suite are refused",
          "[execute][suites]") {
    const Home home{"  default_suite: duo\n"};
    std::string out;
    std::string err;
    CHECK(home.run({"execute", "--suite", "off"}, &out, &err, "hi\n") == 1);
    CHECK(out.empty());
    CHECK(err ==
          "apogee execute: --suite off names no suite, and execute opens a session with one -- "
          "'apogee chat --suite off' is a session without\n");
    CHECK(home.run({"execute", "--suite", "nope"}, &out, &err, "hi\n") == 1);
    CHECK(err ==
          "apogee execute: --suite: no suite named 'nope' (configured: duo, fast, strict)\n");
    CHECK(home.sessions().empty());
}

TEST_CASE("execute with no suite named runs under the config's default", "[execute][suites]") {
    const Home home{"  default_suite: duo\n"};
    std::string out;
    std::string err;
    REQUIRE(home.run({"execute"}, &out, &err, "hi\n/suite\n") == 0);
    CHECK(out == "ROOT<hi>\n");
    CHECK_THAT(err, Catch::Matchers::ContainsSubstring("suite: duo -- chat root · utility helper"));
    const std::vector<apogee::logger::Session> sessions = home.sessions();
    REQUIRE(sessions.size() == 1);
    CHECK(sessions.front().suite == std::optional<std::string>{"duo"});
}

TEST_CASE("a resumed session's suite is execute's when none is named; one with none is refused",
          "[execute][suites]") {
    const Home home;
    std::string out;
    std::string err;
    // A chat under fast, then turned off: execute takes neither "off" nor
    // nothing -- with no default it refuses; named, it runs.
    REQUIRE(home.run({"chat", "--suite", "fast"}, &out, &err, "hi\n") == 0);
    REQUIRE(home.run({"execute", "-c"}, &out, &err, "again\n") == 0);
    CHECK(out == "HELPER<again>\n");
    REQUIRE(home.run({"chat", "-c"}, &out, &err, "/suite off\n") == 0);
    CHECK(home.run({"execute", "-c"}, &out, &err, "more\n") == 1);
    CHECK_THAT(err, Catch::Matchers::StartsWith(
                        "apogee execute: execute opens a session with a suite, and none is named"));
    REQUIRE(home.run({"execute", "-c", "--suite", "duo"}, &out, &err, "more\n") == 0);
    CHECK(out == "ROOT<more>\n");
}

// --- bare input is chat's turn --------------------------------------------------

TEST_CASE("bare input converses with the suite's root exactly as chat --suite does",
          "[execute][session]") {
    const Home chat_home;
    const Home execute_home;
    std::string chat_out;
    std::string chat_err;
    std::string execute_out;
    std::string execute_err;
    const std::string input = "hello\nand then\n/model\n";
    REQUIRE(chat_home.run({"chat", "--suite", "duo"}, &chat_out, &chat_err, input) == 0);
    REQUIRE(execute_home.run({"execute", "--suite", "duo"}, &execute_out, &execute_err, input) ==
            0);
    CHECK(execute_out == chat_out);
    CHECK(execute_err == chat_err);
    CHECK(transcript(execute_home.sessions().front()) == transcript(chat_home.sessions().front()));
}

// --- /play ------------------------------------------------------------------------

TEST_CASE("/play runs the symphony on the members; its output is the answer and the history",
          "[execute][play]") {
    const Home home;
    std::string out;
    std::string err;
    REQUIRE(home.run({"execute", "--suite", "duo"}, &out, &err, "/play echo2 hello\n") == 0);
    // Stage two was given stage one's answer, never the input: the chain on
    // the wire, the root answering last.
    CHECK(out == std::string{kPlayed} + "\n");
    const std::vector<apogee::logger::Session> sessions = home.sessions();
    REQUIRE(sessions.size() == 1);
    CHECK(
        transcript(sessions.front()) ==
        std::vector<std::string>{"user: /play echo2 hello", "assistant: " + std::string{kPlayed}});
    CHECK(sessions.front().turns == 1);
}

TEST_CASE("a line that is not UTF-8 is played, kept and saved as text", "[execute][play][utf8]") {
    // The /play line and a system prompt (--system, /system) never pass
    // build_messages, and the session file is a strict JSON dump: each is
    // mended where it enters the session, so the save cannot throw and every
    // surface keeps one text.
    const std::string replacement = "\xEF\xBF\xBD";
    const Home home;
    std::string out;
    std::string err;
    const int code = home.run({"execute", "--suite", "duo", "--system", "r\xE9sum\xE9"}, &out, &err,
                              "/play echo2 caf\xE9\n/system cr\xE8me\n");
    INFO(err);
    REQUIRE(code == 0);
    const std::string played = "ROOT<Two: HELPER<One: caf" + replacement + ">>";
    CHECK(out == played + "\n");
    const std::vector<apogee::logger::Session> sessions = home.sessions();
    REQUIRE(sessions.size() == 1);
    CHECK(transcript(sessions.front()) ==
          std::vector<std::string>{"system: r" + replacement + "sum" + replacement,
                                   "user: /play echo2 caf" + replacement, "assistant: " + played});
    CHECK(sessions.front().params.system_prompt == "cr" + replacement + "me");
}

TEST_CASE("a play under another suite plays on that suite's members", "[execute][play]") {
    const Home home;
    std::string out;
    std::string err;
    REQUIRE(home.run({"execute", "--suite", "fast"}, &out, &err, "/play echo2 hello\n") == 0);
    CHECK(out == "HELPER<Two: HELPER<One: hello>>\n");
}

TEST_CASE("a play-heavy session resumes with its plays intact", "[execute][play][resume]") {
    const Home home;
    std::string out;
    std::string err;
    REQUIRE(home.run({"execute", "--suite", "duo"}, &out, &err,
                     "/play echo2 a\nbetween\n/play echo2 b\n") == 0);
    REQUIRE(home.run({"execute", "-c"}, &out, &err, "after\n/play echo2 c\n") == 0);
    CHECK(out == "ROOT<after>\nROOT<Two: HELPER<One: c>>\n");
    const std::vector<apogee::logger::Session> sessions = home.sessions();
    REQUIRE(sessions.size() == 1);
    CHECK(transcript(sessions.front()) ==
          std::vector<std::string>{"user: /play echo2 a", "assistant: ROOT<Two: HELPER<One: a>>",
                                   "user: between", "assistant: ROOT<between>",
                                   "user: /play echo2 b", "assistant: ROOT<Two: HELPER<One: b>>",
                                   "user: after", "assistant: ROOT<after>", "user: /play echo2 c",
                                   "assistant: ROOT<Two: HELPER<One: c>>"});
    CHECK(sessions.front().turns == 5);
    CHECK(sessions.front().suite == std::optional<std::string>{"duo"});
}

TEST_CASE("a play that cannot run, or stops, is said and leaves the session as it was",
          "[execute][play]") {
    const Home home;
    std::string out;
    std::string err;
    REQUIRE(home.run({"execute", "--suite", "strict"}, &out, &err,
                     "/play\n/play nope x\n/play echo2\n/play describe-answer what is it?\n"
                     "/play extract-facts The cat sat.\n") == 0);
    CHECK(out.empty());
    CHECK_THAT(err, Catch::Matchers::ContainsSubstring(
                        "[error] /play takes a symphony, then its input -- /play <symphony> "
                        "[input] (/symphonies lists them)"));
    CHECK_THAT(err, Catch::Matchers::ContainsSubstring("[error] no symphony named 'nope'"));
    CHECK_THAT(err, Catch::Matchers::ContainsSubstring(
                        "[error] 'echo2' reads its input (Any text.), and none was given -- "
                        "/play <symphony> [input]"));
    CHECK_THAT(err, Catch::Matchers::ContainsSubstring(
                        "[error] 'describe-answer' takes an image with its input, and none was "
                        "given -- /play gives a symphony text alone; 'apogee symphonies play "
                        "describe-answer --image <file>' plays it with one"));
    // A member whose answer breaks its stage's schema stops the walk, named.
    CHECK_THAT(err, Catch::Matchers::ContainsSubstring("[error] stage 1/1 extract (extraction)"));
    // Nothing was kept: no exchange, no turn.
    const std::vector<apogee::logger::Session> sessions = home.sessions();
    REQUIRE(sessions.size() == 1);
    CHECK(sessions.front().messages.empty());
    CHECK(sessions.front().turns == 0);
}

TEST_CASE("/symphonies lists what /play can play, as symphonies list does", "[execute][play]") {
    const Home home;
    std::string out;
    std::string err;
    std::string listed;
    REQUIRE(home.run({"symphonies", "list"}, &listed, &err) == 0);
    std::string indented;
    std::istringstream rows{listed};
    for (std::string row; std::getline(rows, row);) {
        indented += "  " + row + "\n";
    }
    std::string said;
    REQUIRE(home.run({"execute", "--suite", "duo"}, &out, &said, "/symphonies\n") == 0);
    CHECK(out.empty());
    CHECK(said == "[apogee] suite duo: no member holds memory on this machine\n" + indented);
    CHECK_THAT(said, Catch::Matchers::ContainsSubstring("echo2"));
    CHECK_THAT(said, Catch::Matchers::ContainsSubstring("summarize-verify"));
}

TEST_CASE("/suite switches an execute session's suite and never turns it off",
          "[execute][suites]") {
    const Home home;
    std::string out;
    std::string err;
    REQUIRE(home.run({"execute", "--suite", "duo"}, &out, &err,
                     "/suite off\nhi\n/suite fast\nhi\n/play echo2 x\n") == 0);
    CHECK(out == "ROOT<hi>\nHELPER<hi>\nHELPER<Two: HELPER<One: x>>\n");
    CHECK_THAT(err, Catch::Matchers::ContainsSubstring(
                        "[error] execute runs under a suite -- /suite <name> switches to another; "
                        "'apogee chat' is a session without one"));
    CHECK(home.sessions().front().suite == std::optional<std::string>{"fast"});
}

TEST_CASE("a stage whose role the suite leaves out falls back to the session's own backend",
          "[execute][play]") {
    // `fast` names no vision member and the config no vision pointer: the
    // one chain falls back to the conversation's backend -- the session's,
    // helper -- never to `models.default` (root).
    const Home home;
    std::ofstream{home.home.config_path(), std::ios::app}
        << "  helpless:\n    stages:\n"
           "      - {name: only, role: vision, prompt: 'Read: {{input}}'}\n";
    std::string out;
    std::string err;
    REQUIRE(home.run({"execute", "--suite", "fast"}, &out, &err, "/play helpless x\n") == 0);
    CHECK(out == "HELPER<Read: x>\n");
}

TEST_CASE("a play is held to symphony_caps, the whole walk under one budget", "[execute][play]") {
    const Home home;
    std::ofstream{home.home.config_path(), std::ios::app} << "symphony_caps:\n  stage_calls: 1\n";
    std::string out;
    std::string err;
    REQUIRE(home.run({"execute", "--suite", "duo"}, &out, &err, "/play echo2 x\n") == 0);
    CHECK(out.empty());
    CHECK_THAT(err, Catch::Matchers::ContainsSubstring("the play's budget is spent"));
    CHECK(home.sessions().front().messages.empty());
}

TEST_CASE("a session that turned its suite off resumes in execute under the default",
          "[execute][suites]") {
    const Home home{"  default_suite: duo\n"};
    std::string out;
    std::string err;
    REQUIRE(home.run({"chat", "--suite", "off"}, &out, &err, "hi\n") == 0);
    REQUIRE(home.run({"execute", "-c"}, &out, &err, "again\n/suite\n") == 0);
    CHECK(out == "ROOT<again>\n");
    CHECK_THAT(err, Catch::Matchers::ContainsSubstring("suite: duo -- chat root"));
    CHECK(home.sessions().front().suite == std::optional<std::string>{"duo"});
}

TEST_CASE("a session of plays is summarised for recall, as a chat of turns is",
          "[execute][play][recall]") {
    const Home home;
    const std::string text = home.home.config_text();
    std::ofstream{home.home.config_path(), std::ios::binary | std::ios::trunc}
        << replaced(text, "memory:\n  recall: false\n", "memory:\n  recall: true\n");
    std::string out;
    std::string err;
    REQUIRE(home.run({"execute", "--suite", "duo"}, &out, &err, "/play echo2 a\n/play echo2 b\n") ==
            0);
    CHECK_THAT(err, Catch::Matchers::ContainsSubstring("[memory] "));
}

TEST_CASE("chat has no /play: its table is chat's, and the line is an unknown command",
          "[execute][play]") {
    const Home home;
    std::string out;
    std::string err;
    REQUIRE(home.run({"chat", "--suite", "duo"}, &out, &err, "/play echo2 hello\n/symphonies\n") ==
            0);
    CHECK(out.empty());
    CHECK(err ==
          "[apogee] suite duo: no member holds memory on this machine\n"
          "[error] unknown command '/play' -- /help lists them\n"
          "[error] unknown command '/symphonies' -- /help lists them\n");
}

// --- machine mode -------------------------------------------------------------------

TEST_CASE("a driver sees a played symphony as an ordinary turn, in the events it already has",
          "[execute][play][machine]") {
    const Home home;
    std::string out;
    std::string err;
    REQUIRE(home.run({"execute", "--suite", "duo", "--output-format", "stream-json"}, &out, &err,
                     "{\"type\":\"user\",\"text\":\"/play echo2 hello\"}\n"
                     "{\"type\":\"user\",\"text\":\"hi\"}\n") == 0);
    // Each stage a `tool_status`, as a side call is; the output the answer
    // and the turn's `result` -- nothing a chat turn does not emit.
    CHECK(event_types(out) == std::vector<std::string>{"session", "tool_status", "tool_status",
                                                       "answer_start", "answer_delta", "answer_end",
                                                       "result", "thinking", "answer_start",
                                                       "answer_delta", "answer_end", "result"});
    std::istringstream lines{out};
    std::vector<nlohmann::json> events;
    for (std::string line; std::getline(lines, line);) {
        events.push_back(nlohmann::json::parse(line));
    }
    CHECK_THAT(
        events[1].at("text").get<std::string>(),
        Catch::Matchers::StartsWith("stage 1/2 first — asking utility (helper): One: hello"));
    CHECK_THAT(events[2].at("text").get<std::string>(),
               Catch::Matchers::StartsWith("stage 2/2 second — asking chat (root): Two: HELPER<"));
    CHECK(events[4].at("text") == kPlayed);
    CHECK(events[6].at("text") == kPlayed);
    CHECK(events[6].at("model") == "root");
    CHECK(transcript(home.sessions().front()) ==
          std::vector<std::string>{"user: /play echo2 hello", "assistant: " + std::string{kPlayed},
                                   "user: hi", "assistant: ROOT<hi>"});
}

TEST_CASE("a driver's play that cannot run ends in an error event, and nothing is kept",
          "[execute][play][machine]") {
    const Home home;
    std::string out;
    std::string err;
    REQUIRE(home.run({"execute", "--suite", "duo", "--output-format", "stream-json"}, &out, &err,
                     "{\"type\":\"user\",\"text\":\"/play nope x\"}\n") == 0);
    CHECK(event_types(out) == std::vector<std::string>{"session", "error"});
    CHECK_THAT(err, Catch::Matchers::ContainsSubstring("apogee: no symphony named 'nope'"));
    CHECK(home.sessions().front().messages.empty());
}

// --- the turn functions ---------------------------------------------------------------

namespace {

/// A reporter that keeps what it is told.
class Recorder final : public apogee::agentloop::Reporter {
public:
    std::vector<std::string> said;

    void on_side_call(const apogee::agentloop::SideCall& call) override {
        if (!call.done) {
            said.push_back("side: " + call.role + " — " + call.detail);
        }
    }

    void on_answer_start() override {
        said.emplace_back("answer_start");
    }

    void on_answer_token(std::string_view chunk) override {
        said.push_back("answer: " + std::string{chunk});
    }

    void on_answer_end() override {
        said.emplace_back("answer_end");
    }

    void on_clear_status() override {
        said.emplace_back("clear");
    }
};

/// A harness over the test config's members, each a mock recording what it
/// was sent.
struct Members {
    apogee::testing::TempDir dir{"execute-members-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", dir.path().string()};
    std::filesystem::path config_path = dir.path() / "config" / "config.yaml";
    std::unique_ptr<apogee::harness::Harness> harness;
    std::map<std::string, std::shared_ptr<apogee::backends::MockProvider>> mocks;

    Members() {
        std::filesystem::create_directories(config_path.parent_path());
        const std::string text = config_with(dir.path() / "scripts", "  default_suite: duo\n");
        std::ofstream{config_path, std::ios::binary} << text;
        harness = std::make_unique<apogee::harness::Harness>(
            apogee::harness::parse_config(text, config_path.string()));
        for (const char* name : {"root", "helper"}) {
            apogee::backends::MockProvider::Options options;
            options.backend_name = name;
            options.turns.push_back(apogee::backends::MockTurn{
                .text = std::string{name == std::string_view{"root"} ? "ROOT" : "HELPER"} +
                        "<{{last_user}}>"});
            mocks[name] = std::make_shared<apogee::backends::MockProvider>(options);
            harness->register_provider(name, mocks[name]);
        }
        harness->use_default_router();
    }
};

}  // namespace

TEST_CASE("a play's stages are narrated as side calls and its output said as the answer",
          "[execute][play]") {
    Members members;
    apogee::logger::Session session;
    session.chat_id = "test-play";
    session.backend = "root";
    apogee::agentloop::MemberCalls calls{*members.harness};
    Recorder reporter;
    const c::PreparedPlay prepared = c::prepare_play(members.harness->config(), members.config_path,
                                                     c::parse_play_argument("echo2 hello"));
    REQUIRE(prepared.refusal.empty());
    const c::PlayTurnResult played = c::run_play_turn(
        *members.harness, session, "/play echo2 hello", prepared, calls, reporter, nullptr);
    INFO(played.failure);
    CHECK(played.completed);
    CHECK(reporter.said == std::vector<std::string>{
                               "side: stage 1/2 first — asking utility (helper): One: hello",
                               "side: stage 2/2 second — asking chat (root): Two: HELPER<One: "
                               "hello>",
                               "answer_start", "answer: " + std::string{kPlayed}, "answer_end"});
    // Each member was sent its stage's brief and nothing else.
    REQUIRE(members.mocks["helper"]->requests().size() == 1);
    CHECK(members.mocks["helper"]->requests().front().messages.size() == 1);
    // Saved at once, as every turn is: a kill after a play keeps it.
    const apogee::logger::LoadedSession saved = apogee::logger::load("test-play", {});
    CHECK(
        transcript(saved.session) ==
        std::vector<std::string>{"user: /play echo2 hello", "assistant: " + std::string{kPlayed}});
}

TEST_CASE("the root reads a play on the next question as it reads any earlier answer",
          "[execute][play]") {
    Members members;
    apogee::logger::Session session;
    session.chat_id = "test-history";
    session.backend = "root";
    const auto calls = std::make_shared<apogee::agentloop::MemberCalls>(*members.harness);
    apogee::agentloop::NullReporter reporter;
    const c::PreparedPlay prepared = c::prepare_play(members.harness->config(), members.config_path,
                                                     c::parse_play_argument("echo2 hello"));
    REQUIRE(c::run_play_turn(*members.harness, session, "/play echo2 hello", prepared, *calls,
                             reporter, nullptr)
                .completed);
    const c::RagSettings rag{.config_path = members.config_path};
    const c::ChatTurnResult turn = c::run_chat_turn(
        *members.harness, session, "what did it say?", nullptr, nullptr, calls.get(), {},
        c::ToolGate{}, reporter, [](const std::string&) {}, rag, {}, nullptr, nullptr);
    REQUIRE(turn.completed);
    const apogee::harness::ChatRequest& asked = members.mocks["root"]->requests().back();
    std::vector<std::string> sent;
    for (const apogee::harness::ChatMessage& message : asked.messages) {
        sent.push_back(std::string{apogee::harness::to_string(message.role)} + ": " +
                       message.content.plain_text());
    }
    CHECK(sent == std::vector<std::string>{"user: /play echo2 hello",
                                           "assistant: " + std::string{kPlayed},
                                           "user: what did it say?"});
}

TEST_CASE("/play's argument is the symphony, then the rest of the line as typed",
          "[execute][play]") {
    const c::PlayArgument plain = c::parse_play_argument("echo2 hello  there ");
    CHECK(plain.symphony == "echo2");
    CHECK(plain.input == "hello  there ");
    CHECK(plain.error.empty());
    const c::PlayArgument bare = c::parse_play_argument("summarize-verify");
    CHECK(bare.symphony == "summarize-verify");
    CHECK(bare.input.empty());
    CHECK(c::parse_play_argument("").error ==
          "/play takes a symphony, then its input -- /play <symphony> [input] (/symphonies lists "
          "them)");
    CHECK(c::parse_play_argument("   ").error.starts_with("/play takes a symphony"));
}

// --- the banner -------------------------------------------------------------------------

TEST_CASE("the banner names the suite and what execute can play; chat's is chat's",
          "[execute][banner]") {
    CHECK(c::session_banner(c::SessionMode::Execute, "root", false, "duo", false, 4, std::nullopt,
                            "id-1") ==
          "root  ·  suite duo  ·  4 symphonies  ·  chat id-1  ·  /help for commands");
    CHECK(c::session_banner(c::SessionMode::Execute, "root", true, "duo", true, 1, std::nullopt,
                            "id-1") ==
          "root  ·  base model  ·  suite duo (over budget, --force)  ·  1 symphony  ·  chat id-1  "
          "·  /help for commands");
    CHECK(c::session_banner(c::SessionMode::Chat, "root", false, "duo", false, 4, std::nullopt,
                            "id-1") == "root  ·  suite duo  ·  chat id-1  ·  /help for commands");
    CHECK(c::session_banner(c::SessionMode::Chat, "root", false, "", false, 0, std::nullopt,
                            "id-1") == "root  ·  chat id-1  ·  /help for commands");
    CHECK(c::symphony_count(0) == "0 symphonies");
    // Orchestrating (27t): how many the model is offered as tools -- none
    // said, never left out.
    CHECK(c::session_banner(c::SessionMode::Execute, "root", false, "duo", false, 3, 2, "id-1") ==
          "root  ·  suite duo  ·  3 symphonies  ·  orchestrating 2  ·  chat id-1  ·  /help for "
          "commands");
    CHECK(c::session_banner(c::SessionMode::Execute, "root", false, "duo", false, 0, 0, "id-1") ==
          "root  ·  suite duo  ·  0 symphonies  ·  orchestrating none  ·  chat id-1  ·  /help "
          "for commands");
}

// --- the one table and completion -------------------------------------------------------

namespace {

c::ChatCompletionSources execute_sources() {
    c::ChatCompletionSources sources;
    sources.backends = {{"root", "mock"}, {"helper", "mock"}};
    sources.suites = {{"duo", "The root and its helper."}, {"fast", ""}};
    sources.mode = c::SessionMode::Execute;
    sources.symphonies = [] {
        return std::vector<c::NamedChoice>{{"echo2", "Two stages."},
                                           {"summarize-verify", "Summarize, then check."}};
    };
    return sources;
}

std::vector<std::string> texts(const c::Suggestions& suggestions) {
    std::vector<std::string> out;
    for (const auto& candidate : suggestions.candidates) {
        out.push_back(candidate.text);
    }
    return out;
}

}  // namespace

TEST_CASE("execute's table is what its /help, completion and dispatch all read",
          "[execute][completer]") {
    const c::ChatCompletionSources sources = execute_sources();
    const c::Suggestions bare = c::suggest_chat_input("/", sources);
    const std::vector<std::string> help = c::chat_help_lines(0, c::SessionMode::Execute);
    const auto rows = c::chat_commands(c::SessionMode::Execute);
    REQUIRE(bare.candidates.size() == rows.size());
    REQUIRE(help.size() == rows.size());
    for (std::size_t row = 0; row < rows.size(); ++row) {
        const c::ChatCommandSpec& spec = rows[row];
        const std::string name = "/" + std::string{spec.verb};
        INFO(name);
        CHECK(c::find_chat_command(spec.verb, c::SessionMode::Execute) == &spec);
        CHECK(bare.candidates[row].label == name);
        CHECK(help[row].find(std::string{spec.description}) != std::string::npos);
    }
    // Every handler the session core has is reachable in one session or the
    // other, and execute's own only in execute.
    for (int id = 0; id <= static_cast<int>(c::ChatVerb::Play); ++id) {
        bool found = false;
        for (const c::SessionMode mode : {c::SessionMode::Chat, c::SessionMode::Execute}) {
            for (const c::ChatCommandSpec& spec : c::chat_commands(mode)) {
                found = found || static_cast<int>(spec.id) == id;
            }
        }
        INFO("ChatVerb " << id);
        CHECK(found);
    }
    CHECK(c::find_chat_command("play", c::SessionMode::Chat) == nullptr);
    CHECK(c::find_chat_command("symphonies", c::SessionMode::Chat) == nullptr);
    CHECK(c::find_chat_command("play", c::SessionMode::Execute)->id == c::ChatVerb::Play);
    // One /suite per session: chat's turns a suite off, execute's cannot.
    CHECK(c::find_chat_command("suite", c::SessionMode::Chat)->argument == "[name|off]");
    CHECK(c::find_chat_command("suite", c::SessionMode::Execute)->argument == "[name]");
}

TEST_CASE("execute's /help, golden", "[execute][completer]") {
    CHECK(c::chat_help_lines(0, c::SessionMode::Execute) ==
          std::vector<std::string>{
              "  /help                     List these commands",
              "  /model [backend|model]    Show the backend answering, or switch: a backend, a "
              "roster model, or backend:model",
              "  /models                   List the configured backends",
              "  /suite [name]             Show the suite and what its members hold, or switch "
              "to another (then --force, --warm)",
              "  /symphonies               List the symphonies /play can play",
              "  /play <symphony> [input]  Play a symphony on the input: its stages on the "
              "suite's members, its output this session's answer",
              "  /system <text>            Replace the system prompt",
              "  /temperature <number>     Set the sampling temperature",
              "  /think [on|off|auto]      Show or set whether the model thinks first: on, off, "
              "or auto per question",
              "  /max-tokens <number>      Cap each answer's length in tokens",
              "  /compact                  Summarise the conversation so far, to free context",
              "  /title <name>             Rename this chat",
              "  /retriever [mode]         Show or set how documents are searched: auto, "
              "lexical, vector or hybrid",
              "  /rerank [backend]         Show or set the model that reranks search results, "
              "or off, or auto",
              "  /branch [ref]             Show or set the branch under review: head, "
              "base..head, or off",
              "  /capture [status|link]    Save this conversation as a knowledge record",
              "  /check                    Have the suite's verifier check the last answer, once",
              "  /attach <path>            Attach a file, folder or glob: inlined when it fits, "
              "retrieved when not (then --graph=code|off)",
              "  /attachments              List what is attached",
              "  /detach <name>            Detach an attachment",
              "  /recall [on|off]          Show or set whether turns recall earlier chats",
              "  /private                  Never summarise this chat for recall",
              "  /allow [tool|website]     Allow a tool or website for this chat; alone, list "
              "each answer",
              "  /deny <tool|website>      Refuse a tool or website for this chat, without "
              "asking",
              "  /revoke <tool|website>    Forget this chat's answer for a tool or website",
              "  /permissions              List each tool's answer and where it comes from",
              "  /exit                     Save and leave",
              "  /quit                     Save and leave",
          });
}

TEST_CASE("/play completes the symphonies; execute's /suite completes no off",
          "[execute][completer]") {
    const c::ChatCompletionSources sources = execute_sources();
    CHECK(texts(c::suggest_chat_input("/pl", sources)) == std::vector<std::string>{"/play "});
    CHECK(texts(c::suggest_chat_input("/sy", sources)) ==
          std::vector<std::string>{"/symphonies", "/system "});
    CHECK(texts(c::suggest_chat_input("/play ", sources)) ==
          std::vector<std::string>{"echo2", "summarize-verify"});
    CHECK(texts(c::suggest_chat_input("/play su", sources)) ==
          std::vector<std::string>{"summarize-verify"});
    const c::Suggestions described = c::suggest_chat_input("/play e", sources);
    REQUIRE(described.candidates.size() == 1);
    CHECK(described.candidates.front().description == "Two stages.");
    // The input is free text: nothing completes in it.
    CHECK(c::suggest_chat_input("/play echo2 su", sources).candidates.empty());
    CHECK(texts(c::suggest_chat_input("/suite ", sources)) ==
          std::vector<std::string>{"duo", "fast"});
    // Execute's /suite reads the same switches after the name.
    CHECK(texts(c::suggest_chat_input("/suite duo ", sources)) ==
          std::vector<std::string>{"--force", "--warm"});
    CHECK(texts(c::suggest_chat_input("/suite duo --force ", sources)) ==
          std::vector<std::string>{"--warm"});
    // The same sources in chat: no /play, and /suite offers off.
    c::ChatCompletionSources chat = sources;
    chat.mode = c::SessionMode::Chat;
    CHECK(c::suggest_chat_input("/pl", chat).candidates.empty());
    CHECK(texts(c::suggest_chat_input("/suite ", chat)) ==
          std::vector<std::string>{"duo", "fast", "off"});
}

TEST_CASE("what /play completes, it plays", "[execute][completer][play]") {
    // ADR 0007's offer-is-a-contract, both ways: every name offered /play
    // prepares with no refusal at all, and every one left out it refuses
    // whatever is typed -- a starter whose input takes an image among them.
    Members members;
    const apogee::symphony::Catalog catalog =
        c::session_catalog(members.harness->config(), members.config_path);
    const std::vector<c::NamedChoice> offered = c::symphony_choices(catalog);
    REQUIRE(offered.size() >= 3);
    const auto is_offered = [&offered](const std::string& name) {
        return std::ranges::any_of(
            offered, [&name](const c::NamedChoice& choice) { return choice.name == name; });
    };
    CHECK(is_offered("echo2"));
    CHECK(is_offered("summarize-verify"));
    CHECK_FALSE(is_offered("describe-answer"));
    for (const apogee::symphony::Definition& definition : catalog.definitions) {
        const std::string& name = definition.spec.name;
        INFO(name);
        const c::PreparedPlay prepared =
            c::prepare_play(members.harness->config(), members.config_path,
                            c::parse_play_argument(name + " some input"));
        CHECK(prepared.definition.spec.name == name);
        CHECK(prepared.refusal.empty() == is_offered(name));
    }
    const c::PreparedPlay image = c::prepare_play(members.harness->config(), members.config_path,
                                                  c::parse_play_argument("describe-answer x"));
    CHECK(image.refusal.find("takes an image") != std::string::npos);
}

// --- the whole surface, pinned ------------------------------------------------------------

namespace {

/// What a run printed, with the home's path and a chat's id named.
std::string normalized(const std::string& text, const Home& home) {
    std::string out = replaced(text, home.home.home().generic_string(), "<home>");
    out = replaced(out, home.home.home().string(), "<home>");
    static const std::regex kChatId{"[0-9]{8}-[0-9]{6}-[0-9a-f]{4}"};
    return std::regex_replace(out, kChatId, "<chat-id>");
}

std::string scenario(const Home& home, const std::vector<std::string>& args,
                     const std::string& input = {}) {
    std::string out;
    std::string err;
    const int code = home.run(args, &out, &err, input);
    std::string said = "=== apogee";
    for (const std::string& arg : args) {
        said += " " + (arg.empty() ? std::string{"\"\""} : arg);
    }
    return said + "\n--- stdin\n" + input + "--- exit " + std::to_string(code) + "\n--- stdout\n" +
           normalized(out, home) + "--- stderr\n" + normalized(err, home);
}

std::string battery() {
    std::string out;
    {
        const Home home;
        out += scenario(home, {"execute"}, "hi\n");
        out += scenario(home, {"execute", "--suite", "off"}, "hi\n");
        out += scenario(home, {"execute", "--suite", "duo"},
                        "/help\n/symphonies\nhello\n/play echo2 hello\n/play\n/play nope x\n"
                        "/play echo2\n/suite\n/suite off\n/suite fast\n/play echo2 again\n"
                        "/model\n/exit\n");
        out += scenario(home, {"execute", "-c"}, "/suite\nafter\n");
    }
    {
        const Home home{"  default_suite: duo\n"};
        out += scenario(home, {"execute", "--output-format", "stream-json"},
                        "{\"type\":\"user\",\"text\":\"/play echo2 hello\"}\n"
                        "{\"type\":\"user\",\"text\":\"what was that?\"}\n"
                        "{\"type\":\"user\",\"text\":\"/play echo2\"}\n"
                        "{\"type\":\"user\",\"text\":\"/symphonies\"}\n");
    }
    {
        const Home home;
        out += scenario(home, {"execute", "--help"});
        out += scenario(home, {"__complete", "execute", "--"});
        out += scenario(home, {"__complete", "execute", "--suite", ""});
        out += scenario(home, {"__complete", "ex"});
    }
    return out;
}

std::optional<std::string> slurp(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in.good()) {
        return std::nullopt;
    }
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

}  // namespace

// --- the Orchestrator (27t) ---------------------------------------------------------

namespace {

/// A root that answers with the tools it was offered.
constexpr std::string_view kToolNames = R"({"turns": [{"text": "TOOLS<{{tool_names}}>"}]})";

/// `home`'s config, with a suite `orch` -- duo, orchestrating -- and its root's
/// script replaced by `root`.
void orchestral(const Home& home, std::string_view root) {
    std::string config = config_with(home.home.home() / "scripts", "");
    config = replaced(config, "symphonies:\n",
                      "  orch:\n    members:\n      chat: root\n      utility: helper\n"
                      "    consultable: [utility]\n    orchestrate: true\nsymphonies:\n");
    std::ofstream{home.home.config_path(), std::ios::binary | std::ios::trunc} << config;
    std::ofstream{home.home.home() / "scripts" / "root.json", std::ios::binary | std::ios::trunc}
        << root;
}

}  // namespace

TEST_CASE("without orchestration a session registers no symphony tool, as 27s shipped it",
          "[execute][orchestrate]") {
    const Home home;
    orchestral(home, kToolNames);
    std::string out;
    std::string err;
    REQUIRE(home.run({"execute", "--suite", "duo"}, &out, &err, "hi\n") == 0);
    CHECK(out == "TOOLS<>\n");
    REQUIRE(home.run({"execute", "--suite", "duo", "--tools"}, &out, &err, "hi\n") == 0);
    CHECK(out.starts_with("TOOLS<"));
    CHECK(out.find("play_") == std::string::npos);
    CHECK(err.find("orchestrat") == std::string::npos);
    // The suite's own switch is execute's: a chat under it is a chat.
    REQUIRE(home.run({"chat", "--suite", "orch", "--tools"}, &out, &err, "hi\n") == 0);
    CHECK(out.find("play_") == std::string::npos);
    REQUIRE(home.run({"chat", "--suite", "orch"}, &out, &err, "hi\n") == 0);
    CHECK(out == "TOOLS<>\n");
    // `/suite` moves the offer with the suite: on under orch, off again under duo.
    REQUIRE(home.run({"execute", "--suite", "duo"}, &out, &err,
                     "hi\n/suite orch\nhi\n/suite duo\nhi\n") == 0);
    CHECK(out == "TOOLS<>\nTOOLS<play_echo2,play_extract-facts,play_summarize-verify>\nTOOLS<>\n");
    // And the flag is execute's alone.
    CHECK(home.run({"chat", "--suite", "duo", "--orchestrate"}, &out, &err, "hi\n") != 0);
    CHECK(err.find("--orchestrate") != std::string::npos);
}

TEST_CASE("orchestrating, the root is offered each symphony it can play, framed as orchestrator",
          "[execute][orchestrate]") {
    const Home home;
    orchestral(home, kToolNames);
    std::string out;
    std::string err;
    // The flag, with no --tools: orchestration is its own consent. The vision
    // starter takes an image, so it is not offered -- said under --verbose.
    REQUIRE(home.run({"execute", "--suite", "duo", "--orchestrate"}, &out, &err, "hi\n") == 0);
    CHECK(out == "TOOLS<play_echo2,play_extract-facts,play_summarize-verify>\n");
    CHECK(err.find("orchestrate:") == std::string::npos);
    REQUIRE(home.run({"execute", "--suite", "duo", "--orchestrate", "--verbose"}, &out, &err,
                     "hi\n") == 0);
    CHECK(err.find("orchestrate: not offering describe-answer -- it takes an image, which a tool "
                   "call cannot give") != std::string::npos);
    // The suite's own `orchestrate: true`: no consult without --tools, the
    // consent every other tool asks for.
    REQUIRE(home.run({"execute", "--suite", "orch"}, &out, &err, "hi\n") == 0);
    CHECK(out == "TOOLS<play_echo2,play_extract-facts,play_summarize-verify>\n");
    // With the native tools and consult beside.
    REQUIRE(home.run({"execute", "--suite", "orch", "--tools"}, &out, &err, "hi\n") == 0);
    CHECK(out.find("play_echo2") != std::string::npos);
    CHECK(out.find("play_summarize-verify") != std::string::npos);
    CHECK(out.find("read_file") != std::string::npos);
    CHECK(out.find("consult") != std::string::npos);
    // The environment note frames the root as the orchestrator.
    std::ofstream{home.home.home() / "scripts" / "root.json", std::ios::binary | std::ios::trunc}
        << R"({"turns": [{"text": "SYS<{{system}}>"}]})";
    REQUIRE(home.run({"execute", "--suite", "orch"}, &out, &err, "hi\n") == 0);
    CHECK(out == "SYS<" + std::string{apogee::symphony::orchestrator_framing()} + ">\n");
}

TEST_CASE("a suite whose symphonies all reach a billed member offers none, and says so",
          "[execute][orchestrate]") {
    const Home home;
    // The root bills per call, and every symphony reaches it.
    orchestral(home, R"({"metered": true, "turns": [{"text": "TOOLS<{{tool_names}}>"}]})");
    std::string out;
    std::string err;
    REQUIRE(home.run({"execute", "--suite", "duo", "--orchestrate"}, &out, &err, "hi\n") == 0);
    CHECK(out == "TOOLS<>\n");
    CHECK(err.find("orchestrate: not offering echo2 -- its second stage (chat) is 'root', billed "
                   "per call -- a play the model starts runs on its initiative, which never "
                   "spends") != std::string::npos);
    CHECK(err.find("orchestrate: no symphony can be offered as a tool -- the model answers "
                   "without plays") != std::string::npos);
}

TEST_CASE("the root plays a symphony unprompted: its stages run, its output is a tool result",
          "[execute][orchestrate]") {
    const Home home;
    orchestral(home,
               R"({"turns": [{"text": "", "tool_calls": [{"name": "play_echo2", "arguments": )"
               R"("{\"input\": \"hello\"}"}]}, {"text": "ROOT<{{last_user}}>"}, )"
               R"({"text": "ANSWER<{{last_tool_result}}>"}]})");
    std::string out;
    std::string err;
    REQUIRE(home.run({"execute", "--suite", "duo", "--orchestrate"}, &out, &err, "go\n") == 0);
    const std::string result = "echo2 answered:\n" + std::string{kPlayed};
    CHECK(out == "ANSWER<" + result + ">\n");
    // An ordinary tool call and result in the history -- no new shape.
    const std::vector<apogee::logger::Session> sessions = home.sessions();
    REQUIRE(sessions.size() == 1);
    const std::vector<apogee::harness::ChatMessage>& messages = sessions.front().messages;
    REQUIRE(messages.size() == 4);
    REQUIRE(messages[1].tool_calls.size() == 1);
    CHECK(messages[1].tool_calls.front().name == "play_echo2");
    CHECK(messages[2].role == apogee::harness::Role::Tool);
    CHECK(messages[2].content.plain_text() == result);
}

TEST_CASE("a driver sees the model's play as ordinary tool-call narration, no new event type",
          "[execute][orchestrate][machine]") {
    const Home home;
    orchestral(home,
               R"({"turns": [{"text": "", "tool_calls": [{"name": "play_echo2", "arguments": )"
               R"("{\"input\": \"hello\"}"}]}, {"text": "ROOT<{{last_user}}>"}, )"
               R"({"text": "DONE"}]})");
    std::string out;
    std::string err;
    REQUIRE(home.run({"execute", "--suite", "orch", "--output-format", "stream-json"}, &out, &err,
                     "{\"type\":\"user\",\"text\":\"go\"}\n") == 0);
    const std::set<std::string> known{"session",      "thinking",   "tool_status", "answer_start",
                                      "answer_delta", "answer_end", "result"};
    std::vector<std::string> statuses;
    std::istringstream lines{out};
    for (std::string line; std::getline(lines, line);) {
        const nlohmann::json event = nlohmann::json::parse(line);
        CHECK(known.contains(event.at("type").get<std::string>()));
        if (event.at("type") == "tool_status") {
            statuses.push_back(event.at("text").get<std::string>());
        }
    }
    // The call, the labeled line -- the model's choice and its cost -- and a
    // line for each stage.
    REQUIRE(statuses.size() == 4);
    CHECK(statuses[0] == "[tool] play_echo2");
    CHECK(statuses[1] == "play — the model chose echo2: 2 member calls, utility → chat");
    CHECK_THAT(statuses[2], Catch::Matchers::StartsWith(
                                "stage 1/2 first — asking utility (helper): One: hello"));
    CHECK_THAT(statuses[3], Catch::Matchers::StartsWith("stage 2/2 second — asking chat (root)"));
    CHECK(event_types(out).back() == "result");
}

TEST_CASE("execute's whole surface, golden", "[execute][golden]") {
    const std::string actual = battery();
    // Written for review when asked to; never into the tree.
    if (const char* dir = std::getenv("APOGEE_CHAT_SESSION_GOLDENS");
        dir != nullptr && *dir != '\0') {
        std::ofstream{std::filesystem::path{dir} / "execute_session.golden", std::ios::binary}
            << actual;
    }
    const std::optional<std::string> golden =
        slurp(std::filesystem::path{APOGEE_TEST_FIXTURES} / "cli" / "execute_session.golden");
    REQUIRE(golden.has_value());
    CHECK(actual == *golden);
}
