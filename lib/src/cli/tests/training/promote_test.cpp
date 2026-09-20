#include "training/promote.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "support/env_guard.h"
#include "training/manifest.h"
#include "training/mock_trainer.h"
#include "training/store.h"

/// Promotion's arithmetic and its guarantees, on the mock trainer with
/// scripted closures: the gate under hard, soft and --force; `max + 1`
/// after a prune (the reference derived numbers from the count, so they
/// regressed and a live file was overwritten); fuse exactly once, the
/// converter invoked with the fused tree and the version path, an
/// unparseable GGUF leaving nothing behind, the quantizer with the F16
/// removed after, `--keep-fused`; retention never pruning the active
/// version and marking rather than erasing; the rollback target -- no
/// history, one version, a pruned one named, a missing file.
namespace {

using apogee::training::build_promotion_artifacts;
using apogee::training::eval_gate;
using apogee::training::GateMode;
using apogee::training::GateVerdict;
using apogee::training::MockTrainer;
using apogee::training::MockTrainerOptions;
using apogee::training::PromotePlan;
using apogee::training::RunManifest;
using apogee::training::VersionEntry;
using apogee::training::VersionLedger;

void write(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << content;
}

RunManifest complete_run(const std::filesystem::path& root, bool evaluated = true,
                         bool passed = true) {
    write(root / "snap" / "config.json", R"({"model_type": "llama"})");
    write(root / "snap" / "model.safetensors", "w");
    write(root / "runs" / "r1" / "adapters" / "adapters.safetensors", "a");
    RunManifest m;
    m.run_id = "r1";
    m.trainer = "mock";
    m.base_model = (root / "snap").string();
    m.adapter_dir = (root / "runs" / "r1" / "adapters").string();
    m.status = std::string{apogee::training::kStatusComplete};
    if (evaluated) {
        apogee::training::EvalResults eval;
        eval.passed = passed;
        eval.score = passed ? 1.0 : 0.5;
        eval.total = 2;
        eval.num_passed = passed ? 2 : 1;
        m.eval = eval;
    }
    return m;
}

VersionEntry entry(int version, const std::filesystem::path& dir, bool pruned = false) {
    VersionEntry e;
    e.version = version;
    e.run_id = "r" + std::to_string(version);
    e.gguf_path = (dir / ("v" + std::to_string(version) + ".gguf")).string();
    e.promoted_at = "2026-09-19T12:00:0" + std::to_string(version) + "Z";
    if (pruned) {
        e.pruned_at = "2026-09-19T13:00:00Z";
    } else {
        write(e.gguf_path, "gguf");
    }
    return e;
}

apogee::training::Converter mock_convert(int& calls, std::filesystem::path* fused_seen = nullptr) {
    return [&calls, fused_seen](
               const std::filesystem::path& fused, const std::filesystem::path& gguf,
               const apogee::training::MessageSink&, const apogee::harness::CancellationToken&) {
        ++calls;
        if (fused_seen != nullptr) {
            *fused_seen = fused;
        }
        return apogee::training::write_mock_gguf(fused, gguf);
    };
}

apogee::training::Verifier accept() {
    return [](const std::filesystem::path&) { return std::string{}; };
}

apogee::training::Quantizer no_quantize() {
    return [](const std::filesystem::path&, const std::filesystem::path&, std::string_view) {
        return std::string{"should not be called"};
    };
}

}  // namespace

TEST_CASE(
    "the eval gate: hard refuses an unevaluated or failing run, soft warns, --force skips, "
    "an incomplete run is always refused",
    "[training][promote][gate]") {
    const apogee::testing::TempDir root{"gate-" + std::to_string(std::random_device{}())};
    CHECK(eval_gate(complete_run(root.path()), GateMode::Hard, false).ok);
    CHECK(eval_gate(complete_run(root.path()), GateMode::Hard, false).warning.empty());

    const GateVerdict none = eval_gate(complete_run(root.path(), false), GateMode::Hard, false);
    CHECK_FALSE(none.ok);
    CHECK(none.error.find("no eval results") != std::string::npos);
    CHECK(none.error.find("apogee train eval r1") != std::string::npos);
    CHECK(none.error.find("--force") != std::string::npos);

    const GateVerdict failed =
        eval_gate(complete_run(root.path(), true, false), GateMode::Hard, false);
    CHECK_FALSE(failed.ok);
    CHECK(failed.error.find("50%") != std::string::npos);
    CHECK(failed.error.find("1/2") != std::string::npos);

    const GateVerdict soft =
        eval_gate(complete_run(root.path(), true, false), GateMode::Soft, false);
    CHECK(soft.ok);
    CHECK(soft.warning.find("gate_mode is soft") != std::string::npos);
    CHECK(eval_gate(complete_run(root.path(), false), GateMode::Soft, false).ok);

    const GateVerdict forced = eval_gate(complete_run(root.path(), false), GateMode::Hard, true);
    CHECK(forced.ok);
    CHECK(forced.warning.find("--force") != std::string::npos);

    RunManifest running = complete_run(root.path());
    running.status = std::string{apogee::training::kStatusRunning};
    CHECK_FALSE(eval_gate(running, GateMode::Soft, true).ok);
    RunManifest crashed = complete_run(root.path());
    crashed.status = std::string{apogee::training::kStatusFailed};
    crashed.error = "boom";
    const GateVerdict dead = eval_gate(crashed, GateMode::Hard, true);
    CHECK_FALSE(dead.ok);
    CHECK(dead.error.find("failed") != std::string::npos);
    CHECK(dead.error.find("boom") != std::string::npos);

    CHECK(apogee::training::gate_mode_from_string("soft") == GateMode::Soft);
    CHECK(apogee::training::gate_mode_from_string("hard") == GateMode::Hard);
    CHECK(apogee::training::gate_mode_from_string("") == GateMode::Hard);
}

TEST_CASE("version numbers are max + 1, never the count -- the reference's regression pinned",
          "[training][promote][version]") {
    const apogee::testing::TempDir root{"version-" + std::to_string(std::random_device{}())};
    VersionLedger ledger;
    ledger.backend = "tuned";
    CHECK(apogee::training::next_version(ledger) == 1);
    ledger.versions = {entry(1, root.path(), true), entry(2, root.path(), true),
                       entry(3, root.path())};
    ledger.active_version = 3;
    // Two of three pruned: the count says 1, max + 1 says 4. With the
    // count, the next promote would have written v2.gguf over history.
    CHECK(ledger.kept() == 1);
    CHECK(apogee::training::next_version(ledger) == 4);

    const PromotePlan plan = apogee::training::plan_promotion(
        ledger, root.path() / "runs" / "r9", root.path() / "versions", "tuned", "", false);
    CHECK(plan.version == 4);
    CHECK(plan.fused_dir == root.path() / "runs" / "r9" / "fused");
    CHECK(plan.gguf_path == root.path() / "versions" / "tuned" / "v4.gguf");
    CHECK(plan.f16_path == plan.gguf_path);
    CHECK_FALSE(plan.keep_fused);
    const PromotePlan quantized = apogee::training::plan_promotion(
        ledger, root.path() / "runs" / "r9", root.path() / "versions", "tuned", "Q4_K_M", true);
    CHECK(quantized.f16_path == root.path() / "versions" / "tuned" / "v4.f16.gguf");
    CHECK(quantized.gguf_path == root.path() / "versions" / "tuned" / "v4.gguf");
    CHECK(quantized.quantize_type == "Q4_K_M");
    CHECK(quantized.keep_fused);
}

TEST_CASE(
    "the artifacts: fuse once, the converter with the fused tree and the version path, "
    "the fused tree dropped unless kept, the GGUF's size reported",
    "[training][promote][artifacts]") {
    const apogee::testing::TempDir root{"artifacts-" + std::to_string(std::random_device{}())};
    const RunManifest manifest = complete_run(root.path());
    VersionLedger ledger;
    ledger.backend = "tuned";
    const PromotePlan plan = apogee::training::plan_promotion(
        ledger, root.path() / "runs" / "r1", root.path() / "versions", "tuned", "", false);
    MockTrainer trainer;
    int converts = 0;
    std::filesystem::path fused_seen;
    std::vector<std::string> messages;
    const apogee::training::ArtifactsResult built = build_promotion_artifacts(
        manifest, plan, trainer, mock_convert(converts, &fused_seen), accept(), no_quantize(),
        [&messages](std::string_view text) { messages.emplace_back(text); }, {});
    INFO(built.error);
    REQUIRE(built.ok);
    CHECK(converts == 1);
    CHECK(fused_seen == plan.fused_dir);
    CHECK(std::filesystem::is_regular_file(plan.gguf_path));
    CHECK(built.gguf_bytes > 0);
    CHECK_FALSE(std::filesystem::exists(plan.fused_dir));
    bool fused_message = false;
    for (const std::string& message : messages) {
        fused_message = fused_message || message.find("fusing") != std::string::npos;
    }
    CHECK(fused_message);

    // A second build into the same version refuses: the path is taken.
    const apogee::training::ArtifactsResult again = build_promotion_artifacts(
        manifest, plan, trainer, mock_convert(converts), accept(), no_quantize(), {}, {});
    CHECK_FALSE(again.ok);
    CHECK(again.error.find("already exists") != std::string::npos);
    CHECK(converts == 1);

    // --keep-fused leaves the checkpoint.
    ledger.versions = {entry(1, root.path() / "versions" / "tuned")};
    ledger.active_version = 1;
    const PromotePlan kept = apogee::training::plan_promotion(
        ledger, root.path() / "runs" / "r1", root.path() / "versions", "tuned", "", true);
    const apogee::training::ArtifactsResult with_fused = build_promotion_artifacts(
        manifest, kept, trainer, mock_convert(converts), accept(), no_quantize(), {}, {});
    REQUIRE(with_fused.ok);
    CHECK(std::filesystem::exists(kept.fused_dir / "config.json"));
    CHECK(kept.version == 2);
}

TEST_CASE(
    "a failure at any step leaves nothing behind: fuse, convert, an unparseable GGUF, "
    "the quantizer, cancellation",
    "[training][promote][artifacts]") {
    const apogee::testing::TempDir root{"artifacts-fail-" + std::to_string(std::random_device{}())};
    const RunManifest manifest = complete_run(root.path());
    VersionLedger ledger;
    ledger.backend = "tuned";
    const PromotePlan plan = apogee::training::plan_promotion(
        ledger, root.path() / "runs" / "r1", root.path() / "versions", "tuned", "", false);
    int converts = 0;

    MockTrainer no_fuse{MockTrainerOptions{.fuse_error = "GPU busy"}};
    const apogee::training::ArtifactsResult fuse_failed = build_promotion_artifacts(
        manifest, plan, no_fuse, mock_convert(converts), accept(), no_quantize(), {}, {});
    CHECK_FALSE(fuse_failed.ok);
    CHECK(fuse_failed.error == "fuse: GPU busy");
    CHECK(converts == 0);
    CHECK_FALSE(std::filesystem::exists(plan.gguf_path));

    MockTrainer trainer;
    const apogee::training::ArtifactsResult convert_failed = build_promotion_artifacts(
        manifest, plan, trainer,
        [](const std::filesystem::path&, const std::filesystem::path& gguf,
           const apogee::training::MessageSink&, const apogee::harness::CancellationToken&) {
            write(gguf, "half a file");
            return std::string{"converter died"};
        },
        accept(), no_quantize(), {}, {});
    CHECK_FALSE(convert_failed.ok);
    CHECK(convert_failed.error == "convert: converter died");
    CHECK_FALSE(std::filesystem::exists(plan.gguf_path));
    CHECK_FALSE(std::filesystem::exists(plan.fused_dir));

    // The converter wrote something the header reader rejects: it must not
    // be registered, so it must not exist.
    const apogee::training::ArtifactsResult unparseable = build_promotion_artifacts(
        manifest, plan, trainer, mock_convert(converts),
        [](const std::filesystem::path&) { return std::string{"bad magic"}; }, no_quantize(), {},
        {});
    CHECK_FALSE(unparseable.ok);
    CHECK(unparseable.error.find("does not parse") != std::string::npos);
    CHECK(unparseable.error.find("bad magic") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(plan.gguf_path));

    const PromotePlan quantized = apogee::training::plan_promotion(
        ledger, root.path() / "runs" / "r1", root.path() / "versions", "tuned", "Q4_K_M", false);
    const apogee::training::ArtifactsResult quantize_failed = build_promotion_artifacts(
        manifest, quantized, trainer, mock_convert(converts), accept(),
        [](const std::filesystem::path&, const std::filesystem::path& out, std::string_view) {
            write(out, "partial");
            return std::string{"no llama.cpp"};
        },
        {}, {});
    CHECK_FALSE(quantize_failed.ok);
    CHECK(quantize_failed.error == "quantize: no llama.cpp");
    CHECK_FALSE(std::filesystem::exists(quantized.gguf_path));
    CHECK_FALSE(std::filesystem::exists(quantized.f16_path));

    // And the happy quantized path: the F16 removed once the small one is
    // verified.
    std::vector<std::string> quantize_calls;
    const apogee::training::ArtifactsResult quantize_ok = build_promotion_artifacts(
        manifest, quantized, trainer, mock_convert(converts), accept(),
        [&quantize_calls](const std::filesystem::path& in, const std::filesystem::path& out,
                          std::string_view type) {
            quantize_calls.push_back(in.filename().string() + "->" + out.filename().string() + "@" +
                                     std::string{type});
            std::filesystem::copy_file(in, out);
            return std::string{};
        },
        {}, {});
    INFO(quantize_ok.error);
    REQUIRE(quantize_ok.ok);
    CHECK(quantize_calls == std::vector<std::string>{"v1.f16.gguf->v1.gguf@Q4_K_M"});
    CHECK(std::filesystem::exists(quantized.gguf_path));
    CHECK_FALSE(std::filesystem::exists(quantized.f16_path));
    std::filesystem::remove(quantized.gguf_path);

    const apogee::harness::CancellationToken token = apogee::harness::CancellationToken::create();
    token.cancel();
    const apogee::training::ArtifactsResult cancelled = build_promotion_artifacts(
        manifest, plan, trainer, mock_convert(converts), accept(), no_quantize(), {}, token);
    CHECK_FALSE(cancelled.ok);
    CHECK(cancelled.cancelled);
    CHECK_FALSE(std::filesystem::exists(plan.gguf_path));
}

TEST_CASE(
    "retention prunes the oldest kept versions beyond retain, never the active one, "
    "removing the file and marking the entry",
    "[training][promote][retain]") {
    const apogee::testing::TempDir root{"retain-" + std::to_string(std::random_device{}())};
    VersionLedger ledger;
    ledger.backend = "tuned";
    ledger.versions = {entry(1, root.path()), entry(2, root.path()), entry(3, root.path())};
    ledger.active_version = 3;
    CHECK(apogee::training::prune_candidates(ledger, 0).empty());
    CHECK(apogee::training::prune_candidates(ledger, 3).empty());
    CHECK(apogee::training::prune_candidates(ledger, 2) == std::vector<int>{1});
    CHECK(apogee::training::prune_candidates(ledger, 1) == std::vector<int>{1, 2});

    // The active one is skipped even when it is the oldest.
    ledger.active_version = 1;
    CHECK(apogee::training::prune_candidates(ledger, 1) == std::vector<int>{2, 3});

    ledger.active_version = 3;
    VersionEntry v4 = entry(4, root.path());
    const apogee::training::PruneResult pruned =
        apogee::training::record_promotion(ledger, v4, 2, "2026-09-19T15:00:00Z");
    CHECK(ledger.active_version == 4);
    REQUIRE(ledger.versions.size() == 4);
    CHECK(pruned.removed == std::vector<std::string>{(root.path() / "v1.gguf").string(),
                                                     (root.path() / "v2.gguf").string()});
    CHECK(pruned.failed.empty());
    CHECK_FALSE(std::filesystem::exists(root.path() / "v1.gguf"));
    CHECK_FALSE(std::filesystem::exists(root.path() / "v2.gguf"));
    CHECK(std::filesystem::exists(root.path() / "v3.gguf"));
    CHECK(std::filesystem::exists(root.path() / "v4.gguf"));
    CHECK(ledger.versions[0].pruned_at == "2026-09-19T15:00:00Z");
    CHECK(ledger.versions[1].pruned());
    CHECK_FALSE(ledger.versions[2].pruned());
    CHECK(ledger.kept() == 2);
    // The history is kept: the numbers stay taken.
    CHECK(apogee::training::next_version(ledger) == 5);

    // A file already gone is pruned quietly; retain 0 keeps everything.
    VersionEntry v5 = entry(5, root.path());
    std::filesystem::remove(root.path() / "v3.gguf");
    const apogee::training::PruneResult again =
        apogee::training::record_promotion(ledger, v5, 1, "t");
    CHECK(again.removed == std::vector<std::string>{(root.path() / "v3.gguf").string(),
                                                    (root.path() / "v4.gguf").string()});
    VersionEntry v6 = entry(6, root.path());
    CHECK(apogee::training::record_promotion(ledger, v6, 0, "t").removed.empty());
    CHECK(ledger.kept() == 2);

    const RunManifest manifest = complete_run(root.path());
    const PromotePlan plan = apogee::training::plan_promotion(
        ledger, root.path() / "runs" / "r1", root.path() / "versions", "tuned", "", false);
    const VersionEntry recorded = apogee::training::promotion_entry(manifest, plan, "now");
    CHECK(recorded.version == 7);
    CHECK(recorded.run_id == "r1");
    CHECK(recorded.gguf_path == plan.gguf_path.string());
    CHECK(recorded.promoted_at == "now");
    CHECK(recorded.eval_score == 1.0);
    CHECK(recorded.eval_passed == true);
    CHECK_FALSE(apogee::training::promotion_entry(complete_run(root.path(), false), plan, "now")
                    .eval_passed.has_value());
}

TEST_CASE(
    "the rollback target is the highest version below the active one: no history, one "
    "version, a pruned target and a missing file are refused by name",
    "[training][promote][rollback]") {
    const apogee::testing::TempDir root{"rollback-" + std::to_string(std::random_device{}())};
    VersionLedger ledger;
    ledger.backend = "tuned";
    CHECK(apogee::training::rollback_target(ledger).error.find("no version history") !=
          std::string::npos);

    ledger.versions = {entry(1, root.path())};
    ledger.active_version = 1;
    const apogee::training::RollbackTarget only = apogee::training::rollback_target(ledger);
    CHECK(only.entry == nullptr);
    CHECK(only.error.find("only one promoted version") != std::string::npos);

    // Gaps: v2 was pruned, v3 and v5 exist, active v5 -> v3, not v4.
    ledger.versions = {entry(1, root.path(), true), entry(2, root.path(), true),
                       entry(3, root.path()), entry(5, root.path())};
    ledger.active_version = 5;
    const apogee::training::RollbackTarget target = apogee::training::rollback_target(ledger);
    REQUIRE(target.entry != nullptr);
    CHECK(target.entry->version == 3);
    CHECK(std::filesystem::exists(root.path() / "v5.gguf"));  // nothing deleted

    ledger.active_version = 3;
    const apogee::training::RollbackTarget pruned = apogee::training::rollback_target(ledger);
    CHECK(pruned.entry == nullptr);
    CHECK(pruned.error.find("v2") != std::string::npos);
    CHECK(pruned.error.find("pruned by retain_versions") != std::string::npos);

    ledger.active_version = 5;
    std::filesystem::remove(root.path() / "v3.gguf");
    const apogee::training::RollbackTarget gone = apogee::training::rollback_target(ledger);
    CHECK(gone.entry == nullptr);
    CHECK(gone.error.find("v3") != std::string::npos);
    CHECK(gone.error.find("not at") != std::string::npos);
}
