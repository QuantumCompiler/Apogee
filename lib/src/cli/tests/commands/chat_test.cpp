#include "commands/chat.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "backends/mock.h"
#include "commands/chat_history.h"
#include "commands/registry.h"
#include "commands/root.h"
#include "harness/config.h"
#include "logger/operational.h"
#include "logger/session.h"
#include "support/env_guard.h"

using apogee::commands::format_session_info;
using apogee::commands::format_session_row;
using apogee::commands::parse_slash;
using apogee::commands::sanitize_title;
using apogee::harness::ChatMessage;
using apogee::harness::Config;
using apogee::harness::Harness;

// ---------------------------------------------------------------------------
// Slash parsing
// ---------------------------------------------------------------------------

TEST_CASE("slash commands parse into a verb and an argument", "[chat][slash]") {
    const auto help = parse_slash("/help");
    REQUIRE(help.has_value());
    CHECK(help->name == "help");
    CHECK(help->argument.empty());

    const auto model = parse_slash("/model claude-opus");
    REQUIRE(model.has_value());
    CHECK(model->name == "model");
    CHECK(model->argument == "claude-opus");

    // Everything after the first space, including further spaces.
    const auto system = parse_slash("/system  You are terse.  ");
    REQUIRE(system.has_value());
    CHECK(system->argument == "You are terse.");
}

TEST_CASE("a path is not a slash command", "[chat][slash]") {
    // `/usr/bin/env is a path` is a perfectly reasonable question, and treating
    // it as a command would swallow it.
    CHECK_FALSE(parse_slash("/usr/bin/env is a path").has_value());
    CHECK_FALSE(parse_slash("/etc/hosts").has_value());
}

TEST_CASE("ordinary prompts are not commands", "[chat][slash]") {
    CHECK_FALSE(parse_slash("what is 2+2?").has_value());
    CHECK_FALSE(parse_slash("").has_value());
    CHECK_FALSE(parse_slash("/").has_value());
    // A leading slash mid-sentence is still a prompt.
    CHECK_FALSE(parse_slash("tell me about /proc").has_value());
}

// ---------------------------------------------------------------------------
// Titles and listings
// ---------------------------------------------------------------------------

TEST_CASE("a title is cleaned to one bounded line", "[chat][title]") {
    // A model asked for a title will sometimes answer with a sentence, a quoted
    // phrase, or a paragraph explaining its choice.
    CHECK(sanitize_title("Refactoring the parser") == "Refactoring the parser");
    CHECK(sanitize_title("\"Quoted Title\"") == "Quoted Title");
    CHECK(sanitize_title("Title with a period.") == "Title with a period");
    CHECK(sanitize_title("First line\nAn explanation underneath") == "First line");
    CHECK(sanitize_title("   padded   ") == "padded");
}

TEST_CASE("a runaway title is truncated without breaking a codepoint", "[chat][title]") {
    // Otherwise one long answer makes every listing row wrap.
    const std::string title = sanitize_title(std::string(200, 'x'));
    CHECK(title.size() <= 64);
    CHECK(title.back() != 'x');  // ends with the ellipsis

    const std::string unicode = sanitize_title(std::string(100, 'a') + "héllo wörld");
    // Valid UTF-8 throughout: no truncation mid-sequence.
    for (std::size_t i = 0; i < unicode.size();) {
        const auto lead = static_cast<unsigned char>(unicode[i]);
        std::size_t length = 1;
        if ((lead & 0xE0U) == 0xC0U) {
            length = 2;
        } else if ((lead & 0xF0U) == 0xE0U) {
            length = 3;
        } else if ((lead & 0xF8U) == 0xF0U) {
            length = 4;
        }
        REQUIRE(i + length <= unicode.size());
        i += length;
    }
}

TEST_CASE("a title is the first line with anything on it", "[chat][title]") {
    // A reasoning model's answer begins with the blank lines its closed think
    // block leaves. Taking the first line outright gave "" every time -- and
    // an empty title was asked for again after every turn.
    CHECK(sanitize_title("\n\nRefactoring the parser") == "Refactoring the parser");
    CHECK(sanitize_title("  \r\n\t\nA title\nwhy I chose it") == "A title");
    CHECK(sanitize_title("**Bold Title**") == "Bold Title");
    CHECK(sanitize_title("# Heading Title") == "Heading Title");
    CHECK(sanitize_title("\n\n\n").empty());
}

TEST_CASE("a truncated title never keeps half a codepoint", "[chat][title]") {
    // The cut backed up over continuation bytes and kept the lead byte they
    // belonged to: a title ending in a stray 0xC3 or 0xE2.
    CHECK(sanitize_title(std::string(59, 'a') + "\xC3\xA9\xC3\xA9") ==
          std::string(59, 'a') + "\xE2\x80\xA6");
    CHECK(sanitize_title(std::string(58, 'a') + "\xE2\x82\xAC\xE2\x82\xAC") ==
          std::string(58, 'a') + "\xE2\x80\xA6");
}

TEST_CASE("the title request carries the questions, not the answers", "[chat][title]") {
    // The answers are most of a conversation's length and none of what it is
    // about; sending them made a title cost a full re-read.
    apogee::logger::Session session;
    session.backend = "local";
    session.messages = {ChatMessage::system("be terse"), ChatMessage::user("How do I parse YAML?"),
                        ChatMessage::assistant(std::string(5000, 'x')),
                        ChatMessage::user("And JSON?")};
    const apogee::harness::ChatRequest request = apogee::commands::title_request(session);
    CHECK(request.model == "local");
    REQUIRE(request.messages.size() == 1);
    const std::string text = request.messages.front().content.plain_text();
    CHECK(text.find(apogee::commands::title_prompt()) != std::string::npos);
    CHECK(text.find("How do I parse YAML?") != std::string::npos);
    CHECK(text.find("And JSON?") != std::string::npos);
    CHECK(text.find("xxxx") == std::string::npos);
    CHECK(text.find("be terse") == std::string::npos);
    // Cheap to run: no history of its own, no reasoning first, a small cap.
    CHECK(request.transient.side_request);
    CHECK(request.transient.skip_reasoning);
    REQUIRE(request.max_tokens.has_value());
    CHECK(*request.max_tokens <= 64);

    // However long the conversation, the request stays small.
    session.messages.clear();
    for (int i = 0; i < 100; ++i) {
        session.messages.push_back(ChatMessage::user(std::string(1000, 'q')));
    }
    CHECK(apogee::commands::title_request(session).messages.front().content.plain_text().size() <
          2000);
}

TEST_CASE("the title prompt asks for a title and nothing else", "[chat][title]") {
    const std::string prompt = apogee::commands::title_prompt();
    CHECK(prompt.find("title") != std::string::npos);
    CHECK(prompt.find("only") != std::string::npos);
}

TEST_CASE("listing rows and info carry what identifies a session", "[chat][history]") {
    apogee::logger::Session session;
    session.chat_id = "20260826-120000-abcd";
    session.custom_name = "the refactor";
    session.backend = "claude";
    session.updated_at = "2026-08-26T12:05:00Z";
    session.turns = 7;
    session.messages = {ChatMessage::user("x")};

    const std::string row = format_session_row(session);
    CHECK(row.find("20260826-120000-abcd") != std::string::npos);
    CHECK(row.find("the refactor") != std::string::npos);
    CHECK(row.find("7 turns") != std::string::npos);

    const std::string info = format_session_info(session);
    CHECK(info.find("claude") != std::string::npos);
    CHECK(info.find("2026-08-26T12:05:00Z") != std::string::npos);
}

TEST_CASE("a legacy session says so in its info", "[chat][history]") {
    apogee::logger::Session session;
    session.chat_id = "old";
    session.schema_version = 0;
    CHECK(format_session_info(session).find("legacy") != std::string::npos);
}

// ---------------------------------------------------------------------------
// The operational log
// ---------------------------------------------------------------------------

TEST_CASE("an operational log line is one greppable row", "[chat][log]") {
    // One event per line is the only thing anyone ever does with this file.
    const std::string line = apogee::logger::format_line(
        apogee::logger::Level::Warn, "chat", "something happened", "2026-08-26T12:00:00Z");

    CHECK(line.find("WARN") != std::string::npos);
    CHECK(line.find("[chat]") != std::string::npos);
    CHECK(line.find("something happened") != std::string::npos);
    CHECK(line.back() == '\n');
    CHECK(std::count(line.begin(), line.end(), '\n') == 1);
}

TEST_CASE("an embedded newline cannot break the one-line contract", "[chat][log]") {
    const std::string line = apogee::logger::format_line(apogee::logger::Level::Error, "c",
                                                         "line one\nline two\r\nthree", "t");
    CHECK(std::count(line.begin(), line.end(), '\n') == 1);
    CHECK(line.find("line one line two") != std::string::npos);
}

TEST_CASE("logging never throws, even with nowhere to write", "[chat][log]") {
    // A full disk degrades to a missing log line, never to a failed
    // conversation.
    const apogee::testing::EnvGuard home{"APOGEE_HOME", "/proc/nonexistent/cannot/write"};
    CHECK_NOTHROW(apogee::logger::log(apogee::logger::Level::Info, "test", "message"));
}

// ---------------------------------------------------------------------------
// Auto-titling, through the real command
// ---------------------------------------------------------------------------

namespace {

/// `apogee chat` in process on a scripted mock, fed `input` as its stdin.
struct ScriptedChat {
    apogee::testing::TempDir home{"chat-title-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    std::filesystem::path config_path = home.path() / "config" / "config.yaml";

    explicit ScriptedChat(const std::vector<std::string>& replies) {
        nlohmann::json turns = nlohmann::json::array();
        for (const std::string& reply : replies) {
            turns.push_back({{"text", reply}});
        }
        const std::filesystem::path script = home.path() / "script.json";
        std::filesystem::create_directories(config_path.parent_path());
        std::ofstream{script, std::ios::binary} << nlohmann::json{{"turns", turns}}.dump();
        std::ofstream{config_path, std::ios::binary}
            << "models:\n  default: scripted\nbackends:\n  scripted:\n    type: mock\n"
               "    model_path: "
            << script.string() << "\n";
    }

    [[nodiscard]] apogee::logger::Session run(std::string_view input) const {
        std::ostringstream out;
        std::ostringstream err;
        std::istringstream fed{std::string{input}};
        std::streambuf* old_out = std::cout.rdbuf(out.rdbuf());
        std::streambuf* old_err = std::cerr.rdbuf(err.rdbuf());
        std::streambuf* old_in = std::cin.rdbuf(fed.rdbuf());
        int code = -1;
        {
            apogee::commands::RootCommand root{apogee::commands::default_registry()};
            const std::string path = config_path.string();
            std::vector<const char*> argv{"apogee", "--config", path.c_str(), "chat"};
            code = root.run(static_cast<int>(argv.size()), argv.data());
        }
        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);
        std::cin.rdbuf(old_in);
        std::cin.clear();
        INFO(err.str());
        REQUIRE(code == 0);
        const std::vector<apogee::logger::Session> sessions = apogee::logger::list_sessions();
        REQUIRE(sessions.size() == 1);
        return sessions.front();
    }
};

/// The assistant's replies in `session`, in order.
[[nodiscard]] std::vector<std::string> replies(const apogee::logger::Session& session) {
    std::vector<std::string> out;
    for (const ChatMessage& message : session.messages) {
        if (message.role == apogee::harness::Role::Assistant) {
            out.push_back(message.content.plain_text());
        }
    }
    return out;
}

}  // namespace

TEST_CASE("the first exchange titles the conversation, between turns", "[chat][title][cli]") {
    // The title runs in the background while the next question is typed, and
    // settles before that question reaches the model: the mock answers in
    // script order, so the second reply being the second answer proves the
    // title took exactly its one turn, in between. The leading blank lines are
    // what a reasoning model's closed think block leaves.
    const ScriptedChat chat{{"first answer", "\n\n**Parsing Config Files**", "second answer"}};
    const apogee::logger::Session session = chat.run("first question\nsecond question\n");
    CHECK(session.title == "Parsing Config Files");
    CHECK(replies(session) == std::vector<std::string>{"first answer", "second answer"});
}

TEST_CASE("a title that comes back empty is not asked for again", "[chat][title][cli]") {
    // It was asked for after EVERY turn, each time re-reading the whole
    // conversation: the wait before the next prompt grew with every answer.
    // Were it asked again after turn two, it would take "third answer" -- and
    // title the conversation with it.
    const ScriptedChat chat{{"first answer", "\n\n", "second answer", "third answer"}};
    const apogee::logger::Session session =
        chat.run("first question\nsecond question\nthird question\n");
    CHECK(session.title.empty());
    CHECK(replies(session) ==
          std::vector<std::string>{"first answer", "second answer", "third answer"});
}
