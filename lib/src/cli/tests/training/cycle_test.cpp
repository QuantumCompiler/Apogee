#include "training/cycle.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "harness/types.h"
#include "support/env_guard.h"
#include "training/mock_trainer.h"

/// The cycle's pieces and one pass end to end: the lock exclusive, released
/// and re-acquirable; the breaker at k, on a halt, disabled at 0; the
/// directory source ignoring non-jsonl files, creating a missing queue and
/// empty cleanly; the sessions source refused without consent naming the
/// key and the two risks, honouring the watermark and the filters, a blank
/// side skipped; no data recording `skipped` without a failure counted;
/// the merge skipping blank lines; the six anchor-gate cases; a pass
/// promoting, consuming, setting the anchor on the first pass and resetting
/// the count; a fail counting and halting at k; halt and resume; the
/// history atomic.
namespace {

using apogee::harness::CycleConfig;
using apogee::harness::CycleSourceConfig;
using apogee::training::CycleHistory;
using apogee::training::CycleLock;
using apogee::training::CycleRunRecord;

void write(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << content;
}

std::string read(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

int count_entries(const std::filesystem::path& dir) {
    int n = 0;
    std::error_code code;
    for (const auto& entry : std::filesystem::directory_iterator(dir, code)) {
        (void)entry;
        ++n;
    }
    return n;
}

constexpr std::string_view kPlain =
    "{\"messages\": [{\"role\": \"user\", \"content\": \"hi\"}, {\"role\": \"assistant\", "
    "\"content\": \"hello\"}]}\n";

struct Fixture {
    apogee::testing::TempDir root{"cycle-" + std::to_string(std::random_device{}())};
    std::filesystem::path training = root.path() / "training";
    std::filesystem::path cycle = training / "cycle";
    std::filesystem::path queue = root.path() / "queue";
    std::filesystem::path snap = root.path() / "snap";
    apogee::training::MockTrainer trainer;
    apogee::harness::PipelineSpec spec;
    std::vector<std::string> promoted;
    int next_version = 1;

    Fixture() {
        write(snap / "config.json", R"({"model_type": "llama"})");
        write(snap / "model.safetensors", "w");
        spec.name = "nightly";
        spec.student = "snap";
        apogee::harness::PipelineStageSpec stage;
        stage.name = "base";
        stage.dataset = "ignored";
        stage.eval_suite = "hello";
        stage.iters = 2;
        spec.stages.push_back(stage);
    }

    [[nodiscard]] apogee::training::CycleRequest request(int k = 3) {
        apogee::training::CycleRequest request;
        request.config.pipeline = "nightly";
        request.config.backend = "nightly-model";
        request.config.circuit_breaker_k = k;
        CycleSourceConfig source;
        source.type = "directory";
        source.dir = queue.string();
        request.config.sources.push_back(source);
        request.cycle_dir = cycle;
        request.training_dir = training;
        request.spec = &spec;
        request.stages = {apogee::training::ResolvedStage{
            .dataset = {}, .suite = {{"say hello", "hello"}}, .suite_label = "suite:hello"}};
        request.base_model = snap;
        request.trainer = &trainer;
        request.promote = [this](std::string_view run_id) {
            promoted.emplace_back(run_id);
            return apogee::training::CyclePromotion{
                .ok = true, .version = next_version++, .gguf_path = "/v.gguf"};
        };
        return request;
    }

    [[nodiscard]] CycleHistory history() const {
        std::string error;
        const CycleHistory h = apogee::training::load_history(cycle, "nightly-model", error);
        REQUIRE(error.empty());
        return h;
    }
};

}  // namespace

TEST_CASE("the history round-trips, loads empty for a backend, saves atomically, names a bad file",
          "[training][cycle][history]") {
    const Fixture fixture;
    std::string error;
    const CycleHistory fresh = apogee::training::load_history(fixture.cycle, "tuned", error);
    CHECK(error.empty());
    CHECK(fresh.backend == "tuned");
    CHECK(fresh.total_runs == 0);
    CHECK_FALSE(fresh.halted);
    CHECK_FALSE(apogee::training::history_exists(fixture.cycle));

    CycleHistory history;
    history.backend = "tuned";
    history.anchor_version = 2;
    history.anchor_score = 0.87;
    history.consecutive_fails = 1;
    history.total_runs = 5;
    history.sessions_until = "2026-09-19T12:00:00Z";
    history.runs.push_back(CycleRunRecord{.run_at = "2026-09-18T03:00:00Z",
                                          .source = "directory",
                                          .dataset_rows = 40,
                                          .pipeline_id = "cycle-1",
                                          .gate = "pass",
                                          .cycle_score = 0.87,
                                          .promoted_version = 2});
    history.runs.push_back(CycleRunRecord{
        .run_at = "2026-09-19T03:00:00Z", .gate = "fail", .gate_reason = "regressed", .note = "n"});
    REQUIRE(apogee::training::save_history(fixture.cycle, history).empty());
    CHECK(apogee::training::history_exists(fixture.cycle));
    CHECK(count_entries(fixture.cycle) == 1);  // no temp file beside it
    const CycleHistory back = apogee::training::load_history(fixture.cycle, "other", error);
    CHECK(error.empty());
    CHECK(back.backend == "tuned");  // the file's, not the caller's
    CHECK(back.anchor_version == 2);
    CHECK(back.anchor_score == 0.87);
    CHECK(back.consecutive_fails == 1);
    CHECK(back.total_runs == 5);
    CHECK(back.sessions_until == "2026-09-19T12:00:00Z");
    REQUIRE(back.runs.size() == 2);
    CHECK(back.runs[0].gate == "pass");
    CHECK(back.runs[0].promoted_version == 2);
    CHECK(back.runs[0].dataset_rows == 40);
    CHECK(back.runs[1].gate_reason == "regressed");
    CHECK(back.runs[1].note == "n");
    CHECK_FALSE(apogee::training::history_to_json(back)["runs"][0].contains("note"));

    write(apogee::training::history_path(fixture.cycle), "{not json");
    (void)apogee::training::load_history(fixture.cycle, "tuned", error);
    CHECK(error.find("not JSON") != std::string::npos);
    write(apogee::training::history_path(fixture.cycle), "{\"runs\": 3}");
    (void)apogee::training::load_history(fixture.cycle, "tuned", error);
    CHECK(error.find("runs must be an array") != std::string::npos);
    CHECK_THROWS_AS(apogee::training::history_from_json(nlohmann::json::array()),
                    std::runtime_error);
}

TEST_CASE("the lock is exclusive, released on destruction and by hand, and re-acquirable",
          "[training][cycle][lock]") {
    const Fixture fixture;
    std::string error;
    CHECK_FALSE(apogee::training::cycle_lock_held(fixture.cycle));
    {
        std::optional<CycleLock> first = CycleLock::acquire(fixture.cycle, error);
        REQUIRE(first.has_value());
        CHECK(error.empty());
        CHECK(apogee::training::cycle_lock_held(fixture.cycle));
        CHECK(first->path() == fixture.cycle / "cycle.lock");
        CHECK_FALSE(read(first->path()).empty());  // the pid
        const std::optional<CycleLock> second = CycleLock::acquire(fixture.cycle, error);
        CHECK_FALSE(second.has_value());
        CHECK(error.find("another cycle is already running") != std::string::npos);
        CHECK(error.find((fixture.cycle / "cycle.lock").string()) != std::string::npos);
        first->release();
        CHECK_FALSE(apogee::training::cycle_lock_held(fixture.cycle));
        first->release();  // twice is fine
        const std::optional<CycleLock> third = CycleLock::acquire(fixture.cycle, error);
        CHECK(third.has_value());
    }
    CHECK_FALSE(apogee::training::cycle_lock_held(fixture.cycle));  // the destructor released
    // A moved-from lock does not release on destruction; the moved-to does.
    {
        std::optional<CycleLock> outer;
        {
            std::optional<CycleLock> inner = CycleLock::acquire(fixture.cycle, error);
            REQUIRE(inner.has_value());
            outer.emplace(std::move(*inner));
        }
        CHECK(apogee::training::cycle_lock_held(fixture.cycle));
    }
    CHECK_FALSE(apogee::training::cycle_lock_held(fixture.cycle));
}

TEST_CASE(
    "the circuit breaker: under k ok, halted refused, k reached refused, k 0 disabled; "
    "halt and resume",
    "[training][cycle][breaker]") {
    CycleHistory history;
    history.consecutive_fails = 2;
    CHECK(apogee::training::check_circuit_breaker(history, 5).empty());
    CHECK(apogee::training::check_circuit_breaker(history, 3).empty());
    history.consecutive_fails = 3;
    const std::string tripped = apogee::training::check_circuit_breaker(history, 3);
    CHECK(tripped.find("circuit breaker") != std::string::npos);
    CHECK(tripped.find("3 consecutive") != std::string::npos);
    CHECK(tripped.find("apogee train cycle resume") != std::string::npos);
    history.consecutive_fails = 999;
    CHECK(apogee::training::check_circuit_breaker(history, 0).empty());
    history.consecutive_fails = 0;
    apogee::training::halt_cycle(history, "manual");
    CHECK(history.halted);
    const std::string halted = apogee::training::check_circuit_breaker(history, 0);
    CHECK(halted.find("halted: manual") != std::string::npos);
    CHECK(halted.find("apogee train cycle resume") != std::string::npos);
    history.consecutive_fails = 4;
    CHECK(apogee::training::resume_cycle(history));
    CHECK_FALSE(history.halted);
    CHECK(history.halt_reason.empty());
    CHECK(history.consecutive_fails == 0);
    CHECK_FALSE(apogee::training::resume_cycle(history));

    // trip_breaker: nothing under k or at 0; a halt with the reason at k.
    history.consecutive_fails = 2;
    CHECK_FALSE(apogee::training::trip_breaker(history, 3));
    CHECK_FALSE(apogee::training::trip_breaker(history, 0));
    CHECK(apogee::training::trip_breaker(history, 2));
    CHECK(history.halted);
    CHECK(history.halt_reason.find("circuit_breaker_k 2") != std::string::npos);
}

TEST_CASE(
    "the directory source ignores non-jsonl files, creates a missing queue and is empty "
    "cleanly; consumed files move aside",
    "[training][cycle][sources]") {
    const Fixture fixture;
    apogee::training::DirectoryCollection found =
        apogee::training::collect_directory(fixture.queue / "new" / "deep");
    CHECK(found.error.empty());
    CHECK(found.files.empty());
    CHECK(std::filesystem::is_directory(fixture.queue / "new" / "deep"));

    write(fixture.queue / "b.jsonl", "{}");
    write(fixture.queue / "a.jsonl", "{}");
    write(fixture.queue / "c.txt", "{}");
    write(fixture.queue / "sub" / "d.jsonl", "{}");
    found = apogee::training::collect_directory(fixture.queue);
    REQUIRE(found.files.size() == 2);
    CHECK(found.files[0].filename() == "a.jsonl");
    CHECK(found.files[1].filename() == "b.jsonl");

    CHECK(apogee::training::consume_directory_files(found.files, fixture.queue).empty());
    CHECK_FALSE(std::filesystem::exists(fixture.queue / "a.jsonl"));
    CHECK(std::filesystem::exists(fixture.queue / "consumed" / "a.jsonl"));
    CHECK(std::filesystem::exists(fixture.queue / "consumed" / "b.jsonl"));
    CHECK(std::filesystem::exists(fixture.queue / "c.txt"));
    CHECK_FALSE(
        apogee::training::consume_directory_files({fixture.queue / "gone.jsonl"}, fixture.queue)
            .empty());
}

TEST_CASE(
    "the sessions source is refused without consent naming the key and the two risks, and "
    "with it honours the watermark, the backend and since filters, and skips a blank side",
    "[training][cycle][sources][sessions]") {
    using apogee::harness::ChatMessage;
    const std::vector<ChatMessage> full{ChatMessage::user("q1"), ChatMessage::assistant("a1"),
                                        ChatMessage::user("q2"), ChatMessage::assistant("a2")};
    const std::vector<ChatMessage> blank{ChatMessage::user("q"), ChatMessage::assistant("")};
    const std::vector<apogee::training::SessionView> views{
        {.backend = "local", .started_at = "2026-09-01T10:00:00Z", .messages = &full},
        {.backend = "paid", .started_at = "2026-09-10T10:00:00Z", .messages = &full},
        {.backend = "local", .started_at = "2026-09-15T10:00:00Z", .messages = &blank},
        {.backend = "local", .started_at = "2026-09-19T10:00:00Z", .messages = &full},
    };
    CycleSourceConfig source;
    source.type = "sessions";
    apogee::training::SessionCollection refused =
        apogee::training::collect_sessions(views, source, {});
    CHECK(refused.lines.empty());
    CHECK(refused.error.find("log_consent: true") != std::string::npos);
    CHECK(refused.error.find("privacy") != std::string::npos);
    CHECK(refused.error.find("self-reinforcement") != std::string::npos);

    source.log_consent = true;
    apogee::training::SessionCollection all = apogee::training::collect_sessions(views, source, {});
    CHECK(all.error.empty());
    CHECK(all.lines.size() == 6);  // three full sessions, two exchanges each
    CHECK(all.sessions == 3);
    CHECK(all.skipped == 1);
    CHECK(all.newest_started_at == "2026-09-19T10:00:00Z");

    // The watermark: only sessions started after it.
    const apogee::training::SessionCollection newer =
        apogee::training::collect_sessions(views, source, "2026-09-10T10:00:00Z");
    CHECK(newer.lines.size() == 2);
    CHECK(newer.sessions == 1);
    CHECK(newer.newest_started_at == "2026-09-19T10:00:00Z");
    const apogee::training::SessionCollection none =
        apogee::training::collect_sessions(views, source, "2026-09-19T10:00:00Z");
    CHECK(none.lines.empty());
    CHECK(none.newest_started_at.empty());

    // The filters, as datasets create applies them.
    source.backend = "paid";
    CHECK(apogee::training::collect_sessions(views, source, {}).lines.size() == 2);
    source.backend.clear();
    source.since = "2026-09-15";
    CHECK(apogee::training::collect_sessions(views, source, {}).lines.size() == 2);
    source.since = "soon";
    CHECK_FALSE(apogee::training::collect_sessions(views, source, {}).error.empty());
}

TEST_CASE("merge_datasets concatenates the sources skipping blank lines, and names a missing one",
          "[training][cycle][merge]") {
    const Fixture fixture;
    write(fixture.root.path() / "s1.jsonl", "{\"a\":1}\n{\"a\":2}\n");
    write(fixture.root.path() / "s2.jsonl", "{\"b\":3}\n\n   \n{\"b\":4}");
    const std::filesystem::path out = fixture.root.path() / "work" / "merged.jsonl";
    std::string error;
    CHECK(apogee::training::merge_datasets(
              {fixture.root.path() / "s1.jsonl", fixture.root.path() / "s2.jsonl"}, out, error) ==
          4);
    CHECK(error.empty());
    CHECK(read(out) == "{\"a\":1}\n{\"a\":2}\n{\"b\":3}\n{\"b\":4}\n");
    CHECK(apogee::training::merge_datasets({fixture.root.path() / "nope.jsonl"}, out, error) == -1);
    CHECK(error.find("nope.jsonl") != std::string::npos);
}

TEST_CASE("the anchor gate's six cases, and the last passing score", "[training][cycle][gate]") {
    // No prior data: both halves pass.
    CHECK(apogee::training::anchor_gate_pass(0.5, 0.0, 0.0, 0.05).pass());
    // Above both.
    CHECK(apogee::training::anchor_gate_pass(0.85, 0.80, 0.82, 0.05).pass());
    // Within the threshold of both.
    CHECK(apogee::training::anchor_gate_pass(0.78, 0.80, 0.75, 0.05).pass());
    // Below the previous, no anchor.
    apogee::training::AnchorGate gate = apogee::training::anchor_gate_pass(0.70, 0.80, 0.0, 0.05);
    CHECK_FALSE(gate.prev_ok);
    CHECK(gate.anchor_ok);
    CHECK_FALSE(gate.pass());
    // Below the anchor, no previous.
    gate = apogee::training::anchor_gate_pass(0.70, 0.0, 0.82, 0.05);
    CHECK(gate.prev_ok);
    CHECK_FALSE(gate.anchor_ok);
    // Strict: a hair below the previous fails, at or above the anchor passes.
    gate = apogee::training::anchor_gate_pass(0.80, 0.81, 0.79, 0.0);
    CHECK_FALSE(gate.prev_ok);
    CHECK(gate.anchor_ok);
    CHECK(apogee::training::anchor_gate_pass(0.80, 0.80, 0.80, 0.0).pass());

    CycleHistory history;
    CHECK(apogee::training::last_passing_cycle_score(history) == 0.0);
    history.runs = {CycleRunRecord{.gate = "fail"}, CycleRunRecord{.gate = "skipped"}};
    CHECK(apogee::training::last_passing_cycle_score(history) == 0.0);
    history.runs = {
        CycleRunRecord{.gate = "pass", .cycle_score = 0.8}, CycleRunRecord{.gate = "fail"},
        CycleRunRecord{.gate = "pass", .cycle_score = 0.85}, CycleRunRecord{.gate = "fail"}};
    CHECK(apogee::training::last_passing_cycle_score(history) == 0.85);

    // The counters: a fail counts, a pass resets, a skip touches neither.
    CycleHistory counted;
    apogee::training::record_cycle_run(counted, CycleRunRecord{.gate = "fail"});
    apogee::training::record_cycle_run(counted, CycleRunRecord{.gate = "fail"});
    CHECK(counted.consecutive_fails == 2);
    apogee::training::record_cycle_run(counted, CycleRunRecord{.gate = "skipped"});
    CHECK(counted.consecutive_fails == 2);
    CHECK(counted.total_runs == 3);
    apogee::training::record_cycle_run(counted, CycleRunRecord{.gate = "pass"});
    CHECK(counted.consecutive_fails == 0);
    CHECK(counted.total_runs == 4);
    CHECK(counted.runs.size() == 4);
}

TEST_CASE(
    "one pass: no data records skipped without a failure; a pass promotes the final stage, "
    "consumes the queue, sets the anchor on the first pass and resets the count; a fail "
    "counts and halts at k; halted or locked, the pass refuses",
    "[training][cycle][run]") {
    Fixture fixture;
    std::vector<std::string> said;
    apogee::training::CycleRequest request = fixture.request(2);
    request.on_message = [&said](std::string_view text) { said.emplace_back(text); };

    // Nothing queued: skipped, the history started.
    apogee::training::CycleOutcome outcome = apogee::training::run_cycle(request);
    INFO(outcome.error);
    REQUIRE(outcome.ok);
    REQUIRE(outcome.record.has_value());
    CHECK(outcome.record->gate == "skipped");
    CHECK(outcome.record->source.empty());
    CHECK(fixture.history().total_runs == 1);
    CHECK(fixture.history().consecutive_fails == 0);
    CHECK(fixture.promoted.empty());
    CHECK_FALSE(apogee::training::cycle_lock_held(fixture.cycle));  // released

    // Data queued: the pipeline runs on the merged file, the gate passes,
    // promote is called with the final stage, the file is consumed, the
    // anchor is set.
    write(fixture.queue / "day1.jsonl", kPlain);
    outcome = apogee::training::run_cycle(request);
    INFO(outcome.error);
    REQUIRE(outcome.ok);
    CHECK(outcome.record->gate == "pass");
    CHECK(outcome.record->source == "directory");
    CHECK(outcome.record->dataset_rows == 1);
    CHECK(outcome.record->pipeline_id.starts_with("cycle-"));
    CHECK(outcome.record->promoted_version == 1);
    CHECK(outcome.record->cycle_score == 1.0);
    CHECK(outcome.record->prev_score == 0.0);
    REQUIRE(fixture.promoted.size() == 1);
    CHECK(fixture.promoted[0] == outcome.record->pipeline_id + "-s0");
    CHECK_FALSE(std::filesystem::exists(fixture.queue / "day1.jsonl"));
    CHECK(std::filesystem::exists(fixture.queue / "consumed" / "day1.jsonl"));
    CHECK(read(fixture.cycle / "work" / "merged.jsonl") == kPlain);
    CycleHistory history = fixture.history();
    CHECK(history.anchor_version == 1);
    CHECK(history.anchor_score == 1.0);
    CHECK(history.total_runs == 2);
    // The pipeline's stage trained on the merged dataset with its own suite.
    std::string error;
    const auto stage = apogee::training::read_manifest(
        fixture.training / "runs" / (outcome.record->pipeline_id + "-s0"), error);
    REQUIRE(stage.has_value());
    CHECK(stage->dataset == (fixture.cycle / "work" / "merged.jsonl").string());
    CHECK(stage->eval->suite == "suite:hello");

    // A regressing candidate: the pipeline aborts, the cycle fails, nothing
    // is promoted, the file stays queued, the count grows.
    write(fixture.queue / "day2.jsonl", "{\"mock\": {\"answer\": \"nope\"}}\n");
    outcome = apogee::training::run_cycle(request);
    REQUIRE(outcome.ok);
    CHECK(outcome.record->gate == "fail");
    CHECK(outcome.record->gate_reason.find("pipeline aborted") != std::string::npos);
    CHECK(fixture.promoted.size() == 1);
    CHECK(std::filesystem::exists(fixture.queue / "day2.jsonl"));
    CHECK(fixture.history().consecutive_fails == 1);
    CHECK_FALSE(outcome.halted_now);
    CHECK_FALSE(fixture.history().halted);

    // Again: k = 2 reached, the loop halts, the record carries the note.
    outcome = apogee::training::run_cycle(request);
    REQUIRE(outcome.ok);
    CHECK(outcome.record->gate == "fail");
    CHECK(outcome.halted_now);
    CHECK(outcome.record->note.find("circuit breaker") != std::string::npos);
    history = fixture.history();
    CHECK(history.halted);
    CHECK(history.consecutive_fails == 2);
    CHECK(history.total_runs == 4);

    // Halted: refused naming resume, nothing recorded.
    outcome = apogee::training::run_cycle(request);
    CHECK_FALSE(outcome.ok);
    CHECK(outcome.error.find("halted") != std::string::npos);
    CHECK(outcome.error.find("apogee train cycle resume") != std::string::npos);
    CHECK(fixture.history().total_runs == 4);
    CHECK_FALSE(apogee::training::cycle_lock_held(fixture.cycle));

    // Resumed, the bad file gone, a good one queued: the pass counts against
    // the previous score and the anchor, passes, resets the count.
    REQUIRE(apogee::training::resume_cycle(history));
    REQUIRE(apogee::training::save_history(fixture.cycle, history).empty());
    std::filesystem::remove(fixture.queue / "day2.jsonl");
    write(fixture.queue / "day3.jsonl", kPlain);
    outcome = apogee::training::run_cycle(request);
    REQUIRE(outcome.ok);
    CHECK(outcome.record->gate == "pass");
    CHECK(outcome.record->prev_score == 1.0);
    CHECK(outcome.record->anchor_score == 1.0);
    CHECK(outcome.record->promoted_version == 2);
    CHECK(fixture.history().consecutive_fails == 0);
    CHECK(fixture.history().anchor_version == 1);  // the first pass's, kept

    // Locked: refused naming the lock.
    std::string lock_error;
    const std::optional<CycleLock> held = CycleLock::acquire(fixture.cycle, lock_error);
    REQUIRE(held.has_value());
    outcome = apogee::training::run_cycle(request);
    CHECK_FALSE(outcome.ok);
    CHECK(outcome.error.find("another cycle is already running") != std::string::npos);
}

TEST_CASE(
    "a failed promotion counts as a failed cycle, the --source override replaces the queue, "
    "a pinned anchor_version is honoured, and the sessions source advances the watermark",
    "[training][cycle][run]") {
    Fixture fixture;
    apogee::training::CycleRequest request = fixture.request(3);
    request.promote = [](std::string_view) {
        return apogee::training::CyclePromotion{.ok = false, .error = "no converter"};
    };
    const std::filesystem::path elsewhere = fixture.root.path() / "elsewhere";
    write(elsewhere / "x.jsonl", kPlain);
    request.source_override = elsewhere;
    apogee::training::CycleOutcome outcome = apogee::training::run_cycle(request);
    REQUIRE(outcome.ok);
    CHECK(outcome.record->gate == "fail");
    CHECK(outcome.record->gate_reason == "promote: no converter");
    CHECK(fixture.history().consecutive_fails == 1);
    CHECK(std::filesystem::exists(elsewhere / "x.jsonl"));  // not consumed on a fail
    CHECK_FALSE(std::filesystem::exists(fixture.queue / "x.jsonl"));

    // A pinned anchor: its score is the history's record for that version.
    Fixture pinned;
    apogee::training::CycleRequest pin = pinned.request(3);
    pin.config.anchor_version = 1;
    CycleHistory seeded;
    seeded.backend = "nightly-model";
    seeded.runs.push_back(
        CycleRunRecord{.gate = "pass", .cycle_score = 1.0, .promoted_version = 1});
    seeded.runs.push_back(
        CycleRunRecord{.gate = "pass", .cycle_score = 0.5, .promoted_version = 2});
    seeded.total_runs = 2;
    REQUIRE(apogee::training::save_history(pinned.cycle, seeded).empty());
    write(pinned.queue / "a.jsonl", kPlain);
    outcome = apogee::training::run_cycle(pin);
    REQUIRE(outcome.ok);
    CHECK(outcome.record->anchor_score == 1.0);
    CHECK(outcome.record->prev_score == 0.5);
    CHECK(outcome.record->gate == "pass");
    CHECK(pinned.history().anchor_version == 1);

    // Sessions: consumed once through the watermark.
    Fixture sessions;
    using apogee::harness::ChatMessage;
    const std::vector<ChatMessage> full{ChatMessage::user("say hello"),
                                        ChatMessage::assistant("hello")};
    apogee::training::CycleRequest with_sessions = sessions.request(3);
    with_sessions.config.sources.clear();
    CycleSourceConfig source;
    source.type = "sessions";
    source.log_consent = true;
    with_sessions.config.sources.push_back(source);
    with_sessions.sessions = {
        {.backend = "local", .started_at = "2026-09-18T10:00:00Z", .messages = &full},
        {.backend = "local", .started_at = "2026-09-19T10:00:00Z", .messages = &full}};
    outcome = apogee::training::run_cycle(with_sessions);
    INFO(outcome.error);
    REQUIRE(outcome.ok);
    CHECK(outcome.record->gate == "pass");
    CHECK(outcome.record->source == "sessions");
    CHECK(outcome.record->dataset_rows == 2);
    CHECK(sessions.history().sessions_until == "2026-09-19T10:00:00Z");
    outcome = apogee::training::run_cycle(with_sessions);
    REQUIRE(outcome.ok);
    CHECK(outcome.record->gate == "skipped");
    CHECK(sessions.promoted.size() == 1);
    // Without consent the pass refuses before anything runs.
    with_sessions.config.sources[0].log_consent = false;
    outcome = apogee::training::run_cycle(with_sessions);
    CHECK_FALSE(outcome.ok);
    CHECK(outcome.error.find("log_consent") != std::string::npos);
    CHECK(sessions.history().total_runs == 2);
}

TEST_CASE(
    "the dual gate has teeth under the soft gate: the final stage's score is the cycle's, a "
    "regression against the last pass and the anchor fails and discards, a drop within the "
    "threshold passes, and an equal score passes",
    "[training][cycle][run][gate]") {
    Fixture fixture;
    apogee::training::CycleRequest request = fixture.request(3);
    request.gate_mode = apogee::training::GateMode::Soft;
    request.stages = {
        apogee::training::ResolvedStage{.dataset = {},
                                        .suite = {{"say hello", "hello"}, {"count", "three"}},
                                        .suite_label = "suite:two"}};

    // The echo passes one of two: 50%, the first pass, the anchor.
    write(fixture.queue / "a.jsonl", kPlain);
    apogee::training::CycleOutcome outcome = apogee::training::run_cycle(request);
    INFO(outcome.error);
    REQUIRE(outcome.ok);
    CHECK(outcome.record->gate == "pass");
    CHECK(outcome.record->cycle_score == 0.5);
    CHECK(fixture.history().anchor_score == 0.5);
    CHECK(fixture.promoted.size() == 1);

    // A candidate answering "three" passes the other one: equal, passes.
    write(fixture.queue / "b.jsonl", "{\"mock\": {\"answer\": \"three\"}}\n");
    outcome = apogee::training::run_cycle(request);
    REQUIRE(outcome.ok);
    CHECK(outcome.record->gate == "pass");
    CHECK(outcome.record->cycle_score == 0.5);
    CHECK(outcome.record->prev_score == 0.5);
    CHECK(fixture.promoted.size() == 2);

    // A candidate answering neither: 0%, regressed against both halves --
    // the pipeline completed (soft), the dual gate discards it.
    write(fixture.queue / "c.jsonl", "{\"mock\": {\"answer\": \"zzz\"}}\n");
    outcome = apogee::training::run_cycle(request);
    REQUIRE(outcome.ok);
    CHECK(outcome.record->gate == "fail");
    CHECK(outcome.record->cycle_score == 0.0);
    CHECK(outcome.record->gate_reason.find("regressed against the previous passing cycle") !=
          std::string::npos);
    CHECK(outcome.record->gate_reason.find("regressed against anchor v1") != std::string::npos);
    CHECK(fixture.promoted.size() == 2);
    CHECK(std::filesystem::exists(fixture.queue / "c.jsonl"));
    CHECK(fixture.history().consecutive_fails == 1);

    // The same candidate within a generous threshold passes.
    request.config.regression_threshold = 0.6;
    outcome = apogee::training::run_cycle(request);
    REQUIRE(outcome.ok);
    CHECK(outcome.record->gate == "pass");
    CHECK(fixture.promoted.size() == 3);
    CHECK(fixture.history().consecutive_fails == 0);
}
