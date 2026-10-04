#include "training/eval.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "support/env_guard.h"
#include "training/kit.h"
#include "training/mock_trainer.h"

/// The gate: substring items pass or fail on the exact text; an item with
/// no `expected` is judged pairwise against the untuned base, the verdict
/// parsed with anything but A/B a tie, a judge or baseline failure a tie
/// that passes (the never-fail contract); with no judge such items skip and
/// auto-pass with the count; a candidate error aborts; cancellation ends
/// it; the gate is 100%; the suite loader's rules; the JSON round trip.
namespace {

using apogee::training::CandidateReply;
using apogee::training::CandidateRunner;
using apogee::training::EvalItem;
using apogee::training::EvalOptions;
using apogee::training::EvalRun;
using apogee::training::JudgeReply;
using apogee::training::MockTrainer;
using apogee::training::run_eval;

class Scripted final : public CandidateRunner {
public:
    explicit Scripted(std::string prefix, bool fail = false)
        : prefix_{std::move(prefix)}, fail_{fail} {}

    CandidateReply run(std::string_view prompt,
                       const apogee::harness::CancellationToken&) override {
        ++calls;
        CandidateReply reply;
        if (fail_) {
            reply.error = "no model";
            return reply;
        }
        reply.ok = true;
        reply.text = prefix_ + std::string{prompt};
        return reply;
    }

    int calls = 0;

private:
    std::string prefix_;
    bool fail_;
};

std::vector<EvalItem> items() {
    return {EvalItem{.prompt = "say hello", .expected = "hello"},
            EvalItem{.prompt = "say goodbye", .expected = "HELLO"},
            EvalItem{.prompt = "free form", .expected = ""}};
}

}  // namespace

TEST_CASE("substring items pass or fail on the exact text and the gate is 100%",
          "[training][eval][contains]") {
    Scripted candidate{""};
    const EvalRun run = run_eval(items(), candidate, {});
    REQUIRE(run.ok);
    CHECK(run.results.total == 3);
    REQUIRE(run.results.items.size() == 3);
    CHECK(run.results.items[0].check_type == "contains");
    CHECK(run.results.items[0].passed);
    CHECK(run.results.items[0].got == "say hello");
    // Case-sensitive: the author wrote the exact text.
    CHECK(run.results.items[1].check_type == "contains");
    CHECK_FALSE(run.results.items[1].passed);
    CHECK(run.results.items[2].check_type == "skip");
    CHECK(run.results.items[2].passed);
    CHECK(run.results.num_passed == 2);
    CHECK(run.results.num_skipped == 1);
    CHECK(run.results.score > 0.66);
    CHECK(run.results.score < 0.67);
    CHECK_FALSE(run.results.passed);
    CHECK_FALSE(run.results.run_at.empty());

    Scripted echo_both{"HELLO hello "};
    const EvalRun all = run_eval(items(), echo_both, {});
    CHECK(all.results.passed);
    CHECK(all.results.score == 1.0);
}

TEST_CASE(
    "judge items compare the candidate with the untuned base, the verdict parsed with "
    "anything else a tie",
    "[training][eval][judge]") {
    const std::vector<EvalItem> free{EvalItem{.prompt = "q1"}, EvalItem{.prompt = "q2"},
                                     EvalItem{.prompt = "q3"}, EvalItem{.prompt = "q4"}};
    Scripted candidate{"cand: "};
    Scripted baseline{"base: "};
    std::vector<std::string> verdicts{"A", " b\n", "TIE", "I cannot decide"};
    std::vector<std::string> prompts;
    EvalOptions options;
    options.judge_backend = "paid";
    options.baseline = &baseline;
    options.judge = [&](std::string_view prompt, std::string_view cand, std::string_view base,
                        const apogee::harness::CancellationToken&) {
        prompts.push_back(apogee::training::judge_prompt(prompt, cand, base));
        JudgeReply reply;
        reply.ok = true;
        reply.text = verdicts[prompts.size() - 1];
        return reply;
    };
    const EvalRun run = run_eval(free, candidate, options);
    REQUIRE(run.ok);
    REQUIRE(run.results.items.size() == 4);
    CHECK(run.results.judge_backend == "paid");
    CHECK(run.results.items[0].check_type == "judge");
    CHECK(run.results.items[0].judge_winner == "candidate");
    CHECK(run.results.items[0].passed);
    CHECK(run.results.items[0].baseline_got == "base: q1");
    CHECK(run.results.items[1].judge_winner == "baseline");
    CHECK_FALSE(run.results.items[1].passed);
    CHECK(run.results.items[2].judge_winner == "tie");
    CHECK(run.results.items[2].passed);
    CHECK(run.results.items[3].judge_winner == "tie");
    CHECK(run.results.items[3].passed);
    CHECK(run.results.num_passed == 3);
    CHECK(run.results.num_skipped == 0);
    CHECK_FALSE(run.results.passed);
    CHECK(baseline.calls == 4);
    // The candidate is always Response A.
    CHECK(prompts[0].find("Response A: cand: q1") != std::string::npos);
    CHECK(prompts[0].find("Response B: base: q1") != std::string::npos);
    CHECK(prompts[0].find("Prompt: q1") != std::string::npos);

    CHECK(apogee::training::parse_judge_verdict("A") == "candidate");
    CHECK(apogee::training::parse_judge_verdict("  a.") == "candidate");
    CHECK(apogee::training::parse_judge_verdict("B") == "baseline");
    CHECK(apogee::training::parse_judge_verdict("tie") == "tie");
    CHECK(apogee::training::parse_judge_verdict("") == "tie");
    CHECK(apogee::training::parse_judge_verdict("Both are fine") == "tie");
    CHECK(apogee::training::kJudgeMaxTokens >= 1024);
}

TEST_CASE("the never-fail contract: a judge error or a baseline error is a tie that passes",
          "[training][eval][judge]") {
    const std::vector<EvalItem> free{EvalItem{.prompt = "q"}};
    Scripted candidate{"c "};
    Scripted baseline{"b "};
    EvalOptions options;
    options.baseline = &baseline;
    options.judge = [](std::string_view, std::string_view, std::string_view,
                       const apogee::harness::CancellationToken&) {
        JudgeReply reply;
        reply.error = "429";
        return reply;
    };
    const EvalRun judge_down = run_eval(free, candidate, options);
    REQUIRE(judge_down.ok);
    CHECK(judge_down.results.items[0].check_type == "judge");
    CHECK(judge_down.results.items[0].judge_winner == "tie");
    CHECK(judge_down.results.items[0].passed);
    CHECK(judge_down.results.passed);

    Scripted dead_base{"", true};
    options.baseline = &dead_base;
    int judge_calls = 0;
    options.judge = [&judge_calls](std::string_view, std::string_view, std::string_view,
                                   const apogee::harness::CancellationToken&) {
        ++judge_calls;
        JudgeReply reply;
        reply.ok = true;
        reply.text = "B";
        return reply;
    };
    const EvalRun base_down = run_eval(free, candidate, options);
    REQUIRE(base_down.ok);
    CHECK(base_down.results.items[0].judge_winner == "tie");
    CHECK(base_down.results.items[0].passed);
    CHECK(base_down.results.items[0].baseline_got.empty());
    CHECK(judge_calls == 0);

    // A judge without a baseline, or a baseline without a judge: skip.
    EvalOptions half;
    half.judge = options.judge;
    const EvalRun no_base = run_eval(free, candidate, half);
    CHECK(no_base.results.items[0].check_type == "skip");
    CHECK(no_base.results.num_skipped == 1);
}

TEST_CASE(
    "a candidate error aborts the eval, cancellation ends it, and an empty suite is an "
    "error",
    "[training][eval]") {
    Scripted dead{"", true};
    const EvalRun aborted = run_eval(items(), dead, {});
    CHECK_FALSE(aborted.ok);
    CHECK_FALSE(aborted.cancelled);
    CHECK(aborted.error.find("candidate inference") != std::string::npos);
    CHECK(aborted.error.find("no model") != std::string::npos);
    CHECK(aborted.results.items.empty());

    Scripted candidate{""};
    EvalOptions options;
    options.cancellation = apogee::harness::CancellationToken::create();
    int seen = 0;
    options.on_progress = [&](int done, int) {
        seen = done;
        if (done == 1) {
            options.cancellation.cancel();
        }
    };
    const EvalRun cancelled = run_eval(items(), candidate, options);
    CHECK_FALSE(cancelled.ok);
    CHECK(cancelled.cancelled);
    CHECK(seen == 1);

    const EvalRun empty = run_eval({}, candidate, {});
    CHECK_FALSE(empty.ok);
    CHECK(empty.error.find("no item") != std::string::npos);
}

TEST_CASE("the mock trainer's candidate and base run through the gate end to end",
          "[training][eval][mock]") {
    MockTrainer trainer;
    const auto candidate = trainer.candidate_runner("/snap", "/adapters");
    const auto baseline = trainer.candidate_runner("/snap", {});
    EvalOptions options;
    options.baseline = baseline.get();
    options.judge = [](std::string_view, std::string_view cand, std::string_view base,
                       const apogee::harness::CancellationToken&) {
        JudgeReply reply;
        reply.ok = true;
        reply.text = cand.size() < base.size() ? "A" : "B";
        return reply;
    };
    const EvalRun run =
        run_eval({EvalItem{.prompt = "say hello", .expected = "hello"}, EvalItem{.prompt = "free"}},
                 *candidate, options);
    REQUIRE(run.ok);
    CHECK(run.results.passed);
    CHECK(run.results.items[1].judge_winner == "candidate");
}

TEST_CASE(
    "the suite loader skips blank lines and promptless items, names a bad line, and "
    "refuses an empty file",
    "[training][eval][suite]") {
    const apogee::testing::TempDir root{"eval-suite-" + std::to_string(std::random_device{}())};
    const std::filesystem::path path = root.path() / "suite.jsonl";
    std::ofstream{path, std::ios::binary}
        << "{\"prompt\": \"a\", \"expected\": \"x\"}\n\n   \n{\"prompt\": \"b\"}\r\n"
        << "{\"expected\": \"orphan\"}\n{\"prompt\": \"\"}\n";
    std::string error;
    const std::vector<EvalItem> loaded = apogee::training::load_eval_suite(path, error);
    CHECK(error.empty());
    REQUIRE(loaded.size() == 2);
    CHECK(loaded[0].prompt == "a");
    CHECK(loaded[0].expected == "x");
    CHECK(loaded[1].prompt == "b");
    CHECK(loaded[1].expected.empty());

    std::ofstream{path, std::ios::binary} << "{\"prompt\": \"a\"}\nnot json\n";
    CHECK(apogee::training::load_eval_suite(path, error).empty());
    CHECK(error.find(":2:") != std::string::npos);

    std::ofstream{path, std::ios::binary} << "\n";
    CHECK(apogee::training::load_eval_suite(path, error).empty());
    CHECK(error.find("no eval item") != std::string::npos);

    CHECK(apogee::training::load_eval_suite(root.path() / "missing", error).empty());
    CHECK(error.find("cannot read") != std::string::npos);
}

TEST_CASE("eval results round-trip through JSON", "[training][eval][json]") {
    Scripted candidate{"hello "};
    const EvalRun run = run_eval(items(), candidate, {});
    const nlohmann::json json = apogee::training::eval_results_to_json(run.results);
    CHECK(json["total"] == 3);
    CHECK(json["num_skipped"] == 1);
    CHECK_FALSE(json.contains("judge_backend"));
    const apogee::training::EvalResults back = apogee::training::eval_results_from_json(json);
    CHECK(back.total == 3);
    CHECK(back.num_passed == run.results.num_passed);
    CHECK(back.num_skipped == 1);
    CHECK(back.items.size() == 3);
    CHECK(back.items[0].got == "hello say hello");
    CHECK_THROWS_AS(apogee::training::eval_results_from_json(nlohmann::json{{"items", {1}}}),
                    std::runtime_error);
    CHECK_THROWS_AS(apogee::training::eval_results_from_json(nlohmann::json{1}),
                    std::runtime_error);
}
