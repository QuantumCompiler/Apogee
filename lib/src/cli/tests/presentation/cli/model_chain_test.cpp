#include "cli/model_chain.h"

#include <CLI/CLI.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "cli/job_stream.h"

/// The chain's orchestration (M3) as a table over stand-in stages: what runs,
/// in what order, what is said when one fails, and that the failure goes on
/// with its own exit code. The real stages are `models_convert_test`'s.
namespace {

using apogee::commands::ChainLog;
using apogee::commands::ChainStage;
using apogee::commands::run_chain;

/// Four stages that record their turn; the one at `failing` fails the way a
/// verb does -- having said why, with an exit code -- and the pull, when it
/// succeeds, makes the rest resumable offline.
struct Table {
    std::vector<std::string> ran;

    [[nodiscard]] std::vector<ChainStage> stages(int failing) {
        std::vector<ChainStage> out;
        const std::vector<std::string> labels{"pull org/model's full weights", "convert to F16",
                                              "quantize to Q4_K_M", "register 2 backends"};
        for (int index = 0; std::cmp_less(index, labels.size()); ++index) {
            const std::string& label = labels[static_cast<std::size_t>(index)];
            out.push_back({label, [this, index, failing, label](ChainLog& log) {
                               ran.push_back(label);
                               if (index == failing) {
                                   throw CLI::RuntimeError(index == 2 ? 130 : 1);
                               }
                               if (index == 0) {
                                   log.resume =
                                       "apogee models convert org--model/safetensors/a "
                                       "--register-with Q4_K_M";
                               }
                               if (index == 1) {
                                   log.note("note: a base model");
                               }
                               if (index == 2) {
                                   log.note("note: a base model");  // said once, all the same
                               }
                               if (index == 3) {
                                   log.ready = {"model-F16", "model-Q4_K_M"};
                                   log.next = "chat with one:  apogee chat -m model-Q4_K_M";
                               }
                           }});
        }
        return out;
    }
};

[[nodiscard]] ChainLog start() {
    ChainLog log;
    log.resume = "apogee models pull org/model --safetensors --register-with Q4_K_M";
    return log;
}

}  // namespace

TEST_CASE("a chain runs every stage in order, then says what is ready",
          "[commands][models][chain]") {
    Table table;
    std::ostringstream out;
    const ChainLog log = run_chain(table.stages(-1), start(), out);
    CHECK(table.ran.size() == 4);
    const std::string said = out.str();
    CHECK(said.find("[1/4] pull org/model's full weights\n") == 0);
    CHECK(said.find("[2/4] convert to F16") < said.find("[3/4] quantize to Q4_K_M"));
    CHECK(said.find("[3/4] quantize to Q4_K_M") < said.find("[4/4] register 2 backends"));
    CHECK(said.find("ready:\n  model-F16\n  model-Q4_K_M\n") != std::string::npos);
    // A warning two stages noticed is said once.
    CHECK(said.find("note: a base model") == said.rfind("note: a base model"));
    CHECK(log.notes.size() == 1);
    CHECK(said.ends_with("chat with one:  apogee chat -m model-Q4_K_M\n"));
    CHECK(said.find("stopped at") == std::string::npos);
}

TEST_CASE("once the pull is done the cheaper resume is said, before anything can stop",
          "[commands][models][chain]") {
    Table table;
    std::ostringstream out;
    (void)run_chain(table.stages(-1), start(), out);
    const std::string said = out.str();
    const std::string line =
        "from here on, if this stops, resume with:\n  apogee models convert "
        "org--model/safetensors/a --register-with Q4_K_M\n";
    CHECK(said.find(line) != std::string::npos);
    CHECK(said.find(line) < said.find("[2/4]"));
    CHECK(said.find("from here on") == said.rfind("from here on"));
}

TEST_CASE("a stage that fails stops the chain, says where and how to resume, and keeps its code",
          "[commands][models][chain]") {
    struct Row {
        int failing;
        std::string where;
        std::string resume;
        int code;
    };

    const std::string pull = "apogee models pull org/model --safetensors --register-with Q4_K_M";
    const std::string convert =
        "apogee models convert org--model/safetensors/a --register-with Q4_K_M";
    for (const Row& row :
         {Row{.failing = 0,
              .where = "[1/4] pull org/model's full weights",
              .resume = pull,
              .code = 1},
          Row{.failing = 1, .where = "[2/4] convert to F16", .resume = convert, .code = 1},
          Row{.failing = 2, .where = "[3/4] quantize to Q4_K_M", .resume = convert, .code = 130},
          Row{.failing = 3, .where = "[4/4] register 2 backends", .resume = convert, .code = 1}}) {
        INFO("failing at " << row.where);
        Table table;
        std::ostringstream out;
        int code = 0;
        try {
            (void)run_chain(table.stages(row.failing), start(), out);
        } catch (const CLI::RuntimeError& e) {
            code = e.get_exit_code();
        }
        // The stage's own exit code -- a cancelled quantize stays a cancel.
        CHECK(code == row.code);
        // Nothing after the failing stage ran.
        CHECK(table.ran.size() == static_cast<std::size_t>(row.failing + 1));
        const std::string said = out.str();
        CHECK(said.find("stopped at " + row.where +
                        " -- what the stages before it made is kept.\nresume with:\n  " +
                        row.resume + "\n") != std::string::npos);
        CHECK(said.find("ready:") == std::string::npos);
    }
}

TEST_CASE("any failure, not only a verb's, is said where it stopped and passed on",
          "[commands][models][chain]") {
    std::ostringstream out;
    const std::vector<ChainStage> stages{
        {"convert to F16", [](ChainLog&) { throw std::runtime_error("disk full"); }}};
    CHECK_THROWS_AS(run_chain(stages, start(), out), std::runtime_error);
    CHECK(out.str().find("stopped at [1/1] convert to F16") != std::string::npos);
}

namespace {

/// stdout captured for a test, restored after it.
struct CapturedStdout {
    std::ostringstream text;
    std::streambuf* original = std::cout.rdbuf(text.rdbuf());

    CapturedStdout() = default;
    CapturedStdout(const CapturedStdout&) = delete;
    CapturedStdout& operator=(const CapturedStdout&) = delete;
    CapturedStdout(CapturedStdout&&) = delete;
    CapturedStdout& operator=(CapturedStdout&&) = delete;

    ~CapturedStdout() {
        std::cout.rdbuf(original);
    }

    [[nodiscard]] std::vector<nlohmann::json> events() const {
        std::vector<nlohmann::json> out;
        std::istringstream in{text.str()};
        for (std::string line; std::getline(in, line);) {
            out.push_back(nlohmann::json::parse(line));
        }
        return out;
    }
};

}  // namespace

TEST_CASE("on a job's stream each stage is an event, and the rest of what it says a notice",
          "[commands][models][chain][job]") {
    // 52b: the chain's one narration, framed -- `[i/n]` becomes `stage`
    // events, every other line it prints a `notice`, nothing else on stdout.
    SECTION("every stage done") {
        CapturedStdout stdout_;
        {
            apogee::commands::JobStream job{"models pull"};
            Table table;
            (void)run_chain(table.stages(-1), start(), std::cout);
        }
        const std::vector<nlohmann::json> events = stdout_.events();
        std::vector<std::string> stages;
        std::vector<std::string> notices;
        for (const nlohmann::json& event : events) {
            if (event.at("type") == "stage") {
                stages.push_back(std::to_string(event.at("index").get<int>()) + "/" +
                                 std::to_string(event.at("count").get<int>()) + " " +
                                 event.at("label").get<std::string>() + " " +
                                 event.at("state").get<std::string>());
            } else if (event.at("type") == "notice") {
                notices.push_back(event.at("text").get<std::string>());
            } else {
                CHECK(event.at("type") == "job");
            }
        }
        CHECK(stages == std::vector<std::string>{
                            "1/4 pull org/model's full weights started",
                            "1/4 pull org/model's full weights done", "2/4 convert to F16 started",
                            "2/4 convert to F16 done", "3/4 quantize to Q4_K_M started",
                            "3/4 quantize to Q4_K_M done", "4/4 register 2 backends started",
                            "4/4 register 2 backends done"});
        CHECK(notices == std::vector<std::string>{
                             "from here on, if this stops, resume with:",
                             "  apogee models convert org--model/safetensors/a --register-with "
                             "Q4_K_M",
                             "ready:", "  model-F16", "  model-Q4_K_M", "note: note: a base model",
                             "chat with one:  apogee chat -m model-Q4_K_M"});
    }
    SECTION("a stage that fails: failed, then where and how to resume") {
        CapturedStdout stdout_;
        int code = 0;
        {
            apogee::commands::JobStream job{"models pull"};
            Table table;
            try {
                (void)run_chain(table.stages(1), start(), std::cout);
            } catch (const CLI::RuntimeError& e) {
                code = e.get_exit_code();
            }
        }
        CHECK(code == 1);
        const std::vector<nlohmann::json> events = stdout_.events();
        REQUIRE(events.size() >= 4);
        const auto at = [&](std::size_t from_end) { return events[events.size() - from_end]; };
        CHECK(at(4).at("type") == "stage");
        CHECK(at(4).at("state") == "failed");
        CHECK(at(4).at("label") == "convert to F16");
        CHECK(at(3).at("text") ==
              "stopped at [2/4] convert to F16 -- what the stages before it "
              "made is kept.");
        CHECK(at(2).at("text") == "resume with:");
        CHECK(at(1).at("text") ==
              "  apogee models convert org--model/safetensors/a --register-with Q4_K_M");
    }
}
