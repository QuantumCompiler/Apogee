#include "httpserver/session.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <random>
#include <string>

#include "harness/types.h"
#include "logger/session.h"
#include "support/env_guard.h"

/// The session store: memory that ages, files that do not.
namespace {

using apogee::harness::ChatMessage;
using apogee::httpserver::SessionStore;

struct Fixture {
    // The label carries a random suffix: ctest may run two of these test
    // processes at once, and the support TempDir's counter is per-process.
    apogee::testing::TempDir home{"serve-sessions-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    SessionStore store{[this] { return now; }};
};

}  // namespace

TEST_CASE("a minted session is a chat session on disk from birth", "[httpserver][session]") {
    Fixture fixture;
    apogee::logger::InferenceParams params;
    params.system_prompt = "be brief";
    const std::string id = fixture.store.create("mock", params);

    REQUIRE_FALSE(id.empty());
    CHECK(std::filesystem::exists(apogee::logger::session_path(id)));
    const auto live = fixture.store.get(id);
    REQUIRE(live.has_value());
    CHECK(live->backend == "mock");
    CHECK(live->chat_id == id);
    REQUIRE(live->messages.size() == 1);
    CHECK(live->messages[0].role == apogee::harness::Role::System);
    CHECK(fixture.store.size() == 1);

    // And `apogee chat --resume <id>` would find exactly this.
    const apogee::logger::LoadedSession loaded = apogee::logger::load(id, {});
    CHECK(loaded.session.backend == "mock");
    CHECK(loaded.session.params.system_prompt == "be brief");
}

TEST_CASE("a committed turn is persisted before the live set changes", "[httpserver][session]") {
    Fixture fixture;
    const std::string id = fixture.store.create("mock", {});
    apogee::logger::Session session = *fixture.store.get(id);
    session.messages.push_back(ChatMessage::user("one"));
    session.messages.push_back(ChatMessage::assistant("two"));
    session.turns = 1;
    fixture.store.commit(session);

    CHECK(fixture.store.get(id)->turns == 1);
    const apogee::logger::LoadedSession loaded = apogee::logger::load(id, {});
    CHECK(loaded.session.turns == 1);
    REQUIRE(loaded.session.messages.size() == 2);
    CHECK(loaded.session.messages[1].content.plain_text() == "two");
    REQUIRE(fixture.store.list().size() == 1);
    CHECK(fixture.store.list()[0].turns == 1);
}

TEST_CASE("idle sessions leave memory on their TTL and stay on disk", "[httpserver][session]") {
    Fixture fixture;
    const std::string old = fixture.store.create("mock", {});
    fixture.now += std::chrono::minutes{30};
    const std::string fresh = fixture.store.create("mock", {});
    fixture.now += std::chrono::minutes{31};

    // The first is 61 minutes idle, the second 31.
    CHECK(fixture.store.evict_idle(std::chrono::minutes{60}) == 1);
    CHECK_FALSE(fixture.store.get(old).has_value());
    CHECK(fixture.store.get(fresh).has_value());
    CHECK(std::filesystem::exists(apogee::logger::session_path(old)));

    // A zero TTL keeps everything.
    fixture.now += std::chrono::hours{100};
    CHECK(fixture.store.evict_idle(std::chrono::minutes{0}) == 0);
    CHECK(fixture.store.get(fresh).has_value());

    // A session in use is not idle: committing revives one that was evicted
    // while its turn ran.
    apogee::logger::Session revived = *fixture.store.get(fresh);
    CHECK(fixture.store.evict_idle(std::chrono::minutes{60}) == 1);
    CHECK(fixture.store.size() == 0);
    fixture.store.commit(revived);
    CHECK(fixture.store.get(fresh).has_value());
}

TEST_CASE("ending a session removes it from memory only", "[httpserver][session]") {
    Fixture fixture;
    const std::string id = fixture.store.create("mock", {});
    CHECK(fixture.store.erase(id));
    CHECK_FALSE(fixture.store.erase(id));
    CHECK_FALSE(fixture.store.get(id).has_value());
    CHECK(std::filesystem::exists(apogee::logger::session_path(id)));
}

TEST_CASE("timestamps are RFC 3339 in UTC", "[httpserver][session]") {
    const std::chrono::system_clock::time_point epoch{};
    CHECK(apogee::httpserver::format_utc(epoch) == "1970-01-01T00:00:00Z");
}
