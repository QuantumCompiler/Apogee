#include "cli/chat.h"

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

#include "agentloop/recall.h"
#include "backends/mock.h"
#include "cli/chat_attachments.h"
#include "cli/chat_history.h"
#include "cli/chat_recall.h"
#include "cli/registry.h"
#include "cli/root.h"
#include "contracts/config.h"
#include "contracts/layout.h"
#include "embedstore/store.h"
#include "logger/operational.h"
#include "logger/session.h"
#include "support/env_guard.h"

using apogee::commands::ChatAttachments;
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
    CHECK(request.thinking.off());
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
    // The rewrite was a side call (26n): on a pipe it is drawn nowhere, and
    // no message of the chat carries it.
    CHECK(chat.err.find("rewriting the follow-up") == std::string::npos);
    CHECK(chat.out.find("rewriting the follow-up") == std::string::npos);
    for (const ChatMessage& message : session.messages) {
        CHECK(message.content.plain_text().find("rewriting the follow-up") == std::string::npos);
    }
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

TEST_CASE("a chat with many tools offers each turn the ones its question needs",
          "[chat][cli][tool_selection]") {
    // 26g: the native toolsets are past the threshold, so the chat holds a
    // selection, and --verbose says what each turn offered.
    const nlohmann::json chatty = nlohmann::json::array({{{"text", "it is a log"}}});
    HelperChat chat{chatty, {"Log Notes"}};
    REQUIRE(chat.run({"chat", "--verbose", "--tools"}, "what is in big.log?\n") == 0);
    INFO(chat.err);
    CHECK(chat.err.find("registered: each turn offers the ones its question needs, ranked by") !=
          std::string::npos);
    CHECK(chat.err.find("offered, and find_tools, ranked by") != std::string::npos);
}

TEST_CASE("a follow-up's tools are ranked for it restated by the utility model",
          "[chat][cli][tool_selection]") {
    // No retrieval runs, so the restatement is asked for the tools alone --
    // and only of a utility model the config names.
    const nlohmann::json chatty =
        nlohmann::json::array({{{"text", "first"}}, {{"text", "second"}}});
    HelperChat chat{chatty, {"Notes Chat", "show the commit history"}};
    REQUIRE(chat.run({"chat", "--verbose", "--tools"}, "what is in notes.txt?\nand then?\n") == 0);
    INFO(chat.err);
    CHECK(chat.err.find("tools ranked for, by helper: show the commit history") !=
          std::string::npos);
    // And the turn's tools are ranked for that restatement.
    CHECK(chat.err.find("for this question: git_") != std::string::npos);
}

TEST_CASE("complete with many tools offers the ones its question needs",
          "[chat][cli][tool_selection]") {
    const nlohmann::json chatty = nlohmann::json::array({{{"text", "done"}}});
    HelperChat chat{chatty, {}};
    REQUIRE(chat.run({"complete", "--verbose", "--tools", "show the commit history"}) == 0);
    INFO(chat.err);
    CHECK(chat.err.find("registered: the turn offers the ones its question needs") !=
          std::string::npos);
    CHECK(chat.err.find("offered, and find_tools, ranked by") != std::string::npos);
}

TEST_CASE("the next turn sends an earlier turn's tool result as a stub, and says so",
          "[chat][cli][budget]") {
    // 6 KB: under the utility model's summary threshold, so read whole.
    const nlohmann::json chatty = nlohmann::json::array(
        {{{"tool_calls", {{{"name", "read_file"}, {"arguments", {{"path", "notes.log"}}}}}}},
         {{"text", "it is a log"}},
         {{"text", "second answer"}}});
    HelperChat chat{chatty, {"Log Notes"}};
    const std::filesystem::path work = chat.home.path() / "work";
    std::filesystem::create_directories(work);
    {
        std::ofstream log{work / "notes.log", std::ios::binary};
        for (int line = 0; line < 200; ++line) {
            log << "INFO worker processed a batch\n";
        }
    }
    {
        std::ofstream{chat.config_path, std::ios::binary | std::ios::app}
            << "tools:\n  fs_root: " << work.string() << "\n";
    }
    REQUIRE(chat.run({"chat", "--verbose", "--tools"}, "what is in notes.log?\nand then?\n") == 0);
    INFO(chat.err);
    CHECK(chat.err.find("earlier turns' tool results sent as 1 stub") != std::string::npos);
    // The transcript keeps the result whole.
    const apogee::logger::Session session = HelperChat::only_session();
    bool whole = false;
    for (const ChatMessage& message : session.messages) {
        if (message.role == apogee::harness::Role::Tool &&
            message.content.plain_text().find("INFO worker processed a batch") !=
                std::string::npos) {
            whole = true;
        }
    }
    CHECK(whole);
    CHECK(replies(session).back() == "second answer");
}

TEST_CASE("chat and complete fit retrieval to the model's window, and say what they left out",
          "[chat][rag][cli][budget]") {
    // A 400-token window: its retrieval share has no room for an excerpt.
    HelperChat chat{
        texts({"a chat answer", "a complete answer"}), {"Budget Notes"}, "    context_size: 400\n"};
    const std::filesystem::path docs = chat.home.path() / "docs";
    std::filesystem::create_directories(docs);
    std::ofstream{docs / "heron.md", std::ios::binary}
        << "Project Heron is the billing ledger. It deploys to Frankfurt.\n";
    INFO(chat.err);
    REQUIRE(chat.run({"embed", "ingest", "notes", docs.string()}) == 0);

    REQUIRE(chat.run({"chat", "--verbose", "--rag", "notes"}, "Where does Heron deploy?\n") == 0);
    CHECK(chat.err.find("chunks fit the context budget") != std::string::npos);

    REQUIRE(chat.run({"complete", "--verbose", "--rag", "notes", "Where does Heron deploy?"}) == 0);
    CHECK(chat.err.find("chunks fit the context budget") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Attachments (26d)
// ---------------------------------------------------------------------------

TEST_CASE("a small file attached is inlined; the transcript keeps the message as typed",
          "[chat][cli][attachments]") {
    HelperChat chat{texts({"it is 7731", "yes"}), {"Code Notes"}, "    context_size: 8000\n"};
    const std::filesystem::path notes = chat.home.path() / "notes.md";
    std::ofstream{notes, std::ios::binary} << "The launch code is 7731.\n";
    const std::string mention = "summarize @" + notes.string() + " please";
    INFO(chat.err);
    REQUIRE(chat.run({"chat", "--verbose"},
                     "/attach " + notes.string() + "\nwhat is the code?\n" + mention + "\n") == 0);
    // Named as the user typed it: a Windows path keeps its backslashes.
    CHECK(chat.err.find("attached " + notes.string() + ": 1 file, 1 chunk") != std::string::npos);
    CHECK(chat.err.find("-- inlined whole") != std::string::npos);

    const apogee::logger::Session session = HelperChat::only_session();
    REQUIRE(session.attachments.size() == 1);
    CHECK(session.attachments[0].name == notes.string());
    // It rides the first question; the mention names what is already attached.
    CHECK(session.attachments[0].inline_at == std::optional<std::size_t>{0});
    std::vector<std::string> asked;
    for (const ChatMessage& message : session.messages) {
        if (message.role == apogee::harness::Role::User) {
            asked.push_back(message.content.plain_text());
        }
    }
    CHECK(asked == std::vector<std::string>{"what is the code?", mention});
    CHECK(std::filesystem::exists(ChatAttachments::index_for(session.chat_id)));

    // Deleting the chat deletes its index.
    REQUIRE(chat.run({"chats", "delete", session.chat_id}) == 0);
    CHECK_FALSE(std::filesystem::exists(ChatAttachments::index_for(session.chat_id)));
}

TEST_CASE("an @ mention attaches exactly as /attach would; one naming nothing stays text",
          "[chat][cli][attachments]") {
    HelperChat chat{texts({"a summary"}), {"Report"}, "    context_size: 8000\n"};
    const std::filesystem::path report = chat.home.path() / "report.md";
    std::ofstream{report, std::ios::binary} << "# Report\nSales rose.\n";
    const std::string message = "summarize @" + report.string() + " and @nowhere.txt";
    INFO(chat.err);
    REQUIRE(chat.run({"chat"}, message + "\n") == 0);
    CHECK(chat.err.find("@nowhere.txt: no file or folder there -- left as text") !=
          std::string::npos);
    const apogee::logger::Session session = HelperChat::only_session();
    REQUIRE(session.attachments.size() == 1);
    CHECK(session.attachments[0].name == report.string());
    CHECK(session.attachments[0].inline_at == std::optional<std::size_t>{0});
    REQUIRE_FALSE(session.messages.empty());
    CHECK(session.messages.front().content.plain_text() == message);
}

namespace {

/// `chat`'s config with the helper model as the vision role too.
void with_vision_helper(const HelperChat& chat) {
    std::ifstream in{chat.config_path};
    std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    const std::string utility = "  default_utility: helper\n";
    text.replace(text.find(utility), utility.size(), utility + "  default_vision: helper\n");
    std::ofstream{chat.config_path, std::ios::binary} << text;
}

}  // namespace

TEST_CASE("chat --image attaches the picture: seen with the first message, described after",
          "[chat][cli][attachments][media]") {
    HelperChat chat{texts({"{{last_user}}", "second"}),
                    {"A red screen that says STOP.", "Pic"},
                    "    context_size: 8000\n"};
    with_vision_helper(chat);
    const std::filesystem::path picture = chat.home.path() / "stop.png";
    std::ofstream{picture, std::ios::binary} << "PNGBYTES";
    INFO(chat.err);
    REQUIRE(chat.run({"chat", "--image", picture.string()}, "what is it?\nand now?\n") == 0);
    CHECK(chat.err.find("attached " + picture.string() +
                        ": 1 file, 1 chunk, described by helper") != std::string::npos);
    CHECK(chat.err.find("-- read as it is with your next message, then inlined whole") !=
          std::string::npos);
    const apogee::logger::Session session = HelperChat::only_session();
    REQUIRE(session.attachments.size() == 1);
    REQUIRE(session.attachments[0].files.size() == 1);
    CHECK(session.attachments[0].files[0].reader == "vision: helper");
    // The transcript keeps the messages as typed: no picture saved in it.
    for (const ChatMessage& message : session.messages) {
        CHECK_FALSE(message.content.is_rich());
    }
}

TEST_CASE("a driver attaches a picture with an attach line, and hears it described",
          "[chat][cli][attachments][media][machine]") {
    HelperChat chat{
        texts({"driven answer"}), {"A chart of sales.", "Driven"}, "    context_size: 8000\n"};
    with_vision_helper(chat);
    const std::filesystem::path picture = chat.home.path() / "chart.png";
    std::ofstream{picture, std::ios::binary} << "PNGBYTES";
    const std::string input = R"({"type":"attach","path":")" + picture.generic_string() + "\"}\n" +
                              R"({"type":"user","text":"what is it?"})" + "\n";
    INFO(chat.err);
    REQUIRE(chat.run({"chat", "--input-format", "stream-json", "--output-format", "stream-json"},
                     input) == 0);
    CHECK(chat.out.find("described by helper") != std::string::npos);
    CHECK(chat.out.find("driven answer") != std::string::npos);
}

TEST_CASE("complete --image a model cannot see is described for it; a file that is no image fails",
          "[chat][cli][attachments][media]") {
    HelperChat chat{
        texts({"{{last_user}}"}), {"An invoice for 1,284.50 EUR."}, "    context_size: 8000\n"};
    with_vision_helper(chat);
    const std::filesystem::path picture = chat.home.path() / "invoice.png";
    std::ofstream{picture, std::ios::binary} << "PNGBYTES";
    INFO(chat.err);
    REQUIRE(chat.run({"complete", "--image", picture.string(), "how much?"}) == 0);
    CHECK(chat.err.find("described by helper") != std::string::npos);
    CHECK(chat.out.find("how much?") != std::string::npos);
    // Not an image, or not there: refused before anything runs.
    const std::filesystem::path notes = chat.home.path() / "notes.txt";
    std::ofstream{notes} << "text";
    CHECK(chat.run({"complete", "--image", notes.string(), "x"}) != 0);
    CHECK(chat.err.find("unsupported image type") != std::string::npos);
    CHECK(chat.run({"complete", "--image", (chat.home.path() / "gone.png").string(), "x"}) != 0);
    CHECK(chat.err.find("cannot open file") != std::string::npos);
}

TEST_CASE("complete --attach answers over a temporary index", "[chat][cli][attachments]") {
    HelperChat chat{texts({"a one-shot answer"}), {}, "    context_size: 8000\n"};
    const std::filesystem::path notes = chat.home.path() / "notes.md";
    std::ofstream{notes, std::ios::binary} << "The launch code is 7731.\n";
    INFO(chat.err);
    REQUIRE(chat.run({"complete", "--attach", notes.string(), "what is the code?"}) == 0);
    CHECK(chat.out.find("a one-shot answer") != std::string::npos);
    CHECK(chat.err.find("-- inlined whole") != std::string::npos);
    // Nothing kept: no chat index was made.
    std::error_code code;
    const std::filesystem::path chats = apogee::harness::attachments_dir();
    CHECK((!std::filesystem::exists(chats, code) || std::filesystem::is_empty(chats, code)));
}

TEST_CASE("a driver attaches with an attach line and hears how it went",
          "[chat][cli][attachments][machine]") {
    HelperChat chat{texts({"driven answer"}), {"Driven"}, "    context_size: 8000\n"};
    const std::filesystem::path notes = chat.home.path() / "notes.md";
    std::ofstream{notes, std::ios::binary} << "The launch code is 7731.\n";
    const std::string input = R"({"type":"attach","path":")" + notes.generic_string() + "\"}\n" +
                              R"({"type":"user","text":"what is the code?"})" + "\n";
    INFO(chat.err);
    REQUIRE(chat.run({"chat", "--input-format", "stream-json", "--output-format", "stream-json"},
                     input) == 0);
    CHECK(chat.out.find(R"("type":"notice")") != std::string::npos);
    CHECK(chat.out.find("-- inlined whole") != std::string::npos);
    CHECK(chat.out.find("driven answer") != std::string::npos);
    const apogee::logger::Session session = HelperChat::only_session();
    CHECK(session.attachments.size() == 1);
}

TEST_CASE("complete --attach sends an inlined file on the prompt", "[chat][cli][attachments]") {
    HelperChat chat{texts({"{{last_user}}"}), {}, "    context_size: 8000\n"};
    const std::filesystem::path notes = chat.home.path() / "notes.md";
    std::ofstream{notes, std::ios::binary} << "The launch code is 7731.\n";
    INFO(chat.err);
    REQUIRE(chat.run({"complete", "--attach", notes.string(), "what is the code?"}) == 0);
    CHECK(chat.out.find("--- attached file: " + notes.generic_string() + " ---") !=
          std::string::npos);
    CHECK(chat.out.find("The launch code is 7731.") != std::string::npos);
    CHECK(chat.out.find("what is the code?") != std::string::npos);
}

TEST_CASE("a folder attached sends its map on the prompt, and never into the saved chat",
          "[chat][cli][attachments][map]") {
    const auto folder = [](const std::filesystem::path& root) {
        std::filesystem::create_directories(root / "src");
        std::filesystem::create_directories(root / "docs");
        std::ofstream{root / "src" / "main.cpp"} << "int main() {}\n";
        std::ofstream{root / "docs" / "guide.md"} << "the guide\n";
    };
    {
        // complete: the one-shot surface, through the same class.
        HelperChat chat{texts({"{{last_user}}"}), {}, "    context_size: 8000\n"};
        folder(chat.home.path() / "proj");
        INFO(chat.err);
        REQUIRE(chat.run({"complete", "--attach", (chat.home.path() / "proj").string(),
                          "where is main?"}) == 0);
        CHECK(chat.err.find("2 files, 2 chunks, with a map of its folders") != std::string::npos);
        CHECK(chat.out.find("--- map of attachment: ") != std::string::npos);
        CHECK(chat.out.find("docs/  1 file\nsrc/  1 file\n") != std::string::npos);
        CHECK(chat.out.find("where is main?") != std::string::npos);
    }
    {
        // chat: the map rides the request; the saved message is as typed.
        HelperChat chat{texts({"{{last_user}}"}), {"Mapped"}, "    context_size: 8000\n"};
        folder(chat.home.path() / "proj");
        INFO(chat.err);
        REQUIRE(chat.run({"chat"}, "/attach " + (chat.home.path() / "proj").string() +
                                       "\nwhere is main?\n") == 0);
        CHECK(chat.out.find("--- map of attachment: ") != std::string::npos);
        const apogee::logger::Session session = HelperChat::only_session();
        REQUIRE(session.attachments.size() == 1);
        REQUIRE(session.attachments[0].map_at.has_value());
        const apogee::harness::ChatMessage& asked =
            session.messages.at(*session.attachments[0].map_at);
        CHECK(asked.content.plain_text() == "where is main?");
    }
}

TEST_CASE("an inlined attachment whose exchange the budget dropped is retrieved from then on",
          "[chat][cli][attachments]") {
    // A 4,000-token window holds 2,000 after the reserve; the second question
    // alone is 1,600, so the first exchange -- and the file it carried -- go.
    HelperChat chat{texts({"one", "two"}), {"Dropped"}, "    context_size: 4000\n"};
    const std::filesystem::path notes = chat.home.path() / "notes.md";
    std::ofstream{notes, std::ios::binary} << std::string(1600, 'n') << "\n";
    INFO(chat.err);
    REQUIRE(chat.run({"chat"}, "/attach " + notes.string() + "\nfirst\n" + std::string(6400, 'q') +
                                   "\n") == 0);
    CHECK(chat.err.find("-- inlined whole") != std::string::npos);
    CHECK(chat.err.find("no longer fits the conversation whole") != std::string::npos);
    const apogee::logger::Session session = HelperChat::only_session();
    REQUIRE(session.attachments.size() == 1);
    CHECK_FALSE(session.attachments[0].inline_at.has_value());
}

TEST_CASE("compaction moves an inlined attachment to retrieval", "[chat][cli][attachments]") {
    // A 2,000-token window: the second question takes it past 90%.
    HelperChat chat{
        texts({"one", "two"}), {"Compacted", "what was said"}, "    context_size: 2000\n"};
    const std::filesystem::path notes = chat.home.path() / "notes.md";
    std::ofstream{notes, std::ios::binary} << std::string(600, 'n') << "\n";
    INFO(chat.err);
    REQUIRE(chat.run({"chat"}, "/attach " + notes.string() + "\nfirst\n" + std::string(7200, 'q') +
                                   "\n") == 0);
    CHECK(chat.err.find("-- inlined whole") != std::string::npos);
    CHECK(chat.err.find("compaction folded the messages the attachments rode") !=
          std::string::npos);
    const apogee::logger::Session session = HelperChat::only_session();
    REQUIRE(session.attachments.size() == 1);
    CHECK_FALSE(session.attachments[0].inline_at.has_value());
}

TEST_CASE("a collection gets what the attachments leave of the retrieval share",
          "[chat][cli][attachments][rag]") {
    // A 7,000-token window: 3,500 after the reserve, 700 of it retrieval's.
    // The attachment's four excerpts take about 600; a collection's chunk
    // needs about 180, and so none fits beside them.
    HelperChat chat{texts({"answered"}), {"Shared"}, "    context_size: 7000\n"};
    std::string manual;
    for (int chunk = 0; chunk < 12; ++chunk) {
        std::string piece(448, 'x');
        for (std::size_t at = 63; at < piece.size(); at += 64) {
            piece[at] = ' ';  // words, not one long token
        }
        if (chunk % 3 == 0) {
            piece.replace(200, 9, " zarquon ");  // a word in this chunk alone
        }
        manual += piece;
    }
    const std::filesystem::path file = chat.home.path() / "manual.txt";
    std::ofstream{file, std::ios::binary} << manual;
    const std::filesystem::path docs = chat.home.path() / "docs";
    std::filesystem::create_directories(docs);
    for (const char* name : {"a.md", "b.md"}) {
        std::ofstream{docs / name, std::ios::binary} << "zarquon " << std::string(480, 'w') << "\n";
    }
    INFO(chat.err);
    REQUIRE(chat.run({"embed", "ingest", "notes", docs.string()}) == 0);
    REQUIRE(chat.run({"chat", "--rag", "notes"}, "/attach " + file.string() + "\nzarquon?\n") == 0);
    CHECK(chat.err.find("excerpts from the attachments") != std::string::npos);
    CHECK(chat.err.find("0 of 2 chunks fit the context budget") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Whether the model thinks first (26i)
// ---------------------------------------------------------------------------

TEST_CASE("/think shows and sets the chat's thinking, and the session keeps it",
          "[chat][cli][thinking]") {
    HelperChat chat{texts({"first answer"}), {"Thinking Title"}, "    thinking_budget: 512\n"};
    REQUIRE(chat.run({"chat"}, "/think\n/think off\nfirst question\n/think sometimes\n") == 0);
    const std::string said = chat.out + chat.err;
    // Bare: what the next question gets -- the backend's budget included.
    CHECK(said.find("thinking: on, at most 512 tokens") != std::string::npos);
    CHECK(said.find("not a thinking mode: 'sometimes' (on, off or auto)") != std::string::npos);
    const apogee::logger::Session session = HelperChat::only_session();
    CHECK(session.params.thinking == apogee::harness::ThinkingMode::Off);
    // The budget stayed the backend's: only what was set is saved.
    CHECK_FALSE(session.params.thinking_budget.has_value());
    // Off asks no judge: the helper's one reply went to the title.
    CHECK(session.title == "Thinking Title");
}

TEST_CASE(
    "chat --think auto asks the utility model per question, and --verbose says what it decided",
    "[chat][cli][thinking][helpers]") {
    HelperChat chat{texts({"Rayleigh scattering."}), {"no", "Sky Title"}};
    REQUIRE(chat.run({"chat", "--think", "auto", "--think-budget", "256", "--verbose"},
                     "Why is the sky blue?\n") == 0);
    CHECK(chat.err.find("thinking: auto -- off, by the utility model") != std::string::npos);
    const apogee::logger::Session session = HelperChat::only_session();
    CHECK(session.params.thinking == apogee::harness::ThinkingMode::Auto);
    CHECK(session.params.thinking_budget == 256);
    // The judge took exactly its one reply, before the title.
    CHECK(session.title == "Sky Title");
    CHECK(replies(session) == std::vector<std::string>{"Rayleigh scattering."});
}

TEST_CASE("what --think asks for reaches the model, and the backend's default otherwise",
          "[chat][cli][thinking]") {
    // The mock's `{{thinking}}` says what the request carried.
    HelperChat chat{texts({"{{thinking}}", "{{thinking}}"}),
                    {"Title"},
                    "    thinking: off\n    thinking_budget: 128\n"};
    REQUIRE(chat.run({"chat", "--think", "on"}, "first\n/think auto\nsecond\n") == 0);
    // `/think auto` with a judge that says nothing useful ("Title") -- the
    // rule, and "second" is short and plain.
    CHECK(replies(HelperChat::only_session()) == std::vector<std::string>{"on:128", "off:128"});

    HelperChat complete{texts({"{{thinking}}"}), {"unused"}, "    thinking: off\n"};
    REQUIRE(complete.run({"complete", "hello"}) == 0);
    CHECK(complete.out.find("off") != std::string::npos);
    REQUIRE(complete.run({"complete", "--think", "on", "--think-budget", "64", "hello"}) == 0);
    CHECK(complete.out.find("on:64") != std::string::npos);
}

TEST_CASE("complete takes the same thinking flags, and refuses a bad one",
          "[chat][cli][thinking][helpers]") {
    HelperChat chat{texts({"391"}), {"yes"}};
    REQUIRE(chat.run({"complete", "--think", "auto", "--verbose", "Hello there!"}) == 0);
    CHECK(chat.err.find("thinking: auto -- on, by the utility model") != std::string::npos);
    CHECK(chat.run({"complete", "--think", "sometimes", "Hello"}) != 0);
    CHECK(chat.run({"complete", "--think-budget", "-1", "Hello"}) != 0);
    CHECK(chat.run({"chat", "--think", "sometimes"}, "") != 0);
}

// ---------------------------------------------------------------------------
// The conversation's saved state (26j)
// ---------------------------------------------------------------------------

namespace {

[[nodiscard]] std::size_t occurrences(std::string_view text, std::string_view needle) {
    std::size_t count = 0;
    for (std::size_t at = text.find(needle); at != std::string_view::npos;
         at = text.find(needle, at + needle.size())) {
        ++count;
    }
    return count;
}

}  // namespace

TEST_CASE("a chat names its conversation to its backend, and saves it at a clean exit",
          "[chat][cli][prompt-cache]") {
    // The mock's `{{conversation}}` says which chat it was named for, and its
    // save says it was asked.
    HelperChat chat{texts({"{{conversation}}"}), {"Title"}};
    REQUIRE(chat.run({"chat", "--verbose"}, "hello\n") == 0);
    const apogee::logger::Session first = HelperChat::only_session();
    CHECK(replies(first) == std::vector<std::string>{first.chat_id});
    CHECK(occurrences(chat.err, "nothing saved for " + first.chat_id) == 1);

    // Resumed, the same conversation is named again.
    REQUIRE(chat.run({"chat", "--resume", first.chat_id}, "again\n") == 0);
    CHECK(replies(HelperChat::only_session()) ==
          std::vector<std::string>{first.chat_id, first.chat_id});
}

TEST_CASE("a compacted chat's state is saved after the turn that reads it",
          "[chat][cli][prompt-cache][helpers]") {
    HelperChat chat{texts({"one", "two", "three"}), {"Title", "what was said"}};
    REQUIRE(chat.run({"chat", "--verbose"}, "first\nsecond\n/compact\nthird\n") == 0);
    const apogee::logger::Session session = HelperChat::only_session();
    CHECK(session.compactions == 1);
    // Once after the third turn, once at the exit.
    CHECK(occurrences(chat.err, "nothing saved for " + session.chat_id) == 2);
}

// ---------------------------------------------------------------------------
// Recall across chats (26l)
// ---------------------------------------------------------------------------

namespace {

[[nodiscard]] std::size_t recalled_chats(const HelperChat& chat) {
    return apogee::agentloop::RecallIndex{chat.home.path() / "memory" / "chats.db", std::nullopt}
        .chats();
}

/// The first chat: two turns stating a fact, summarised at its exit.
[[nodiscard]] std::string first_chat(HelperChat& chat) {
    REQUIRE(chat.run({"chat", "--verbose"}, "Our project uses Postgres 16.\nThanks.\n") == 0);
    CHECK(chat.err.find("[memory] summarised for recall by helper") != std::string::npos);
    const std::string id = HelperChat::only_session().chat_id;
    // The next chat echoes what it was sent, and titles itself.
    (void)chat.script("chatty", texts({"{{system}}"}));
    (void)chat.script("helper", texts({"Later Title"}));
    return id;
}

}  // namespace

TEST_CASE("a finished chat is recalled in a new one, and never written into it",
          "[chat][cli][recall][helpers]") {
    HelperChat chat{texts({"Noted.", "Fine."}), {"Title", "The project uses Postgres 16."}};
    const std::string first = first_chat(chat);
    CHECK(recalled_chats(chat) == 1);

    REQUIRE(chat.run({"chat"}, "Which Postgres version does the project use?\n") == 0);
    CHECK((chat.out + chat.err).find("[memory] 1 past chat") != std::string::npos);
    const auto sessions = apogee::logger::list_sessions();
    REQUIRE(sessions.size() == 2);
    const apogee::logger::Session& later =
        sessions.front().chat_id == first ? sessions.back() : sessions.front();
    // The model was handed the summary, as notes on an earlier conversation.
    const std::string sent = replies(later).front();
    CHECK(sent.find(std::string{apogee::agentloop::kRecallHeader}) != std::string::npos);
    CHECK(sent.find("Postgres 16") != std::string::npos);
    // Transient: no message of the new chat carries it.
    for (const ChatMessage& message : later.messages) {
        if (message.role != apogee::harness::Role::Assistant) {
            CHECK(message.content.plain_text().find("Postgres 16") == std::string::npos);
        }
    }
}

TEST_CASE("recall stops for a run, for a session, and for good when the chat is deleted",
          "[chat][cli][recall][helpers]") {
    HelperChat chat{texts({"Noted.", "Fine."}), {"Title", "The project uses Postgres 16."}};
    const std::string first = first_chat(chat);
    const std::string question = "Which Postgres version does the project use?\n";
    const auto recalled_in_last = [&chat] {
        const auto sessions = apogee::logger::list_sessions();
        for (const auto& session : sessions) {
            for (const std::string& reply : replies(session)) {
                if (reply.find(std::string{apogee::agentloop::kRecallHeader}) !=
                    std::string::npos) {
                    return true;
                }
            }
        }
        return false;
    };

    REQUIRE(chat.run({"chat", "--no-recall"}, question) == 0);
    CHECK_FALSE(recalled_in_last());
    REQUIRE(chat.run({"chat"}, "/recall off\n" + question) == 0);
    CHECK_FALSE(recalled_in_last());

    REQUIRE(chat.run({"chats", "delete", first}) == 0);
    CHECK(recalled_chats(chat) == 0);
    REQUIRE(chat.run({"chat"}, question) == 0);
    CHECK_FALSE(recalled_in_last());
}

TEST_CASE("a private chat, or one of a single turn, is never summarised",
          "[chat][cli][recall][helpers]") {
    HelperChat chat{texts({"Noted.", "Fine."}), {"Title", "The project uses Postgres 16."}};
    REQUIRE(chat.run({"chat", "--verbose"}, "Postgres 16.\n/private\nThanks.\n") == 0);
    CHECK(chat.err.find("summarised for recall by") == std::string::npos);
    CHECK(chat.err.find("it will never be summarised for recall") != std::string::npos);
    CHECK(recalled_chats(chat) == 0);
    CHECK(HelperChat::only_session().private_chat);

    HelperChat single{texts({"Noted."}), {"Title", "The project uses Postgres 16."}};
    REQUIRE(single.run({"chat", "--verbose"}, "Postgres 16.\n") == 0);
    CHECK(recalled_chats(single) == 0);
}

TEST_CASE("complete never recalls", "[chat][cli][recall][helpers]") {
    HelperChat chat{texts({"Noted.", "Fine."}), {"Title", "The project uses Postgres 16."}};
    (void)first_chat(chat);
    REQUIRE(chat.run({"complete", "Which Postgres version does the project use?"}) == 0);
    CHECK(chat.out.find(std::string{apogee::agentloop::kRecallHeader}) == std::string::npos);
}

TEST_CASE("a chat a process left behind is summarised at the next start, an open one is not",
          "[chat][cli][recall][helpers]") {
    HelperChat chat{texts({"Noted.", "Fine."}), {"Title", "Left behind: Postgres 16."}};
    REQUIRE(chat.run({"chat", "--no-recall"}, "Our project uses Postgres 16.\nThanks.\n") == 0);
    const std::string id = HelperChat::only_session().chat_id;
    // As if it had died: no summary, and a marker naming a process gone.
    apogee::agentloop::RecallIndex{chat.home.path() / "memory" / "chats.db", std::nullopt}.remove(
        id);
    std::filesystem::create_directories(chat.home.path() / "memory" / "pending");
    std::ofstream{chat.home.path() / "memory" / "pending" / id} << "999999999\n";
    // And another chat still open in a process that is running: init or
    // launchd on POSIX, the System process on Windows, where there is no 1.
#if defined(_WIN32)
    constexpr std::string_view kAlwaysRunning = "4\n";
#else
    constexpr std::string_view kAlwaysRunning = "1\n";
#endif
    std::ofstream{chat.home.path() / "memory" / "pending" / "open-elsewhere"} << kAlwaysRunning;

    (void)chat.script("helper", texts({"Left behind: Postgres 16."}));
    REQUIRE(chat.run({"chat"}, "") == 0);
    CHECK(chat.err.find("left by a chat that did not finish") != std::string::npos);
    CHECK(recalled_chats(chat) == 1);
    CHECK_FALSE(std::filesystem::exists(chat.home.path() / "memory" / "pending" / id));
    CHECK(std::filesystem::exists(chat.home.path() / "memory" / "pending" / "open-elsewhere"));
}

TEST_CASE("a chat never recalls itself, and is not summarised again unchanged",
          "[chat][cli][recall][helpers]") {
    HelperChat chat{texts({"Noted.", "Fine."}), {"Title", "The project uses Postgres 16."}};
    const std::string first = first_chat(chat);
    // Resumed and asked: its own summary is not "an earlier conversation".
    REQUIRE(chat.run({"chat", "--resume", first},
                     "Which Postgres version does the project use?\n") == 0);
    CHECK((chat.out + chat.err).find("past chat") == std::string::npos);
    // Continued, it is summarised again, as it now stands.
    CHECK(chat.err.find("[memory] summarised for recall") != std::string::npos);
    // Resumed and left as it was: the summary it has stands.
    REQUIRE(chat.run({"chat", "--resume", first, "--verbose"}, "") == 0);
    CHECK(chat.err.find("summarised for recall") == std::string::npos);
}

TEST_CASE("/private takes back a summary already kept", "[chat][cli][recall][helpers]") {
    HelperChat chat{texts({"Noted.", "Fine."}), {"Title", "The project uses Postgres 16."}};
    const std::string first = first_chat(chat);
    REQUIRE(recalled_chats(chat) == 1);
    REQUIRE(chat.run({"chat", "--resume", first}, "/private\n") == 0);
    CHECK(recalled_chats(chat) == 0);
}

TEST_CASE("a recorded decision is recalled beside past chats", "[chat][cli][recall][helpers]") {
    HelperChat chat{texts({"{{system}}"}), {"Title"}};
    std::filesystem::create_directories(chat.home.path() / "embeddings");
    apogee::embedstore::Store{chat.home.path() / "embeddings" / "knowledge.db"}.replace_source(
        "record-1", {"Decision: the migration targets Postgres 16, chosen for its JSON support."});
    REQUIRE(chat.run({"chat"}, "Which Postgres version does the migration target?\n") == 0);
    CHECK((chat.out + chat.err).find("[memory] 1 decision") != std::string::npos);
    CHECK(replies(HelperChat::only_session()).front().find("Postgres 16") != std::string::npos);
}

TEST_CASE("recall never bills a backend for a summary", "[chat][recall]") {
    apogee::harness::Harness harness{apogee::harness::Config{}};
    apogee::backends::MockProvider::Options billed;
    billed.backend_name = "billed";
    billed.metered = true;
    harness.register_provider("billed", std::make_shared<apogee::backends::MockProvider>(billed));
    apogee::backends::MockProvider::Options local;
    local.backend_name = "local";
    harness.register_provider("local", std::make_shared<apogee::backends::MockProvider>(local));
    harness.use_default_router();
    std::string reason;
    CHECK(apogee::commands::recall_summariser(harness, {}, "local", reason) == "local");
    CHECK(apogee::commands::recall_summariser(harness, {}, "billed", reason).empty());
    CHECK(reason.find("billed per call") != std::string::npos);
    apogee::harness::Config config;
    config.models.default_utility = "local";
    CHECK(apogee::commands::recall_summariser(harness, config, "billed", reason) == "local");
}

TEST_CASE("machine mode hears a turn's side calls as tool_status lines",
          "[chat][cli][helpers][machine][side-call]") {
    // The tool-summary and compaction turn of the test above, driven: each
    // side call is the existing display-prose event (26n), never new vocabulary.
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
    std::ofstream{chat.config_path, std::ios::binary | std::ios::app}
        << "tools:\n  fs_root: " << work.string() << "\n";
    const std::string input = std::string{R"({"type":"user","text":"what is in big.log?"})"} +
                              "\n" + R"({"type":"user","text":"and then?"})" + "\n";
    INFO(chat.err);
    REQUIRE(chat.run({"chat", "--tools", "--input-format", "stream-json", "--output-format",
                      "stream-json"},
                     input) == 0);
    CHECK(chat.out.find(R"("type":"tool_status")") != std::string::npos);
    CHECK(chat.out.find("utility — summarising read_file's 12 KB result with helper") !=
          std::string::npos);
    CHECK(chat.out.find("utility — compacting the conversation with helper") != std::string::npos);
    // Display prose only: the saved chat carries none of it.
    for (const ChatMessage& message : HelperChat::only_session().messages) {
        CHECK(message.content.plain_text().find("compacting the conversation") ==
              std::string::npos);
    }
}

TEST_CASE("machine mode hears a follow-up's rewrite as a tool_status line",
          "[chat][rag][cli][helpers][machine][side-call]") {
    HelperChat chat{texts({"Heron is the billing ledger.", "It deploys to Frankfurt."}),
                    {"Heron Notes", "Project Heron deployment region"}};
    const std::filesystem::path docs = chat.home.path() / "docs";
    std::filesystem::create_directories(docs);
    std::ofstream{docs / "heron.md", std::ios::binary}
        << "Project Heron is the billing ledger. It deploys to Frankfurt.\n";
    REQUIRE(chat.run({"embed", "ingest", "notes", docs.string()}) == 0);
    const std::string input =
        std::string{R"({"type":"user","text":"Tell me about Project Heron."})"} + "\n" +
        R"({"type":"user","text":"where does it deploy?"})" + "\n";
    REQUIRE(chat.run({"chat", "--rag", "notes", "--input-format", "stream-json", "--output-format",
                      "stream-json"},
                     input) == 0);
    // Once: the first question stands alone and is not restated.
    const std::string said = "utility — rewriting the follow-up into a search query with helper";
    CHECK(chat.out.find(said) != std::string::npos);
    CHECK(chat.out.find(said) == chat.out.rfind(said));
}

TEST_CASE("machine mode hears the rerank judge as a tool_status line",
          "[chat][rag][cli][helpers][machine][side-call]") {
    HelperChat chat{texts({"Heron is the billing ledger."}), {"[1]", "Heron Notes"}};
    const std::filesystem::path docs = chat.home.path() / "docs";
    std::filesystem::create_directories(docs);
    std::ofstream{docs / "heron.md", std::ios::binary}
        << "Project Heron is the billing ledger. It deploys to Frankfurt.\n";
    REQUIRE(chat.run({"embed", "ingest", "notes", docs.string()}) == 0);
    REQUIRE(chat.run({"chat", "--rag", "notes", "--rerank", "on", "--input-format", "stream-json",
                      "--output-format", "stream-json"},
                     std::string{R"({"type":"user","text":"Tell me about Project Heron."})"} +
                         "\n") == 0);
    CHECK(chat.out.find("rerank — judging") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Session permission presets (26o)
// ---------------------------------------------------------------------------

namespace {

/// A chat model that writes a file, then says what the tool answered.
[[nodiscard]] nlohmann::json writes_a_file() {
    return nlohmann::json::array(
        {{{"tool_calls",
           {{{"name", "write_file"}, {"arguments", {{"path", "out.txt"}, {"content", "hello"}}}}}}},
         {{"text", "{{last_tool_result}}"}}});
}

void sandboxed(HelperChat& chat) {
    std::filesystem::create_directories(chat.home.path() / "work");
    std::ofstream{chat.config_path, std::ios::binary | std::ios::app}
        << "tools:\n  fs_root: " << (chat.home.path() / "work").string() << "\n";
}

}  // namespace

TEST_CASE("complete --allow is the user answering at invocation; without it, denied as ever",
          "[chat][cli][permissions][presets]") {
    HelperChat chat{writes_a_file(), {"unused"}};
    sandboxed(chat);
    const std::filesystem::path written = chat.home.path() / "work" / "out.txt";
    REQUIRE(chat.run({"complete", "--tools", "write it"}) == 0);
    CHECK_FALSE(std::filesystem::exists(written));
    CHECK(chat.out.find("denied") != std::string::npos);

    REQUIRE(chat.run({"complete", "--tools", "--allow", "write_file", "write it"}) == 0);
    CHECK(std::filesystem::exists(written));

    // A typo grants nothing, and says what would have.
    CHECK(chat.run({"complete", "--tools", "--allow", "wrte_file", "write it"}) != 0);
    CHECK(chat.err.find("write_file") != std::string::npos);
    // Without tools there is nothing to answer for.
    CHECK(chat.run({"complete", "--allow", "write_file", "write it"}) != 0);
}

TEST_CASE("a repeated --allow, --deny or --allow-host is taken whole, on chat and complete",
          "[chat][cli][permissions][presets]") {
    // Each flag is repeatable for real: a second use was refused by the
    // parser ("At Most 1 required but received 2") until 27i.
    HelperChat chat{texts({"fine"}), {"Title"}};
    REQUIRE(chat.run({"chat", "--tools", "--allow", "write_file", "--allow", "edit_file", "--deny",
                      "run_command", "--deny", "example.org", "--allow-host", "docs.python.org",
                      "--allow-host", "en.wikipedia.org"},
                     "/permissions\n") == 0);
    const std::string said = chat.out + chat.err;
    CHECK(said.find("write_file   allow  (this session)") != std::string::npos);
    CHECK(said.find("edit_file    allow  (this session)") != std::string::npos);
    CHECK(said.find("run_command  deny   (this session)") != std::string::npos);
    CHECK(said.find("website example.org: deny (this session)") != std::string::npos);
    CHECK(said.find("website docs.python.org: allow (this session)") != std::string::npos);
    CHECK(said.find("website en.wikipedia.org: allow (this session)") != std::string::npos);

    HelperChat once{writes_a_file(), {"unused"}};
    sandboxed(once);
    REQUIRE(once.run({"complete", "--tools", "--allow", "edit_file", "--allow", "write_file",
                      "--deny", "run_command", "--deny", "delete_file", "--allow-host",
                      "docs.python.org", "--allow-host", "en.wikipedia.org", "write it"}) == 0);
    CHECK(std::filesystem::exists(once.home.path() / "work" / "out.txt"));
}

TEST_CASE("chat --allow writes without asking, and a fresh chat asks again",
          "[chat][cli][permissions][presets]") {
    HelperChat chat{writes_a_file(), {"Title"}};
    sandboxed(chat);
    const std::filesystem::path written = chat.home.path() / "work" / "out.txt";
    REQUIRE(chat.run({"chat", "--tools", "--allow", "write_file"}, "write it\n") == 0);
    CHECK(std::filesystem::exists(written));
    std::filesystem::remove(written);
    // Grants die with the process: on a pipe nobody can be asked, so denied.
    REQUIRE(chat.run({"chat", "--tools"}, "write it\n") == 0);
    CHECK_FALSE(std::filesystem::exists(written));
}

TEST_CASE("the slash verbs answer for the chat, and /permissions says from where",
          "[chat][cli][permissions][presets]") {
    HelperChat chat{texts({"fine"}), {"Title"}, "", "permissions:\n  delete_file: allow\n"};
    REQUIRE(chat.run({"chat", "--tools", "--deny", "example.org"},
                     "/allow write_file\n/deny run_command\n/permissions\n/revoke run_command\n"
                     "/revoke delete_file\n/allow wrte_file\n/permissions\n") == 0);
    const std::string said = chat.out + chat.err;
    CHECK(said.find("write_file allowed for this chat") != std::string::npos);
    CHECK(said.find("run_command denied for this chat") != std::string::npos);
    CHECK(said.find("run_command  deny   (this session)") != std::string::npos);
    CHECK(said.find("delete_file  allow  (config)") != std::string::npos);
    CHECK(said.find("website example.org: deny (this session)") != std::string::npos);
    CHECK(said.find("run_command: asked again from now on") != std::string::npos);
    CHECK(said.find("delete_file is the config's answer") != std::string::npos);
    CHECK(said.find("'wrte_file' is not a tool this chat asks about") != std::string::npos);
    CHECK(said.find("run_command  ask    (default)") != std::string::npos);

    // Resumed, it starts clean: nothing a session answered survives it.
    const std::string id = HelperChat::only_session().chat_id;
    REQUIRE(chat.run({"chat", "--tools", "--resume", id}, "/permissions\n") == 0);
    CHECK((chat.out + chat.err).find("write_file   ask    (default)") != std::string::npos);
    CHECK((chat.out + chat.err).find("this session") == std::string::npos);
}

TEST_CASE(
    "a base model's turns run without tools, said once at the start; an instruct one keeps them",
    "[chat][cli][tools][base]") {
    const auto count_of = [](std::string_view haystack, std::string_view needle) {
        std::size_t n = 0;
        for (std::size_t at = haystack.find(needle); at != std::string_view::npos;
             at = haystack.find(needle, at + needle.size())) {
            ++n;
        }
        return n;
    };
    const auto base_script = [](HelperChat& chat, const std::vector<std::string>& replies) {
        // A script that says its model is a base model: one with no chat
        // template (26r).
        std::ofstream{chat.home.path() / "chatty.json", std::ios::binary}
            << nlohmann::json{{"turns", texts(replies)}, {"base_model", true}}.dump();
    };
    {
        HelperChat chat{texts({}), {"Base"}};
        base_script(chat, {"tools {{tool_count}}", "still {{tool_count}}"});
        INFO(chat.err);
        REQUIRE(chat.run({"chat", "--tools"}, "hello\nagain\n") == 0);
        // Asked and answered, ungated -- with no tool in either request.
        CHECK(chat.out.find("tools 0") != std::string::npos);
        CHECK(chat.out.find("still 0") != std::string::npos);
        // Said once, at the start.
        CHECK(count_of(chat.err, "tools off: chatty is a base model") == 1);
    }
    {
        HelperChat chat{texts({}), {}};
        base_script(chat, {"tools {{tool_count}}"});
        INFO(chat.err);
        REQUIRE(chat.run({"complete", "--tools", "hello"}) == 0);
        CHECK(chat.out.find("tools 0") != std::string::npos);
        CHECK(count_of(chat.err, "tools off: chatty is a base model") == 1);
    }
    {
        // An instruct model keeps every tool, and nothing is said.
        HelperChat chat{texts({"tools {{tool_count}}"}), {"Instruct"}};
        INFO(chat.err);
        REQUIRE(chat.run({"chat", "--tools"}, "hello\n") == 0);
        CHECK(chat.out.find("tools 0") == std::string::npos);
        CHECK(chat.out.find("tools ") != std::string::npos);
        CHECK(chat.err.find("tools off") == std::string::npos);
    }
}

TEST_CASE("an attachment with nothing relevant injects nothing, says so the same way everywhere",
          "[chat][cli][attachments][floor]") {
    // A window nobody knows inlines nothing, so a one-line file is retrieved
    // per turn -- the probe's case (26s): an unrelated question matched it on
    // "is" and "the", and the excerpt reached the model at 0.000.
    const std::string floored_line =
        "nothing relevant in the attachments -- the best match is under "
        "the floor (";
    std::string terminal_line;
    {
        HelperChat chat{texts({"answered: {{last_user}}"}), {}};
        const std::filesystem::path notes = chat.home.path() / "notes.md";
        std::ofstream{notes, std::ios::binary} << "The launch code is 7731.\n";
        INFO(chat.err);
        REQUIRE(chat.run({"complete", "--attach", notes.string(),
                          "What is the capital of France?"}) == 0);
        // The turn still answers -- with nothing injected into it.
        CHECK(chat.out.find("answered: What is the capital of France?") != std::string::npos);
        CHECK(chat.out.find("7731") == std::string::npos);
        const std::size_t at = chat.err.find(floored_line);
        REQUIRE(at != std::string::npos);
        terminal_line = chat.err.substr(at, chat.err.find('\n', at) - at);
        // The raw score and its retriever ride beside the words.
        CHECK(terminal_line.find("[lexical], none of the question's words)") != std::string::npos);
    }
    {
        // Machine mode says the identical line: one renderer.
        HelperChat chat{texts({"driven"}), {"Driven"}};
        const std::filesystem::path notes = chat.home.path() / "notes.md";
        std::ofstream{notes, std::ios::binary} << "The launch code is 7731.\n";
        const std::string input =
            R"({"type":"attach","path":")" + notes.generic_string() + "\"}\n" +
            R"({"type":"user","text":"What is the capital of France?"})" + "\n";
        INFO(chat.err);
        REQUIRE(
            chat.run({"chat", "--input-format", "stream-json", "--output-format", "stream-json"},
                     input) == 0);
        // Its diagnostics ride stderr, stdout being the protocol's alone.
        CHECK(chat.err.find("apogee: " + terminal_line + "\n") != std::string::npos);
        CHECK(chat.out.find("7731") == std::string::npos);
    }
}

TEST_CASE("chat and complete offer consult under a suite that designates a member",
          "[chat][cli][consult]") {
    // 27f: the root calls consult; the member answers with the brief it was
    // sent -- so the relayed answer is the wire's own record -- on every
    // surface that registers the tool, each opening the turn's budget.
    const nlohmann::json chatty = nlohmann::json::array(
        {{{"tool_calls",
           {{{"name", "consult"},
             {"arguments", {{"member", "utility"}, {"question", "What is the codeword?"}}}}}}},
         {{"text", "relayed {{last_tool_result}}"}}});
    HelperChat chat{chatty,
                    {"MEMBER-ANSWER heard [{{last_user}}] system [{{system}}]"},
                    {},
                    "suites:\n  research:\n    members:\n      chat: chatty\n"
                    "      utility: helper\n    consultable: [utility]\n"};
    const std::string relayed =
        "relayed utility (helper) answered:\nMEMBER-ANSWER heard [What is the codeword?] "
        "system []";

    // Under the suite as the config's default: complete, on a terminal's
    // path and on machine mode's.
    std::string text;
    {
        std::ifstream in{chat.config_path, std::ios::binary};
        std::ostringstream read;
        read << in.rdbuf();
        text = read.str();
    }
    const std::string models = "models:\n  default: chatty\n";
    text.replace(text.find(models), models.size(), models + "  default_suite: research\n");
    std::ofstream{chat.config_path, std::ios::binary} << text;
    REQUIRE(chat.run({"complete", "--tools", "what is the codeword?"}) == 0);
    INFO(chat.err);
    CHECK(chat.out.find(relayed) != std::string::npos);
    REQUIRE(chat.run({"complete", "--tools", "--output-format", "stream-json",
                      "what is the codeword?"}) == 0);
    CHECK(chat.out.find("\"consult — asking utility (helper): What is the codeword?\"") !=
          std::string::npos);
    CHECK(chat.out.find("MEMBER-ANSWER heard [What is the codeword?] system []") !=
          std::string::npos);
    // Without --tools nothing is offered, as for every tool.
    REQUIRE(chat.run({"complete", "what is the codeword?"}) == 0);
    CHECK(chat.out.find("MEMBER-ANSWER") == std::string::npos);

    // And chat.
    REQUIRE(chat.run({"chat", "--tools"}, "what is the codeword?\n") == 0);
    CHECK(chat.out.find(relayed) != std::string::npos);
    // With the suite off, no consult: the call names a tool that is not there.
    REQUIRE(chat.run({"chat", "--tools", "--suite", "off"}, "what is the codeword?\n") == 0);
    CHECK(chat.out.find("MEMBER-ANSWER") == std::string::npos);
    CHECK(chat.out.find("relayed Error: no tool named 'consult'") != std::string::npos);
}

TEST_CASE("/check has the suite's verifier check the last answer once, and says both sides",
          "[chat][cli][validate]") {
    // 27g: the verifier, once, briefed with the question and the answer; the
    // chat's model answers the objection once; both said; the conversation
    // untouched. The helper titles the chat first, as the utility member.
    HelperChat chat{texts({"381", "You're right: 391."}),
                    {"Helper Title", "OBJECT: 17 x 23 is 391, not 381."},
                    {},
                    "suites:\n  checked:\n    members:\n      chat: chatty\n"
                    "      utility: helper\n"};
    REQUIRE(chat.run({"chat", "--suite", "checked"}, "what is 17 x 23?\n/check\n") == 0);
    INFO(chat.err);
    const std::string said = chat.out + chat.err;
    CHECK(said.find("check: utility (helper) objects to the answer -- \"17 x 23 is 391, not "
                    "381.\"") != std::string::npos);
    CHECK(said.find("check: shown the objection, the model answered -- \"You're right: 391.\"") !=
          std::string::npos);
    // Never a transcript mutation: the question and the answer as given.
    const apogee::logger::Session session = HelperChat::only_session();
    REQUIRE(session.messages.size() == 2);
    CHECK(session.messages.back().content.plain_text() == "381");
}

TEST_CASE("/check says why it cannot run, and asks nobody", "[chat][cli][validate]") {
    HelperChat chat{texts({"381"}),
                    {"Helper Title"},
                    {},
                    "suites:\n  lonely:\n    members:\n      chat: chatty\n"};
    REQUIRE(chat.run({"chat"}, "/check\n") == 0);
    CHECK((chat.out + chat.err).find("check: no suite is active") != std::string::npos);
    REQUIRE(chat.run({"chat", "--suite", "lonely"}, "what?\n/check\n") == 0);
    CHECK((chat.out + chat.err).find("check: suite lonely has no utility member to check with") !=
          std::string::npos);
    HelperChat fresh{texts({"381"}),
                     {"Helper Title"},
                     {},
                     "suites:\n  checked:\n    members:\n      chat: chatty\n"
                     "      utility: helper\n"};
    REQUIRE(fresh.run({"chat", "--suite", "checked"}, "/check\n") == 0);
    CHECK((fresh.out + fresh.err).find("check: there is no answer to check yet") !=
          std::string::npos);
}

TEST_CASE("answers always: every answer checked, in chat and machine mode alike",
          "[chat][cli][validate]") {
    HelperChat chat{texts({"381", "You're right: 391."}),
                    {"OBJECT: 17 x 23 is 391, not 381.", "Helper Title"},
                    {},
                    "suites:\n  checked:\n    members:\n      chat: chatty\n"
                    "      utility: helper\n    validate:\n      answers: always\n"};
    REQUIRE(chat.run({"chat", "--suite", "checked", "--output-format", "stream-json"},
                     R"({"type":"user","text":"what is 17 x 23?"})"
                     "\n") == 0);
    INFO(chat.out);
    // Said as notices: events of a type the protocol already has.
    CHECK(chat.out.find("{\"text\":\"validate: utility (helper) objects to the answer -- \\\"17 x "
                        "23 is 391, not 381.\\\"\",\"type\":\"notice\"}") != std::string::npos);
    CHECK(chat.out.find("\"validate — asking utility (helper): Check an answer against the "
                        "question it answers.") != std::string::npos);
    const apogee::logger::Session session = HelperChat::only_session();
    REQUIRE(session.messages.size() == 2);
    CHECK(session.messages.back().content.plain_text() == "381");
}

TEST_CASE("answers always reaches complete, with tools or without", "[chat][cli][validate]") {
    HelperChat chat{texts({"381", "You're right: 391."}),
                    {"OBJECT: 17 x 23 is 391, not 381."},
                    {},
                    "suites:\n  checked:\n    members:\n      chat: chatty\n"
                    "      utility: helper\n    validate:\n      answers: always\n"};
    std::string text;
    {
        const std::ifstream in{chat.config_path, std::ios::binary};
        std::ostringstream read;
        read << in.rdbuf();
        text = read.str();
    }
    const std::string models = "models:\n  default: chatty\n";
    text.replace(text.find(models), models.size(), models + "  default_suite: checked\n");
    std::ofstream{chat.config_path, std::ios::binary} << text;
    REQUIRE(chat.run({"complete", "what is 17 x 23?"}) == 0);
    INFO(chat.err);
    CHECK(chat.out.find("381") != std::string::npos);
    CHECK((chat.out + chat.err)
              .find("validate: utility (helper) objects to the answer -- \"17 x 23 is 391, not "
                    "381.\"") != std::string::npos);
}
