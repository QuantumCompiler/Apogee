#include "commands/train.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "commands/registry.h"
#include "commands/root.h"
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
    CHECK(read_file(fixture.base.config_path) ==
          before + "\n  tuned:\n    type: llamacpp\n    model_path: " + v1.string() + "\n");
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
    expected.replace(expected.find(v1.string()), v1.string().size(), v2.string());
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
    std::string expected = at_v2;
    expected.replace(expected.find(v2.string()), v2.string().size(), v1.string());
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
