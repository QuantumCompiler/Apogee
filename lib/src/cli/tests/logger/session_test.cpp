#include "logger/session.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <string>

#include "harness/config_edit.h"
#include "harness/types.h"
#include "support/env_guard.h"

using apogee::harness::ChatMessage;
using apogee::harness::Role;
using apogee::harness::ToolCall;
using apogee::harness::ToolResult;
using apogee::logger::deserialize;
using apogee::logger::KnownDependencies;
using apogee::logger::Session;
using apogee::logger::WarningKind;
using apogee::testing::EnvGuard;
using apogee::testing::TempDir;

namespace {

Session sample() {
    Session session;
    session.chat_id = "20260826-120000-abcd";
    session.backend = "claude";
    session.started_at = "2026-08-26T12:00:00Z";
    session.updated_at = "2026-08-26T12:05:00Z";
    session.turns = 2;
    session.params.temperature = 0.4;
    session.params.max_tokens = 512;
    session.messages = {ChatMessage::user("hello"), ChatMessage::assistant("hi there")};
    return session;
}

}  // namespace

TEST_CASE("a session round-trips through serialize/deserialize", "[chat][session]") {
    const Session original = sample();
    const auto loaded = deserialize(apogee::logger::serialize(original), {});

    CHECK(loaded.session.chat_id == original.chat_id);
    CHECK(loaded.session.backend == original.backend);
    CHECK(loaded.session.turns == original.turns);
    CHECK(loaded.session.params.temperature == 0.4);
    CHECK(loaded.session.params.max_tokens == 512);
    REQUIRE(loaded.session.messages.size() == 2);
    CHECK(loaded.session.messages[0].content.plain_text() == "hello");
    CHECK(loaded.session.messages[1].role == Role::Assistant);
    CHECK(loaded.warnings.empty());
}

TEST_CASE("every written session carries the current schema version", "[chat][session]") {
    const auto json = nlohmann::json::parse(apogee::logger::serialize(sample()));
    CHECK(json.at("schema_version") == apogee::logger::kCurrentSchemaVersion);
}

TEST_CASE("a legacy session resumes best-effort with a warning", "[chat][session][resume]") {
    // Refusing would make a schema bump destroy conversations. It resumes, and
    // says so.
    constexpr std::string_view legacy = R"({
        "chat_id": "old-chat",
        "backend": "claude",
        "messages": [{"role":"user","content":"from before"}]
    })";

    const auto loaded = deserialize(legacy, {});

    CHECK(loaded.session.chat_id == "old-chat");
    CHECK(loaded.session.schema_version == 0);
    REQUIRE(loaded.session.messages.size() == 1);
    REQUIRE(loaded.warnings.size() == 1);
    CHECK(loaded.warnings[0].kind == WarningKind::SchemaLegacy);
    CHECK(loaded.warnings[0].message.find("best-effort") != std::string::npos);
}

TEST_CASE("a vanished backend is a warning, not a failure", "[chat][session][resume]") {
    // A conversation that cannot be reopened because one config key moved is
    // worse than one that reopens with a note.
    Session session = sample();
    session.backend = "removed-backend";

    KnownDependencies known;
    known.backends = {"claude", "mock"};

    const auto loaded = deserialize(apogee::logger::serialize(session), known);

    REQUIRE_FALSE(loaded.warnings.empty());
    const auto& warning = loaded.warnings.front();
    CHECK(warning.kind == WarningKind::BackendMissing);
    CHECK(warning.subject == "removed-backend");
    // And the session is intact and usable.
    CHECK(loaded.session.messages.size() == 2);
}

TEST_CASE("an empty known list disables backend checking", "[chat][session][resume]") {
    Session session = sample();
    session.backend = "anything";
    CHECK(deserialize(apogee::logger::serialize(session), {}).warnings.empty());
}

TEST_CASE("a field of the wrong shape is dropped with a warning", "[chat][session][resume]") {
    // Degrade, never fail.
    constexpr std::string_view odd = R"({
        "schema_version": 1, "chat_id": "c", "backend": 42,
        "messages": []
    })";
    const auto loaded = deserialize(odd, {});

    CHECK(loaded.session.chat_id == "c");
    CHECK(loaded.session.backend.empty());
    REQUIRE_FALSE(loaded.warnings.empty());
    CHECK(loaded.warnings[0].kind == WarningKind::FieldDropped);
}

TEST_CASE("one unreadable message does not cost the conversation", "[chat][session][resume]") {
    constexpr std::string_view mixed = R"({
        "schema_version": 1, "chat_id": "c",
        "messages": [
            {"role":"user","content":"kept"},
            {"role":"wizard","content":"bad role"},
            {"role":"assistant","content":"also kept"}
        ]
    })";
    const auto loaded = deserialize(mixed, {});

    REQUIRE(loaded.session.messages.size() == 2);
    CHECK(loaded.session.messages[0].content.plain_text() == "kept");
    CHECK(loaded.session.messages[1].content.plain_text() == "also kept");
    CHECK_FALSE(loaded.warnings.empty());
}

TEST_CASE("unparseable JSON is the one hard failure", "[chat][session]") {
    CHECK_THROWS_AS(deserialize("{not json", {}), std::runtime_error);
}

TEST_CASE("tool calls and results survive a round trip", "[chat][session]") {
    // Needed for a faithful resume: without them the model sees an assistant
    // turn whose tool calls were never answered, which several providers reject.
    Session session = sample();
    ChatMessage assistant = ChatMessage::assistant("");
    assistant.tool_calls = {ToolCall{"c1", "search", R"({"q":"x"})"}};
    session.messages.push_back(assistant);
    session.messages.push_back(
        ChatMessage::from_tool_result(ToolResult{"c1", "search", "3 results", false}));

    const auto loaded = deserialize(apogee::logger::serialize(session), {});

    REQUIRE(loaded.session.messages.size() == 4);
    REQUIRE(loaded.session.messages[2].tool_calls.size() == 1);
    CHECK(loaded.session.messages[2].tool_calls[0].id == "c1");
    CHECK(loaded.session.messages[3].role == Role::Tool);
    CHECK(loaded.session.messages[3].tool_call_id == "c1");
}

TEST_CASE("a persisted session contains no thinking", "[chat][session][cleanliness]") {
    // The cleanliness constraint, locked from this end. It holds because the
    // loop never puts thinking into history in the first place -- this asserts
    // nothing downstream reintroduces it.
    Session session = sample();
    session.messages.push_back(ChatMessage::assistant("the visible answer"));

    const std::string text = apogee::logger::serialize(session);
    CHECK(text.find("thinking") == std::string::npos);
    CHECK(text.find("reasoning") == std::string::npos);
    CHECK(text.find("the visible answer") != std::string::npos);
}

TEST_CASE("display_name prefers custom name, then title, then id", "[chat][session]") {
    Session session;
    session.chat_id = "the-id";
    CHECK(session.display_name() == "the-id");

    session.title = "A Title";
    CHECK(session.display_name() == "A Title");

    session.custom_name = "My Name";
    CHECK(session.display_name() == "My Name");
}

TEST_CASE("chat ids are unique and sort chronologically", "[chat][session]") {
    const std::string a = apogee::logger::new_chat_id();
    const std::string b = apogee::logger::new_chat_id();
    CHECK(a != b);
    // The date prefix makes a directory listing sort by time.
    CHECK(a.size() > 8);
    CHECK(a.substr(0, 8) == b.substr(0, 8));
}

// ---------------------------------------------------------------------------
// The filesystem paths
// ---------------------------------------------------------------------------

TEST_CASE("save then load round-trips through the filesystem", "[chat][session]") {
    const TempDir dir{"session"};
    const EnvGuard home{"APOGEE_HOME", dir.path().string()};

    const Session original = sample();
    apogee::logger::save(original);

    const auto loaded = apogee::logger::load(original.chat_id, {});
    CHECK(loaded.session.chat_id == original.chat_id);
    CHECK(loaded.session.messages.size() == 2);
    // save() stamps the write time.
    CHECK_FALSE(loaded.session.updated_at.empty());
}

TEST_CASE("a session is loadable by its custom name", "[chat][session]") {
    const TempDir dir{"session-name"};
    const EnvGuard home{"APOGEE_HOME", dir.path().string()};

    Session session = sample();
    session.custom_name = "my project";
    apogee::logger::save(session);

    CHECK(apogee::logger::load("my project", {}).session.chat_id == session.chat_id);
    CHECK_THROWS_AS(apogee::logger::load("no such chat", {}), std::runtime_error);
}

TEST_CASE("listing is newest first and skips corrupt files", "[chat][session]") {
    // One bad file must not make `apogee chats list` unusable.
    const TempDir dir{"session-list"};
    const EnvGuard home{"APOGEE_HOME", dir.path().string()};

    Session older = sample();
    older.chat_id = "older";
    older.updated_at = "2026-08-01T00:00:00Z";
    apogee::logger::save(older);

    Session newer = sample();
    newer.chat_id = "newer";
    apogee::logger::save(newer);

    apogee::harness::write_file_atomically(apogee::logger::session_path("broken"), "{not json");

    const auto sessions = apogee::logger::list_sessions();
    REQUIRE(sessions.size() == 2);
    CHECK(sessions.front().chat_id == "newer");
    CHECK(apogee::logger::most_recent()->chat_id == "newer");
}

TEST_CASE("most_recent is empty when nothing is saved", "[chat][session]") {
    const TempDir dir{"session-empty"};
    const EnvGuard home{"APOGEE_HOME", dir.path().string()};
    CHECK(apogee::logger::list_sessions().empty());
    CHECK_FALSE(apogee::logger::most_recent().has_value());
}

TEST_CASE("a partially written session cannot replace a good one", "[chat][session][crash]") {
    // Crash safety: save() goes through temp-file-then-rename, so an
    // interrupted write leaves the previous session intact rather than a
    // truncated file that will not parse.
    const TempDir dir{"session-atomic"};
    const EnvGuard home{"APOGEE_HOME", dir.path().string()};

    Session session = sample();
    apogee::logger::save(session);

    session.messages.push_back(ChatMessage::user("a later turn"));
    apogee::logger::save(session);

    // No temp files left behind.
    int files = 0;
    for (const auto& entry : std::filesystem::directory_iterator{apogee::logger::sessions_dir()}) {
        ++files;
        CHECK(entry.path().extension() == ".json");
    }
    CHECK(files == 1);
    CHECK(apogee::logger::load(session.chat_id, {}).session.messages.size() == 3);
}

TEST_CASE("retriever and rerank settings round-trip and default to empty",
          "[logger][session][retrieval]") {
    // The SETTING, never a per-turn resolution: what the user chose is what a
    // resumed session continues with.
    apogee::logger::Session session;
    session.chat_id = "abc";
    session.backend = "mock";
    session.retriever = "lexical";
    session.rerank = "haiku";
    const std::string text = apogee::logger::serialize(session);
    const auto json = nlohmann::json::parse(text);
    CHECK(json.at("retriever") == "lexical");
    CHECK(json.at("rerank") == "haiku");

    const auto loaded = apogee::logger::deserialize(text, {{"mock", "haiku"}});
    CHECK(loaded.session.retriever == "lexical");
    CHECK(loaded.session.rerank == "haiku");
    CHECK(loaded.warnings.empty());

    // Unset settings are omitted, not written as empty strings, so an older
    // reader sees nothing new and a newer one reads auto.
    apogee::logger::Session plain;
    plain.chat_id = "p";
    plain.backend = "mock";
    const std::string plain_text = apogee::logger::serialize(plain);
    CHECK(plain_text.find("retriever") == std::string::npos);
    CHECK(apogee::logger::deserialize(plain_text, {{"mock"}}).session.retriever.empty());
}

TEST_CASE("a rerank judge that has since been deleted is dropped with a warning, never fatal",
          "[logger][session][retrieval]") {
    apogee::logger::Session session;
    session.chat_id = "abc";
    session.backend = "mock";
    session.rerank = "vanished";
    const auto loaded = apogee::logger::deserialize(apogee::logger::serialize(session), {{"mock"}});
    CHECK(loaded.session.rerank.empty());
    REQUIRE(loaded.warnings.size() == 1);
    CHECK(loaded.warnings.front().kind == apogee::logger::WarningKind::RerankBackendMissing);
    CHECK(loaded.warnings.front().message.find("vanished") != std::string::npos);

    // `off` is a setting, not a backend, and is never checked against the list.
    session.rerank = "off";
    CHECK(apogee::logger::deserialize(apogee::logger::serialize(session), {{"mock"}})
              .warnings.empty());
}
