#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "backends/model_roster.h"
#include "support/cli_home.h"

/// Roster model switching (33): `/model` takes what `-m` takes -- a vendor
/// roster's model pinned on its sole configured owner for the rest of the
/// chat, restored when the chat leaves it, saved with the chat and pinned
/// again on a resume -- driven through `apogee chat` in process on mock
/// entries whose answers say the model they run (`{{model}}`), with the
/// config file never touched.
namespace {

using apogee::testing::CliHome;

/// Two mock entries, `main` first by key -- so the mock roster's owner -- and
/// `other`, each answering with the model it runs; `backends` adds entries,
/// `extra` top-level sections.
std::string config_with(const std::filesystem::path& scripts, const std::string& backends,
                        const std::string& extra) {
    std::filesystem::create_directories(scripts);
    std::ofstream{scripts / "main.json"}
        << R"({"turns": [{"text": "M[{{model}}]<{{last_user}}>"}]})";
    std::ofstream{scripts / "other.json"}
        << R"({"turns": [{"text": "O[{{model}}]<{{last_user}}>"}]})";
    return "backends:\n"
           "  main:\n    type: mock\n    model: main-own\n    model_path: " +
           (scripts / "main.json").generic_string() +
           "\n  other:\n    type: mock\n    model_path: " +
           (scripts / "other.json").generic_string() + "\n" + backends +
           "models:\n  default: main\nmemory:\n  recall: false\n" + extra;
}

/// A throwaway install of that config, with the mock type's roster cached.
struct Home {
    CliHome home{std::string{}};
    std::string config;

    explicit Home(const std::string& backends = {}, const std::string& extra = {}) {
        config = config_with(home.home() / "scripts", backends, extra);
        std::ofstream{home.config_path(), std::ios::binary | std::ios::trunc} << config;
        apogee::backends::RosterCache cache;
        cache.rosters["mock"] = apogee::backends::ProviderRoster{
            {{"mock-pro", "Pro"}, {"mock-mini", "Mini"}}, "2026-10-09"};
        REQUIRE(apogee::backends::save_roster_cache(cache).empty());
    }

    struct Run {
        int code = 0;
        std::string out;
        std::string err;
    };

    /// `apogee <args>` with `input` on stdin.
    [[nodiscard]] Run run(const std::vector<std::string>& args, const std::string& input) const {
        const std::istringstream fed{input};
        std::streambuf* old_in = std::cin.rdbuf(fed.rdbuf());
        Run ran;
        ran.code = home.run(args, &ran.out, &ran.err);
        std::cin.rdbuf(old_in);
        std::cin.clear();
        return ran;
    }
};

[[nodiscard]] bool has(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

/// Whether `text` holds `line` as a whole line.
[[nodiscard]] bool has_line(const std::string& text, const std::string& line) {
    std::istringstream in{text};
    for (std::string each; std::getline(in, each);) {
        if (each == line) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE(
    "/model switches onto a roster model on its owner's entry, and back, the config untouched",
    "[chat][roster]") {
    const Home home;
    const Home::Run ran = home.run(
        {"chat"}, "hello\n/model mock-pro\nagain\n/model\n/models\n/model main\nthird\n/exit\n");
    INFO(ran.err);
    REQUIRE(ran.code == 0);
    // The entry's own model, the pinned one, then its own again.
    CHECK(has(ran.out, "M[main-own]<hello>\nM[mock-pro]<again>\nM[main-own]<third>\n"));
    CHECK(has(ran.err, "[apogee] switched to mock-pro -- mock's roster, on backend 'main'\n"));
    CHECK(has_line(ran.err, "main (mock-pro)"));  // /model names the pin
    CHECK(has(ran.err, "* main (mock-pro)\n  other\n"));
    CHECK(has(ran.err,
              "  mock's roster: 2 models (fetched 2026-10-09) -- /model <id> runs one; Tab lists "
              "them\n"));
    CHECK(has(ran.err, "[apogee] switched to main\n"));
    CHECK(home.home.config_text() == home.config);
}

TEST_CASE(
    "leaving a pinned entry for another restores it, and a resume pins the chat's model again",
    "[chat][roster]") {
    const Home home;
    // Pinned, then away to `other`: `main` runs its own model again there.
    const Home::Run away =
        home.run({"chat"}, "/model mock-pro\none\n/model other\ntwo\n/model main\nthree\n");
    REQUIRE(away.code == 0);
    CHECK(has(away.out, "M[mock-pro]<one>\nO[mock-1]<two>\nM[main-own]<three>\n"));
    CHECK(home.home.config_text() == home.config);

    // Left on a roster model -- in a home of its own, so `-c` has one chat to
    // continue: the next resume opens on it, said as -m says it.
    const Home fresh;
    const Home::Run left = fresh.run({"chat"}, "/model mock-mini\nfour\n");
    REQUIRE(left.code == 0);
    const Home::Run resumed = fresh.run({"chat", "-c"}, "/model\nfive\n");
    INFO(resumed.err);
    REQUIRE(resumed.code == 0);
    CHECK(has_line(resumed.err, "model 'mock-mini' -- mock's roster, on backend 'main'"));
    CHECK(has_line(resumed.err, "main (mock-mini)"));
    CHECK(has(resumed.out, "M[mock-mini]<five>\n"));

    // -m on the resume wins, and the chat is saved on it.
    const Home::Run named = fresh.run({"chat", "-c", "-m", "main"}, "six\n");
    REQUIRE(named.code == 0);
    CHECK(has(named.out, "M[main-own]<six>\n"));
    CHECK_FALSE(has(named.err, "mock's roster"));
    const Home::Run after = fresh.run({"chat", "-c"}, "/model\n");
    CHECK(has_line(after.err, "main"));
    CHECK_FALSE(has(after.err, "mock's roster"));
    CHECK(fresh.home.config_text() == fresh.config);
}

TEST_CASE("a roster model named with -m is saved with the chat, as /model's pin is",
          "[chat][roster]") {
    const Home home;
    const Home::Run launched = home.run({"chat", "-m", "mock-pro"}, "hi\n/model\n");
    INFO(launched.err);
    REQUIRE(launched.code == 0);
    CHECK(has(launched.err, "model 'mock-pro' -- mock's roster, on backend 'main'\n"));
    CHECK(has(launched.out, "M[mock-pro]<hi>\n"));
    CHECK(has_line(launched.err, "main (mock-pro)"));
    const Home::Run resumed = home.run({"chat", "-c"}, "again\n");
    CHECK(has(resumed.out, "M[mock-pro]<again>\n"));
}

TEST_CASE("/model refuses a roster model two configured types list, and a name nothing defines",
          "[chat][roster]") {
    // An anthropic entry with no key: configured, never built -- its roster
    // still owns what it lists.
    const Home home{"  claude:\n    type: anthropic\n"};
    apogee::backends::RosterCache cache = apogee::backends::load_roster_cache();
    cache.rosters["anthropic"] =
        apogee::backends::ProviderRoster{{{"mock-pro", "Pro"}}, "2026-10-09"};
    REQUIRE(apogee::backends::save_roster_cache(cache).empty());
    const Home::Run ran =
        home.run({"chat"}, "/model mock-pro\n/model nothing\n/model mock-mini\nhi\n");
    INFO(ran.err);
    REQUIRE(ran.code == 0);
    CHECK(has(ran.err,
              "'mock-pro' is on anthropic and mock's rosters -- pin it to one entry with "
              "'apogee config add-backend <name> --type <type> --model mock-pro'\n"));
    CHECK(has(ran.err, "no backend named 'nothing'\n"));
    CHECK(has(ran.out, "M[mock-mini]<hi>\n"));  // the sole owner's still runs
}

TEST_CASE("a suite that moves the chat off a pinned entry restores it", "[chat][roster][suites]") {
    const Home home{{}, "suites:\n  away:\n    members:\n      chat: other\n"};
    const Home::Run ran = home.run(
        {"chat"}, "/model mock-pro\none\n/suite away\ntwo\n/suite off\n/model main\nthree\n");
    INFO(ran.err);
    REQUIRE(ran.code == 0);
    // Restored by the suite's move, not by the /model after it: that /model
    // has no pin left to undo.
    CHECK(has(ran.out, "M[mock-pro]<one>\nO[mock-1]<two>\nM[main-own]<three>\n"));
}

TEST_CASE("/model <backend>:<model> pins any model on the named entry, and -m takes it too",
          "[chat][roster][pins]") {
    // 34: a vendor CLI lists no models; the model is the user's words, handed
    // to the entry as its own.
    const Home home{"  local:\n    type: llamacpp\n    model_path: /nowhere/model.gguf\n"};
    const Home::Run ran = home.run({"chat"},
                                   "/model other:custom-x\none\n/model\n/model local:anything\n"
                                   "/model other\ntwo\n/model other:custom-y\n");
    INFO(ran.err);
    REQUIRE(ran.code == 0);
    CHECK(has(ran.out, "O[custom-x]<one>\nO[mock-1]<two>\n"));
    CHECK(has_line(ran.err, "[apogee] switched to custom-x -- on backend 'other'"));
    CHECK(has_line(ran.err, "other (custom-x)"));
    CHECK(has(ran.err,
              "'local' runs the weights at its model_path -- a model is pinned by name "
              "only on an API or vendor-CLI backend"));

    // Left on it, the chat reopens on it, said as the pin was made.
    const Home::Run resumed = home.run({"chat", "-c"}, "three\n");
    CHECK(has_line(resumed.err, "model 'custom-y' -- on backend 'other'"));
    CHECK(has(resumed.out, "O[custom-y]<three>\n"));

    const Home fresh;
    const Home::Run launched = fresh.run({"chat", "-m", "main:custom-z"}, "hi\n");
    REQUIRE(launched.code == 0);
    CHECK(has_line(launched.err, "model 'custom-z' -- on backend 'main'"));
    CHECK(has(launched.out, "M[custom-z]<hi>\n"));
    CHECK(home.home.config_text() == home.config);
    CHECK(fresh.home.config_text() == fresh.config);
}
