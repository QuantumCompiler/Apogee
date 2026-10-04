#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "platform/child_process.h"
#include "support/env_guard.h"
#include "training/script_runner.h"

/// The shipped `prepare_dataset.py`, run under whatever `python3` the host
/// has (a test may; the product never does): every preset over a
/// standard-library file, auto-detection, `--map`, `--as-eval`, `--flat`,
/// the no-match refusal that lists the columns, and the terminal record.
/// Skipped by name where there is no python3. Plus the one static rule:
/// the environment guards precede the first ML import.
namespace {

using apogee::training::run_script;
using apogee::training::ScriptEvent;
using apogee::training::ScriptOutcome;
using apogee::training::ScriptRequest;

const std::filesystem::path kScript =
    std::filesystem::path{APOGEE_ASSETS_DIR} / "training" / "prepare_dataset.py";

struct Run {
    ScriptOutcome outcome;
    std::vector<std::string> messages;
    nlohmann::json record;
};

std::string python3() {
    return apogee::platform::find_on_path("python3");
}

Run prepare(const std::filesystem::path& source, const std::filesystem::path& out,
            std::vector<std::string> extra = {}) {
    ScriptRequest request;
    request.interpreter = python3();
    request.script = kScript;
    request.arguments = {"--source", source.string(), "--out", out.string()};
    request.arguments.insert(request.arguments.end(), extra.begin(), extra.end());
    Run run;
    run.outcome = run_script(request,
                             [&run](const ScriptEvent& event) {
                                 if (event.kind == ScriptEvent::Kind::Record) {
                                     run.record = event.record;
                                 } else {
                                     run.messages.push_back(event.text);
                                 }
                             },
                             {});
    return run;
}

std::vector<std::string> lines_of(const std::filesystem::path& path) {
    std::ifstream in{path};
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    return lines;
}

}  // namespace

TEST_CASE("the environment guards precede the first ML import", "[training][scripts][guards]") {
    std::ifstream in{kScript};
    REQUIRE(in.good());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::size_t offline = text.find("os.environ[\"HF_HUB_OFFLINE\"] = \"1\"");
    const std::size_t tokenizers = text.find("os.environ.setdefault(\"TOKENIZERS_PARALLELISM\"");
    const std::size_t ml_import = text.find("from datasets import");
    REQUIRE(offline != std::string::npos);
    REQUIRE(tokenizers != std::string::npos);
    REQUIRE(ml_import != std::string::npos);
    CHECK(offline < ml_import);
    CHECK(tokenizers < ml_import);
    CHECK(text.find("os.environ[\"HF_DATASETS_OFFLINE\"] = \"1\"") < ml_import);
    CHECK(text.find("os.environ[\"HF_HUB_DISABLE_TELEMETRY\"] = \"1\"") < ml_import);
}

TEST_CASE("prepare_dataset.py converts every standard-library format",
          "[training][scripts][python]") {
    // Both halves matter. python3 can be on PATH -- it is on the Windows
    // runner images -- while the platform still cannot start it: child_process
    // has no Windows implementation, so the spawn fails with "this build
    // cannot spawn child processes". Checking only the interpreter let these
    // three cases run and fail there (2026-09-22). supports_child_processes()
    // is the same guard tools/shell_test.cpp uses.
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    if (python3().empty()) {
        SKIP("no python3 on PATH");
    }
    const apogee::testing::TempDir dir{"prepare-" + std::to_string(std::random_device{}())};

    SECTION("prompt-completion JSONL, auto-detected, to chat lines") {
        std::ofstream{dir.path() / "pc.jsonl"}
            << R"({"prompt": "hi", "completion": "hello"})" << "\n\n"
            << R"({"prompt": "", "completion": "dropped"})" << "\n";
        const Run run = prepare(dir.path() / "pc.jsonl", dir.path() / "out.jsonl");
        INFO(run.outcome.describe());
        REQUIRE(run.outcome.ok);
        CHECK(run.record["rows_written"] == 1);
        CHECK(run.record["rows_skipped"] == 1);
        CHECK(run.record["out"] == (dir.path() / "out.jsonl").string());
        const std::vector<std::string> lines = lines_of(dir.path() / "out.jsonl");
        REQUIRE(lines.size() == 1);
        CHECK(
            lines[0] ==
            R"({"messages": [{"role": "user", "content": "hi"}, {"role": "assistant", "content": "hello"}]})");
        bool detected = false;
        for (const std::string& message : run.messages) {
            detected = detected || message.find("Auto-detected format: 'prompt-completion'") !=
                                       std::string::npos;
        }
        CHECK(detected);
    }

    SECTION("alpaca CSV with --as-eval and --flat") {
        std::ofstream{dir.path() / "alp.csv"}
            << "instruction,input,output\nSay hi,,hello\nAdd,2 and 3,5\n";
        Run run = prepare(dir.path() / "alp.csv", dir.path() / "eval.jsonl", {"--as-eval"});
        REQUIRE(run.outcome.ok);
        std::vector<std::string> lines = lines_of(dir.path() / "eval.jsonl");
        REQUIRE(lines.size() == 2);
        CHECK(lines[0] == R"({"prompt": "Say hi", "expected": "hello"})");
        CHECK(lines[1] == R"({"prompt": "Add\n\n2 and 3", "expected": "5"})");
        run = prepare(dir.path() / "alp.csv", dir.path() / "flat.jsonl", {"--flat"});
        REQUIRE(run.outcome.ok);
        lines = lines_of(dir.path() / "flat.jsonl");
        CHECK(lines[0] == R"({"prompt": "Say hi", "completion": "hello"})");
    }

    SECTION("sharegpt JSON array, and chatml messages") {
        std::ofstream{dir.path() / "sg.json"}
            << R"([{"conversations": [{"from": "human", "value": "q"}, {"from": "gpt", "value": "a"}]},
                   {"conversations": []}])";
        const Run run = prepare(dir.path() / "sg.json", dir.path() / "sg.jsonl");
        REQUIRE(run.outcome.ok);
        const std::vector<std::string> lines = lines_of(dir.path() / "sg.jsonl");
        REQUIRE(lines.size() == 1);
        CHECK(
            lines[0] ==
            R"({"messages": [{"role": "user", "content": "q"}, {"role": "assistant", "content": "a"}]})");
        std::ofstream{dir.path() / "cm.jsonl"}
            << R"({"messages": [{"role": "user", "content": "q2"}, {"role": "assistant", "content": "a2"}]})"
            << "\n";
        const Run chatml =
            prepare(dir.path() / "cm.jsonl", dir.path() / "cm.out.jsonl", {"--flat"});
        REQUIRE(chatml.outcome.ok);
        CHECK(lines_of(dir.path() / "cm.out.jsonl")[0] ==
              R"({"prompt": "q2", "completion": "a2"})");
    }

    SECTION("oasst rows are paired, and --map renames columns") {
        std::ofstream{dir.path() / "oa.csv"}
            << "role,text\nprompter,Q1\nassistant,A1\nassistant,orphan\nprompter,Q2\n";
        const Run run = prepare(dir.path() / "oa.csv", dir.path() / "oa.jsonl");
        REQUIRE(run.outcome.ok);
        const std::vector<std::string> lines = lines_of(dir.path() / "oa.jsonl");
        REQUIRE(lines.size() == 1);
        CHECK(lines[0].find("\"Q1\"") != std::string::npos);

        std::ofstream{dir.path() / "odd.jsonl"} << R"({"question": "q", "reply": "r"})" << "\n";
        const Run mapped =
            prepare(dir.path() / "odd.jsonl", dir.path() / "mapped.jsonl",
                    {"--format", "prompt-completion", "--map", "prompt=question,completion=reply"});
        INFO(mapped.outcome.describe());
        REQUIRE(mapped.outcome.ok);
        CHECK(lines_of(dir.path() / "mapped.jsonl")[0].find("\"q\"") != std::string::npos);
    }

    SECTION("no matching preset is an error listing the columns and a --map") {
        std::ofstream{dir.path() / "odd.csv"} << "a,b\n1,2\n";
        const Run run = prepare(dir.path() / "odd.csv", dir.path() / "never.jsonl");
        CHECK_FALSE(run.outcome.ok);
        CHECK(run.outcome.error.find("'a', 'b'") != std::string::npos);
        CHECK(run.outcome.error.find("--map prompt=a,completion=b") != std::string::npos);
        REQUIRE(run.outcome.exit_code.has_value());
        CHECK(*run.outcome.exit_code == 1);
        CHECK_FALSE(std::filesystem::exists(dir.path() / "never.jsonl"));
    }

    SECTION("a JSON object of splits picks the split, and a missing split says which exist") {
        std::ofstream{dir.path() / "splits.json"}
            << R"({"train": [{"prompt": "p", "completion": "c"}], "test": []})";
        const Run run = prepare(dir.path() / "splits.json", dir.path() / "s.jsonl");
        REQUIRE(run.outcome.ok);
        CHECK(run.record["rows_written"] == 1);
        const Run missing =
            prepare(dir.path() / "splits.json", dir.path() / "m.jsonl", {"--split", "dev"});
        CHECK_FALSE(missing.outcome.ok);
        CHECK(missing.outcome.error.find("'dev'") != std::string::npos);
        CHECK(missing.outcome.error.find("test") != std::string::npos);
    }

    SECTION("a directory of JSONL files is read whole") {
        std::filesystem::create_directories(dir.path() / "many");
        std::ofstream{dir.path() / "many" / "a.jsonl"} << R"({"prompt": "1", "completion": "x"})"
                                                       << "\n";
        std::ofstream{dir.path() / "many" / "b.jsonl"} << R"({"prompt": "2", "completion": "y"})"
                                                       << "\n";
        const Run run = prepare(dir.path() / "many", dir.path() / "many.jsonl");
        REQUIRE(run.outcome.ok);
        CHECK(run.record["rows_written"] == 2);
    }
}
