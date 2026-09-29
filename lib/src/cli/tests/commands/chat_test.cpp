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
    // A small utility model answered the question it was shown, and the
    // invented answer became the title.
    CHECK(prompt.find("do not answer them") != std::string::npos);
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

// ---------------------------------------------------------------------------
// The utility model does the chat's chores (26b)
// ---------------------------------------------------------------------------

namespace {

/// A chat on two scripted models, `chatty` and the utility model `helper`,
/// each answering from its own script in order -- so which one was asked is
/// read straight off what the chat ends up with.
struct HelperChat {
    apogee::testing::TempDir home{"chat-helper-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    std::filesystem::path config_path = home.path() / "config" / "config.yaml";
    std::string out;
    std::string err;

    /// `chatty_extra` is YAML under the chat backend's entry; `extra` goes at
    /// the top level.
    HelperChat(const nlohmann::json& chatty_turns, const std::vector<std::string>& helper_replies,
               const std::string& chatty_extra = {}, const std::string& extra = {}) {
        std::filesystem::create_directories(config_path.parent_path());
        nlohmann::json helper_turns = nlohmann::json::array();
        for (const std::string& reply : helper_replies) {
            helper_turns.push_back({{"text", reply}});
        }
        std::ofstream{config_path, std::ios::binary}
            << "models:\n  default: chatty\n  default_utility: helper\nbackends:\n"
            << "  chatty:\n    type: mock\n    model_path: " << script("chatty", chatty_turns)
            << "\n"
            << chatty_extra
            << "  helper:\n    type: mock\n    model_path: " << script("helper", helper_turns)
            << "\n"
            << extra;
    }

    [[nodiscard]] std::string script(const std::string& name, const nlohmann::json& turns) const {
        const std::filesystem::path path = home.path() / (name + ".json");
        std::ofstream{path, std::ios::binary} << nlohmann::json{{"turns", turns}}.dump();
        return path.string();
    }

    /// Runs `apogee <args>` in process with `input` on stdin.
    int run(const std::vector<std::string>& args, const std::string& input = {}) {
        std::ostringstream captured_out;
        std::ostringstream captured_err;
        std::istringstream fed{input};
        std::streambuf* old_out = std::cout.rdbuf(captured_out.rdbuf());
        std::streambuf* old_err = std::cerr.rdbuf(captured_err.rdbuf());
        std::streambuf* old_in = std::cin.rdbuf(fed.rdbuf());
        int code = -1;
        {
            apogee::commands::RootCommand root{apogee::commands::default_registry()};
            const std::string path = config_path.string();
            std::vector<const char*> argv{"apogee", "--config", path.c_str()};
            for (const std::string& arg : args) {
                argv.push_back(arg.c_str());
            }
            code = root.run(static_cast<int>(argv.size()), argv.data());
        }
        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);
        std::cin.rdbuf(old_in);
        std::cin.clear();
        out = captured_out.str();
        err = captured_err.str();
        return code;
    }

    [[nodiscard]] static apogee::logger::Session only_session() {
        const std::vector<apogee::logger::Session> sessions = apogee::logger::list_sessions();
        REQUIRE(sessions.size() == 1);
        return sessions.front();
    }
};

[[nodiscard]] nlohmann::json texts(const std::vector<std::string>& replies) {
    nlohmann::json turns = nlohmann::json::array();
    for (const std::string& reply : replies) {
        turns.push_back({{"text", reply}});
    }
    return turns;
}

}  // namespace

TEST_CASE("a utility model titles and compacts the chat, and the chat's answers stay its own",
          "[chat][title][cli][helpers]") {
    HelperChat chat{texts({"first answer", "second answer"}),
                    {"Helper Title", "what was said, in short"}};
    INFO(chat.err);
    REQUIRE(chat.run({"chat", "--verbose"}, "first question\nsecond question\n/compact\n") == 0);

    const apogee::logger::Session session = HelperChat::only_session();
    CHECK(session.title == "Helper Title");
    CHECK(session.compactions == 1);
    // The summary is the helper's; the one answer kept is the chat's.
    bool summarised = false;
    for (const ChatMessage& message : session.messages) {
        if (message.role == apogee::harness::Role::System &&
            message.content.plain_text().find("what was said, in short") != std::string::npos) {
            summarised = true;
        }
    }
    CHECK(summarised);
    CHECK(replies(session) == std::vector<std::string>{"second answer"});
    // Said under --verbose, naming the model that did it.
    CHECK(chat.err.find("titled by helper: Helper Title") != std::string::npos);
    CHECK(chat.err.find("history compacted by helper") != std::string::npos);
}

TEST_CASE("a utility model restates a follow-up for retrieval; the chat is asked it as written",
          "[chat][rag][cli][helpers]") {
    HelperChat chat{texts({"Heron is the billing ledger.", "It deploys to Frankfurt."}),
                    {"Heron Notes", "Project Heron deployment region"}};
    const std::filesystem::path docs = chat.home.path() / "docs";
    std::filesystem::create_directories(docs);
    std::ofstream{docs / "heron.md", std::ios::binary}
        << "Project Heron is the billing ledger. It deploys to Frankfurt.\n";
    std::ofstream{docs / "kestrel.md", std::ios::binary}
        << "Project Kestrel is the push service. It deploys to Sydney.\n";
    INFO(chat.err);
    REQUIRE(chat.run({"embed", "ingest", "notes", docs.string()}) == 0);
    REQUIRE(chat.run({"chat", "--verbose", "--rag", "notes"},
                     "Tell me about Project Heron.\nwhere does it deploy?\n") == 0);

    // The first question stands alone and is searched as asked; the follow-up
    // is restated by the helper before the search.
    CHECK(chat.err.find("search query by helper: Project Heron deployment region") !=
          std::string::npos);
    const apogee::logger::Session session = HelperChat::only_session();
    CHECK(session.title == "Heron Notes");
    CHECK(replies(session) ==
          std::vector<std::string>{"Heron is the billing ledger.", "It deploys to Frankfurt."});
    // What the chat model was asked is the user's own words.
    std::vector<std::string> asked;
    for (const ChatMessage& message : session.messages) {
        if (message.role == apogee::harness::Role::User) {
            asked.push_back(message.content.plain_text());
        }
    }
    CHECK(asked ==
          std::vector<std::string>{"Tell me about Project Heron.", "where does it deploy?"});
}

TEST_CASE("a utility model summarises a large tool result, and compacts a full context",
          "[chat][cli][helpers]") {
    // The chat model reads a 12 KB file, then answers at length; the second
    // question finds the 300-token window over 90% full.
    const std::string long_answer(1200, 'a');
    const nlohmann::json chatty = nlohmann::json::array(
        {{{"tool_calls", {{{"name", "read_file"}, {"arguments", {{"path", "big.log"}}}}}}},
         {{"text", long_answer}},
         {{"text", "second answer"}}});
    HelperChat chat{chatty,
                    {"the log, in short", "Log Review", "what was said, in short"},
                    "    context_size: 300\n",
                    ""};
    const std::filesystem::path work = chat.home.path() / "work";
    std::filesystem::create_directories(work);
    {
        std::ofstream log{work / "big.log", std::ios::binary};
        for (int line = 0; line < 400; ++line) {
            log << "INFO worker processed a batch\n";
        }
    }
    {
        std::ofstream{chat.config_path, std::ios::binary | std::ios::app}
            << "tools:\n  fs_root: " << work.string() << "\n";
    }
    INFO(chat.err);
    REQUIRE(chat.run({"chat", "--verbose", "--tools"}, "what is in big.log?\nand then?\n") == 0);
    INFO(chat.err);
    CHECK(chat.err.find("read_file's 12 KB result summarised by helper") != std::string::npos);
    CHECK(chat.err.find("compacting with helper") != std::string::npos);
    const apogee::logger::Session session = HelperChat::only_session();
    CHECK(session.title == "Log Review");
    CHECK(session.compactions == 1);
    bool summarised = false;
    for (const ChatMessage& message : session.messages) {
        if (message.content.plain_text().find("what was said, in short") != std::string::npos) {
            summarised = true;
        }
    }
    CHECK(summarised);
    CHECK(replies(session).back() == "second answer");
}
