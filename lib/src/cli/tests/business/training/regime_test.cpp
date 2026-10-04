#include "training/regime.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "support/env_guard.h"
#include "training/manifest.h"
#include "training/mock_trainer.h"

/// The regime: flag precedence over a loaded spec, `--all-kits` alphabetical
/// with an explicit `--kit` list winning, the kit validation refusals, the
/// per-kit stage defaults, and a run on a scripted teacher and the mock
/// trainer -- synth wired through the core with each kit's eval
/// materialised, one stage per kit, the last passing stage named for
/// promote -- plus a teacher that produces nothing and cancellation.
namespace {

using apogee::harness::RegimeSpec;
using apogee::training::RegimeFlags;

void write(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << content;
}

std::string kit_text(std::string_view name, int iters) {
    return "name: " + std::string{name} +
           "\ndescription: a test kit\nsynth:\n  system: |\n    Make examples.\n  seeds:\n    - "
           "greetings\n  count: 4\n  per_seed: 2\ntrain:\n  iters: " +
           std::to_string(iters) +
           "\n  batch_size: 2\n  num_layers: 4\neval:\n  - prompt: please say hello\n    expected: "
           "hello\n";
}

struct Fixture {
    apogee::testing::TempDir root{"regime-" + std::to_string(std::random_device{}())};
    std::filesystem::path kits = root.path() / "kits";
    std::filesystem::path snap = root.path() / "snap";
    apogee::training::MockTrainer trainer;

    Fixture() {
        write(kits / "beta.yaml", kit_text("beta", 3));
        write(kits / "alpha.yaml", kit_text("alpha", 5));
        write(snap / "config.json", R"({"model_type": "llama"})");
        write(snap / "model.safetensors", "w");
    }

    [[nodiscard]] apogee::training::RegimeRequest request(RegimeSpec spec) {
        apogee::training::RegimeRequest request;
        request.spec = std::move(spec);
        request.kits_dir = kits;
        request.training_dir = root.path() / "training";
        request.regime_run_id = "regime-1";
        request.work_dir = root.path() / "training" / "regime" / "regime-1";
        request.base_model = snap;
        request.trainer = &trainer;
        request.generate = [](std::string_view, std::string_view, double, int,
                              const apogee::harness::CancellationToken&) {
            return apogee::training::GenerateOutcome{
                .ok = true,
                .text =
                    "[{\"prompt\": \"say hello\", \"completion\": \"hello\"}, {\"prompt\": "
                    "\"say hi\", \"completion\": \"hi\"}]"};
        };
        return request;
    }
};

}  // namespace

TEST_CASE(
    "flags override a loaded spec field by field, --all-kits runs the installed kits "
    "alphabetically, and an explicit --kit list wins",
    "[training][regime][flags]") {
    RegimeSpec base;
    base.name = "mine";
    base.teacher = "paid";
    base.student = "snap";
    base.kits = {"reasoning"};
    base.count = 50;
    base.promote_as = "tuned";
    base.iters = 100;
    base.temperature = 0.5;
    const std::vector<std::string> installed{"summarization", "alpha", "reasoning"};

    const RegimeSpec untouched = apogee::training::apply_regime_flags(base, {}, installed);
    CHECK(untouched.teacher == "paid");
    CHECK(untouched.kits == std::vector<std::string>{"reasoning"});
    CHECK(untouched.count == 50);
    CHECK(untouched.iters == 100);
    CHECK(untouched.temperature == 0.5);

    RegimeFlags flags;
    flags.teacher = "local";
    flags.student = "other";
    flags.kits = {"beta", "alpha"};
    flags.all_kits = true;
    flags.count = 7;
    flags.promote_as = "new";
    flags.iters = 9;
    flags.temperature = 1.1;
    const RegimeSpec overridden = apogee::training::apply_regime_flags(base, flags, installed);
    CHECK(overridden.teacher == "local");
    CHECK(overridden.student == "other");
    CHECK(overridden.kits == std::vector<std::string>{"beta", "alpha"});  // --kit wins, in order
    CHECK(overridden.count == 7);
    CHECK(overridden.promote_as == "new");
    CHECK(overridden.iters == 9);
    CHECK(overridden.temperature == 1.1);
    CHECK(overridden.name == "mine");

    RegimeFlags all;
    all.all_kits = true;
    const RegimeSpec everything = apogee::training::apply_regime_flags({}, all, installed);
    CHECK(everything.kits == std::vector<std::string>{"alpha", "reasoning", "summarization"});
    CHECK(everything.name == "regime");
}

TEST_CASE("the kit validation: no kits, an unknown kit, a broken kit file, and a valid set",
          "[training][regime][validate]") {
    const Fixture fixture;
    RegimeSpec spec;
    CHECK(apogee::training::validate_regime_kits(spec, fixture.kits).find("no kits") !=
          std::string::npos);
    spec.kits = {"alpha", "nope"};
    CHECK(apogee::training::validate_regime_kits(spec, fixture.kits).find("kit 'nope'") !=
          std::string::npos);
    write(fixture.kits / "broken.yaml", "name: broken\neval: []\n");
    spec.kits = {"broken"};
    CHECK(apogee::training::validate_regime_kits(spec, fixture.kits).find("kit 'broken'") !=
          std::string::npos);
    spec.kits = {"beta", "alpha"};
    CHECK(apogee::training::validate_regime_kits(spec, fixture.kits).empty());

    // A stage per kit: the regime's iters when set, else the kit's; the
    // kit's batch size and layer count.
    const apogee::training::Kit alpha = apogee::training::load_kit(fixture.kits / "alpha.yaml");
    apogee::harness::PipelineStageSpec stage =
        apogee::training::regime_stage(spec, alpha, "/d.jsonl", "/s.jsonl");
    CHECK(stage.name == "alpha");
    CHECK(stage.dataset == "/d.jsonl");
    CHECK(stage.eval_suite == "/s.jsonl");
    CHECK(stage.iters == 5);
    CHECK(stage.batch_size == 2);
    CHECK(stage.num_layers == 4);
    spec.iters = 11;
    stage = apogee::training::regime_stage(spec, alpha, "/d.jsonl", "/s.jsonl");
    CHECK(stage.iters == 11);
}

TEST_CASE(
    "run_regime synthesises a dataset and materialises the eval per kit, runs one stage per "
    "kit with the kit's defaults, and names the last passing stage for promote",
    "[training][regime][run]") {
    Fixture fixture;
    RegimeSpec spec;
    spec.name = "two";
    spec.student = "snap";
    spec.kits = {"beta", "alpha"};
    spec.count = 2;
    apogee::training::RegimeRequest request = fixture.request(spec);
    std::vector<std::string> messages;
    request.on_message = [&messages](std::string_view text) { messages.emplace_back(text); };
    const apogee::training::RegimeOutcome outcome = apogee::training::run_regime(request);
    INFO(outcome.error);
    REQUIRE(outcome.ok);
    REQUIRE(outcome.kits.size() == 2);
    CHECK(outcome.kits[0].kit == "beta");
    CHECK(outcome.kits[1].kit == "alpha");
    CHECK(outcome.kits[0].examples == 2);
    CHECK(outcome.kits[0].calls == 1);
    CHECK(outcome.kits[0].dataset == request.work_dir / "beta.jsonl");
    CHECK(outcome.kits[0].suite == request.work_dir / "beta.eval.jsonl");
    CHECK(std::filesystem::exists(request.work_dir / "alpha.jsonl"));
    CHECK(std::filesystem::exists(request.work_dir / "alpha.eval.jsonl"));
    std::ifstream in{request.work_dir / "beta.jsonl"};
    std::string line;
    REQUIRE(std::getline(in, line));
    CHECK(line ==
          "{\"messages\":[{\"role\":\"user\",\"content\":\"say hello\"},{\"role\":"
          "\"assistant\",\"content\":\"hello\"}]}");
    REQUIRE(outcome.pipeline.has_value());
    CHECK(outcome.pipeline->ok);
    CHECK(outcome.pipeline->manifest.pipeline_run_id == "regime-1-pipe");
    CHECK(outcome.pipeline->manifest.spec_name == "two");
    REQUIRE(outcome.pipeline->manifest.stages.size() == 2);
    CHECK(outcome.pipeline->manifest.stages[0].name == "beta");
    CHECK(outcome.pipeline->manifest.stages[1].name == "alpha");
    CHECK(outcome.promote_run_id == "regime-1-pipe-s1");
    std::string error;
    const auto beta_run =
        apogee::training::read_manifest(request.training_dir / "runs" / "regime-1-pipe-s0", error);
    REQUIRE(beta_run.has_value());
    CHECK(beta_run->iters == 3);
    CHECK(beta_run->batch_size == 2);
    CHECK(beta_run->pipeline_run_id == "regime-1-pipe");
    CHECK(beta_run->eval->suite == "kit:beta");
    bool announced = false;
    for (const std::string& message : messages) {
        announced = announced || message.find("kit 'beta'") != std::string::npos;
    }
    CHECK(announced);

    // The regime's iters override every kit's.
    Fixture overriding;
    spec.iters = 1;
    const apogee::training::RegimeOutcome shorter =
        apogee::training::run_regime(overriding.request(spec));
    REQUIRE(shorter.ok);
    CHECK(apogee::training::read_manifest(
              overriding.root.path() / "training" / "runs" / "regime-1-pipe-s0", error)
              ->iters == 1);
}

TEST_CASE("a teacher that produces nothing fails naming the kit, and the refusals come first",
          "[training][regime][run]") {
    Fixture fixture;
    RegimeSpec spec;
    spec.kits = {"alpha"};
    spec.count = 2;
    apogee::training::RegimeRequest request = fixture.request(spec);
    request.generate = [](std::string_view, std::string_view, double, int,
                          const apogee::harness::CancellationToken&) {
        return apogee::training::GenerateOutcome{.ok = false, .error = "quota", .retryable = false};
    };
    apogee::training::RegimeOutcome outcome = apogee::training::run_regime(request);
    CHECK_FALSE(outcome.ok);
    CHECK(outcome.error.find("kit 'alpha'") != std::string::npos);
    CHECK(outcome.error.find("nothing usable") != std::string::npos);
    CHECK_FALSE(outcome.pipeline.has_value());

    // An unknown kit is refused before the work directory exists.
    Fixture untouched;
    spec.kits = {"nope"};
    outcome = apogee::training::run_regime(untouched.request(spec));
    CHECK(outcome.error.find("kit 'nope'") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(untouched.root.path() / "training" / "regime"));
    request = fixture.request(spec);
    request.trainer = nullptr;
    CHECK(apogee::training::run_regime(request).error == "no trainer");
    request = fixture.request(spec);
    request.generate = nullptr;
    CHECK(apogee::training::run_regime(request).error == "no teacher");

    // Cancellation before the first teacher call.
    spec.kits = {"alpha"};
    request = fixture.request(spec);
    request.cancellation = apogee::harness::CancellationToken::create();
    request.cancellation.cancel();
    outcome = apogee::training::run_regime(request);
    CHECK(outcome.cancelled);
    CHECK_FALSE(outcome.ok);
}
