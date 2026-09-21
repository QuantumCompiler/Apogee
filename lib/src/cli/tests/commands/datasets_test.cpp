#include "commands/datasets.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "commands/registry.h"
#include "commands/root.h"
#include "harness/assets.h"
#include "harness/config.h"
#include "harness/layout.h"
#include "harness/types.h"
#include "logger/session.h"
#include "support/env_guard.h"
#include "training/datasets.h"

/// `apogee datasets` on the scripted mock: create from the template, from
/// nothing and from the user's sessions with every filter; synth through a
/// teacher that must be named and must not be a vendor CLI, de-duplicating
/// and stopping at the bound, refusing an existing dataset; kits, list, info,
/// delete; prepare refusing on a pipe before the environment exists, naming
/// `apogee train setup`.
namespace {

using apogee::training::chat_line;

constexpr std::string_view kBatch =
    R"([{"prompt": "What is 15% of 240?", "completion": "36"},
        {"prompt": "what is 15% of 240?", "completion": "dup"},
        {"prompt": "If 3x = 21, what is x?", "completion": "7"}])";

void write(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << content;
}

std::string bytes(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::string script_of(std::string_view text, bool metered = false) {
    nlohmann::json script{{"turns", nlohmann::json::array({{{"text", std::string{text}}}})}};
    if (metered) {
        script["metered"] = true;
    }
    return script.dump();
}

struct Fixture {
    apogee::testing::TempDir home{"datasets-cli-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    std::filesystem::path config_path = home.path() / "config" / "config.yaml";

    Fixture() {
        write(home.path() / "teacher.json", script_of(kBatch));
        write(home.path() / "metered.json", script_of(kBatch, true));
        write(home.path() / "prose.json", script_of("I only speak prose."));
        std::string config = "models:\n  default: teacher\nbackends:\n";
        config += "  teacher:\n    type: mock\n    model_path: " +
                  (home.path() / "teacher.json").string() + "\n";
        config +=
            "  paid:\n    type: mock\n    model_path: " + (home.path() / "metered.json").string() +
            "\n";
        config +=
            "  prose:\n    type: mock\n    model_path: " + (home.path() / "prose.json").string() +
            "\n";
        config += "  vendor:\n    type: claude-cli\n";
        write(config_path, config);
        REQUIRE(apogee::harness::seed_data_directory(home.path()).ok());
    }

    int run(const std::vector<std::string>& args, std::string* out = nullptr,
            std::string* err = nullptr) const {
        std::ostringstream captured_out;
        std::ostringstream captured_err;
        std::istringstream fed{std::string{}};
        std::streambuf* old_out = std::cout.rdbuf(captured_out.rdbuf());
        std::streambuf* old_err = std::cerr.rdbuf(captured_err.rdbuf());
        std::streambuf* old_in = std::cin.rdbuf(fed.rdbuf());
        int code = -1;
        try {
            apogee::commands::RootCommand root{apogee::commands::default_registry()};
            std::vector<std::string> full{"--config", config_path.string()};
            full.insert(full.end(), args.begin(), args.end());
            std::vector<const char*> argv{"apogee"};
            for (const std::string& arg : full) {
                argv.push_back(arg.c_str());
            }
            code = root.run(static_cast<int>(argv.size()), argv.data());
        } catch (...) {
            std::cout.rdbuf(old_out);
            std::cerr.rdbuf(old_err);
            std::cin.rdbuf(old_in);
            throw;
        }
        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);
        std::cin.rdbuf(old_in);
        if (out != nullptr) {
            *out = captured_out.str();
        }
        if (err != nullptr) {
            *err = captured_err.str();
        }
        return code;
    }

    [[nodiscard]] std::filesystem::path dataset(std::string_view name) const {
        return home.path() / "training" / "datasets" / (std::string{name} + ".jsonl");
    }

    void save_session(std::string backend, std::string started_at,
                      std::vector<apogee::harness::ChatMessage> messages) const {
        apogee::logger::Session session;
        session.chat_id = apogee::logger::new_chat_id();
        session.backend = std::move(backend);
        session.started_at = std::move(started_at);
        session.messages = std::move(messages);
        apogee::logger::save(session);
    }
};

}  // namespace

TEST_CASE("datasets create scaffolds the template, refuses a duplicate, overwrites on --force",
          "[commands][datasets][create]") {
    const Fixture fixture;
    std::string out;
    std::string err;
    REQUIRE(fixture.run({"datasets", "create", "starter"}, &out, &err) == 0);
    CHECK(out.find("2 example(s)") != std::string::npos);
    const std::vector<std::string> expected = apogee::training::template_lines();
    CHECK(bytes(fixture.dataset("starter")) == expected[0] + "\n" + expected[1] + "\n");

    CHECK(fixture.run({"datasets", "create", "starter"}, &out, &err) == 1);
    CHECK(err.find("already exists") != std::string::npos);
    CHECK(err.find("--force") != std::string::npos);

    REQUIRE(fixture.run({"datasets", "create", "starter", "--from", "empty", "--force"}, &out,
                        &err) == 0);
    CHECK(bytes(fixture.dataset("starter")).empty());
    CHECK(out.find("0 example(s)") != std::string::npos);

    CHECK(fixture.run({"datasets", "create", "x", "--from", "chat-logs"}, &out, &err) == 1);
    CHECK(err.find("--from must be") != std::string::npos);
    CHECK(fixture.run({"datasets", "create", "../escape"}, &out, &err) == 1);
}

TEST_CASE("datasets create --from sessions mines completed exchanges with the filters",
          "[commands][datasets][sessions]") {
    using apogee::harness::ChatMessage;
    const Fixture fixture;
    fixture.save_session("teacher", "2026-09-19T10:00:00Z",
                         {ChatMessage::user("What is 2+2?"), ChatMessage::assistant("4"),
                          ChatMessage::user("unanswered")});
    fixture.save_session("paid", "2026-09-01T10:00:00Z",
                         {ChatMessage::user("Q"), ChatMessage::assistant("A")});
    std::string out;
    std::string err;
    REQUIRE(fixture.run({"datasets", "create", "mine", "--from", "sessions"}, &out, &err) == 0);
    CHECK(out.find("2 example(s) from 2 session(s), 1 exchange(s) skipped") != std::string::npos);
    const std::string content = bytes(fixture.dataset("mine"));
    CHECK(content.find(chat_line("What is 2+2?", "4")) != std::string::npos);
    CHECK(content.find(chat_line("Q", "A")) != std::string::npos);

    REQUIRE(
        fixture.run({"datasets", "create", "paid-only", "--from", "sessions", "--backend", "paid"},
                    &out, &err) == 0);
    CHECK(bytes(fixture.dataset("paid-only")) == chat_line("Q", "A") + "\n");

    REQUIRE(
        fixture.run({"datasets", "create", "recent", "--from", "sessions", "--since", "2026-09-10"},
                    &out, &err) == 0);
    CHECK(bytes(fixture.dataset("recent")) == chat_line("What is 2+2?", "4") + "\n");

    CHECK(fixture.run({"datasets", "create", "bad", "--from", "sessions", "--since", "soon"}, &out,
                      &err) == 1);
    CHECK(err.find("--since: invalid date") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(fixture.dataset("bad")));
}

TEST_CASE("datasets synth distils through a named teacher, de-duplicates, and stops at the bound",
          "[commands][datasets][synth]") {
    const Fixture fixture;
    std::string out;
    std::string err;
    REQUIRE(fixture.run({"datasets", "synth", "maths", "--teacher", "teacher", "--kit", "reasoning",
                         "--count", "2"},
                        &out, &err) == 0);
    INFO(err);
    CHECK(out.find("2 example(s)") != std::string::npos);
    CHECK(bytes(fixture.dataset("maths")) == chat_line("What is 15% of 240?", "36") + "\n" +
                                                 chat_line("If 3x = 21, what is x?", "7") + "\n");
    // The mock is not an API backend: one batch in flight.
    CHECK(err.find("1 batch(es) in flight") != std::string::npos);

    // The same teacher can only ever produce these two: asked for more, the
    // run ends at the call bound with what it got rather than spinning.
    REQUIRE(fixture.run({"datasets", "synth", "more", "--teacher", "teacher", "--kit", "reasoning",
                         "--count", "5"},
                        &out, &err) == 0);
    CHECK(out.find("2 example(s)") != std::string::npos);
    CHECK(out.find("batch(es) skipped") == std::string::npos);

    CHECK(fixture.run({"datasets", "synth", "maths", "--teacher", "teacher", "--kit", "reasoning"},
                      &out, &err) == 1);
    CHECK(err.find("already exists") != std::string::npos);
    REQUIRE(fixture.run({"datasets", "synth", "maths", "--teacher", "teacher", "--kit", "reasoning",
                         "--count", "1", "--force"},
                        &out, &err) == 0);
    CHECK(bytes(fixture.dataset("maths")) == chat_line("What is 15% of 240?", "36") + "\n");
}

TEST_CASE(
    "datasets synth refuses an unknown teacher, a vendor CLI, an unknown kit, and a "
    "teacher that never produces; a metered teacher is fine because it was named",
    "[commands][datasets][synth][policy]") {
    const Fixture fixture;
    std::string out;
    std::string err;
    CHECK(fixture.run({"datasets", "synth", "a", "--teacher", "nope", "--kit", "reasoning"}, &out,
                      &err) == 1);
    CHECK(err.find("'nope' is not a configured backend") != std::string::npos);

    CHECK(fixture.run({"datasets", "synth", "a", "--teacher", "vendor", "--kit", "reasoning"}, &out,
                      &err) == 1);
    CHECK(err.find("vendor-CLI backend") != std::string::npos);

    CHECK(fixture.run({"datasets", "synth", "a", "--teacher", "teacher", "--kit", "nokit"}, &out,
                      &err) == 1);
    CHECK(err.find("no kit named 'nokit'") != std::string::npos);

    // A kit that parses but cannot gate is refused by name, before any call.
    write(fixture.home.path() / "training" / "kits" / "noeval.yaml",
          "name: noeval\nsynth:\n  system: teach\neval: []\n");
    CHECK(fixture.run({"datasets", "synth", "a", "--teacher", "teacher", "--kit", "noeval"}, &out,
                      &err) == 1);
    CHECK(err.find("kit 'noeval': at least one eval item") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(fixture.dataset("a")));

    CHECK(fixture.run({"datasets", "synth", "a", "--teacher", "prose", "--kit", "reasoning",
                       "--count", "1"},
                      &out, &err) == 1);
    CHECK(err.find("no usable examples") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(fixture.dataset("a")));

    REQUIRE(fixture.run({"datasets", "synth", "a", "--teacher", "paid", "--kit", "reasoning",
                         "--count", "1"},
                        &out, &err) == 0);
    CHECK(std::filesystem::exists(fixture.dataset("a")));
}

TEST_CASE("datasets kits, list, info and delete", "[commands][datasets][listing]") {
    const Fixture fixture;
    std::string out;
    std::string err;
    REQUIRE(fixture.run({"datasets", "kits"}, &out, &err) == 0);
    CHECK(out.find("instruction-following") != std::string::npos);
    CHECK(out.find("reasoning") != std::string::npos);
    CHECK(out.find("structured-output") != std::string::npos);
    CHECK(out.find("summarization") != std::string::npos);
    CHECK(out.find("tool-use") == std::string::npos);

    REQUIRE(fixture.run({"datasets", "list"}, &out, &err) == 0);
    CHECK(out.find("no datasets") != std::string::npos);
    REQUIRE(fixture.run({"datasets", "create", "one"}, &out, &err) == 0);
    REQUIRE(fixture.run({"datasets", "list"}, &out, &err) == 0);
    CHECK(out.find("one") != std::string::npos);
    CHECK(out.find("chat") != std::string::npos);
    REQUIRE(fixture.run({"datasets", "info", "one"}, &out, &err) == 0);
    CHECK(out.find("lines:  2") != std::string::npos);
    CHECK(fixture.run({"datasets", "info", "two"}, &out, &err) == 1);

    REQUIRE(fixture.run({"datasets", "delete", "one"}, &out, &err) == 0);
    CHECK(out.find("--yes") != std::string::npos);
    CHECK(std::filesystem::exists(fixture.dataset("one")));
    REQUIRE(fixture.run({"datasets", "delete", "one", "-y"}, &out, &err) == 0);
    CHECK_FALSE(std::filesystem::exists(fixture.dataset("one")));
    CHECK(fixture.run({"datasets", "delete", "one", "-y"}, &out, &err) == 1);
}

TEST_CASE("datasets prepare refuses on a pipe until the environment exists, naming the command",
          "[commands][datasets][prepare][python]") {
    const Fixture fixture;
    write(fixture.home.path() / "raw.jsonl", "{\"prompt\": \"p\", \"completion\": \"c\"}\n");
    std::string out;
    std::string err;
    CHECK(fixture.run({"datasets", "prepare", (fixture.home.path() / "raw.jsonl").string()}, &out,
                      &err) == 1);
    CHECK(err.find("apogee train setup") != std::string::npos);
    CHECK(err.find("never touching the system Python") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(fixture.dataset("raw")));

    CHECK(fixture.run({"datasets", "prepare", (fixture.home.path() / "missing.csv").string()}, &out,
                      &err) == 1);
    CHECK(err.find("no such file") != std::string::npos);
}

TEST_CASE("the teacher resolution and the parallel rule", "[commands][datasets][teacher]") {
    apogee::harness::Config config = apogee::harness::parse_config(
        "backends:\n  api:\n    type: openai\n    api_key: k\n  local:\n    type: mock\n  cli:\n"
        "    type: gemini-cli\n",
        "<test>");
    apogee::commands::TeacherResolution api = apogee::commands::resolve_teacher(config, "api");
    CHECK(api.key == "api");
    CHECK(api.parallel_safe);
    CHECK(apogee::commands::effective_parallel(api, 4) == 4);
    CHECK(apogee::commands::effective_parallel(api, 0) == 1);
    CHECK(apogee::commands::effective_parallel(api, 99) == 16);
    apogee::commands::TeacherResolution local = apogee::commands::resolve_teacher(config, "local");
    CHECK(local.key == "local");
    CHECK_FALSE(local.parallel_safe);
    CHECK(apogee::commands::effective_parallel(local, 4) == 1);
    CHECK(apogee::commands::resolve_teacher(config, "cli").key.empty());
    CHECK(apogee::commands::resolve_teacher(config, "").error.find("--teacher") !=
          std::string::npos);
}
