#include "commands/train.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "commands/registry.h"
#include "commands/root.h"
#include "harness/config_edit.h"
#include "support/env_guard.h"

/// `apogee train setup` refusals -- an interpreter named in the config that
/// does not exist, an unknown requirement set, a bad trainer name -- each
/// before anything is created. The real creation runs in the shell
/// lifecycle test against the host's python3.
namespace {

struct Fixture {
    apogee::testing::TempDir home{"train-cli-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    std::filesystem::path config_path = home.path() / "config" / "config.yaml";

    explicit Fixture(std::string_view config = "backends:\n  local:\n    type: mock\n") {
        std::filesystem::create_directories(config_path.parent_path());
        std::ofstream{config_path, std::ios::binary} << config;
    }

    int run(const std::vector<std::string>& args, std::string* err) const {
        std::ostringstream captured_out;
        std::ostringstream captured_err;
        std::streambuf* old_out = std::cout.rdbuf(captured_out.rdbuf());
        std::streambuf* old_err = std::cerr.rdbuf(captured_err.rdbuf());
        int code = -1;
        try {
            apogee::commands::RootCommand root{apogee::commands::default_registry()};
            std::vector<std::string> full{"--config", config_path.string()};
            full.insert(full.end(), args.begin(), args.end());
            std::vector<const char*> argv{"apogee"};
            for (const std::string& arg : full) {
                argv.push_back(arg.c_str());
            }
            code = root.run(static_cast<int>(argv.size()), argv.data());
        } catch (...) {
            std::cout.rdbuf(old_out);
            std::cerr.rdbuf(old_err);
            throw;
        }
        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);
        *err = captured_err.str();
        return code;
    }
};

}  // namespace

TEST_CASE(
    "train setup refuses a configured interpreter that does not exist, before creating "
    "anything",
    "[commands][train][setup]") {
    const Fixture fixture{
        "training:\n  python: /no/such/python3\nbackends:\n  local:\n    type: mock\n"};
    std::string err;
    CHECK(fixture.run({"train", "setup"}, &err) == 1);
    CHECK(err.find("/no/such/python3") != std::string::npos);
    CHECK(err.find("training.python") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(fixture.home.path() / "training" / "venv"));
}

TEST_CASE("train setup refuses an unknown requirement set and a bad trainer by name",
          "[commands][train][setup]") {
    const Fixture fixture;
    std::string err;
    CHECK(fixture.run({"train", "setup", "--with", "nope"}, &err) == 1);
    CHECK(err.find("unknown requirement set 'nope'") != std::string::npos);
    CHECK(err.find("prepare, mlx, peft, convert") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(fixture.home.path() / "training" / "venv"));

    // The interpreter is located before the trainer name is judged, so a
    // host with no python3 still refuses -- with one message or the other.
    CHECK(fixture.run({"train", "setup", "--trainer", "bogus"}, &err) == 1);
    CHECK((err.find("--trainer must be") != std::string::npos ||
           err.find("python") != std::string::npos));
}

TEST_CASE("the trainer detection names its reason when nothing fits", "[commands][train][setup]") {
    std::string reason;
    const std::string trainer = apogee::commands::detect_trainer(reason);
    if (trainer.empty()) {
        CHECK(reason.find("nvidia-smi") != std::string::npos);
    } else {
        CHECK((trainer == "mlx" || trainer == "peft"));
        CHECK(reason.empty());
    }
}

// ---------------------------------------------------------------------------
// The run item: `train run|eval|promote|rollback|versions|status` on the mock
// trainer through the real command tree, with the scripted mock as the judge.
// ---------------------------------------------------------------------------

#include <nlohmann/json.hpp>

#include "harness/assets.h"
#include "harness/layout.h"
#include "models/quantize.h"
#include "training/manifest.h"
#include "training/store.h"
#include "training/trainer.h"

namespace {

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

void write_file(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << content;
}

/// A home with a seeded layout, a snapshot named `tiny`, a template dataset,
/// and a scripted mock judge whose verdict the config's `judge.json` holds.
struct RunFixture {
    Fixture base;
    std::filesystem::path home = base.home.path();
    std::filesystem::path training = home / "training";

    explicit RunFixture(std::string verdict = "A", std::string extra_config = {}) {
        write_file(home / "judge.json",
                   nlohmann::json{{"turns", nlohmann::json::array({{{"text", verdict}}})}}.dump());
        std::string config = "models:\n  default: judge\nbackends:\n";
        config +=
            "  judge:\n    type: mock\n    model_path: " + (home / "judge.json").string() + "\n";
        config += "  vendor:\n    type: claude-cli\n";
        config += "  cloud:\n    type: anthropic\n    model: claude\n";
        config += extra_config;
        write_file(base.config_path, config);
        REQUIRE(apogee::harness::seed_data_directory(home).ok());
        write_file(home / "models" / "tiny" / "config.json",
                   R"({"architectures": ["LlamaForCausalLM"], "model_type": "llama"})");
        write_file(home / "models" / "tiny" / "model.safetensors", "w");
        write_file(training / "datasets" / "starter.jsonl",
                   "{\"messages\": [{\"role\": \"user\", \"content\": \"hi\"}, "
                   "{\"role\": \"assistant\", \"content\": \"hello\"}]}\n");
        write_file(
            home / "suite.jsonl",
            "{\"prompt\": \"say hello\", \"expected\": \"hello\"}\n{\"prompt\": \"free\"}\n");
    }

    int run(const std::vector<std::string>& args, std::string* out = nullptr,
            std::string* err = nullptr) const {
        std::ostringstream captured_out;
        std::ostringstream captured_err;
        std::istringstream fed{std::string{}};
        std::streambuf* old_out = std::cout.rdbuf(captured_out.rdbuf());
        std::streambuf* old_err = std::cerr.rdbuf(captured_err.rdbuf());
        std::streambuf* old_in = std::cin.rdbuf(fed.rdbuf());
        int code = -1;
        try {
            apogee::commands::RootCommand root{apogee::commands::default_registry()};
            std::vector<std::string> full{"--config", base.config_path.string()};
            full.insert(full.end(), args.begin(), args.end());
            std::vector<const char*> argv{"apogee"};
            for (const std::string& arg : full) {
                argv.push_back(arg.c_str());
            }
            code = root.run(static_cast<int>(argv.size()), argv.data());
        } catch (...) {
            std::cout.rdbuf(old_out);
            std::cerr.rdbuf(old_err);
            std::cin.rdbuf(old_in);
            throw;
        }
        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);
        std::cin.rdbuf(old_in);
        if (out != nullptr) {
            *out = captured_out.str();
        }
        if (err != nullptr) {
            *err = captured_err.str();
        }
        return code;
    }

    /// A complete mock run, its id.
    std::string trained(int iters = 5) const {
        std::string err;
        REQUIRE(run({"train", "run", "tiny", "--dataset", "starter", "--trainer", "mock", "--iters",
                     std::to_string(iters)},
                    nullptr, &err) == 0);
        const std::vector<apogee::training::RunSummary> runs =
            apogee::training::TrainingStore{training}.list_runs();
        REQUIRE_FALSE(runs.empty());
        return runs.front().id;
    }

    std::string evaluated(const std::string& id, std::vector<std::string> extra = {}) const {
        std::vector<std::string> args{"train", "eval", id, "--suite",
                                      (home / "suite.jsonl").string()};
        args.insert(args.end(), extra.begin(), extra.end());
        std::string out;
        std::string err;
        REQUIRE(run(args, &out, &err) == 0);
        return out;
    }

    apogee::training::RunManifest manifest(const std::string& id) const {
        std::string error;
        const auto m = apogee::training::read_manifest(training / "runs" / id, error);
        REQUIRE(m.has_value());
        return *m;
    }
};

}  // namespace

TEST_CASE(
    "train run on the mock writes a running manifest that ends complete, with the status "
    "line seen and the dataset hashed",
    "[commands][train][run]") {
    const RunFixture fixture;
    std::string out;
    std::string err;
    REQUIRE(fixture.run({"train", "run", "tiny", "--dataset", "starter", "--trainer", "mock",
                         "--iters", "4", "--method", "qlora", "--mask-prompt"},
                        &out, &err) == 0);
    CHECK(err.find("iter 4/4") != std::string::npos);
    CHECK(err.find("[train] run ") != std::string::npos);
    CHECK(out.find("complete") != std::string::npos);
    CHECK(out.find("apogee train eval") != std::string::npos);
    const std::string id = apogee::training::TrainingStore{fixture.training}.list_runs().front().id;
    const apogee::training::RunManifest m = fixture.manifest(id);
    CHECK(m.status == "complete");
    CHECK(m.trainer == "mock");
    CHECK(m.method == "qlora");
    CHECK(m.iters == 4);
    CHECK(m.iterations == 4);
    CHECK(m.mask_prompt);
    CHECK(m.base_model == (fixture.home / "models" / "tiny").string());
    CHECK(m.dataset == (fixture.training / "datasets" / "starter.jsonl").string());
    CHECK(m.dataset_hash.size() == 12);
    CHECK_FALSE(m.started_at.empty());
    CHECK_FALSE(m.finished_at.empty());
    CHECK(std::filesystem::exists(m.adapter_dir));
    CHECK(m.final_loss > 0.0);

    // The dataset by path, the student by path, the config's trainer.
    write_file(fixture.base.config_path,
               read_file(fixture.base.config_path) + "training:\n  trainer: mock\n");
    REQUIRE(
        fixture.run({"train", "run", (fixture.home / "models" / "tiny").string(), "--dataset",
                     (fixture.training / "datasets" / "starter.jsonl").string(), "--iters", "2"},
                    &out, &err) == 0);
    CHECK(apogee::training::TrainingStore{fixture.training}.list_runs().size() == 2);
}

TEST_CASE(
    "train run refuses a GGUF, a backend name, a missing snapshot, an unknown dataset, "
    "a bad method and an unknown trainer -- before any run directory exists",
    "[commands][train][run]") {
    const RunFixture fixture;
    std::string err;
    CHECK(fixture.run({"train", "run", "x.gguf", "--dataset", "starter", "--trainer", "mock"},
                      nullptr, &err) == 1);
    CHECK(err.find("GGUF") != std::string::npos);
    CHECK(err.find("--safetensors") != std::string::npos);
    CHECK(fixture.run({"train", "run", "cloud", "--dataset", "starter", "--trainer", "mock"},
                      nullptr, &err) == 1);
    CHECK(err.find("backend entry (anthropic)") != std::string::npos);
    CHECK(fixture.run({"train", "run", "nope", "--dataset", "starter", "--trainer", "mock"},
                      nullptr, &err) == 1);
    CHECK(err.find("not a snapshot directory") != std::string::npos);
    CHECK(fixture.run({"train", "run", "tiny", "--dataset", "nope", "--trainer", "mock"}, nullptr,
                      &err) == 1);
    CHECK(err.find("no dataset named 'nope'") != std::string::npos);
    CHECK(fixture.run({"train", "run", "tiny", "--dataset", "starter", "--trainer", "mock",
                       "--method", "full"},
                      nullptr, &err) == 1);
    CHECK(err.find("--method must be") != std::string::npos);
    CHECK(fixture.run({"train", "run", "tiny", "--dataset", "starter", "--trainer", "cuda"},
                      nullptr, &err) == 1);
    CHECK(err.find("unknown trainer 'cuda'") != std::string::npos);
    // A real trainer on a pipe with no environment: the refusal names setup.
    CHECK(fixture.run({"train", "run", "tiny", "--dataset", "starter", "--trainer", "mlx"}, nullptr,
                      &err) == 1);
    CHECK(err.find("apogee train setup") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(fixture.training / "runs"));
}

TEST_CASE(
    "the resolvers: a student by directory or by name under the roots, a dataset by "
    "name or path, a suite by path, under suites/, as a prepared eval, or as a kit",
    "[commands][train][resolve]") {
    const RunFixture fixture;
    const apogee::harness::Config config = apogee::harness::load_config(fixture.base.config_path);
    const std::filesystem::path models = fixture.home / "models";
    const std::filesystem::path hf = fixture.home / "hf";
    write_file(hf / "org--repo" / "config.json", "{}");
    write_file(hf / "org--repo" / "a.safetensors", "w");
    CHECK(apogee::commands::resolve_student(config, models, hf, "tiny").path == models / "tiny");
    CHECK(apogee::commands::resolve_student(config, models, hf, "org--repo").path ==
          hf / "org--repo");
    CHECK(apogee::commands::resolve_student(config, models, hf, (models / "tiny").string()).path ==
          models / "tiny");
    CHECK_FALSE(apogee::commands::resolve_student(config, models, hf, "../tiny").error.empty());
    CHECK(apogee::commands::resolve_student(config, models, hf, "judge")
              .error.find("backend entry") != std::string::npos);

    std::string error;
    CHECK(apogee::commands::resolve_dataset(fixture.training / "datasets", "starter", error) ==
          fixture.training / "datasets" / "starter.jsonl");
    CHECK(apogee::commands::resolve_dataset(fixture.training / "datasets",
                                            (fixture.home / "suite.jsonl").string(),
                                            error) == fixture.home / "suite.jsonl");
    CHECK(apogee::commands::resolve_dataset(fixture.training / "datasets", "nope", error).empty());
    CHECK_FALSE(error.empty());

    const apogee::commands::SuiteResolution by_path =
        apogee::commands::resolve_suite(fixture.training, (fixture.home / "suite.jsonl").string());
    CHECK(by_path.error.empty());
    CHECK(by_path.items.size() == 2);
    write_file(fixture.training / "suites" / "mine.jsonl",
               "{\"prompt\": \"p\", \"expected\": \"e\"}\n");
    const apogee::commands::SuiteResolution named =
        apogee::commands::resolve_suite(fixture.training, "mine");
    CHECK(named.items.size() == 1);
    CHECK(named.label == (fixture.training / "suites" / "mine.jsonl").string());
    write_file(fixture.training / "datasets" / "prepped.eval.jsonl", "{\"prompt\": \"q\"}\n");
    CHECK(apogee::commands::resolve_suite(fixture.training, "prepped").items.size() == 1);
    const apogee::commands::SuiteResolution kit =
        apogee::commands::resolve_suite(fixture.training, "reasoning");
    CHECK(kit.error.empty());
    CHECK(kit.label == "kit:reasoning");
    CHECK(kit.items.size() == 8);
    CHECK(
        apogee::commands::resolve_suite(fixture.training, "nowhere").error.find("no eval suite") !=
        std::string::npos);

    apogee::training::ProgressEvent event;
    event.kind = apogee::training::ProgressEvent::Kind::Iteration;
    event.iteration = 12;
    event.total_iters = 100;
    event.loss = 1.23456;
    event.lr = 0.0001;
    event.throughput = 3.25;
    CHECK(apogee::commands::render_iteration(event) ==
          "iter 12/100 · loss 1.2346 · lr 1.00e-04 · 3.2 it/s");
    event.total_iters = 0;
    CHECK(apogee::commands::render_iteration(event).starts_with("iter 12 · "));
}

TEST_CASE(
    "train eval gates at 100%, judges pairwise through a named backend, skips loudly "
    "without one, refuses a vendor CLI, records, and re-runs only with --force",
    "[commands][train][eval]") {
    const RunFixture fixture{"B"};
    const std::string id = fixture.trained();
    std::string out;
    std::string err;

    // No judge: the free item skips and auto-passes, loudly.
    REQUIRE(fixture.run({"train", "eval", id, "--suite", (fixture.home / "suite.jsonl").string()},
                        &out, &err) == 0);
    CHECK(out.find("eval PASSED -- 100% (2/2)") != std::string::npos);
    CHECK(out.find("1 item(s) with no `expected` skipped") != std::string::npos);
    CHECK(err.find("no judge") != std::string::npos);
    apogee::training::RunManifest m = fixture.manifest(id);
    REQUIRE(m.eval.has_value());
    CHECK(m.eval->passed);
    CHECK(m.eval->num_skipped == 1);
    CHECK(m.eval->suite == (fixture.home / "suite.jsonl").string());

    // Already evaluated: nothing runs without --force.
    REQUIRE(fixture.run({"train", "eval", id, "--suite", (fixture.home / "suite.jsonl").string()},
                        &out, &err) == 0);
    CHECK(out.find("already ran") != std::string::npos);

    // A judge answering B: the free item is lost to the baseline, the gate
    // fails, and the manifest says so.
    REQUIRE(fixture.run({"train", "eval", id, "--suite", (fixture.home / "suite.jsonl").string(),
                         "--judge", "judge", "--force"},
                        &out, &err) == 0);
    CHECK(out.find("eval FAILED -- 50% (1/2)") != std::string::npos);
    CHECK(out.find("(judge: baseline)") != std::string::npos);
    CHECK(out.find("--force") != std::string::npos);
    m = fixture.manifest(id);
    CHECK_FALSE(m.eval->passed);
    CHECK(m.eval->judge_backend == "judge");
    CHECK(m.eval->items[1].baseline_got == "base: free");

    // The judge from the config, and a vendor CLI refused by type.
    write_file(fixture.base.config_path,
               read_file(fixture.base.config_path) + "training:\n  judge_backend: vendor\n");
    CHECK(fixture.run(
              {"train", "eval", id, "--suite", (fixture.home / "suite.jsonl").string(), "--force"},
              &out, &err) == 1);
    CHECK(err.find("vendor-CLI") != std::string::npos);
    CHECK(fixture.run({"train", "eval", id, "--suite", (fixture.home / "suite.jsonl").string(),
                       "--judge", "nope", "--force"},
                      &out, &err) == 1);
    CHECK(err.find("not a configured backend") != std::string::npos);
    // The metered cloud entry IS allowed as a judge: naming it satisfies
    // the spend rule -- its failure to answer here is a tie, never a refusal.
    CHECK(fixture.run({"train", "eval", "nope-run", "--suite", "x"}, &out, &err) == 1);
    CHECK(err.find("no run 'nope-run'") != std::string::npos);
    CHECK(fixture.run({"train", "eval", id, "--suite", "nowhere", "--force"}, &out, &err) == 1);
    CHECK(err.find("no eval suite") != std::string::npos);
}

TEST_CASE(
    "train promote gates, registers a new llamacpp entry byte-exactly, repoints an "
    "existing one in place, writes the ledger with max + 1, and prunes",
    "[commands][train][promote]") {
    const RunFixture fixture;
    const std::string id = fixture.trained();
    std::string out;
    std::string err;

    // The gate, before anything is built.
    CHECK(fixture.run({"train", "promote", id, "--as", "tuned"}, &out, &err) == 1);
    CHECK(err.find("no eval results") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(fixture.training / "versions"));
    CHECK(fixture.run({"train", "promote", id, "--as", "judge", "--force"}, &out, &err) == 1);
    CHECK(err.find("is a mock backend") != std::string::npos);
    CHECK(fixture.run({"train", "promote", id, "--as", "bad name", "--force"}, &out, &err) == 1);
    if (!apogee::models::quantize_supported()) {
        CHECK(fixture.run(
                  {"train", "promote", id, "--as", "tuned", "--force", "--quantize", "Q4_K_M"},
                  &out, &err) == 1);
        CHECK(err.find("APOGEE_ENABLE_LLAMA") != std::string::npos);
        CHECK(err.find("apogee models quantize") != std::string::npos);
    }
    CHECK(fixture.run({"train", "promote", id, "--as", "tuned", "--force", "--quantize", "Q9"},
                      &out, &err) == 1);
    CHECK(err.find("unknown quantization type") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(fixture.training / "versions"));

    (void)fixture.evaluated(id);
    const std::string before = read_file(fixture.base.config_path);
    REQUIRE(fixture.run({"train", "promote", id, "--as", "tuned"}, &out, &err) == 0);
    const std::filesystem::path v1 = fixture.training / "versions" / "tuned" / "v1.gguf";
    CHECK(std::filesystem::exists(v1));
    CHECK(out.find("promoted to new backend tuned -> v1") != std::string::npos);
    // The written path goes through the editor's one quoting rule (a Windows
    // drive colon makes it a quoted scalar), so the expectation does too.
    const auto scalar = [](const std::filesystem::path& path) {
        return apogee::harness::yaml_scalar(path.string());
    };
    CHECK(read_file(fixture.base.config_path) ==
          before + "\n  tuned:\n    type: llamacpp\n    model_path: " + scalar(v1) + "\n");
    const std::optional<apogee::training::VersionLedger> ledger =
        apogee::training::TrainingStore{fixture.training}.list_versions("tuned");
    REQUIRE(ledger.has_value());
    CHECK(ledger->active_version == 1);
    REQUIRE(ledger->versions.size() == 1);
    CHECK(ledger->versions[0].run_id == id);
    CHECK(ledger->versions[0].eval_passed == true);
    CHECK_FALSE(std::filesystem::exists(fixture.training / "runs" / id / "fused"));

    // Into the existing entry: only the path changes; --keep-fused keeps.
    const std::string id2 = fixture.trained(3);
    (void)fixture.evaluated(id2);
    const std::string registered = read_file(fixture.base.config_path);
    REQUIRE(fixture.run({"train", "promote", id2, "--as", "tuned", "--keep-fused"}, &out, &err) ==
            0);
    const std::filesystem::path v2 = fixture.training / "versions" / "tuned" / "v2.gguf";
    CHECK(out.find("updated backend tuned -> v2") != std::string::npos);
    std::string expected = registered;
    REQUIRE(expected.find(scalar(v1)) != std::string::npos);
    expected.replace(expected.find(scalar(v1)), scalar(v1).size(), scalar(v2));
    CHECK(read_file(fixture.base.config_path) == expected);
    CHECK(std::filesystem::exists(fixture.training / "runs" / id2 / "fused" / "config.json"));

    // Soft mode warns through, hard refuses, --force skips a failed gate.
    const RunFixture failing{"B"};
    const std::string id3 = failing.trained();
    (void)failing.evaluated(id3, {"--judge", "judge"});
    CHECK(failing.run({"train", "promote", id3, "--as", "tuned"}, &out, &err) == 1);
    CHECK(err.find("eval gate failed") != std::string::npos);
    write_file(failing.base.config_path,
               read_file(failing.base.config_path) +
                   "training:\n  gate_mode: soft\n  retain_versions: 1\n");
    REQUIRE(failing.run({"train", "promote", id3, "--as", "tuned"}, &out, &err) == 0);
    CHECK(err.find("gate_mode is soft") != std::string::npos);
    REQUIRE(failing.run({"train", "promote", id3, "--as", "tuned", "--force"}, &out, &err) == 0);
    CHECK(out.find("pruned:") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(failing.training / "versions" / "tuned" / "v1.gguf"));
    CHECK(std::filesystem::exists(failing.training / "versions" / "tuned" / "v2.gguf"));
    // max + 1 after the prune: the next is v3, never v2 again.
    REQUIRE(failing.run({"train", "promote", id3, "--as", "tuned", "--force"}, &out, &err) == 0);
    CHECK(std::filesystem::exists(failing.training / "versions" / "tuned" / "v3.gguf"));
    CHECK(fixture.run({"train", "promote", "nope", "--as", "x"}, &out, &err) == 1);
}

TEST_CASE(
    "train rollback repoints at the previous version and deletes nothing; versions and "
    "status read the ledgers",
    "[commands][train][rollback]") {
    const RunFixture fixture;
    std::string out;
    std::string err;
    CHECK(fixture.run({"train", "rollback", "tuned"}, &out, &err) == 1);
    CHECK(err.find("no version history") != std::string::npos);
    CHECK(fixture.run({"train", "versions", "tuned"}, &out, &err) == 1);
    REQUIRE(fixture.run({"train", "versions"}, &out, &err) == 0);
    CHECK(out.find("no promoted versions") != std::string::npos);
    REQUIRE(fixture.run({"train", "status"}, &out, &err) == 0);
    CHECK(out.find("Runs: none") != std::string::npos);

    const std::string id = fixture.trained();
    (void)fixture.evaluated(id);
    REQUIRE(fixture.run({"train", "promote", id, "--as", "tuned"}, &out, &err) == 0);
    CHECK(fixture.run({"train", "rollback", "tuned"}, &out, &err) == 1);
    CHECK(err.find("only one promoted version") != std::string::npos);
    REQUIRE(fixture.run({"train", "promote", id, "--as", "tuned"}, &out, &err) == 0);
    const std::string at_v2 = read_file(fixture.base.config_path);
    REQUIRE(fixture.run({"train", "rollback", "tuned"}, &out, &err) == 0);
    CHECK(out.find("v2 -> v1") != std::string::npos);
    const std::filesystem::path v1 = fixture.training / "versions" / "tuned" / "v1.gguf";
    const std::filesystem::path v2 = fixture.training / "versions" / "tuned" / "v2.gguf";
    CHECK(std::filesystem::exists(v1));
    CHECK(std::filesystem::exists(v2));
    const auto scalar = [](const std::filesystem::path& path) {
        return apogee::harness::yaml_scalar(path.string());
    };
    std::string expected = at_v2;
    REQUIRE(expected.find(scalar(v2)) != std::string::npos);
    expected.replace(expected.find(scalar(v2)), scalar(v2).size(), scalar(v1));
    CHECK(read_file(fixture.base.config_path) == expected);
    const std::optional<apogee::training::VersionLedger> ledger =
        apogee::training::TrainingStore{fixture.training}.list_versions("tuned");
    REQUIRE(ledger.has_value());
    CHECK(ledger->active_version == 1);
    CHECK(ledger->versions.size() == 2);

    REQUIRE(fixture.run({"train", "versions", "tuned"}, &out, &err) == 0);
    CHECK(out.find("active v1") != std::string::npos);
    CHECK(out.find("v1 ") != std::string::npos);
    CHECK(out.find("<- active") != std::string::npos);
    CHECK(out.find("pass 100%") != std::string::npos);
    REQUIRE(fixture.run({"train", "status"}, &out, &err) == 0);
    CHECK(out.find("Runs: 1 (0 running)") != std::string::npos);
    CHECK(out.find("eval pass 100%") != std::string::npos);
    CHECK(out.find("active v1  (2 kept of 2)") != std::string::npos);

    // The oldest version has nothing below it.
    CHECK(fixture.run({"train", "rollback", "tuned"}, &out, &err) == 1);
    CHECK(err.find("oldest version") != std::string::npos);

    // A backend removed from the config cannot be repointed.
    REQUIRE(fixture.run({"train", "promote", id, "--as", "tuned"}, &out, &err) == 0);
    REQUIRE(fixture.run({"config", "delete-backend", "tuned"}, &out, &err) == 0);
    CHECK(fixture.run({"train", "rollback", "tuned"}, &out, &err) == 1);
    CHECK(err.find("not in the config") != std::string::npos);
}

TEST_CASE(
    "a failing driver is a failed run through the command line, and a promote whose build "
    "fails leaves the config and the ledger untouched",
    "[commands][train][run][promote]") {
    const RunFixture fixture;
    write_file(fixture.training / "datasets" / "broken.jsonl",
               "{\"mock\": {\"error\": \"GPU on fire\"}}\n");
    std::string out;
    std::string err;
    CHECK(fixture.run(
              {"train", "run", "tiny", "--dataset", "broken", "--trainer", "mock", "--iters", "3"},
              &out, &err) == 2);
    CHECK(err.find("GPU on fire") != std::string::npos);
    CHECK(err.find("recorded as failed") != std::string::npos);
    const std::vector<apogee::training::RunSummary> runs =
        apogee::training::TrainingStore{fixture.training}.list_runs();
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].status == "failed");
    const apogee::training::RunManifest failed = fixture.manifest(runs[0].id);
    CHECK(failed.error == "GPU on fire");
    CHECK(failed.iterations == 1);
    CHECK_FALSE(failed.finished_at.empty());
    // A failed run can be neither evaluated nor promoted.
    CHECK(fixture.run(
              {"train", "eval", runs[0].id, "--suite", (fixture.home / "suite.jsonl").string()},
              &out, &err) == 1);
    CHECK(err.find("only a complete run") != std::string::npos);
    CHECK(fixture.run({"train", "promote", runs[0].id, "--as", "tuned", "--force"}, &out, &err) ==
          1);
    CHECK(err.find("only a complete run") != std::string::npos);

    // A run whose adapter cannot be fused: the build fails after the gate,
    // and nothing downstream happens.
    write_file(fixture.training / "datasets" / "unfusable.jsonl",
               "{\"mock\": {\"fuse_error\": \"cannot fuse\"}}\n");
    REQUIRE(fixture.run({"train", "run", "tiny", "--dataset", "unfusable", "--trainer", "mock",
                         "--iters", "2"},
                        &out, &err) == 0);
    const std::string id = apogee::training::TrainingStore{fixture.training}.list_runs().front().id;
    (void)fixture.evaluated(id);
    const std::string before = read_file(fixture.base.config_path);
    CHECK(fixture.run({"train", "promote", id, "--as", "tuned"}, &out, &err) == 2);
    CHECK(err.find("cannot fuse") != std::string::npos);
    CHECK(err.find("unchanged") != std::string::npos);
    CHECK(read_file(fixture.base.config_path) == before);
    CHECK_FALSE(std::filesystem::exists(fixture.training / "versions"));
    CHECK_FALSE(std::filesystem::exists(fixture.training / "runs" / id / "fused"));
}

// ---------------------------------------------------------------------------
// The pipelines item: `train pipeline run|resume|status`, `train regime run`
// and `train cycle run|status|halt|resume` on the mock trainer, the mock
// backend as teacher and judge, through the real command tree.
// ---------------------------------------------------------------------------

#include "logger/session.h"
#include "training/cycle.h"
#include "training/pipeline.h"

namespace {

constexpr std::string_view kKitTemplate =
    "name: {name}\ndescription: a test kit\nsynth:\n  system: |\n    Make examples.\n  seeds:\n"
    "    - greetings\n  count: 4\n  per_seed: 2\ntrain:\n  iters: 2\neval:\n  - prompt: please "
    "say hello\n    expected: hello\n";

std::string kit_text(const std::string& name) {
    std::string text{kKitTemplate};
    text.replace(text.find("{name}"), 6, name);
    return text;
}

/// The run fixture plus a mock teacher scripted to answer a JSON array of
/// examples, two custom kits whose suites the mock's echo passes, and the
/// suites and datasets the pipeline cases use. `{home}` in the extra
/// config is the fixture's home.
struct PipelineFixture {
    RunFixture base;
    std::filesystem::path home = base.home;
    std::filesystem::path training = base.training;

    explicit PipelineFixture(std::string extra_config = {}) {
        std::string config = read_file(base.base.config_path);
        config.insert(config.find("  vendor:\n"), "  teacher:\n    type: mock\n    model_path: " +
                                                      (home / "teacher.json").string() + "\n");
        for (std::size_t at = extra_config.find("{home}"); at != std::string::npos;
             at = extra_config.find("{home}")) {
            extra_config.replace(at, 6, home.string());
        }
        write_file(base.base.config_path, config + extra_config);
        write_file(
            home / "teacher.json",
            nlohmann::json{{"turns", nlohmann::json::array(
                                         {{{"text",
                                            "[{\"prompt\": \"say hello\", \"completion\": "
                                            "\"hello\"}, {\"prompt\": \"say hi\", \"completion\": "
                                            "\"hi\"}]"}}})}}
                .dump());
        write_file(training / "kits" / "alpha.yaml", kit_text("alpha"));
        write_file(training / "kits" / "beta.yaml", kit_text("beta"));
        write_file(training / "suites" / "hello.jsonl",
                   "{\"prompt\": \"say hello\", \"expected\": \"hello\"}\n");
        write_file(training / "suites" / "nope.jsonl",
                   "{\"prompt\": \"say nope\", \"expected\": \"nope\"}\n");
        write_file(training / "datasets" / "regress.jsonl", "{\"mock\": {\"answer\": \"nope\"}}\n");
    }

    int run(const std::vector<std::string>& args, std::string* out = nullptr,
            std::string* err = nullptr) const {
        return base.run(args, out, err);
    }

    [[nodiscard]] std::string spec_file(std::string_view second_dataset) const {
        const std::filesystem::path path = home / "pipe.yaml";
        write_file(path,
                   "name: two\nstudent: tiny\nstages:\n  - name: a\n    dataset: starter\n"
                   "    eval_suite: hello\n    iters: 2\n  - name: b\n    dataset: " +
                       std::string{second_dataset} + "\n    eval_suite: nope\n    iters: 2\n");
        return path.string();
    }

    [[nodiscard]] std::vector<apogee::training::PipelineSummary> pipelines() const {
        return apogee::training::TrainingStore{training}.list_pipelines();
    }

    [[nodiscard]] apogee::training::PipelineRunManifest pipeline(const std::string& id) const {
        const auto m = apogee::training::TrainingStore{training}.get_pipeline(id);
        REQUIRE(m.has_value());
        return *m;
    }

    /// The one pipeline run that was not there before -- two runs in one
    /// second share a start time, so "newest" is by exclusion.
    [[nodiscard]] std::string pipeline_since(
        const std::vector<apogee::training::PipelineSummary>& before) const {
        for (const apogee::training::PipelineSummary& candidate : pipelines()) {
            bool seen = false;
            for (const apogee::training::PipelineSummary& old : before) {
                seen = seen || old.id == candidate.id;
            }
            if (!seen) {
                return candidate.id;
            }
        }
        FAIL("no new pipeline run");
        return {};
    }

    void save_session(std::string started_at,
                      std::vector<apogee::harness::ChatMessage> messages) const {
        apogee::logger::Session session;
        session.chat_id = apogee::logger::new_chat_id();
        session.backend = "judge";
        session.started_at = std::move(started_at);
        session.messages = std::move(messages);
        apogee::logger::save(session);
    }
};

}  // namespace

TEST_CASE(
    "train pipeline run chains fused checkpoints under the cumulative gate, aborts on a "
    "regression, resumes after the fix, prints status, and promotes a stage run; refusals",
    "[commands][train][pipeline]") {
    const PipelineFixture fixture;
    std::string out;
    std::string err;
    const std::string spec = fixture.spec_file("regress");
    CHECK(fixture.run({"train", "pipeline", "run", "--pipeline", spec, "--trainer", "mock"}, &out,
                      &err) == 2);
    CHECK(err.find("cumulative eval gate") != std::string::npos);
    CHECK(err.find("[pipeline] two as pipe-") != std::string::npos);
    CHECK(out.find("pipeline aborted") != std::string::npos);
    REQUIRE(fixture.pipelines().size() == 1);
    const std::string id = fixture.pipelines().front().id;
    CHECK(id.starts_with("pipe-"));
    apogee::training::PipelineRunManifest m = fixture.pipeline(id);
    CHECK(m.status == "aborted");
    CHECK(m.stages[0].status == "passed");
    CHECK(m.stages[1].status == "failed");
    CHECK(std::filesystem::exists(fixture.training / "runs" / (id + "-s0") / "fused"));
    CHECK(fixture.base.manifest(id + "-s1").pipeline_run_id == id);
    CHECK(fixture.base.manifest(id + "-s1").parent_run == id + "-s0");

    REQUIRE(fixture.run({"train", "pipeline", "status", id}, &out, &err) == 0);
    CHECK(out.find("status:  aborted") != std::string::npos);
    CHECK(out.find("Resume from stage 2") != std::string::npos);
    CHECK(out.find("apogee train promote " + id + "-s0") != std::string::npos);
    REQUIRE(fixture.run({"train", "pipeline", "status"}, &out, &err) == 0);
    CHECK(out.find(id) != std::string::npos);
    CHECK(out.find("1/2") != std::string::npos);
    REQUIRE(fixture.run({"train", "status"}, &out, &err) == 0);
    CHECK(out.find("Pipeline: none in progress; 1 run(s), latest " + id + " aborted") !=
          std::string::npos);
    CHECK(out.find("Cycle: not configured") != std::string::npos);

    // Fixed, resumed: only stage b runs, the run completes, the stage run
    // is an ordinary run that eval and promote take.
    write_file(fixture.training / "datasets" / "regress.jsonl",
               read_file(fixture.training / "datasets" / "starter.jsonl"));
    const std::string s0_started = fixture.base.manifest(id + "-s0").started_at;
    REQUIRE(
        fixture.run({"train", "pipeline", "resume", id, "--pipeline", spec, "--trainer", "mock"},
                    &out, &err) == 0);
    CHECK(out.find("pipeline complete") != std::string::npos);
    CHECK(out.find("apogee train promote " + id + "-s1") != std::string::npos);
    CHECK(err.find("[pipeline] resuming") != std::string::npos);
    m = fixture.pipeline(id);
    CHECK(m.complete());
    CHECK(m.stages[1].status == "passed");
    CHECK(fixture.base.manifest(id + "-s0").started_at == s0_started);
    REQUIRE(fixture.run({"train", "eval", id + "-s1", "--suite", "hello"}, &out, &err) == 0);
    CHECK(out.find("already ran") != std::string::npos);
    REQUIRE(fixture.run({"train", "promote", id + "-s1", "--as", "staged"}, &out, &err) == 0);
    CHECK(std::filesystem::exists(fixture.training / "versions" / "staged" / "v1.gguf"));
    CHECK(fixture.run({"train", "pipeline", "resume", id, "--pipeline", spec}, &out, &err) == 1);
    CHECK(err.find("already complete") != std::string::npos);

    // A named pipeline from the config; resume finds the spec by name.
    write_file(fixture.base.base.config_path,
               read_file(fixture.base.base.config_path) +
                   "training:\n  trainer: mock\n  pipelines:\n    named:\n      student: tiny\n"
                   "      stages:\n        - name: only\n          dataset: starter\n"
                   "          eval_suite: hello\n          iters: 1\n");
    REQUIRE(fixture.run({"train", "pipeline", "run", "--pipeline", "named"}, &out, &err) == 0);
    CHECK(fixture.pipelines().size() == 2);
    const std::string named = fixture.pipelines().front().id;
    CHECK(fixture.pipeline(named).spec_name == "named");
    CHECK(fixture.run({"train", "pipeline", "resume", named}, &out, &err) == 1);
    CHECK(err.find("already complete") != std::string::npos);

    // Refusals, each before a run directory exists.
    const std::size_t before = fixture.pipelines().size();
    CHECK(fixture.run({"train", "pipeline", "run", "--pipeline", "nowhere"}, &out, &err) == 1);
    CHECK(err.find("no pipeline 'nowhere'") != std::string::npos);
    CHECK(fixture.run({"train", "pipeline", "run", "--pipeline", "nowhere.yaml"}, &out, &err) == 1);
    CHECK(err.find("no pipeline spec file") != std::string::npos);
    const std::string missing = fixture.spec_file("missing");
    CHECK(fixture.run({"train", "pipeline", "run", "--pipeline", missing}, &out, &err) == 1);
    CHECK(err.find("stage 'b'") != std::string::npos);
    CHECK(err.find("no dataset named 'missing'") != std::string::npos);
    write_file(fixture.home / "bad.yaml", "student: tiny\nstages: []\n");
    CHECK(fixture.run(
              {"train", "pipeline", "run", "--pipeline", (fixture.home / "bad.yaml").string()},
              &out, &err) == 1);
    CHECK(err.find("at least one stage") != std::string::npos);
    write_file(fixture.home / "nostudent.yaml",
               "stages:\n  - name: a\n    dataset: starter\n    eval_suite: hello\n");
    CHECK(fixture.run({"train", "pipeline", "run", "--pipeline",
                       (fixture.home / "nostudent.yaml").string()},
                      &out, &err) == 1);
    CHECK(err.find("names no student") != std::string::npos);
    CHECK(fixture.run({"train", "pipeline", "resume", "nope"}, &out, &err) == 1);
    CHECK(err.find("no pipeline run 'nope'") != std::string::npos);
    CHECK(fixture.run({"train", "pipeline", "resume", "../x"}, &out, &err) == 1);
    CHECK(fixture.run({"train", "pipeline", "status", "nope"}, &out, &err) == 1);
    CHECK(fixture.pipelines().size() == before);

    // Judge items through a named judge; a lost verdict aborts.
    write_file(fixture.training / "suites" / "free.jsonl", "{\"prompt\": \"free\"}\n");
    write_file(fixture.home / "judged.yaml",
               "student: tiny\nstages:\n  - name: a\n    dataset: starter\n    eval_suite: "
               "free\n    iters: 1\n");
    REQUIRE(fixture.run({"train", "pipeline", "run", "--pipeline",
                         (fixture.home / "judged.yaml").string(), "--judge", "judge"},
                        &out, &err) == 0);
    CHECK(fixture.pipeline(fixture.pipelines().front().id).stages[0].eval->judge_backend ==
          "judge");
    CHECK(fixture.run({"train", "pipeline", "run", "--pipeline",
                       (fixture.home / "judged.yaml").string(), "--judge", "vendor"},
                      &out, &err) == 1);
    CHECK(err.find("vendor-CLI") != std::string::npos);
}

TEST_CASE(
    "train regime run distils through the teacher over the kits in order, promotes with --as "
    "and stops with --no-promote; flags win over a named regime; every refusal comes first",
    "[commands][train][regime]") {
    const PipelineFixture fixture;
    std::string out;
    std::string err;
    REQUIRE(
        fixture.run({"train", "regime", "run", "--teacher", "teacher", "--student", "tiny", "--kit",
                     "beta", "--kit", "alpha", "--count", "2", "--trainer", "mock", "--no-promote"},
                    &out, &err) == 0);
    CHECK(err.find("[regime] regime as regime-") != std::string::npos);
    CHECK(err.find("kits beta -> alpha") != std::string::npos);
    CHECK(out.find("kit beta: 2 example(s) in 1 call(s)") != std::string::npos);
    CHECK(out.find("pipeline complete") != std::string::npos);
    CHECK(out.find("Regime complete. Promote when ready") != std::string::npos);
    REQUIRE(fixture.pipelines().size() == 1);
    const std::string pipe = fixture.pipelines().front().id;
    CHECK(pipe.starts_with("regime-"));
    CHECK(pipe.ends_with("-pipe"));
    const apogee::training::PipelineRunManifest m = fixture.pipeline(pipe);
    CHECK(m.stages[0].name == "beta");
    CHECK(m.stages[1].name == "alpha");
    CHECK(m.complete());
    const std::filesystem::path work =
        fixture.training / "regime" / pipe.substr(0, pipe.size() - 5);
    CHECK(std::filesystem::exists(work / "beta.jsonl"));
    CHECK(std::filesystem::exists(work / "beta.eval.jsonl"));
    CHECK(std::filesystem::exists(work / "alpha.jsonl"));
    CHECK(fixture.base.manifest(pipe + "-s0").iters == 2);  // the kit's train.iters
    CHECK_FALSE(std::filesystem::exists(fixture.training / "versions"));
    // --no-promote wins over --as: the pipeline runs, nothing is promoted.
    REQUIRE(
        fixture.run({"train", "regime", "run", "--teacher", "teacher", "--student", "tiny", "--kit",
                     "alpha", "--count", "2", "--trainer", "mock", "--as", "held", "--no-promote"},
                    &out, &err) == 0);
    CHECK(out.find("Promote when ready") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(fixture.training / "versions"));

    // Promoted as a new backend, the ledger and the config written.
    std::vector<apogee::training::PipelineSummary> seen = fixture.pipelines();
    REQUIRE(
        fixture.run({"train", "regime", "run", "--teacher", "teacher", "--student", "tiny", "--kit",
                     "alpha", "--count", "2", "--trainer", "mock", "--as", "tuned", "--iters", "1"},
                    &out, &err) == 0);
    CHECK(out.find("regime regime complete -- tuned is now v1") != std::string::npos);
    CHECK(std::filesystem::exists(fixture.training / "versions" / "tuned" / "v1.gguf"));
    CHECK(read_file(fixture.base.base.config_path).find("  tuned:\n    type: llamacpp\n") !=
          std::string::npos);
    CHECK(fixture.base.manifest(fixture.pipeline_since(seen) + "-s0").iters == 1);

    // A named regime, the flags winning field by field.
    write_file(fixture.base.base.config_path,
               read_file(fixture.base.base.config_path) +
                   "training:\n  trainer: mock\n  regimes:\n    mine:\n      teacher: teacher\n"
                   "      student: tiny\n      kits: [alpha, beta]\n      count: 2\n");
    seen = fixture.pipelines();
    REQUIRE(fixture.run({"train", "regime", "run", "mine", "--kit", "beta", "--no-promote"}, &out,
                        &err) == 0);
    CHECK(err.find("[regime] mine as") != std::string::npos);
    const std::string named = fixture.pipeline_since(seen);
    CHECK(fixture.pipeline(named).stages.size() == 1);
    CHECK(fixture.pipeline(named).stages[0].name == "beta");
    write_file(fixture.home / "spec.yaml",
               "teacher: teacher\nstudent: tiny\nkits: [alpha]\ncount: 2\n");
    REQUIRE(fixture.run({"train", "regime", "run", "--regime",
                         (fixture.home / "spec.yaml").string(), "--no-promote"},
                        &out, &err) == 0);
    CHECK(err.find("[regime] spec as") != std::string::npos);

    // The refusals, before any teacher call or work directory.
    const std::size_t before = fixture.pipelines().size();
    CHECK(fixture.run({"train", "regime", "run", "--student", "tiny", "--kit", "alpha"}, &out,
                      &err) == 1);
    CHECK(err.find("no teacher") != std::string::npos);
    CHECK(fixture.run({"train", "regime", "run", "--teacher", "vendor", "--student", "tiny",
                       "--kit", "alpha"},
                      &out, &err) == 1);
    CHECK(err.find("vendor-CLI") != std::string::npos);
    CHECK(fixture.run({"train", "regime", "run", "--teacher", "teacher", "--kit", "alpha"}, &out,
                      &err) == 1);
    CHECK(err.find("no student") != std::string::npos);
    CHECK(fixture.run({"train", "regime", "run", "--teacher", "teacher", "--student", "nope",
                       "--kit", "alpha"},
                      &out, &err) == 1);
    CHECK(err.find("student:") != std::string::npos);
    CHECK(fixture.run({"train", "regime", "run", "--teacher", "teacher", "--student", "tiny"}, &out,
                      &err) == 1);
    CHECK(err.find("no kits") != std::string::npos);
    CHECK(fixture.run({"train", "regime", "run", "--teacher", "teacher", "--student", "tiny",
                       "--kit", "nope"},
                      &out, &err) == 1);
    CHECK(err.find("kit 'nope'") != std::string::npos);
    CHECK(fixture.run({"train", "regime", "run", "nowhere"}, &out, &err) == 1);
    CHECK(err.find("no regime 'nowhere'") != std::string::npos);
    CHECK(fixture.run({"train", "regime", "run", "--regime", "nowhere.yaml"}, &out, &err) == 1);
    CHECK(fixture.run({"train", "regime", "run", "--teacher", "teacher", "--student", "tiny",
                       "--kit", "alpha", "--as", "judge"},
                      &out, &err) == 1);
    CHECK(err.find("is a mock backend") != std::string::npos);
    CHECK(fixture.run({"train", "regime", "run", "--teacher", "teacher", "--student", "tiny",
                       "--kit", "alpha", "--max-tokens", "0"},
                      &out, &err) == 1);
    CHECK(fixture.pipelines().size() == before);
}

TEST_CASE(
    "train cycle run: a pass promotes and sets the anchor, no data skips, a regression fails "
    "and trips the breaker, halted refuses until resume; status, halt, --source; the sessions "
    "source needs consent and is consumed once",
    "[commands][train][cycle]") {
    const std::string cycle_block =
        "training:\n  trainer: mock\n  pipelines:\n    nightly:\n      student: tiny\n"
        "      stages:\n        - name: base\n          dataset: starter\n"
        "          eval_suite: hello\n          iters: 2\n  cycle:\n    pipeline: nightly\n"
        "    backend: nightly-model\n    circuit_breaker_k: 1\n    sources:\n"
        "      - type: directory\n        dir: {home}/queue\n";
    const PipelineFixture fixture{cycle_block};
    const std::filesystem::path queue = fixture.home / "queue";
    const std::filesystem::path cycle_dir = fixture.training / "cycle";
    std::string out;
    std::string err;

    REQUIRE(fixture.run({"train", "cycle", "status"}, &out, &err) == 0);
    CHECK(out.find("no cycle runs yet") != std::string::npos);
    REQUIRE(fixture.run({"train", "status"}, &out, &err) == 0);
    CHECK(out.find("Cycle: configured (pipeline 'nightly' -> nightly-model), no runs yet") !=
          std::string::npos);

    REQUIRE(fixture.run({"train", "cycle", "run"}, &out, &err) == 0);
    CHECK(out.find("cycle skipped") != std::string::npos);
    CHECK(std::filesystem::is_directory(queue));
    CHECK_FALSE(std::filesystem::exists(cycle_dir / "cycle.lock"));

    write_file(queue / "day1.jsonl", read_file(fixture.training / "datasets" / "starter.jsonl"));
    const std::string before = read_file(fixture.base.base.config_path);
    REQUIRE(fixture.run({"train", "cycle", "run"}, &out, &err) == 0);
    CHECK(out.find("cycle PASSED -- nightly-model promoted to v1") != std::string::npos);
    CHECK(err.find("[cycle] anchor set to v1") != std::string::npos);
    // The entry landed in the backends section through the one editor, and
    // not a line of the config was removed.
    const std::string after = read_file(fixture.base.base.config_path);
    CHECK(after.find("  nightly-model:\n    type: llamacpp\n    model_path: " +
                     apogee::harness::yaml_scalar(
                         (fixture.training / "versions" / "nightly-model" / "v1.gguf").string()) +
                     "\n") != std::string::npos);
    CHECK(after.find("  nightly-model:") < after.find("training:"));
    std::size_t cursor = 0;
    std::istringstream lines{before};
    for (std::string line; std::getline(lines, line);) {
        cursor = after.find(line + "\n", cursor);
        REQUIRE(cursor != std::string::npos);
        cursor += line.size() + 1;
    }
    CHECK(std::filesystem::exists(queue / "consumed" / "day1.jsonl"));
    CHECK_FALSE(std::filesystem::exists(queue / "day1.jsonl"));
    std::string error;
    apogee::training::CycleHistory history =
        apogee::training::load_history(cycle_dir, "nightly-model", error);
    CHECK(history.anchor_version == 1);
    CHECK(history.total_runs == 2);
    REQUIRE(fixture.pipelines().size() == 1);
    CHECK(fixture.pipelines().front().id.starts_with("cycle-"));

    REQUIRE(fixture.run({"train", "cycle", "status"}, &out, &err) == 0);
    CHECK(out.find("anchor:            v1 (100%)") != std::string::npos);
    CHECK(out.find("skipped") != std::string::npos);
    CHECK(out.find("pass") != std::string::npos);
    CHECK(out.find("state:             idle") != std::string::npos);
    REQUIRE(fixture.run({"train", "status"}, &out, &err) == 0);
    CHECK(out.find("Cycle: idle; backend nightly-model; 2 run(s), 0 consecutive failure(s); "
                   "anchor v1 (100%)") != std::string::npos);

    // A regression: the pipeline aborts, the cycle fails, the breaker at 1
    // halts, nothing reached inference, the file stays queued.
    write_file(queue / "day2.jsonl", "{\"mock\": {\"answer\": \"nope\"}}\n");
    const std::string at_v1 = read_file(fixture.base.base.config_path);
    CHECK(fixture.run({"train", "cycle", "run"}, &out, &err) == 2);
    CHECK(out.find("cycle FAILED") != std::string::npos);
    CHECK(out.find("nothing reached inference") != std::string::npos);
    CHECK(out.find("the loop is halted") != std::string::npos);
    CHECK(read_file(fixture.base.base.config_path) == at_v1);
    CHECK(std::filesystem::exists(queue / "day2.jsonl"));
    CHECK(fixture.run({"train", "cycle", "run"}, &out, &err) == 1);
    CHECK(err.find("halted") != std::string::npos);
    CHECK(err.find("apogee train cycle resume") != std::string::npos);
    REQUIRE(fixture.run({"train", "status"}, &out, &err) == 0);
    CHECK(out.find("Cycle: HALTED -- circuit breaker") != std::string::npos);
    REQUIRE(fixture.run({"train", "cycle", "resume"}, &out, &err) == 0);
    CHECK(out.find("cycle resumed") != std::string::npos);
    history = apogee::training::load_history(cycle_dir, "nightly-model", error);
    CHECK_FALSE(history.halted);
    CHECK(history.consecutive_fails == 0);
    REQUIRE(fixture.run({"train", "cycle", "halt"}, &out, &err) == 0);
    CHECK(out.find("cycle halted") != std::string::npos);
    REQUIRE(fixture.run({"train", "cycle", "halt"}, &out, &err) == 0);
    CHECK(out.find("already halted") != std::string::npos);
    CHECK(fixture.run({"train", "cycle", "run"}, &out, &err) == 1);
    REQUIRE(fixture.run({"train", "cycle", "resume"}, &out, &err) == 0);
    REQUIRE(fixture.run({"train", "cycle", "resume"}, &out, &err) == 0);
    CHECK(out.find("was not halted") != std::string::npos);

    // --source replaces the queue for one run; the pass repoints v1 -> v2.
    std::filesystem::remove(queue / "day2.jsonl");
    write_file(fixture.home / "elsewhere" / "x.jsonl",
               read_file(fixture.training / "datasets" / "starter.jsonl"));
    REQUIRE(
        fixture.run({"train", "cycle", "run", "--source", (fixture.home / "elsewhere").string()},
                    &out, &err) == 0);
    CHECK(out.find("promoted to v2") != std::string::npos);
    CHECK(std::filesystem::exists(fixture.home / "elsewhere" / "consumed" / "x.jsonl"));
    CHECK(apogee::training::load_history(cycle_dir, "nightly-model", error).anchor_version == 1);

    // Unconfigured: refused naming the keys.
    const RunFixture bare;
    CHECK(bare.run({"train", "cycle", "run"}, &out, &err) == 1);
    CHECK(err.find("training.cycle is not configured") != std::string::npos);
    CHECK(bare.run({"train", "cycle", "resume"}, &out, &err) == 0);
    CHECK(out.find("nothing to resume") != std::string::npos);

    // Sessions: refused at load without consent; with it, consumed once.
    const std::string sessions_block =
        "training:\n  trainer: mock\n  pipelines:\n    nightly:\n      student: tiny\n"
        "      stages:\n        - name: base\n          dataset: starter\n"
        "          eval_suite: hello\n          iters: 2\n  cycle:\n    pipeline: nightly\n"
        "    backend: nightly-model\n    sources:\n      - type: sessions\n";
    const PipelineFixture refused{sessions_block};
    CHECK(refused.run({"train", "cycle", "run"}, &out, &err) == 1);
    CHECK(err.find("log_consent: true") != std::string::npos);
    const PipelineFixture consented{sessions_block + "        log_consent: true\n"};
    consented.save_session("2026-09-18T10:00:00Z",
                           {apogee::harness::ChatMessage::user("say hello"),
                            apogee::harness::ChatMessage::assistant("hello")});
    consented.save_session("2026-09-19T10:00:00Z", {apogee::harness::ChatMessage::user("q"),
                                                    apogee::harness::ChatMessage::assistant("a")});
    REQUIRE(consented.run({"train", "cycle", "run"}, &out, &err) == 0);
    CHECK(out.find("cycle PASSED") != std::string::npos);
    CHECK(err.find("[cycle] sessions: 2 exchange(s) from 2 session(s)") != std::string::npos);
    history = apogee::training::load_history(consented.training / "cycle", "nightly-model", error);
    CHECK(history.sessions_until == "2026-09-19T10:00:00Z");
    REQUIRE(consented.run({"train", "cycle", "run"}, &out, &err) == 0);
    CHECK(out.find("cycle skipped") != std::string::npos);
    REQUIRE(consented.run({"train", "cycle", "status"}, &out, &err) == 0);
    CHECK(out.find("sessions consumed: through 2026-09-19T10:00:00Z") != std::string::npos);
}
