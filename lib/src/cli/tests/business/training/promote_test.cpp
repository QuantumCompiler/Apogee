#include "training/promote.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "support/env_guard.h"
#include "training/eval.h"
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

/// An MLX version (27c): a stored directory with a file in it.
VersionEntry mlx_entry(int version, const std::filesystem::path& dir, bool pruned = false) {
    VersionEntry e;
    e.version = version;
    e.run_id = "r" + std::to_string(version);
    e.mlx_path = (dir / ("mlx-v" + std::to_string(version))).string();
    e.promoted_at = "2026-10-04T12:00:0" + std::to_string(version) + "Z";
    if (pruned) {
        e.pruned_at = "2026-10-04T13:00:00Z";
    } else {
        write(std::filesystem::path{e.mlx_path} / "model.safetensors", "w");
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

TEST_CASE("version numbers are max + 1, never the count, so a prune cannot regress them",
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

    // Built where the command says -- a staging directory the model store
    // commits -- and named for the backend and version, so the stored file
    // says what it is wherever it lands.
    const PromotePlan plan = apogee::training::plan_promotion(
        ledger, root.path() / "runs" / "r9", root.path() / "stage", "tuned", "", false);
    CHECK(plan.version == 4);
    CHECK(plan.fused_dir == root.path() / "runs" / "r9" / "fused");
    CHECK(plan.gguf_path == root.path() / "stage" / "tuned-v4.gguf");
    CHECK(plan.f16_path == plan.gguf_path);
    CHECK_FALSE(plan.keep_fused);
    const PromotePlan quantized = apogee::training::plan_promotion(
        ledger, root.path() / "runs" / "r9", root.path() / "stage", "tuned", "Q4_K_M", true);
    CHECK(quantized.f16_path == root.path() / "stage" / "tuned-v4.f16.gguf");
    CHECK(quantized.gguf_path == root.path() / "stage" / "tuned-v4.gguf");
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
    CHECK(built.bytes > 0);
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
    CHECK(quantize_calls == std::vector<std::string>{"tuned-v1.f16.gguf->tuned-v1.gguf@Q4_K_M"});
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

TEST_CASE("retention removes through the store's remover, and never a file another version keeps",
          "[training][promote][retention]") {
    // Identical weights promoted twice share one stored file. Pruning the
    // older version must not take the file the newer one still records --
    // and what "removing a version" means (its whole store directory, its
    // kept fine-tune) is the store's to say, not this package's.
    const apogee::testing::TempDir root{"shared-" + std::to_string(std::random_device{}())};
    VersionLedger ledger;
    ledger.backend = "tuned";
    ledger.versions = {entry(1, root.path()), entry(2, root.path())};
    ledger.versions[1].gguf_path = ledger.versions[0].gguf_path;  // v2 is v1's bytes again
    ledger.active_version = 2;

    std::vector<int> removed;
    const apogee::training::ArtifactRemover remover = [&removed](const VersionEntry& old) {
        removed.push_back(old.version);
        return std::string{};
    };
    VersionEntry v3 = entry(3, root.path());
    const apogee::training::PruneResult pruned =
        apogee::training::record_promotion(ledger, v3, 2, "t", remover);
    // v1 was pruned -- marked, recorded -- but its file is v2's, still kept.
    CHECK(ledger.find(1)->pruned());
    CHECK(pruned.removed == std::vector<std::string>{ledger.versions[0].gguf_path});
    CHECK(removed.empty());
    CHECK(std::filesystem::exists(ledger.versions[0].gguf_path));

    // Once nothing kept records it, the remover is asked.
    VersionEntry v4 = entry(4, root.path());
    (void)apogee::training::record_promotion(ledger, v4, 2, "t", remover);
    CHECK(removed == std::vector<int>{2});
    CHECK(ledger.find(2)->pruned());

    // A remover's failure is reported, and the entry left unpruned.
    const apogee::training::ArtifactRemover refusing = [](const VersionEntry&) {
        return std::string{"permission denied"};
    };
    VersionEntry v5 = entry(5, root.path());
    const apogee::training::PruneResult failed =
        apogee::training::record_promotion(ledger, v5, 2, "t", refusing);
    REQUIRE(failed.failed.size() == 1);
    CHECK(failed.failed.front().find("permission denied") != std::string::npos);
    CHECK_FALSE(ledger.find(3)->pruned());
}

// ---------------------------------------------------------------------------
// The MLX target (27c)
// ---------------------------------------------------------------------------

TEST_CASE("the targets are named for the store's formats, and say what a version is",
          "[training][promote][mlx]") {
    using apogee::training::PromoteTarget;
    CHECK(apogee::training::to_string(PromoteTarget::Gguf) == "gguf");
    CHECK(apogee::training::to_string(PromoteTarget::Mlx) == "mlx");
    CHECK(apogee::training::promote_target_from_string("mlx") == PromoteTarget::Mlx);
    CHECK(apogee::training::promote_target_from_string("gguf") == PromoteTarget::Gguf);
    CHECK_FALSE(apogee::training::promote_target_from_string("safetensors").has_value());
    CHECK(apogee::training::promote_target_names().size() == 2);
    const apogee::testing::TempDir root{"targets-" + std::to_string(std::random_device{}())};
    CHECK(apogee::training::target_of(entry(1, root.path())) == PromoteTarget::Gguf);
    CHECK(apogee::training::target_of(mlx_entry(1, root.path())) == PromoteTarget::Mlx);
    CHECK(mlx_entry(2, root.path()).weights() == (root.path() / "mlx-v2").string());
    CHECK(entry(3, root.path()).weights() == (root.path() / "v3.gguf").string());
}

TEST_CASE(
    "an MLX promotion fuses straight into the store's staging, reads it whole, and never "
    "converts or quantizes",
    "[training][promote][mlx][artifacts]") {
    const apogee::testing::TempDir root{"mlx-artifacts-" + std::to_string(std::random_device{}())};
    const RunManifest manifest = complete_run(root.path());
    VersionLedger ledger;
    ledger.backend = "tuned";
    const std::filesystem::path staging = root.path() / "store" / "m" / "mlx" / ".incoming-x";
    // A quantize type and --keep-fused mean nothing here: the fused weights
    // are the version, kept by being it.
    const PromotePlan plan =
        apogee::training::plan_promotion(ledger, root.path() / "runs" / "r1", staging, "tuned", "",
                                         false, apogee::training::PromoteTarget::Mlx);
    CHECK(plan.version == 1);
    CHECK(plan.target == apogee::training::PromoteTarget::Mlx);
    CHECK(plan.fused_dir == staging);
    CHECK(plan.artifact() == staging);
    CHECK(plan.gguf_path.empty());
    CHECK(plan.quantize_type.empty());
    CHECK(plan.keep_fused);

    MockTrainer trainer;
    int converts = 0;
    std::filesystem::path verified;
    std::vector<std::string> messages;
    const apogee::training::ArtifactsResult built = build_promotion_artifacts(
        manifest, plan, trainer, mock_convert(converts),
        [&verified](const std::filesystem::path& dir) {
            verified = dir;
            return std::string{};
        },
        no_quantize(), [&messages](std::string_view text) { messages.emplace_back(text); }, {});
    INFO(built.error);
    REQUIRE(built.ok);
    // No conversion ran -- the acceptance's own words -- and the reader was
    // handed the fused directory itself.
    CHECK(converts == 0);
    CHECK(verified == staging);
    CHECK(std::filesystem::exists(staging / "config.json"));
    CHECK(std::filesystem::exists(staging / "model.safetensors"));
    CHECK(built.bytes > 0);
    CHECK_FALSE(std::filesystem::exists(root.path() / "runs" / "r1" / "fused"));
    CHECK(std::ranges::any_of(messages, [](const std::string& message) {
        return message.find("no GGUF conversion") != std::string::npos;
    }));

    // The version path is taken: a second build into it refuses.
    const apogee::training::ArtifactsResult again = build_promotion_artifacts(
        manifest, plan, trainer, mock_convert(converts), accept(), no_quantize(), {}, {});
    CHECK_FALSE(again.ok);
    CHECK(again.error.find("already exists") != std::string::npos);

    const VersionEntry recorded = apogee::training::promotion_entry(manifest, plan, "now");
    CHECK(recorded.mlx_path == staging.string());
    CHECK(recorded.gguf_path.empty());
    CHECK(recorded.mlx());
    CHECK(recorded.version == 1);
    CHECK(recorded.eval_passed == true);
}

TEST_CASE(
    "an MLX promotion that fails at any step leaves nothing: fuse, a directory that does not "
    "read whole, cancellation",
    "[training][promote][mlx][artifacts]") {
    const apogee::testing::TempDir root{"mlx-fail-" + std::to_string(std::random_device{}())};
    const RunManifest manifest = complete_run(root.path());
    VersionLedger ledger;
    ledger.backend = "tuned";
    const std::filesystem::path staging = root.path() / "store" / "m" / "mlx" / ".incoming-y";
    const PromotePlan plan =
        apogee::training::plan_promotion(ledger, root.path() / "runs" / "r1", staging, "tuned", "",
                                         false, apogee::training::PromoteTarget::Mlx);
    int converts = 0;

    MockTrainer no_fuse{MockTrainerOptions{.fuse_error = "GPU busy"}};
    const apogee::training::ArtifactsResult fuse_failed = build_promotion_artifacts(
        manifest, plan, no_fuse, mock_convert(converts), accept(), no_quantize(), {}, {});
    CHECK(fuse_failed.error == "fuse: GPU busy");
    CHECK_FALSE(std::filesystem::exists(staging));

    MockTrainer trainer;
    const apogee::training::ArtifactsResult unreadable = build_promotion_artifacts(
        manifest, plan, trainer, mock_convert(converts),
        [](const std::filesystem::path&) {
            return std::string{"cannot load: no tokenizer in the directory"};
        },
        no_quantize(), {}, {});
    CHECK_FALSE(unreadable.ok);
    CHECK(unreadable.error ==
          "the fused model is not a whole model directory: cannot load: no tokenizer in the "
          "directory");
    // It must not be registered, so it must not exist.
    CHECK_FALSE(std::filesystem::exists(staging));

    const apogee::harness::CancellationToken token = apogee::harness::CancellationToken::create();
    token.cancel();
    const apogee::training::ArtifactsResult cancelled = build_promotion_artifacts(
        manifest, plan, trainer, mock_convert(converts), accept(), no_quantize(), {}, token);
    CHECK(cancelled.cancelled);
    CHECK_FALSE(std::filesystem::exists(staging));
    CHECK(converts == 0);
}

TEST_CASE(
    "an MLX version is numbered, retained and rolled back as a GGUF one is, and a backend's "
    "versions never mix",
    "[training][promote][mlx][retain][rollback]") {
    const apogee::testing::TempDir root{"mlx-ledger-" + std::to_string(std::random_device{}())};
    VersionLedger ledger;
    ledger.backend = "tuned";
    ledger.versions = {mlx_entry(1, root.path(), true), mlx_entry(2, root.path()),
                       mlx_entry(3, root.path())};
    ledger.active_version = 3;
    // max + 1, a pruned number never reissued.
    CHECK(apogee::training::plan_promotion(ledger, root.path() / "runs" / "r9",
                                           root.path() / "stage", "tuned", "", false,
                                           apogee::training::PromoteTarget::Mlx)
              .version == 4);

    // Retention: the oldest kept directory goes whole, the active one never;
    // the entry is marked, not erased.
    const apogee::training::PruneResult pruned = apogee::training::record_promotion(
        ledger, mlx_entry(4, root.path()), 2, "2026-10-04T15:00:00Z");
    CHECK(ledger.active_version == 4);
    CHECK(pruned.removed == std::vector<std::string>{(root.path() / "mlx-v2").string()});
    CHECK_FALSE(std::filesystem::exists(root.path() / "mlx-v2"));
    CHECK(std::filesystem::exists(root.path() / "mlx-v3" / "model.safetensors"));
    CHECK(ledger.find(2)->pruned());
    CHECK(ledger.kept() == 2);

    // Rollback: the highest below, its directory there; deletes nothing.
    const apogee::training::RollbackTarget back = apogee::training::rollback_target(ledger);
    REQUIRE(back.entry != nullptr);
    CHECK(back.entry->version == 3);
    CHECK(std::filesystem::exists(root.path() / "mlx-v4"));
    ledger.active_version = 3;
    const apogee::training::RollbackTarget gone = apogee::training::rollback_target(ledger);
    CHECK(gone.entry == nullptr);
    CHECK(gone.error.find("its MLX model is gone") != std::string::npos);
    ledger.active_version = 4;
    std::filesystem::remove_all(root.path() / "mlx-v3");
    const apogee::training::RollbackTarget missing = apogee::training::rollback_target(ledger);
    CHECK(missing.entry == nullptr);
    CHECK(missing.error.find("the MLX model for v3 of 'tuned' is not at") != std::string::npos);

    // Identical weights promoted twice share one stored directory: never
    // removed while a kept version records it.
    VersionLedger shared;
    shared.backend = "same";
    shared.versions = {mlx_entry(1, root.path() / "s"), mlx_entry(2, root.path() / "s")};
    shared.versions[1].mlx_path = shared.versions[0].mlx_path;
    shared.active_version = 2;
    const apogee::training::PruneResult kept =
        apogee::training::record_promotion(shared, mlx_entry(3, root.path() / "s"), 2, "t");
    CHECK(shared.find(1)->pruned());
    CHECK(std::filesystem::exists(shared.versions[0].mlx_path));
    CHECK(kept.failed.empty());

    // One kind per backend: a promote of the other kind is refused before
    // anything is built, and a rollback never crosses.
    VersionLedger gguf;
    gguf.backend = "tuned";
    CHECK(apogee::training::target_conflict(gguf, apogee::training::PromoteTarget::Mlx).empty());
    gguf.versions = {entry(1, root.path()), entry(2, root.path())};
    gguf.active_version = 2;
    CHECK(apogee::training::target_conflict(gguf, apogee::training::PromoteTarget::Gguf).empty());
    const std::string conflict =
        apogee::training::target_conflict(gguf, apogee::training::PromoteTarget::Mlx);
    CHECK(conflict.find("'tuned' holds GGUFs (active v2)") != std::string::npos);
    CHECK(conflict.find("promote --target mlx under a new name") != std::string::npos);
    VersionLedger mixed;
    mixed.backend = "mixed";
    mixed.versions = {entry(1, root.path()), mlx_entry(2, root.path() / "m")};
    mixed.active_version = 2;
    CHECK(apogee::training::target_conflict(mixed, apogee::training::PromoteTarget::Gguf)
              .find("holds MLX models") != std::string::npos);
    const apogee::training::RollbackTarget across = apogee::training::rollback_target(mixed);
    CHECK(across.entry == nullptr);
    CHECK(across.error.find("never changes a backend's type") != std::string::npos);
}

// ---------------------------------------------------------------------------
// The adapter promote (53b): the fuse skipped, the adapter the version
// ---------------------------------------------------------------------------

namespace {

using apogee::training::PromoteMode;
using apogee::training::PromoteTarget;

/// An adapter version (53b): its file -- or, for MLX, its directory -- under
/// `dir`, pinned to a base.
VersionEntry adapter_entry(int version, const std::filesystem::path& dir, bool mlx = false,
                           bool pruned = false) {
    VersionEntry e;
    e.version = version;
    e.run_id = "r" + std::to_string(version);
    e.adapter_path = apogee::training::adapter_version_path(
                         dir, "tuned", version, mlx ? PromoteTarget::Mlx : PromoteTarget::Gguf)
                         .string();
    e.base_sha256 = std::string(64, 'b');
    e.promoted_at = "2026-10-21T12:00:0" + std::to_string(version) + "Z";
    if (pruned) {
        e.pruned_at = "2026-10-21T13:00:00Z";
    } else if (mlx) {
        write(std::filesystem::path{e.adapter_path} / "adapters.safetensors", "a");
    } else {
        write(e.adapter_path, "GGUF lora");
    }
    return e;
}

std::string read_back(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// A converter that counts, records what it was handed, and writes the
/// mock's GGUF LoRA.
struct CountingAdapterConverter {
    int calls = 0;
    std::filesystem::path adapter;
    std::filesystem::path base;
    std::filesystem::path out;

    apogee::training::AdapterConverter closure() {
        return
            [this](const std::filesystem::path& adapter_dir, const std::filesystem::path& base_dir,
                   const std::filesystem::path& gguf, const apogee::training::MessageSink&,
                   const apogee::harness::CancellationToken&) {
                ++calls;
                adapter = adapter_dir;
                base = base_dir;
                out = gguf;
                return apogee::training::write_mock_lora_gguf(adapter_dir, base_dir, gguf);
            };
    }
};

}  // namespace

TEST_CASE(
    "the adapter plan sits beside the fused plan: max + 1, a small file or directory named for "
    "the version under training/versions/<backend>/, nothing fused; the default plan is merge",
    "[training][promote][adapter]") {
    const apogee::testing::TempDir root{"adapter-plan-" + std::to_string(std::random_device{}())};
    VersionLedger ledger;
    ledger.backend = "tuned";
    ledger.versions = {adapter_entry(1, root.path(), false, true), adapter_entry(2, root.path())};
    ledger.active_version = 2;
    const PromotePlan gguf = apogee::training::plan_adapter_promotion(
        ledger, root.path() / "versions", "tuned", PromoteTarget::Gguf);
    CHECK(gguf.version == 3);
    CHECK(gguf.mode == PromoteMode::Adapter);
    CHECK(gguf.target == PromoteTarget::Gguf);
    CHECK(gguf.adapter_path == root.path() / "versions" / "tuned" / "v3.lora.gguf");
    CHECK(gguf.artifact() == gguf.adapter_path);
    CHECK(gguf.fused_dir.empty());
    CHECK(gguf.gguf_path.empty());
    CHECK(gguf.quantize_type.empty());
    CHECK_FALSE(gguf.keep_fused);
    const PromotePlan mlx = apogee::training::plan_adapter_promotion(
        ledger, root.path() / "versions", "tuned", PromoteTarget::Mlx);
    CHECK(mlx.adapter_path == root.path() / "versions" / "tuned" / "v3.lora");
    CHECK(mlx.artifact() == mlx.adapter_path);

    // The default is the merge, its plan exactly as it was: no adapter path.
    const PromotePlan merged = apogee::training::plan_promotion(
        ledger, root.path() / "runs" / "r9", root.path() / "stage", "tuned", "", false);
    CHECK(merged.mode == PromoteMode::Merge);
    CHECK(merged.adapter_path.empty());
    CHECK(merged.artifact() == merged.gguf_path);

    // What a version is made by, and its kind, read back from the ledger.
    CHECK(apogee::training::mode_of(adapter_entry(4, root.path())) == PromoteMode::Adapter);
    CHECK(apogee::training::mode_of(entry(5, root.path())) == PromoteMode::Merge);
    CHECK(apogee::training::target_of(adapter_entry(6, root.path())) == PromoteTarget::Gguf);
    CHECK(apogee::training::target_of(adapter_entry(7, root.path(), true)) == PromoteTarget::Mlx);
    CHECK(adapter_entry(8, root.path()).weights() ==
          (root.path() / "tuned" / "v8.lora.gguf").string());
}

TEST_CASE(
    "a GGUF adapter version skips the fuse, converts once -- the run's adapter against its "
    "base, into the plan's file -- verifies it, and leaves nothing on a failure",
    "[training][promote][adapter]") {
    const apogee::testing::TempDir root{"adapter-gguf-" + std::to_string(std::random_device{}())};
    const RunManifest manifest = complete_run(root.path());
    VersionLedger ledger;
    ledger.backend = "tuned";
    const PromotePlan plan = apogee::training::plan_adapter_promotion(
        ledger, root.path() / "versions", "tuned", PromoteTarget::Gguf);
    CountingAdapterConverter convert;
    int verified = 0;
    const apogee::training::Verifier verify = [&verified](const std::filesystem::path&) {
        ++verified;
        return std::string{};
    };
    std::vector<std::string> messages;
    const apogee::training::ArtifactsResult built = apogee::training::build_adapter_artifacts(
        manifest, plan, convert.closure(), verify,
        [&messages](std::string_view text) { messages.emplace_back(text); }, {});
    INFO(built.error);
    REQUIRE(built.ok);
    CHECK(convert.calls == 1);
    CHECK(convert.adapter == std::filesystem::path{manifest.adapter_dir});
    CHECK(convert.base == std::filesystem::path{manifest.base_model});
    // Written under a staging name beside its place, renamed in once read.
    CHECK(convert.out == plan.adapter_path.parent_path() / ".incoming-v1.lora.gguf");
    CHECK_FALSE(std::filesystem::exists(convert.out));
    CHECK(verified == 1);
    CHECK(std::filesystem::is_regular_file(plan.adapter_path));
    CHECK(built.bytes == static_cast<std::int64_t>(std::filesystem::file_size(plan.adapter_path)));
    // Nothing fused: no checkpoint beside the run, and the fuse never said.
    CHECK_FALSE(std::filesystem::exists(root.path() / "runs" / "r1" / "fused"));
    bool fused_message = false;
    bool no_fuse = false;
    for (const std::string& message : messages) {
        fused_message = fused_message || message.find("fusing") != std::string::npos;
        no_fuse = no_fuse || message.find("no fuse") != std::string::npos;
    }
    CHECK_FALSE(fused_message);
    CHECK(no_fuse);
    // The run's adapter is as it was.
    CHECK(
        std::filesystem::exists(root.path() / "runs" / "r1" / "adapters" / "adapters.safetensors"));

    // The version's path is taken: a second build refuses before converting.
    const apogee::training::ArtifactsResult again = apogee::training::build_adapter_artifacts(
        manifest, plan, convert.closure(), verify, {}, {});
    CHECK_FALSE(again.ok);
    CHECK(again.error.find("already exists, though the ledger records no v1") != std::string::npos);
    CHECK(convert.calls == 1);
    CHECK(std::filesystem::is_regular_file(plan.adapter_path));

    // A conversion that fails, or one that writes something unreadable,
    // leaves nothing at the version's path.
    VersionLedger next = ledger;
    next.versions = {adapter_entry(1, root.path() / "elsewhere")};
    next.active_version = 1;
    const PromotePlan second = apogee::training::plan_adapter_promotion(
        next, root.path() / "versions", "tuned", PromoteTarget::Gguf);
    const apogee::training::AdapterConverter failing =
        [](const std::filesystem::path&, const std::filesystem::path&,
           const std::filesystem::path& gguf, const apogee::training::MessageSink&,
           const apogee::harness::CancellationToken&) {
            write(gguf, "half");
            return std::string{"Model FooForCausalLM is not supported"};
        };
    const apogee::training::ArtifactsResult convert_failed =
        apogee::training::build_adapter_artifacts(manifest, second, failing, verify, {}, {});
    CHECK_FALSE(convert_failed.ok);
    CHECK(convert_failed.error.find("convert: Model FooForCausalLM") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(second.adapter_path));
    CHECK_FALSE(
        std::filesystem::exists(second.adapter_path.parent_path() / ".incoming-v2.lora.gguf"));
    const apogee::training::Verifier unreadable = [](const std::filesystem::path&) {
        return std::string{"bad magic"};
    };
    const apogee::training::ArtifactsResult unparsed = apogee::training::build_adapter_artifacts(
        manifest, second, convert.closure(), unreadable, {}, {});
    CHECK_FALSE(unparsed.ok);
    CHECK(unparsed.error.find("the converted LoRA does not parse: bad magic") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(second.adapter_path));

    // Cancelled before it starts: nothing converted, nothing written.
    const apogee::harness::CancellationToken token = apogee::harness::CancellationToken::create();
    token.cancel();
    const int before = convert.calls;
    const apogee::training::ArtifactsResult cancelled = apogee::training::build_adapter_artifacts(
        manifest, second, convert.closure(), verify, {}, token);
    CHECK(cancelled.cancelled);
    CHECK(convert.calls == before);
    CHECK_FALSE(std::filesystem::exists(second.adapter_path));

    // What an interrupted attempt left under the staging name was never
    // registered: the next build replaces it.
    write(second.adapter_path.parent_path() / ".incoming-v2.lora.gguf", "half of a killed run");
    const apogee::training::ArtifactsResult resumed = apogee::training::build_adapter_artifacts(
        manifest, second, convert.closure(), verify, {}, {});
    INFO(resumed.error);
    CHECK(resumed.ok);
    CHECK(std::filesystem::is_regular_file(second.adapter_path));
    CHECK_FALSE(
        std::filesystem::exists(second.adapter_path.parent_path() / ".incoming-v2.lora.gguf"));
    std::filesystem::remove(second.adapter_path);

    // An adapter that is not there is said.
    RunManifest gone = manifest;
    gone.adapter_dir = (root.path() / "runs" / "r1" / "nothing").string();
    const apogee::training::ArtifactsResult missing =
        apogee::training::build_adapter_artifacts(gone, second, convert.closure(), verify, {}, {});
    CHECK_FALSE(missing.ok);
    CHECK(missing.error.find("the run's adapter is not at") != std::string::npos);
}

TEST_CASE(
    "an MLX adapter version copies the adapter directory as it is -- the trainer's numbered "
    "checkpoints left behind -- and converts nothing",
    "[training][promote][adapter][mlx]") {
    const apogee::testing::TempDir root{"adapter-mlx-" + std::to_string(std::random_device{}())};
    const RunManifest manifest = complete_run(root.path());
    const std::filesystem::path run_adapter{manifest.adapter_dir};
    write(run_adapter / "adapter_config.json", R"({"lora_parameters": {"rank": 8}})");
    write(run_adapter / "0000100_adapters.safetensors", "checkpoint");
    write(run_adapter / "0000200_adapters.safetensors", "checkpoint");
    VersionLedger ledger;
    ledger.backend = "tuned";
    const PromotePlan plan = apogee::training::plan_adapter_promotion(
        ledger, root.path() / "versions", "tuned", PromoteTarget::Mlx);
    CountingAdapterConverter convert;
    const apogee::training::ArtifactsResult built = apogee::training::build_adapter_artifacts(
        manifest, plan, convert.closure(), accept(), {}, {});
    INFO(built.error);
    REQUIRE(built.ok);
    CHECK(convert.calls == 0);
    CHECK(std::filesystem::is_directory(plan.adapter_path));
    CHECK(read_back(plan.adapter_path / "adapter_config.json") ==
          R"({"lora_parameters": {"rank": 8}})");
    CHECK(read_back(plan.adapter_path / "adapters.safetensors") == "a");
    CHECK_FALSE(std::filesystem::exists(plan.adapter_path / "0000100_adapters.safetensors"));
    CHECK_FALSE(std::filesystem::exists(plan.adapter_path / "0000200_adapters.safetensors"));
    // What was copied, and nothing else: the configuration and the weights.
    CHECK(built.bytes ==
          static_cast<std::int64_t>(std::string_view{R"({"lora_parameters": {"rank": 8}})"}.size() +
                                    std::string_view{"a"}.size()));
    // The run keeps its own copy, checkpoints and all.
    CHECK(std::filesystem::exists(run_adapter / "0000100_adapters.safetensors"));
    CHECK_FALSE(std::filesystem::exists(root.path() / "runs" / "r1" / "fused"));

    // A copy the check refuses is removed whole.
    VersionLedger next;
    next.backend = "tuned";
    next.versions = {adapter_entry(1, root.path() / "elsewhere", true)};
    next.active_version = 1;
    const PromotePlan second = apogee::training::plan_adapter_promotion(
        next, root.path() / "versions", "tuned", PromoteTarget::Mlx);
    const apogee::training::Verifier refusing = [](const std::filesystem::path&) {
        return std::string{"it holds no adapter_config.json"};
    };
    const apogee::training::ArtifactsResult refused = apogee::training::build_adapter_artifacts(
        manifest, second, convert.closure(), refusing, {}, {});
    CHECK_FALSE(refused.ok);
    CHECK(refused.error.find("not an mlx-lm adapter directory") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(second.adapter_path));
    CHECK_FALSE(std::filesystem::exists(second.adapter_path.parent_path() / ".incoming-v2.lora"));
    CHECK(convert.calls == 0);
}

TEST_CASE(
    "adapter formats never cross, said by name: an mlx run's adapter onto a llamacpp target, a "
    "peft run's onto an mlx one; a later pipeline stage's adapter applies over no entry's base",
    "[training][promote][adapter][refusal]") {
    const apogee::testing::TempDir root{"adapter-cross-" + std::to_string(std::random_device{}())};
    RunManifest run = complete_run(root.path());
    using apogee::training::adapter_promotion_conflict;

    run.trainer = "mlx";
    const std::string mlx_to_gguf = adapter_promotion_conflict(run, PromoteTarget::Gguf);
    CHECK(mlx_to_gguf.find("mlx-lm adapter (trainer mlx)") != std::string::npos);
    CHECK(mlx_to_gguf.find("GGUF LoRA") != std::string::npos);
    CHECK(mlx_to_gguf.find("PEFT adapter (trainer peft)") != std::string::npos);
    CHECK(mlx_to_gguf.find("never cross") != std::string::npos);
    CHECK(mlx_to_gguf.find("--target mlx") != std::string::npos);
    CHECK(adapter_promotion_conflict(run, PromoteTarget::Mlx).empty());

    run.trainer = "peft";
    const std::string peft_to_mlx = adapter_promotion_conflict(run, PromoteTarget::Mlx);
    CHECK(peft_to_mlx.find("PEFT adapter (trainer peft)") != std::string::npos);
    CHECK(peft_to_mlx.find("mlx-lm adapter directory (trainer mlx)") != std::string::npos);
    CHECK(peft_to_mlx.find("--target gguf") != std::string::npos);
    CHECK(adapter_promotion_conflict(run, PromoteTarget::Gguf).empty());

    run.trainer = "mock";
    CHECK(adapter_promotion_conflict(run, PromoteTarget::Gguf).empty());
    CHECK(adapter_promotion_conflict(run, PromoteTarget::Mlx).empty());

    // A format nobody recorded is not guessed.
    run.trainer.clear();
    CHECK(adapter_promotion_conflict(run, PromoteTarget::Gguf).find("records no trainer") !=
          std::string::npos);

    // A stage after the first trained over the stage before's fused weights.
    run.trainer = "peft";
    run.parent_run = "pipe-1-s0";
    const std::string staged = adapter_promotion_conflict(run, PromoteTarget::Gguf);
    CHECK(staged.find("pipeline stage trained over stage pipe-1-s0") != std::string::npos);
    CHECK(staged.find("without --adapter") != std::string::npos);
}

TEST_CASE(
    "a backend's versions are merged or adapters, never both: each is refused into a ledger of "
    "the other, before anything is built",
    "[training][promote][adapter][refusal]") {
    const apogee::testing::TempDir root{"adapter-kinds-" + std::to_string(std::random_device{}())};
    VersionLedger empty;
    empty.backend = "tuned";
    CHECK(apogee::training::target_conflict(empty, PromoteTarget::Gguf, PromoteMode::Adapter)
              .empty());

    VersionLedger merged;
    merged.backend = "tuned";
    merged.versions = {entry(1, root.path())};
    merged.active_version = 1;
    CHECK(apogee::training::target_conflict(merged, PromoteTarget::Gguf).empty());
    const std::string onto_merged =
        apogee::training::target_conflict(merged, PromoteTarget::Gguf, PromoteMode::Adapter);
    CHECK(onto_merged.find("'tuned' holds merged versions (active v1)") != std::string::npos);
    CHECK(onto_merged.find("not the base an adapter was trained over") != std::string::npos);

    VersionLedger adapters;
    adapters.backend = "tuned";
    adapters.versions = {adapter_entry(1, root.path() / "a"), adapter_entry(2, root.path() / "a")};
    adapters.active_version = 2;
    CHECK(apogee::training::target_conflict(adapters, PromoteTarget::Gguf, PromoteMode::Adapter)
              .empty());
    // The default -- a merge -- is refused into it: it would replace the
    // base under the adapters.
    const std::string onto_adapters =
        apogee::training::target_conflict(adapters, PromoteTarget::Gguf);
    CHECK(onto_adapters.find("'tuned' holds adapter versions (active v2)") != std::string::npos);
    CHECK(onto_adapters.find("with --adapter") != std::string::npos);
    // Within adapters, the target still never changes.
    CHECK(apogee::training::target_conflict(adapters, PromoteTarget::Mlx, PromoteMode::Adapter)
              .find("holds GGUF LoRA adapters (active v2)") != std::string::npos);

    // A rollback never crosses from an adapter version to a merged one.
    VersionLedger mixed;
    mixed.backend = "tuned";
    mixed.versions = {entry(1, root.path() / "m"), adapter_entry(2, root.path() / "m")};
    mixed.active_version = 2;
    const apogee::training::RollbackTarget across = apogee::training::rollback_target(mixed);
    CHECK(across.entry == nullptr);
    CHECK(across.error.find("v1 of 'tuned' is a GGUF") != std::string::npos);
    CHECK(across.error.find("never changes a backend's type") != std::string::npos);
}

TEST_CASE(
    "adapter versions are retained and rolled back as any version is: kilobyte files removed "
    "beyond retain_versions, never the active one, a rollback to one gone refused",
    "[training][promote][adapter][retain][rollback]") {
    const apogee::testing::TempDir root{"adapter-retain-" + std::to_string(std::random_device{}())};
    VersionLedger ledger;
    ledger.backend = "tuned";
    ledger.versions = {adapter_entry(1, root.path()), adapter_entry(2, root.path()),
                       adapter_entry(3, root.path())};
    ledger.active_version = 3;
    const apogee::training::PruneResult pruned = apogee::training::record_promotion(
        ledger, adapter_entry(4, root.path()), 2, "2026-10-21T15:00:00Z");
    CHECK(ledger.active_version == 4);
    CHECK(pruned.removed ==
          std::vector<std::string>{(root.path() / "tuned" / "v1.lora.gguf").string(),
                                   (root.path() / "tuned" / "v2.lora.gguf").string()});
    CHECK_FALSE(std::filesystem::exists(root.path() / "tuned" / "v1.lora.gguf"));
    CHECK_FALSE(std::filesystem::exists(root.path() / "tuned" / "v2.lora.gguf"));
    CHECK(std::filesystem::exists(root.path() / "tuned" / "v3.lora.gguf"));
    CHECK(std::filesystem::exists(root.path() / "tuned" / "v4.lora.gguf"));
    CHECK(ledger.find(1)->pruned());
    CHECK(ledger.kept() == 2);

    // Rollback repoints at the adapter below, its pin with it; deletes nothing.
    const apogee::training::RollbackTarget back = apogee::training::rollback_target(ledger);
    REQUIRE(back.entry != nullptr);
    CHECK(back.entry->version == 3);
    CHECK(back.entry->adapter_path == (root.path() / "tuned" / "v3.lora.gguf").string());
    CHECK(back.entry->base_sha256 == std::string(64, 'b'));
    CHECK(std::filesystem::exists(root.path() / "tuned" / "v4.lora.gguf"));
    ledger.active_version = 3;
    const apogee::training::RollbackTarget gone = apogee::training::rollback_target(ledger);
    CHECK(gone.entry == nullptr);
    CHECK(gone.error.find("its adapter is gone") != std::string::npos);
    ledger.active_version = 4;
    std::filesystem::remove(root.path() / "tuned" / "v3.lora.gguf");
    const apogee::training::RollbackTarget missing = apogee::training::rollback_target(ledger);
    CHECK(missing.entry == nullptr);
    CHECK(missing.error.find("the adapter for v3 of 'tuned' is not at") != std::string::npos);

    // An MLX adapter version's directory goes whole.
    VersionLedger mlx;
    mlx.backend = "tuned";
    mlx.versions = {adapter_entry(1, root.path() / "x", true),
                    adapter_entry(2, root.path() / "x", true)};
    mlx.active_version = 2;
    (void)apogee::training::record_promotion(mlx, adapter_entry(3, root.path() / "x", true), 2,
                                             "t");
    CHECK_FALSE(std::filesystem::exists(root.path() / "x" / "tuned" / "v1.lora"));
    CHECK(
        std::filesystem::exists(root.path() / "x" / "tuned" / "v2.lora" / "adapters.safetensors"));

    // The ledger entry a successful adapter build records.
    const PromotePlan plan = apogee::training::plan_adapter_promotion(
        ledger, root.path() / "versions", "tuned", PromoteTarget::Gguf);
    const VersionEntry recorded =
        apogee::training::promotion_entry(complete_run(root.path()), plan, "now");
    CHECK(recorded.version == 5);
    CHECK(recorded.adapter_path == plan.adapter_path.string());
    CHECK(recorded.gguf_path.empty());
    CHECK(recorded.mlx_path.empty());
    CHECK(recorded.adapter());
}

// ---------------------------------------------------------------------------
// The gate on the base that serves (53c): a candidate bound for a quantized
// base scored there, once, over scripted closures.
// ---------------------------------------------------------------------------

namespace {

using apogee::training::CandidateReply;
using apogee::training::CandidateRunner;
using apogee::training::EvalItem;
using apogee::training::EvalOptions;
using apogee::training::gate_on_serving_base;
using apogee::training::plan_serving_gate;
using apogee::training::ServingGate;
using apogee::training::ServingGateRequest;
using apogee::training::ServingRunnerFactory;

/// A runner answering every prompt with `answer` -- or the prompt itself
/// when it is empty -- counting what it is asked; failing when told to.
class Answering final : public CandidateRunner {
public:
    Answering(std::string answer, int& asked, bool fail)
        : answer_{std::move(answer)}, asked_{asked}, fail_{fail} {}

    CandidateReply run(std::string_view prompt,
                       const apogee::harness::CancellationToken& cancellation) override {
        ++asked_;
        CandidateReply reply;
        if (fail_ || cancellation.stop_requested()) {
            reply.error = "the runtime stopped";
            return reply;
        }
        reply.ok = true;
        reply.text = answer_.empty() ? std::string{prompt} : answer_;
        return reply;
    }

private:
    std::string answer_;
    int& asked_;
    bool fail_;
};

/// The serving base's runtime, scripted: what the tuned candidate answers,
/// what refuses to build, and every build and ask counted -- so a test pins
/// which closure the gate scored on.
struct Served {
    std::string answer{};
    std::string refuse_tuned{};
    std::string refuse_bare{};
    bool fail = false;
    int tuned = 0;
    int bare = 0;
    int asked = 0;

    [[nodiscard]] ServingRunnerFactory factory() {
        return [this](bool tuned_build, std::string& error) -> std::unique_ptr<CandidateRunner> {
            const std::string& refused = tuned_build ? refuse_tuned : refuse_bare;
            if (!refused.empty()) {
                error = refused;
                return nullptr;
            }
            ++(tuned_build ? tuned : bare);
            return std::make_unique<Answering>(tuned_build ? answer : "base says no", asked, fail);
        };
    }
};

/// A request scoring two substring items on a Q4_K_M base.
ServingGateRequest on_q4(GateMode mode = GateMode::Hard, bool force = false) {
    ServingGateRequest request;
    request.base = {.backend = "local",
                    .weights = "/models/m/gguf/0123456789ab/m-Q4_K_M.gguf",
                    .label = "m-Q4_K_M.gguf",
                    .precision = "Q4_K_M",
                    .quantized = true};
    request.items = {EvalItem{.prompt = "say hello", .expected = "hello"},
                     EvalItem{.prompt = "say bye", .expected = "bye"}};
    request.suite = "kit:demo";
    request.mode = mode;
    request.force = force;
    return request;
}

constexpr std::string_view kWhere = "m-Q4_K_M.gguf (Q4_K_M), the base 'local' serves";

}  // namespace

TEST_CASE(
    "a candidate bound for a quantized base is scored there, once, whatever its run's own eval "
    "said",
    "[training][promote][gate][serving]") {
    const apogee::testing::TempDir root{"serving-" + std::to_string(std::random_device{}())};
    CHECK(apogee::training::describe(on_q4().base) == "m-Q4_K_M.gguf (Q4_K_M)");

    // The run's own eval FAILED where its trainer ran; on the base that will
    // serve, the candidate passes -- and that is the verdict.
    Served served;
    const ServingGate passing =
        gate_on_serving_base(complete_run(root.path(), true, false), on_q4(), served.factory(), {});
    CHECK(passing.verdict.ok);
    CHECK(passing.verdict.warning.empty());
    REQUIRE(passing.results.has_value());
    CHECK(passing.results->passed);
    CHECK(passing.results->num_passed == 2);
    CHECK(passing.results->suite == "kit:demo");
    // The serving closure, its candidate built once and asked each item; no
    // baseline without a judge, and nothing else is asked.
    CHECK(served.tuned == 1);
    CHECK(served.bare == 0);
    CHECK(served.asked == 2);

    // The run's own eval PASSED; on the quantized base the candidate
    // degrades under the floor -- refused, saying where it was scored.
    Served degraded{.answer = "something else"};
    const ServingGate refused =
        gate_on_serving_base(complete_run(root.path()), on_q4(), degraded.factory(), {});
    CHECK_FALSE(refused.verdict.ok);
    CHECK(refused.verdict.error == "the eval gate failed for run r1, scored on " +
                                       std::string{kWhere} +
                                       " (0%: 0/2 passed). Fix the training or the suite, then "
                                       "promote again, or pass --force to skip the gate");
    REQUIRE(refused.results.has_value());
    CHECK_FALSE(refused.results->passed);
    CHECK(degraded.asked == 2);

    // Soft: let through, said, and the failing score kept for the record.
    Served soft_run{.answer = "say hello"};
    const ServingGate soft = gate_on_serving_base(complete_run(root.path()), on_q4(GateMode::Soft),
                                                  soft_run.factory(), {});
    CHECK(soft.verdict.ok);
    CHECK(
        soft.verdict.warning.starts_with("gate_mode is soft: the eval gate failed for run r1, "
                                         "scored on " +
                                         std::string{kWhere} + " (50%: 1/2 passed)"));
    REQUIRE(soft.results.has_value());

    // The version records the gate that let it through, never the run's
    // own eval: where it scored, and how.
    VersionEntry version = entry(1, root.path());
    version.eval_score = 1.0;
    version.eval_passed = true;
    apogee::training::record_serving_gate(version, soft, on_q4().base);
    CHECK(version.eval_score == 0.5);
    CHECK(version.eval_passed == false);
    CHECK(version.eval_scored_on == "m-Q4_K_M.gguf (Q4_K_M)");
}

TEST_CASE(
    "the gate on the serving base: --force scores nothing, no suite refuses a hard gate, and "
    "an incomplete run never passes",
    "[training][promote][gate][serving]") {
    const apogee::testing::TempDir root{"serving-" + std::to_string(std::random_device{}())};

    // --force: no eval at all, no judge paid -- and said.
    Served forced;
    const ServingGate skipped = gate_on_serving_base(
        complete_run(root.path()), on_q4(GateMode::Hard, true), forced.factory(), {});
    CHECK(skipped.verdict.ok);
    CHECK(skipped.verdict.warning ==
          "promoting despite the gate (--force): run r1 was not scored on " + std::string{kWhere});
    CHECK_FALSE(skipped.results.has_value());
    CHECK(forced.tuned + forced.bare + forced.asked == 0);
    CHECK_FALSE(plan_serving_gate(complete_run(root.path()), on_q4(GateMode::Hard, true)).score);
    // The version then carries no eval, not its run's.
    VersionEntry version = entry(1, root.path());
    version.eval_score = 1.0;
    version.eval_passed = true;
    apogee::training::record_serving_gate(version, skipped, on_q4().base);
    CHECK_FALSE(version.eval_score.has_value());
    CHECK_FALSE(version.eval_passed.has_value());
    CHECK(version.eval_scored_on.empty());

    // No suite: a hard gate refuses before anything is built, naming the
    // flag; a soft one lets it through, said.
    ServingGateRequest bare = on_q4();
    bare.items.clear();
    const apogee::training::ServingPlan plan = plan_serving_gate(complete_run(root.path()), bare);
    CHECK_FALSE(plan.verdict.ok);
    CHECK_FALSE(plan.score);
    CHECK(plan.verdict.error ==
          "no eval suite to score run r1 on " + std::string{kWhere} +
              " -- pass --suite <path|name>, or set training.eval_suite_path, or pass --force to "
              "skip the gate");
    Served unasked;
    CHECK_FALSE(
        gate_on_serving_base(complete_run(root.path()), bare, unasked.factory(), {}).verdict.ok);
    bare.mode = GateMode::Soft;
    const ServingGate soft =
        gate_on_serving_base(complete_run(root.path()), bare, unasked.factory(), {});
    CHECK(soft.verdict.ok);
    CHECK(soft.verdict.warning.starts_with("gate_mode is soft: no eval suite"));
    CHECK(unasked.tuned + unasked.bare == 0);

    // A run that did not complete is refused, whatever the mode or --force.
    RunManifest crashed = complete_run(root.path());
    crashed.status = std::string{apogee::training::kStatusFailed};
    const ServingGate dead =
        gate_on_serving_base(crashed, on_q4(GateMode::Soft, true), unasked.factory(), {});
    CHECK_FALSE(dead.verdict.ok);
    CHECK(dead.verdict.error.find("only a complete run can be promoted") != std::string::npos);
}

TEST_CASE(
    "a candidate the serving base cannot build or ask is held to the gate unscored; a judge or "
    "baseline that fails never costs it",
    "[training][promote][gate][serving]") {
    const apogee::testing::TempDir root{"serving-" + std::to_string(std::random_device{}())};

    // The runtime cannot build the candidate: nothing to say for it.
    Served unbuilt{.refuse_tuned = "llama.cpp is not in this build"};
    const ServingGate no_runtime =
        gate_on_serving_base(complete_run(root.path()), on_q4(), unbuilt.factory(), {});
    CHECK_FALSE(no_runtime.verdict.ok);
    CHECK(no_runtime.verdict.error == "run r1 could not be scored on " + std::string{kWhere} +
                                          ": llama.cpp is not in this build, or pass --force to "
                                          "skip the gate");
    CHECK_FALSE(no_runtime.results.has_value());
    // Built, but it cannot answer: the eval aborts, held the same way.
    Served broken{.fail = true};
    const ServingGate aborted =
        gate_on_serving_base(complete_run(root.path()), on_q4(), broken.factory(), {});
    CHECK_FALSE(aborted.verdict.ok);
    CHECK(aborted.verdict.error.find("could not be scored on " + std::string{kWhere} +
                                     ": candidate inference") != std::string::npos);

    // A judged suite: the baseline -- the bare serving base -- is built for
    // the judge alone, and a judge that fails is a tie, which passes.
    ServingGateRequest judged = on_q4();
    judged.items = {EvalItem{.prompt = "write a haiku"}};
    EvalOptions options;
    options.judge_backend = "judge";
    options.judge = [](std::string_view, std::string_view, std::string_view,
                       const apogee::harness::CancellationToken&) {
        return apogee::training::JudgeReply{.ok = false, .text = {}, .error = "judge down"};
    };
    Served judging;
    const ServingGate tied =
        gate_on_serving_base(complete_run(root.path()), judged, judging.factory(), options);
    CHECK(tied.verdict.ok);
    REQUIRE(tied.results.has_value());
    CHECK(tied.results->items.front().judge_winner == "tie");
    CHECK(judging.tuned == 1);
    CHECK(judging.bare == 1);
    // A baseline the runtime cannot build leaves the judged items to skip,
    // counted -- never a refusal.
    Served baseless{.refuse_bare = "no memory for a second copy"};
    const ServingGate skipped =
        gate_on_serving_base(complete_run(root.path()), judged, baseless.factory(), options);
    CHECK(skipped.verdict.ok);
    REQUIRE(skipped.results.has_value());
    CHECK(skipped.results->num_skipped == 1);

    // Cancelled mid-eval: said as cancelled, no verdict either way.
    EvalOptions cancelling;
    cancelling.cancellation = apogee::harness::CancellationToken::create();
    cancelling.cancellation.cancel();
    Served stopped;
    const ServingGate cancelled =
        gate_on_serving_base(complete_run(root.path()), on_q4(), stopped.factory(), cancelling);
    CHECK(cancelled.cancelled);
    CHECK_FALSE(cancelled.results.has_value());
}
