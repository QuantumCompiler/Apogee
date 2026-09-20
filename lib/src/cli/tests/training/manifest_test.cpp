#include "training/manifest.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

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
}

TEST_CASE("run ids are UTC timestamps, suffixed on collision, and never path-shaped",
          "[training][manifest][id]") {
    const apogee::testing::TempDir root{"runid-" + std::to_string(std::random_device{}())};
    const std::chrono::system_clock::time_point at =
        std::chrono::system_clock::from_time_t(1789000000);  // 2026-09-10T00:26:40Z
    CHECK(apogee::training::new_run_id(root.path(), at) == "20260910-002640");
    std::filesystem::create_directories(root.path() / "20260910-002640");
    CHECK(apogee::training::new_run_id(root.path(), at) == "20260910-002640-2");
    std::filesystem::create_directories(root.path() / "20260910-002640-2");
    CHECK(apogee::training::new_run_id(root.path(), at) == "20260910-002640-3");

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
