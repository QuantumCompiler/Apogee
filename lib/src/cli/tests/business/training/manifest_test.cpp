#include "training/manifest.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "support/env_guard.h"

/// The run manifest round-trips every field including `status`, the error
/// and the eval; it is written atomically and read back; an unreadable or
/// shapeless one names its problem; run ids are timestamps with a suffix
/// on collision and never path-shaped; the dataset digest is a sha256
/// prefix.
namespace {

using apogee::training::EvalItemResult;
using apogee::training::EvalResults;
using apogee::training::RunManifest;

RunManifest sample() {
    RunManifest m;
    m.run_id = "20260919-120000";
    m.trainer = "mlx";
    m.base_model = "/snap/tiny";
    m.dataset = "/data/set.jsonl";
    m.dataset_hash = "abc123def456";
    m.method = "qlora";
    m.iters = 100;
    m.batch_size = 4;
    m.num_layers = 16;
    m.grad_checkpoint = true;
    m.mask_prompt = true;
    m.final_loss = 0.42;
    m.iterations = 100;
    m.adapter_dir = "/runs/x/adapters";
    m.status = std::string{apogee::training::kStatusFailed};
    m.error = "the driver gave up";
    m.started_at = "2026-09-19T12:00:00Z";
    m.finished_at = "2026-09-19T12:30:00Z";
    EvalResults eval;
    eval.passed = false;
    eval.score = 0.5;
    eval.total = 2;
    eval.num_passed = 1;
    eval.judge_backend = "paid";
    eval.suite = "kit:reasoning";
    eval.run_at = "2026-09-19T13:00:00Z";
    EvalItemResult item;
    item.prompt = "p";
    item.expected = "e";
    item.got = "g";
    item.check_type = "contains";
    item.passed = false;
    eval.items.push_back(item);
    EvalItemResult judged;
    judged.prompt = "q";
    judged.got = "a";
    judged.check_type = "judge";
    judged.passed = true;
    judged.baseline_got = "b";
    judged.judge_winner = "candidate";
    eval.items.push_back(judged);
    m.eval = eval;
    return m;
}

}  // namespace

TEST_CASE("a manifest round-trips through JSON, status and eval included", "[training][manifest]") {
    const RunManifest original = sample();
    const nlohmann::json json = apogee::training::manifest_to_json(original);
    CHECK(json["status"] == "failed");
    CHECK(json["error"] == "the driver gave up");
    CHECK(json["eval_results"]["items"][1]["judge_winner"] == "candidate");
    CHECK_FALSE(json["eval_results"]["items"][1].contains("expected"));
    CHECK_FALSE(json["eval_results"]["items"][0].contains("judge_winner"));
    const RunManifest back = apogee::training::manifest_from_json(json);
    CHECK(back.run_id == original.run_id);
    CHECK(back.trainer == "mlx");
    CHECK(back.method == "qlora");
    CHECK(back.iters == 100);
    CHECK(back.grad_checkpoint);
    CHECK(back.mask_prompt);
    CHECK(back.final_loss == 0.42);
    CHECK(back.status == "failed");
    CHECK(back.error == "the driver gave up");
    CHECK(back.finished_at == "2026-09-19T12:30:00Z");
    REQUIRE(back.eval.has_value());
    CHECK(back.eval->score == 0.5);
    CHECK(back.eval->suite == "kit:reasoning");
    REQUIRE(back.eval->items.size() == 2);
    CHECK(back.eval->items[0].expected == "e");
    CHECK(back.eval->items[1].baseline_got == "b");
    CHECK_FALSE(back.complete());

    // A complete run with no eval, no error: those keys are absent.
    RunManifest clean = original;
    clean.status = std::string{apogee::training::kStatusComplete};
    clean.error.clear();
    clean.eval.reset();
    const nlohmann::json lean = apogee::training::manifest_to_json(clean);
    CHECK_FALSE(lean.contains("error"));
    CHECK_FALSE(lean.contains("eval_results"));
    CHECK(apogee::training::manifest_from_json(lean).complete());

    // A manifest with no status (a reference-era file) reads as complete;
    // one with no run_id is refused.
    CHECK(apogee::training::manifest_from_json(nlohmann::json{{"run_id", "x"}}).complete());
    CHECK_THROWS_AS(apogee::training::manifest_from_json(nlohmann::json{{"trainer", "mlx"}}),
                    std::runtime_error);
    CHECK_THROWS_AS(apogee::training::manifest_from_json(nlohmann::json::array()),
                    std::runtime_error);
}

TEST_CASE("write_manifest lands the file atomically and read_manifest names what is wrong",
          "[training][manifest]") {
    const apogee::testing::TempDir root{"manifest-" + std::to_string(std::random_device{}())};
    const std::filesystem::path run_dir = root.path() / "runs" / "20260919-120000";
    CHECK(apogee::training::write_manifest(run_dir, sample()).empty());
    CHECK(std::filesystem::exists(apogee::training::manifest_path(run_dir)));
    std::string error;
    const auto back = apogee::training::read_manifest(run_dir, error);
    REQUIRE(back.has_value());
    CHECK(error.empty());
    CHECK(back->run_id == "20260919-120000");
    // No temp file left beside it.
    int entries = 0;
    for (const auto& entry : std::filesystem::directory_iterator(run_dir)) {
        (void)entry;
        ++entries;
    }
    CHECK(entries == 1);

    CHECK_FALSE(apogee::training::read_manifest(root.path() / "nope", error).has_value());
    CHECK(error.find("no manifest") != std::string::npos);
    std::ofstream{apogee::training::manifest_path(run_dir), std::ios::binary} << "{not json";
    CHECK_FALSE(apogee::training::read_manifest(run_dir, error).has_value());
    CHECK(error.find("not JSON") != std::string::npos);
    std::ofstream{apogee::training::manifest_path(run_dir), std::ios::binary} << "{\"a\": 1}";
    CHECK_FALSE(apogee::training::read_manifest(run_dir, error).has_value());
    CHECK(error.find("run_id") != std::string::npos);
    // A field of the wrong type is named with its file (57h), as a malformed
    // one is -- never the library's bare message, so a list can skip it.
    std::ofstream{apogee::training::manifest_path(run_dir), std::ios::binary}
        << R"({"run_id": "20260919-120000", "iters": "two"})";
    CHECK_FALSE(apogee::training::read_manifest(run_dir, error).has_value());
    CHECK(error.starts_with(apogee::training::manifest_path(run_dir).string() + ": "));
    CHECK(error.find("type must be number") != std::string::npos);
}

namespace {

/// A reserved id, its error required empty.
std::string reserved(const std::filesystem::path& dir, std::chrono::system_clock::time_point at,
                     std::string_view prefix = {}) {
    std::string error;
    std::string id = apogee::training::new_run_id(dir, at, prefix, error);
    INFO(error);
    REQUIRE(error.empty());
    return id;
}

}  // namespace

TEST_CASE(
    "run ids are UTC timestamps, reserved by their directory, suffixed on collision, "
    "and never path-shaped",
    "[training][manifest][id]") {
    const apogee::testing::TempDir root{"runid-" + std::to_string(std::random_device{}())};
    const std::chrono::system_clock::time_point at =
        std::chrono::system_clock::from_time_t(1789000000);  // 2026-09-10T00:26:40Z
    // Each reservation makes its directory (57g), so the next in the same
    // second takes the next suffix with nothing written in between.
    CHECK(reserved(root.path(), at) == "20260910-002640");
    CHECK(std::filesystem::is_directory(root.path() / "20260910-002640"));
    CHECK(reserved(root.path(), at) == "20260910-002640-2");
    CHECK(std::filesystem::is_directory(root.path() / "20260910-002640-2"));
    // A directory already there, and a file of the name, are both taken.
    std::filesystem::create_directories(root.path() / "20260910-002640-3");
    std::ofstream{root.path() / "20260910-002640-4", std::ios::binary} << "x";
    CHECK(reserved(root.path(), at) == "20260910-002640-5");

    CHECK(apogee::training::valid_run_id("20260910-002640-2"));
    CHECK(apogee::training::valid_run_id("pipe_x-s1"));
    CHECK_FALSE(apogee::training::valid_run_id(""));
    CHECK_FALSE(apogee::training::valid_run_id("../etc"));
    CHECK_FALSE(apogee::training::valid_run_id("a/b"));
    CHECK_FALSE(apogee::training::valid_run_id("a b"));
    CHECK_FALSE(apogee::training::valid_run_id(std::string(65, 'a')));
}

TEST_CASE("the dataset digest is the file's sha256 prefix, and empty for a missing file",
          "[training][manifest][digest]") {
    const apogee::testing::TempDir root{"digest-" + std::to_string(std::random_device{}())};
    std::ofstream{root.path() / "a.jsonl", std::ios::binary} << "abc";
    // sha256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
    CHECK(apogee::training::dataset_digest(root.path() / "a.jsonl") == "ba7816bf8f01");
    CHECK(apogee::training::dataset_digest(root.path() / "missing").empty());
}

TEST_CASE("a stage run's lineage round-trips and is absent from a plain run; a prefixed id",
          "[training][manifest][pipeline]") {
    RunManifest stage = sample();
    stage.parent_run = "pipe-1-s0";
    stage.pipeline_run_id = "pipe-1";
    const nlohmann::json json = apogee::training::manifest_to_json(stage);
    CHECK(json["parent_run"] == "pipe-1-s0");
    CHECK(json["pipeline_run_id"] == "pipe-1");
    const RunManifest back = apogee::training::manifest_from_json(json);
    CHECK(back.parent_run == "pipe-1-s0");
    CHECK(back.pipeline_run_id == "pipe-1");
    const nlohmann::json plain = apogee::training::manifest_to_json(sample());
    CHECK_FALSE(plain.contains("parent_run"));
    CHECK_FALSE(plain.contains("pipeline_run_id"));
    CHECK(apogee::training::manifest_from_json(plain).pipeline_run_id.empty());

    const apogee::testing::TempDir root{"manifest-prefix-" +
                                        std::to_string(std::random_device{}())};
    const std::chrono::system_clock::time_point at =
        std::chrono::system_clock::from_time_t(1789000000);
    CHECK(reserved(root.path(), at, "pipe-") == "pipe-20260910-002640");
    CHECK(reserved(root.path(), at, "cycle-") == "cycle-20260910-002640");
    CHECK(reserved(root.path(), at, "pipe-") == "pipe-20260910-002640-2");
    CHECK(apogee::training::valid_run_id("pipe-20260910-002640-2-s0"));
}

TEST_CASE(
    "two reservations of one second's id, from two threads at once, get two ids and "
    "two directories",
    "[training][manifest][id]") {
    // The race `train pipeline run` twice at once ran into (57g, probe pI):
    // every taker of one second's id at the same moment, each its own.
    const apogee::testing::TempDir root{"runid-race-" + std::to_string(std::random_device{}())};
    const std::chrono::system_clock::time_point at =
        std::chrono::system_clock::from_time_t(1789000000);
    constexpr int kTakers = 8;
    std::vector<std::string> ids(kTakers);
    std::vector<std::string> errors(kTakers);
    {
        std::vector<std::thread> takers;
        std::atomic<bool> go{false};
        for (int i = 0; i < kTakers; ++i) {
            takers.emplace_back([&, i]() {
                while (!go.load()) {
                    std::this_thread::yield();
                }
                ids[static_cast<std::size_t>(i)] = apogee::training::new_run_id(
                    root.path(), at, "pipe-", errors[static_cast<std::size_t>(i)]);
            });
        }
        go.store(true);
        for (std::thread& taker : takers) {
            taker.join();
        }
    }
    const std::set<std::string> distinct{ids.begin(), ids.end()};
    CHECK(distinct.size() == static_cast<std::size_t>(kTakers));
    for (std::size_t i = 0; i < ids.size(); ++i) {
        CHECK(errors[i].empty());
        CHECK(ids[i].starts_with("pipe-20260910-002640"));
        CHECK(std::filesystem::is_directory(root.path() / ids[i]));
    }
}

TEST_CASE("a run id that cannot be reserved is an error naming the place, before anything runs",
          "[training][manifest][id]") {
    const apogee::testing::TempDir root{"runid-blocked-" + std::to_string(std::random_device{}())};
    // `runs` is a file: nothing can be made under it.
    std::ofstream{root.path() / "runs", std::ios::binary} << "not a directory";
    std::string error;
    const std::string id = apogee::training::new_run_id(
        root.path() / "runs", std::chrono::system_clock::from_time_t(1789000000), {}, error);
    CHECK(id.empty());
    CHECK(error.find((root.path() / "runs").string()) != std::string::npos);
}
