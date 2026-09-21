#include "training/synth.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "harness/cancellation.h"

/// The synth core, model-free: the contract prompt, the per-batch
/// instruction, the array extraction and the aliases, de-duplication, the
/// call bound that ends a run that never produces, retries with backoff on
/// a retryable failure and none on a final one, parallel batches reaching
/// the same count as serial ones, cancellation.
namespace {

using apogee::training::extract_json_array;
using apogee::training::GenerateFn;
using apogee::training::GenerateOutcome;
using apogee::training::Kit;
using apogee::training::max_synth_calls;
using apogee::training::parse_synth_examples;
using apogee::training::synth_system_prompt;
using apogee::training::synth_user_prompt;
using apogee::training::synthesize;
using apogee::training::SynthOptions;
using apogee::training::SynthResult;

Kit kit_with(int count, int per_seed, std::vector<std::string> seeds = {"alpha", "beta"}) {
    Kit kit;
    kit.name = "test";
    kit.synth.system = "Teach the thing.";
    kit.synth.seeds = std::move(seeds);
    kit.synth.count = count;
    kit.synth.per_seed = per_seed;
    kit.synth.temperature = 0.5;
    kit.eval.push_back({"p", "e"});
    return kit;
}

GenerateOutcome ok(std::string text) {
    GenerateOutcome outcome;
    outcome.ok = true;
    outcome.text = std::move(text);
    return outcome;
}

SynthOptions no_sleep() {
    SynthOptions options;
    options.sleep = [](std::chrono::milliseconds) {};
    return options;
}

}  // namespace

TEST_CASE("the contract is appended to the kit's prompt and the batch asks for n",
          "[training][synth]") {
    const std::string system = synth_system_prompt("  Teach.  ");
    CHECK(system.rfind("Teach.", 0) == 0);
    CHECK(system.find("ONLY a JSON array") != std::string::npos);
    CHECK(system.find("\"prompt\"") != std::string::npos);

    const std::string user = synth_user_prompt(6, "rates", "trains");
    CHECK(user.find("Generate exactly 6 distinct") != std::string::npos);
    CHECK(user.find("Focus this batch on: rates") != std::string::npos);
    CHECK(user.find("Additional focus: trains") != std::string::npos);
    CHECK(user.find("Return ONLY the JSON array.") != std::string::npos);
    const std::string bare = synth_user_prompt(2, "", "");
    CHECK(bare.find("Focus") == std::string::npos);
}

TEST_CASE("the array is extracted through prose, fences and brackets inside strings",
          "[training][synth][parse]") {
    CHECK(extract_json_array("Sure! ```json\n[{\"a\": \"[x]\"}]\n``` done") ==
          "[{\"a\": \"[x]\"}]");
    CHECK(extract_json_array("no array here").empty());
    CHECK(extract_json_array("[unterminated").empty());
    CHECK(extract_json_array("[\"a]\", 1]") == "[\"a]\", 1]");
}

TEST_CASE("examples are read through the field aliases and blank halves are dropped",
          "[training][synth][parse]") {
    const auto examples = parse_synth_examples(
        R"(Here you go:
[{"prompt": "P1", "completion": "C1"},
 {"input": "P2", "output": "C2"},
 {"Instruction": "P3", "Response": "C3"},
 {"question": "P4", "answer": " "},
 {"prompt": "", "completion": "C5"},
 "not an object"])");
    REQUIRE(examples.size() == 3);
    CHECK(examples[0].prompt == "P1");
    CHECK(examples[1].completion == "C2");
    CHECK(examples[2].prompt == "P3");
    CHECK(examples[2].completion == "C3");
    CHECK(parse_synth_examples("prose only").empty());
}

TEST_CASE("the call bound is Ommi's", "[training][synth]") {
    CHECK(max_synth_calls(200, 8, 8) == (200 / 8 + 1) * 3 + 8);
    CHECK(max_synth_calls(4, 0, 1) == (4 / 8 + 1) * 3 + 1);
}

TEST_CASE("duplicates are dropped and an under-producing teacher stops at the bound",
          "[training][synth]") {
    // The teacher always answers the same two prompts: asked for four, the
    // run yields two and ends at the call bound instead of spinning.
    int calls = 0;
    const GenerateFn teacher = [&calls](std::string_view, std::string_view, double, int,
                                        const apogee::harness::CancellationToken&) {
        ++calls;
        return ok(
            R"([{"prompt": "One", "completion": "1"}, {"prompt": "one", "completion": "dup"}])");
    };
    SynthOptions options = no_sleep();
    options.count = 4;
    int last_produced = -1;
    options.on_progress = [&last_produced](int produced, int) { last_produced = produced; };
    const SynthResult result = synthesize(kit_with(200, 8), teacher, options);
    CHECK(result.error.empty());
    REQUIRE(result.examples.size() == 1);
    CHECK(result.examples[0].completion == "1");
    CHECK(calls == max_synth_calls(4, 8, 2));
    CHECK(result.calls == calls);
    CHECK(last_produced == 1);
}

TEST_CASE("the batch asks for the remaining count and cycles the seeds", "[training][synth]") {
    std::vector<std::string> users;
    const GenerateFn teacher = [&users](std::string_view system, std::string_view user,
                                        double temperature, int max_tokens,
                                        const apogee::harness::CancellationToken&) {
        CHECK(system.find("Teach the thing.") == 0);
        CHECK(temperature == 0.5);
        CHECK(max_tokens == 4096);
        users.emplace_back(user);
        static int n = 0;
        ++n;
        return ok("[{\"prompt\": \"p" + std::to_string(n) + "\", \"completion\": \"c\"}]");
    };
    SynthOptions options = no_sleep();
    options.count = 3;
    const SynthResult result = synthesize(kit_with(200, 2), teacher, options);
    REQUIRE(result.examples.size() == 3);
    REQUIRE(users.size() == 3);
    CHECK(users[0].find("exactly 2 distinct") != std::string::npos);
    CHECK(users[0].find("Focus this batch on: alpha") != std::string::npos);
    CHECK(users[1].find("Focus this batch on: beta") != std::string::npos);
    CHECK(users[2].find("exactly 1 distinct") != std::string::npos);
    CHECK(users[2].find("Focus this batch on: alpha") != std::string::npos);
}

TEST_CASE("a retryable failure is retried with backoff, a final one is not",
          "[training][synth][retry]") {
    int attempts = 0;
    std::vector<std::chrono::milliseconds> slept;
    const GenerateFn flaky = [&attempts](std::string_view, std::string_view, double, int,
                                         const apogee::harness::CancellationToken&) {
        ++attempts;
        if (attempts < 3) {
            GenerateOutcome outcome;
            outcome.error = "429 rate limited";
            outcome.retryable = true;
            return outcome;
        }
        return ok(R"([{"prompt": "P", "completion": "C"}])");
    };
    SynthOptions options;
    options.count = 1;
    options.base_delay = std::chrono::milliseconds{100};
    options.max_delay = std::chrono::milliseconds{150};
    options.sleep = [&slept](std::chrono::milliseconds delay) { slept.push_back(delay); };
    SynthResult result = synthesize(kit_with(200, 8), flaky, options);
    REQUIRE(result.examples.size() == 1);
    CHECK(result.retries == 2);
    CHECK(result.calls == 3);
    REQUIRE(slept.size() == 2);
    CHECK(slept[0] == std::chrono::milliseconds{100});
    CHECK(slept[1] == std::chrono::milliseconds{150});  // doubled, then capped

    attempts = 0;
    slept.clear();
    const GenerateFn refused = [&attempts](std::string_view, std::string_view, double, int,
                                           const apogee::harness::CancellationToken&) {
        ++attempts;
        GenerateOutcome outcome;
        outcome.error = "no API key";
        outcome.retryable = false;
        return outcome;
    };
    options.count = 1;
    result = synthesize(kit_with(1, 8, {"s"}), refused, options);
    CHECK(result.examples.empty());
    CHECK(result.retries == 0);
    CHECK(slept.empty());
    CHECK(result.failed_batches == max_synth_calls(1, 8, 1));
    CHECK(result.error.find("no usable examples") != std::string::npos);
    CHECK(result.error.find(std::to_string(result.calls)) != std::string::npos);
}

TEST_CASE("a throwing teacher is a failed batch, not a crash", "[training][synth]") {
    const GenerateFn boom = [](std::string_view, std::string_view, double, int,
                               const apogee::harness::CancellationToken&) -> GenerateOutcome {
        throw std::runtime_error("kaboom");
    };
    SynthOptions options = no_sleep();
    options.count = 1;
    options.max_retries = 0;
    const SynthResult result = synthesize(kit_with(1, 8, {"s"}), boom, options);
    CHECK(result.examples.empty());
    CHECK(result.failed_batches > 0);
    CHECK_FALSE(result.error.empty());
}

TEST_CASE("cancellation ends the run and marks it", "[training][synth]") {
    apogee::harness::CancellationToken token = apogee::harness::CancellationToken::create();
    int calls = 0;
    const GenerateFn teacher = [&calls, &token](std::string_view, std::string_view, double, int,
                                                const apogee::harness::CancellationToken&) {
        ++calls;
        token.cancel();
        return ok(R"([{"prompt": "P", "completion": "C"}])");
    };
    SynthOptions options = no_sleep();
    options.count = 10;
    options.cancellation = token;
    const SynthResult result = synthesize(kit_with(200, 1), teacher, options);
    CHECK(result.cancelled);
    CHECK(calls == 1);
    CHECK(result.error.empty());
    // The batch that arrived after the cancellation is not merged: a run
    // that was stopped hands back nothing it produced past the stop.
    CHECK(result.examples.empty());
}

TEST_CASE("parallel batches really run at once", "[training][synth][parallel]") {
    // Each call waits for a second call to be in flight before answering;
    // serial workers would wait forever, so they time out and fail.
    std::atomic<int> in_flight{0};
    std::atomic<int> counter{0};
    const GenerateFn teacher = [&](std::string_view, std::string_view, double, int,
                                   const apogee::harness::CancellationToken&) {
        ++in_flight;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
        while (in_flight.load() < 2 && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        const bool concurrent = in_flight.load() >= 2;
        --in_flight;
        if (!concurrent) {
            GenerateOutcome outcome;
            outcome.error = "never concurrent";
            outcome.retryable = false;
            return outcome;
        }
        const int n = ++counter;
        return ok("[{\"prompt\": \"p" + std::to_string(n) + "\", \"completion\": \"c\"}]");
    };
    SynthOptions options = no_sleep();
    options.count = 2;
    options.parallel = 2;
    options.max_retries = 0;
    const SynthResult result = synthesize(kit_with(200, 1), teacher, options);
    INFO(result.error);
    // Two examples means two calls overlapped; a worker that took a third
    // batch while the second's result was pending times out alone, which is
    // the overshoot parallel batches inherently have and not a failure of
    // concurrency -- so the batch count is not asserted, the examples are.
    CHECK(result.examples.size() == 2);
}

TEST_CASE("parallel batches reach the target with distinct prompts",
          "[training][synth][parallel]") {
    std::atomic<int> counter{0};
    const GenerateFn teacher = [&counter](std::string_view, std::string_view, double, int,
                                          const apogee::harness::CancellationToken&) {
        const int n = ++counter;
        return ok("[{\"prompt\": \"p" + std::to_string(n) +
                  "a\", \"completion\": \"c\"}, {\"prompt\": \"p" + std::to_string(n) +
                  "b\", \"completion\": \"c\"}]");
    };
    SynthOptions options = no_sleep();
    options.count = 9;
    options.parallel = 4;
    std::mutex mutex;
    int progress_calls = 0;
    options.on_progress = [&](int, int target) {
        const std::lock_guard<std::mutex> lock{mutex};
        ++progress_calls;
        CHECK(target == 9);
    };
    const SynthResult result = synthesize(kit_with(200, 2), teacher, options);
    CHECK(result.error.empty());
    CHECK(result.examples.size() == 9);
    CHECK(progress_calls >= 5);
    CHECK(result.calls <= max_synth_calls(9, 2, 2));
}

TEST_CASE("no teacher is an error", "[training][synth]") {
    const SynthResult result = synthesize(kit_with(1, 1), GenerateFn{}, no_sleep());
    CHECK(result.error.find("teacher") != std::string::npos);
}
