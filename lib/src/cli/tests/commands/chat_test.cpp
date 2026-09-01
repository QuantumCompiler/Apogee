#include "commands/chat.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <string>

#include "backends/mock.h"
#include "commands/chat_history.h"
#include "harness/config.h"
#include "logger/operational.h"
#include "support/env_guard.h"

using apogee::commands::ContextUsage;
using apogee::commands::format_session_info;
using apogee::commands::format_session_row;
using apogee::commands::measure_context;
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
// Context monitoring
// ---------------------------------------------------------------------------

TEST_CASE("the 80 and 90 percent thresholds fire in order", "[chat][context]") {
    ContextUsage usage;
    usage.window = 1000;

    usage.used_tokens = 700;
    CHECK_FALSE(usage.should_warn());
    CHECK_FALSE(usage.should_compact());

    usage.used_tokens = 800;
    CHECK(usage.should_warn());
    CHECK_FALSE(usage.should_compact());

    usage.used_tokens = 899;
    CHECK(usage.should_warn());
    CHECK_FALSE(usage.should_compact());

    usage.used_tokens = 900;
    CHECK(usage.should_warn());
    CHECK(usage.should_compact());
}

TEST_CASE("an unknown window never triggers either threshold", "[chat][context]") {
    // 0 means unknown, not full. Warning on every turn for a model whose window
    // we cannot resolve would train the user to ignore the warning.
    ContextUsage usage;
    usage.window = 0;
    usage.used_tokens = 1'000'000;

    CHECK(usage.fraction() == 0.0);
    CHECK_FALSE(usage.should_warn());
    CHECK_FALSE(usage.should_compact());
}

TEST_CASE("context is measured against the message about to be sent", "[chat][context]") {
    // The ordering that actually matters. Measuring the SAVED history alone
    // means the first turn always reads as empty and a single large prompt
    // never trips the threshold it should -- which is exactly what shipped
    // before this test existed.
    const Config config = apogee::harness::parse_config(R"(
backends:
  small:
    type: mock
    model: mock-1
    context_size: 50
)",
                                                        "<test>");
    Harness harness{config};
    harness.register_provider("small", std::make_shared<apogee::backends::MockProvider>(
                                           apogee::backends::MockProvider::Options{}));
    harness.use_default_router();

    // An empty history is not close to full...
    CHECK_FALSE(measure_context(harness, {}, "small").should_compact());

    // ...but the history PLUS a large incoming message is.
    const std::vector<ChatMessage> prospective{ChatMessage::user(std::string(400, 'x'))};
    CHECK(measure_context(harness, prospective, "small").should_compact());
}

TEST_CASE("measure_context resolves the window and flags estimates", "[chat][context]") {
    // A warning that fires at the wrong point is worse than none, so whether
    // the number is exact or estimated is carried rather than smoothed over.
    const Config config = apogee::harness::parse_config(R"(
backends:
  small:
    type: mock
    model: mock-1
    context_size: 100
)",
                                                        "<test>");
    Harness harness{config};
    harness.register_provider("small", std::make_shared<apogee::backends::MockProvider>(
                                           apogee::backends::MockProvider::Options{}));
    harness.use_default_router();

    const std::vector<ChatMessage> messages{ChatMessage::user(std::string(400, 'x'))};
    const ContextUsage usage = measure_context(harness, messages, "small");

    CHECK(usage.window == 100);
    CHECK(usage.used_tokens > 0);
    CHECK_FALSE(usage.exact);  // no provider counting API is wired yet
    CHECK(usage.should_compact());
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
