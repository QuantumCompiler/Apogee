#include "cli/train.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "cli/registry.h"
#include "cli/root.h"
#include "contracts/config_edit.h"
#include "modelstore/mlx_info.h"
#include "modelstore/sidecar.h"
#include "modelstore/snapshot.h"
#include "support/env_guard.h"
#include "support/gguf_builder.h"

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

#include "backends/adapters.h"
#include "contracts/assets.h"
#include "contracts/layout.h"
#include "contracts/sha256.h"
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
    /// The student, where the model store keeps a SafeTensors set.
    std::filesystem::path student = home / "models" / "tiny" / "safetensors" / "aaaaaaaaaaaa";

    /// The GGUF a promoted version's ledger entry records -- in the model
    /// store, beside the base model it was trained from.
    [[nodiscard]] std::filesystem::path version_file(std::string_view backend, int version) const {
        const std::optional<apogee::training::VersionLedger> ledger =
            apogee::training::TrainingStore{training}.list_versions(backend);
        if (!ledger.has_value() || ledger->find(version) == nullptr) {
            return {};
        }
        return std::filesystem::path{ledger->find(version)->gguf_path};
    }

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
        write_file(student / "config.json",
                   R"({"architectures": ["LlamaForCausalLM"], "model_type": "llama"})");
        write_file(student / "model.safetensors", "w");
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
    CHECK(m.base_model == fixture.student.string());
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
    CHECK(err.find("no model 'nope'") != std::string::npos);
    CHECK(err.find("--safetensors") != std::string::npos);
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
    const std::filesystem::path org = hf / "org--repo" / "safetensors" / "bbbbbbbbbbbb";
    write_file(org / "config.json", "{}");
    write_file(org / "a.safetensors", "w");
    // A model's newest set, by name, by owner/repo, or by any path into it.
    CHECK(apogee::commands::resolve_student(config, models, hf, "tiny").path == fixture.student);
    CHECK(apogee::commands::resolve_student(config, models, hf, "org--repo").path == org);
    CHECK(apogee::commands::resolve_student(config, models, hf, "org/repo").path == org);
    CHECK(apogee::commands::resolve_student(config, models, hf, fixture.student.string()).path ==
          fixture.student);
    CHECK(apogee::commands::resolve_student(config, models, hf, "tiny/safetensors/aaaaaaaaaaaa")
              .path == fixture.student);
    CHECK_FALSE(apogee::commands::resolve_student(config, models, hf, "../tiny").error.empty());
    // The flat layout: refused with the migration named, not searched.
    write_file(models / "flat" / "config.json", "{}");
    write_file(models / "flat" / "a.safetensors", "w");
    CHECK(apogee::commands::resolve_student(config, models, hf, "flat")
              .error.find("apogee models migrate") != std::string::npos);
    CHECK(apogee::commands::resolve_student(config, models, hf, "judge")
              .error.find("backend entry") != std::string::npos);
    // A snapshot an older `models pull --safetensors` damaged: its config.json
    // is a download record. Refused by name, not failed on minutes into a run.
    write_file(hf / "damaged" / "safetensors" / "cccccccccccc" / "config.json",
               R"({"file": "config.json", "file_digest": "ab", "source_url": "https://x",
                   "verification": {"size_checked": true}})");
    write_file(hf / "damaged" / "safetensors" / "cccccccccccc" / "a.safetensors", "w");
    const apogee::commands::StudentResolution damaged =
        apogee::commands::resolve_student(config, models, hf, "damaged");
    CHECK(damaged.path.empty());
    CHECK(damaged.error.find("download record") != std::string::npos);

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
    CHECK(kit.items.size() == 24);
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
    const std::filesystem::path v1 = fixture.version_file("tuned", 1);
    CHECK(std::filesystem::exists(v1));
    // In the model store, beside the model it was trained from, under its
    // own id -- with the record every stored GGUF has.
    CHECK(v1.filename() == "tuned-v1.gguf");
    CHECK(v1.parent_path().parent_path() == fixture.home / "models" / "tiny" / "gguf");
    const std::optional<apogee::models::Sidecar> v1_record = apogee::models::load_sidecar(v1);
    REQUIRE(v1_record.has_value());
    CHECK(v1_record->source == "train");
    CHECK(v1_record->ref == "tuned v1");
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
    const std::filesystem::path v2 = fixture.version_file("tuned", 2);
    CHECK(out.find("updated backend tuned -> v2") != std::string::npos);
    CHECK(v2 != v1);  // another run's weights: its own id beside the first
    std::string expected = registered;
    REQUIRE(expected.find(scalar(v1)) != std::string::npos);
    expected.replace(expected.find(scalar(v1)), scalar(v1).size(), scalar(v2));
    CHECK(read_file(fixture.base.config_path) == expected);
    // Kept, the fine-tuned weights join the store too: a SafeTensors set of
    // the base model, recorded as a training run's, out of the run directory.
    const std::optional<apogee::training::VersionLedger> kept =
        apogee::training::TrainingStore{fixture.training}.list_versions("tuned");
    REQUIRE(kept.has_value());
    REQUIRE(kept->find(2) != nullptr);
    const std::filesystem::path fused{kept->find(2)->fused_path};
    CHECK(std::filesystem::exists(fused / "config.json"));
    CHECK(fused.parent_path() == fixture.home / "models" / "tiny" / "safetensors");
    CHECK(apogee::models::load_snapshot(fused)->source == "train");
    CHECK_FALSE(std::filesystem::exists(fixture.training / "runs" / id2 / "fused"));

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
    const std::optional<apogee::training::VersionLedger> retained =
        apogee::training::TrainingStore{failing.training}.list_versions("tuned");
    REQUIRE(retained.has_value());
    CHECK(retained->find(1)->pruned());
    // The same run promoted twice is the same weights -- one stored file,
    // which v2 still records, so pruning v1 did not remove it.
    CHECK(failing.version_file("tuned", 1) == failing.version_file("tuned", 2));
    CHECK(std::filesystem::exists(failing.version_file("tuned", 2)));
    // max + 1 after the prune: the next is v3, never v2 again.
    REQUIRE(failing.run({"train", "promote", id3, "--as", "tuned", "--force"}, &out, &err) == 0);
    CHECK(std::filesystem::exists(failing.version_file("tuned", 3)));
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
    const std::string second = fixture.trained(3);
    (void)fixture.evaluated(second);
    REQUIRE(fixture.run({"train", "promote", second, "--as", "tuned"}, &out, &err) == 0);
    const std::string at_v2 = read_file(fixture.base.config_path);
    REQUIRE(fixture.run({"train", "rollback", "tuned"}, &out, &err) == 0);
    CHECK(out.find("v2 -> v1") != std::string::npos);
    const std::filesystem::path v1 = fixture.version_file("tuned", 1);
    const std::filesystem::path v2 = fixture.version_file("tuned", 2);
    CHECK(v1 != v2);
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
    CHECK(out.find("Runs: 2 (0 running)") != std::string::npos);
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
    "train promote --target mlx registers the fused model as an mlx entry through the one "
    "editor -- versioned, rolled back, retained -- and no GGUF conversion runs",
    "[commands][train][promote][mlx]") {
    // 27c, the training shortcut: the fused SafeTensors run as they are.
    const RunFixture fixture;
    const std::string id = fixture.trained();
    (void)fixture.evaluated(id);
    std::string out;
    std::string err;
    const auto scalar = [](const std::filesystem::path& path) {
        return apogee::harness::yaml_scalar(path.string());
    };
    const auto ledger = [&fixture] {
        const std::optional<apogee::training::VersionLedger> found =
            apogee::training::TrainingStore{fixture.training}.list_versions("fast");
        REQUIRE(found.has_value());
        return *found;
    };

    // Refused before anything is built: a target nobody knows, and
    // --quantize, which makes a GGUF.
    CHECK(fixture.run({"train", "promote", id, "--as", "fast", "--target", "onnx"}, &out, &err) ==
          1);
    CHECK(err.find("--target must be gguf or mlx") != std::string::npos);
    CHECK(fixture.run(
              {"train", "promote", id, "--as", "fast", "--target", "mlx", "--quantize", "Q4_K_M"},
              &out, &err) == 1);
    CHECK(err.find("--quantize makes a quantized GGUF") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(fixture.training / "versions"));

    const std::string before = read_file(fixture.base.config_path);
    REQUIRE(fixture.run({"train", "promote", id, "--as", "fast", "--target", "mlx"}, &out, &err) ==
            0);
    INFO(out << err);
    CHECK(out.find("promoted to new backend fast -> v1") != std::string::npos);
    CHECK(out.find("no GGUF conversion ran") != std::string::npos);
    CHECK(err.find("no GGUF conversion: the fused model is registered as it is") !=
          std::string::npos);
    const apogee::training::VersionLedger first = ledger();
    REQUIRE(first.find(1) != nullptr);
    const std::filesystem::path v1{first.find(1)->mlx_path};
    CHECK(first.find(1)->gguf_path.empty());
    CHECK(first.find(1)->run_id == id);
    CHECK(first.find(1)->eval_passed == true);
    // In the model store's mlx/ row, beside the model it was trained from,
    // under the id its weights hash to -- read whole, with its record.
    CHECK(v1.parent_path() == fixture.home / "models" / "tiny" / "mlx");
    CHECK(apogee::models::read_mlx_info(v1).complete);
    const std::optional<apogee::models::Snapshot> record = apogee::models::load_snapshot(v1);
    REQUIRE(record.has_value());
    CHECK(record->source == "train");
    CHECK(record->ref == "fast v1");
    CHECK(record->transform == "promote run " + id);
    // No conversion: no GGUF anywhere for this model, and the run's fused
    // directory was never made (the fuse wrote into the store).
    CHECK_FALSE(std::filesystem::exists(fixture.home / "models" / "tiny" / "gguf"));
    CHECK_FALSE(std::filesystem::exists(fixture.training / "runs" / id / "fused"));
    // Registered through the one editor: exactly one entry appended.
    CHECK(read_file(fixture.base.config_path) ==
          before + "\n  fast:\n    type: mlx\n    model_path: " + scalar(v1) + "\n");

    // Into the existing entry, its type saying the target: only the path changes.
    const std::string id2 = fixture.trained(3);
    (void)fixture.evaluated(id2);
    CHECK(fixture.run({"train", "promote", id2, "--as", "fast", "--target", "gguf"}, &out, &err) ==
          1);
    CHECK(err.find("'fast' is a mlx backend, and --target gguf makes something else") !=
          std::string::npos);
    const std::string registered = read_file(fixture.base.config_path);
    REQUIRE(fixture.run({"train", "promote", id2, "--as", "fast"}, &out, &err) == 0);
    CHECK(out.find("updated backend fast -> v2") != std::string::npos);
    const std::filesystem::path v2{ledger().find(2)->mlx_path};
    CHECK(v2 != v1);
    std::string expected = registered;
    REQUIRE(expected.find(scalar(v1)) != std::string::npos);
    expected.replace(expected.find(scalar(v1)), scalar(v1).size(), scalar(v2));
    CHECK(read_file(fixture.base.config_path) == expected);

    // A llamacpp entry takes no MLX version, and a backend's versions never
    // mix: an entry deleted, its GGUF ledger still refuses an MLX promote.
    REQUIRE(fixture.run({"train", "promote", id, "--as", "tuned"}, &out, &err) == 0);
    CHECK(fixture.run({"train", "promote", id2, "--as", "tuned", "--target", "mlx"}, &out, &err) ==
          1);
    CHECK(err.find("'tuned' is a llamacpp backend") != std::string::npos);
    REQUIRE(fixture.run({"config", "delete-backend", "tuned"}, &out, &err) == 0);
    CHECK(fixture.run({"train", "promote", id2, "--as", "tuned", "--target", "mlx"}, &out, &err) ==
          1);
    CHECK(err.find("'tuned' holds GGUFs (active v1)") != std::string::npos);

    // An entry edited to another type since is never pointed at weights it
    // cannot run: refused, nothing repointed.
    const std::string typed = read_file(fixture.base.config_path);
    std::string edited = typed;
    const std::string mlx_type = "  fast:\n    type: mlx\n";
    REQUIRE(edited.find(mlx_type) != std::string::npos);
    edited.replace(edited.find(mlx_type), mlx_type.size(), "  fast:\n    type: llamacpp\n");
    write_file(fixture.base.config_path, edited);
    CHECK(fixture.run({"train", "rollback", "fast"}, &out, &err) == 1);
    CHECK(err.find("backend 'fast' is a llamacpp backend, and v1 runs on a mlx one") !=
          std::string::npos);
    CHECK(read_file(fixture.base.config_path) == edited);
    write_file(fixture.base.config_path, typed);

    // Rollback: repointed, nothing deleted.
    REQUIRE(fixture.run({"train", "rollback", "fast"}, &out, &err) == 0);
    CHECK(out.find("rolled back fast: v2 -> v1") != std::string::npos);
    CHECK(out.find("mlx:  " + v1.string()) != std::string::npos);
    CHECK(read_file(fixture.base.config_path) == registered);
    CHECK(std::filesystem::exists(v2));
    REQUIRE(fixture.run({"train", "versions", "fast"}, &out, &err) == 0);
    CHECK(out.find("WEIGHTS") != std::string::npos);
    CHECK(out.find("mlx/" + v1.filename().string() + "  <- active") != std::string::npos);

    // Retention: retain 1 keeps the active version alone, the others removed
    // whole from the store and kept in the ledger as history.
    write_file(fixture.base.config_path,
               read_file(fixture.base.config_path) + "training:\n  retain_versions: 1\n");
    const std::string id3 = fixture.trained(4);
    (void)fixture.evaluated(id3);
    REQUIRE(fixture.run({"train", "promote", id3, "--as", "fast"}, &out, &err) == 0);
    CHECK(out.find("updated backend fast -> v3") != std::string::npos);
    const apogee::training::VersionLedger retained = ledger();
    CHECK(retained.active_version == 3);
    CHECK(retained.find(1)->pruned());
    CHECK(retained.find(2)->pruned());
    CHECK_FALSE(std::filesystem::exists(v1));
    CHECK_FALSE(std::filesystem::exists(v2));
    const std::filesystem::path v3{retained.find(3)->mlx_path};
    CHECK(std::filesystem::exists(v3 / "config.json"));
    CHECK(fixture.run({"train", "rollback", "fast"}, &out, &err) == 1);
    CHECK(err.find("its MLX model is gone") != std::string::npos);

    // The doctor reads the MLX ledger as it reads a GGUF one.
    (void)fixture.run({"check", "--no-color"}, &out, &err);
    CHECK(out.find("active v3 of 3, and the backend points at it") != std::string::npos);
}

// ---------------------------------------------------------------------------
// The adapter promote (53b): `train promote --adapter`, the fuse skipped
// ---------------------------------------------------------------------------

namespace {

/// An entry that runs the run's base -- a stand-in GGUF in the store, read
/// only for its hash -- appended under `backends:`.
std::filesystem::path add_base_entry(const RunFixture& fixture, std::string_view name) {
    const std::filesystem::path base =
        fixture.home / "models" / "tiny" / "gguf" / "bbbbbbbbbbbb" / "tiny-F16.gguf";
    write_file(base, "GGUF the base weights");
    write_file(fixture.base.config_path, read_file(fixture.base.config_path) + "  " +
                                             std::string{name} +
                                             ":\n    type: llamacpp\n    model_path: " +
                                             apogee::harness::yaml_scalar(base.string()) + "\n");
    return base;
}

/// The ledger of `backend`, which must exist.
apogee::training::VersionLedger ledger_of(const RunFixture& fixture, std::string_view backend) {
    const std::optional<apogee::training::VersionLedger> found =
        apogee::training::TrainingStore{fixture.training}.list_versions(backend);
    REQUIRE(found.has_value());
    return *found;
}

}  // namespace

TEST_CASE(
    "train promote --adapter writes a small GGUF LoRA, registers it on the entry's adapters "
    "through the one editor with the base untouched and pinned, and repoints in place; "
    "rollback restores the pin, retention prunes kilobytes",
    "[commands][train][promote][adapter]") {
    const RunFixture fixture;
    const std::filesystem::path base = add_base_entry(fixture, "based");
    const std::string base_bytes = read_file(base);
    const std::string pin = apogee::models::sha256_hex(base_bytes);
    const std::string id = fixture.trained();
    std::string out;
    std::string err;

    // The gate runs first, whatever the mode: nothing is judged differently.
    CHECK(fixture.run({"train", "promote", id, "--as", "based", "--adapter"}, &out, &err) == 1);
    CHECK(err.find("no eval results") != std::string::npos);
    (void)fixture.evaluated(id);

    // Refused before anything is built: a name no entry has (an adapter goes
    // on an entry that runs its base), and the flags that act on a fuse.
    CHECK(fixture.run({"train", "promote", id, "--as", "nowhere", "--adapter"}, &out, &err) == 1);
    CHECK(err.find("'nowhere' is not configured") != std::string::npos);
    CHECK(err.find("apogee config add-backend nowhere --type llamacpp") != std::string::npos);
    CHECK(
        fixture.run({"train", "promote", id, "--as", "based", "--adapter", "--quantize", "Q4_K_M"},
                    &out, &err) == 1);
    CHECK(err.find("--quantize has nothing to act on") != std::string::npos);
    CHECK(fixture.run({"train", "promote", id, "--as", "based", "--adapter", "--keep-fused"}, &out,
                      &err) == 1);
    CHECK(err.find("--keep-fused has nothing to act on") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(fixture.training / "versions"));

    const std::string before = read_file(fixture.base.config_path);
    REQUIRE(fixture.run({"train", "promote", id, "--as", "based", "--adapter"}, &out, &err) == 0);
    INFO(out << err);
    const std::filesystem::path v1 = fixture.training / "versions" / "based" / "v1.lora.gguf";
    CHECK(out.find("updated backend based -> v1 (adapter)") != std::string::npos);
    CHECK(out.find("adapter: " + v1.string()) != std::string::npos);
    CHECK(out.find("pinned by sha256 " + pin.substr(0, 12)) != std::string::npos);
    CHECK(err.find("no fuse: the adapter is the version") != std::string::npos);
    CHECK(err.find("converting the adapter to a GGUF LoRA") != std::string::npos);
    // A small file, parsed as a GGUF adapter, and nothing fused anywhere.
    REQUIRE(std::filesystem::is_regular_file(v1));
    CHECK(std::filesystem::file_size(v1) < 1024);
    CHECK_FALSE(std::filesystem::exists(fixture.training / "runs" / id / "fused"));
    // The base untouched: the same bytes, still what model_path names.
    CHECK(read_file(base) == base_bytes);
    // Registered through the one editor, and nothing else changed: the
    // config is the editor's own output over the config before.
    const apogee::harness::AdapterConfig a1{.path = v1.string(), .base_sha256 = pin};
    CHECK(read_file(fixture.base.config_path) ==
          apogee::harness::set_backend_adapters(before, "based", {a1}));
    const apogee::harness::Config loaded = apogee::harness::load_config(fixture.base.config_path);
    REQUIRE(loaded.find_backend("based") != nullptr);
    CHECK(loaded.find_backend("based")->model_path == base.string());
    CHECK(loaded.find_backend("based")->adapters == std::vector{a1});
    // The ledger: the adapter and the base's hash, no GGUF of a model.
    const apogee::training::VersionLedger first = ledger_of(fixture, "based");
    CHECK(first.active_version == 1);
    REQUIRE(first.find(1) != nullptr);
    CHECK(first.find(1)->adapter_path == v1.string());
    CHECK(first.find(1)->base_sha256 == pin);
    CHECK(first.find(1)->gguf_path.empty());
    CHECK(first.find(1)->run_id == id);
    CHECK(first.find(1)->eval_passed == true);

    // A second version takes the first's place in the list, at its scale;
    // an adapter the ledger did not put there stays where it stands.
    const apogee::harness::AdapterConfig style{
        .path = (fixture.home / "style.gguf").string(), .scale = 0.5, .base_sha256 = pin};
    write_file(fixture.base.config_path,
               apogee::harness::set_backend_adapters(read_file(fixture.base.config_path), "based",
                                                     {a1, style}));
    const std::string id2 = fixture.trained(3);
    (void)fixture.evaluated(id2);
    const std::string at_v1 = read_file(fixture.base.config_path);
    REQUIRE(fixture.run({"train", "promote", id2, "--as", "based", "--adapter"}, &out, &err) == 0);
    const std::filesystem::path v2 = fixture.training / "versions" / "based" / "v2.lora.gguf";
    CHECK(out.find("updated backend based -> v2 (adapter)") != std::string::npos);
    CHECK(out.find("apogee train rollback based") != std::string::npos);
    const apogee::harness::AdapterConfig a2{.path = v2.string(), .base_sha256 = pin};
    CHECK(read_file(fixture.base.config_path) ==
          apogee::harness::set_backend_adapters(at_v1, "based", {a2, style}));
    CHECK(read_file(base) == base_bytes);

    // Rollback repoints the adapter, its recorded pin with it; deletes nothing.
    REQUIRE(fixture.run({"train", "rollback", "based"}, &out, &err) == 0);
    CHECK(out.find("rolled back based: v2 -> v1") != std::string::npos);
    CHECK(out.find("adapter: " + v1.string()) != std::string::npos);
    CHECK(read_file(fixture.base.config_path) == at_v1);
    CHECK(std::filesystem::exists(v2));
    CHECK(ledger_of(fixture, "based").active_version == 1);
    REQUIRE(fixture.run({"train", "versions", "based"}, &out, &err) == 0);
    CHECK(out.find("v1.lora.gguf  <- active") != std::string::npos);
    CHECK(out.find("v2.lora.gguf") != std::string::npos);

    // The doctor's ledger row reads an adapter version on the entry's adapters.
    (void)fixture.run({"check", "--no-color"}, &out, &err);
    CHECK(out.find("active v1 of 2, and the backend applies it") != std::string::npos);

    // A backend's versions never mix: a merged promote into it is refused.
    CHECK(fixture.run({"train", "promote", id2, "--as", "based"}, &out, &err) == 1);
    CHECK(err.find("'based' holds adapter versions (active v1)") != std::string::npos);
    // Nor an adapter onto an entry whose versions are merged fine-tunes.
    REQUIRE(fixture.run({"train", "promote", id, "--as", "tuned"}, &out, &err) == 0);
    CHECK(fixture.run({"train", "promote", id2, "--as", "tuned", "--adapter"}, &out, &err) == 1);
    CHECK(err.find("'tuned' holds merged versions (active v1)") != std::string::npos);

    // Retention counts adapter versions as any: retain 1 keeps the active
    // one alone, the others' files removed and their entries kept.
    write_file(fixture.base.config_path,
               read_file(fixture.base.config_path) + "training:\n  retain_versions: 1\n");
    REQUIRE(fixture.run({"train", "promote", id2, "--as", "based", "--adapter"}, &out, &err) == 0);
    CHECK(out.find("updated backend based -> v3 (adapter)") != std::string::npos);
    CHECK(out.find("pruned:") != std::string::npos);
    const apogee::training::VersionLedger retained = ledger_of(fixture, "based");
    CHECK(retained.active_version == 3);
    CHECK(retained.find(1)->pruned());
    CHECK(retained.find(2)->pruned());
    CHECK_FALSE(std::filesystem::exists(v1));
    CHECK_FALSE(std::filesystem::exists(v2));
    CHECK(std::filesystem::exists(fixture.training / "versions" / "based" / "v3.lora.gguf"));
    CHECK(read_file(base) == base_bytes);
    CHECK(fixture.run({"train", "rollback", "based"}, &out, &err) == 1);
    CHECK(err.find("its adapter is gone") != std::string::npos);
}

TEST_CASE(
    "train promote --adapter onto an mlx entry registers the run's adapter directory as it is, "
    "converting nothing; formats never cross, said by name",
    "[commands][train][promote][adapter][mlx]") {
    const RunFixture fixture;
    // An mlx entry over a stand-in MLX directory: a configuration and a shard.
    const std::filesystem::path base = fixture.home / "models" / "tiny" / "mlx" / "cccccccccccc";
    write_file(base / "config.json", R"({"model_type": "llama"})");
    write_file(base / "model.safetensors", "the base shard");
    write_file(fixture.base.config_path, read_file(fixture.base.config_path) +
                                             "  fast:\n    type: mlx\n    model_path: " +
                                             apogee::harness::yaml_scalar(base.string()) + "\n");
    (void)add_base_entry(fixture, "based");
    const std::string id = fixture.trained();
    (void)fixture.evaluated(id);
    std::string out;
    std::string err;

    const std::string before = read_file(fixture.base.config_path);
    REQUIRE(fixture.run({"train", "promote", id, "--as", "fast", "--adapter"}, &out, &err) == 0);
    INFO(out << err);
    const std::filesystem::path v1 = fixture.training / "versions" / "fast" / "v1.lora";
    CHECK(out.find("updated backend fast -> v1 (adapter)") != std::string::npos);
    CHECK(out.find("the mlx-lm directory as it is") != std::string::npos);
    CHECK(err.find("no conversion: the mlx-lm adapter directory is registered as it is") !=
          std::string::npos);
    CHECK(err.find("converting") == std::string::npos);
    CHECK(err.find("mock converter") == std::string::npos);
    // The run's directory, copied whole -- and the run's own copy kept.
    const std::filesystem::path run_adapter{fixture.manifest(id).adapter_dir};
    CHECK(read_file(v1 / "adapter_config.json") == read_file(run_adapter / "adapter_config.json"));
    CHECK(read_file(v1 / "adapters.safetensors") ==
          read_file(run_adapter / "adapters.safetensors"));
    CHECK(std::filesystem::exists(run_adapter / "adapters.safetensors"));
    // Pinned to the MLX base by the store's id rule, registered through the
    // one editor, the base's files untouched.
    const std::string pin = apogee::backends::weights_sha256(base);
    REQUIRE_FALSE(pin.empty());
    CHECK(read_file(fixture.base.config_path) ==
          apogee::harness::set_backend_adapters(
              before, "fast",
              {apogee::harness::AdapterConfig{.path = v1.string(), .base_sha256 = pin}}));
    CHECK(read_file(base / "model.safetensors") == "the base shard");
    const apogee::training::VersionLedger ledger = ledger_of(fixture, "fast");
    CHECK(ledger.find(1)->adapter_path == v1.string());
    CHECK(ledger.find(1)->base_sha256 == pin);
    CHECK(ledger.find(1)->mlx());

    // mlx-lm loads one adapter: another already on the entry is said.
    write_file(fixture.base.config_path,
               apogee::harness::set_backend_adapters(
                   read_file(fixture.base.config_path), "fast",
                   {apogee::harness::AdapterConfig{.path = (fixture.home / "other").string(),
                                                   .base_sha256 = pin}}));
    const std::string id2 = fixture.trained(3);
    (void)fixture.evaluated(id2);
    CHECK(fixture.run({"train", "promote", id2, "--as", "fast", "--adapter"}, &out, &err) == 1);
    CHECK(err.find("'fast' is an mlx entry, which applies one adapter") != std::string::npos);
    CHECK(err.find("apogee config set-backend fast --adapters") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(fixture.training / "versions" / "fast" / "v2.lora"));

    // Formats never cross: an mlx run's adapter onto a llamacpp entry is
    // refused naming both formats, and a peft run's onto an mlx entry.
    apogee::training::RunManifest mlx_run = fixture.manifest(id2);
    mlx_run.trainer = "mlx";
    REQUIRE(apogee::training::write_manifest(fixture.training / "runs" / id2, mlx_run).empty());
    const std::string unchanged = read_file(fixture.base.config_path);
    CHECK(fixture.run({"train", "promote", id2, "--as", "based", "--adapter"}, &out, &err) == 1);
    CHECK(err.find("an mlx-lm adapter (trainer mlx)") != std::string::npos);
    CHECK(err.find("a llamacpp entry applies a GGUF LoRA") != std::string::npos);
    CHECK(err.find("adapter formats never cross") != std::string::npos);
    apogee::training::RunManifest peft_run = mlx_run;
    peft_run.trainer = "peft";
    REQUIRE(apogee::training::write_manifest(fixture.training / "runs" / id2, peft_run).empty());
    CHECK(fixture.run({"train", "promote", id2, "--as", "fast", "--adapter"}, &out, &err) == 1);
    CHECK(err.find("a PEFT adapter (trainer peft)") != std::string::npos);
    CHECK(read_file(fixture.base.config_path) == unchanged);
    CHECK_FALSE(std::filesystem::exists(fixture.training / "versions" / "based"));
}

// ---------------------------------------------------------------------------
// The gate on the base that serves (53c): an adapter onto a quantized base is
// scored there, through the real command tree on the mock trainer.
// ---------------------------------------------------------------------------

namespace {

/// An entry over a Q4_K_M base -- a header that says so, in the store -- a
/// stand-in the mock's candidate runs over, appended under `backends:`.
std::filesystem::path add_q4_entry(const RunFixture& fixture, std::string_view name) {
    const std::filesystem::path base =
        fixture.home / "models" / "tiny" / "gguf" / "cccccccccccc" / "tiny-Q4_K_M.gguf";
    apogee::testing::GgufBuilder header;
    header.magic().u32(3).u64(1).u64(2);
    header.string_kv("general.architecture", "llama");
    header.u32_kv("general.file_type", 15);  // MOSTLY_Q4_K_M
    header.tensor("token_embd.weight");
    write_file(base, header.bytes());
    write_file(fixture.base.config_path, read_file(fixture.base.config_path) + "  " +
                                             std::string{name} +
                                             ":\n    type: llamacpp\n    model_path: " +
                                             apogee::harness::yaml_scalar(base.string()) + "\n");
    return base;
}

/// A complete mock run over `dataset`, its id.
std::string trained_on(const RunFixture& fixture, const std::string& dataset) {
    std::string err;
    REQUIRE(fixture.run(
                {"train", "run", "tiny", "--dataset", dataset, "--trainer", "mock", "--iters", "2"},
                nullptr, &err) == 0);
    return apogee::training::TrainingStore{fixture.training}.list_runs().front().id;
}

}  // namespace

TEST_CASE(
    "train promote --adapter onto a quantized base scores the candidate there, once, says so, "
    "and a candidate under the floor never registers",
    "[commands][train][promote][adapter][serving]") {
    const RunFixture fixture;
    (void)add_q4_entry(fixture, "q4");
    (void)add_base_entry(fixture, "based");
    const std::string suite = (fixture.home / "suite.jsonl").string();
    const std::string id = fixture.trained();
    std::string out;
    std::string err;

    // No suite anywhere -- the run never evaluated, none configured: refused
    // before anything is built, naming the flag.
    CHECK(fixture.run({"train", "promote", id, "--as", "q4", "--adapter"}, &out, &err) == 1);
    CHECK(err.find("no eval suite to score run " + id +
                   " on tiny-Q4_K_M.gguf (Q4_K_M), the base 'q4' serves -- pass --suite "
                   "<path|name>") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(fixture.training / "versions"));
    // --suite has nothing to act on where the run's own eval gates -- past
    // that gate, here by --force.
    CHECK(fixture.run(
              {"train", "promote", id, "--as", "based", "--adapter", "--suite", suite, "--force"},
              &out, &err) == 1);
    CHECK(err.find("--suite has nothing to act on: this promote is gated on run " + id +
                   "'s recorded eval") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(fixture.training / "versions"));

    // With a suite: no `train eval` first -- the one eval is the gate's, on
    // the base the version will serve on, and the report says where.
    const std::string before = read_file(fixture.base.config_path);
    REQUIRE(fixture.run({"train", "promote", id, "--as", "q4", "--adapter", "--suite", suite}, &out,
                        &err) == 0);
    INFO(out << err);
    CHECK(err.find("[promote] scoring the candidate where it will serve: tiny-Q4_K_M.gguf "
                   "(Q4_K_M), the base 'q4' serves -- suite " +
                   suite + " (2 item(s)); no judge: unmatched items skip") != std::string::npos);
    CHECK(err.find("[promote] scored on tiny-Q4_K_M.gguf (Q4_K_M): 100% (2/2)") !=
          std::string::npos);
    CHECK(out.find("  eval:    100% (2/2), scored on tiny-Q4_K_M.gguf (Q4_K_M)\n") !=
          std::string::npos);
    // The full-precision score was never computed: the run has no eval.
    CHECK_FALSE(fixture.manifest(id).eval.has_value());
    // The version records the gate that let it through, and where.
    const apogee::training::VersionLedger ledger = ledger_of(fixture, "q4");
    REQUIRE(ledger.find(1) != nullptr);
    CHECK(ledger.find(1)->eval_passed == true);
    CHECK(ledger.find(1)->eval_score == 1.0);
    CHECK(ledger.find(1)->eval_scored_on == "tiny-Q4_K_M.gguf (Q4_K_M)");
    CHECK(read_file(fixture.base.config_path) != before);

    // A candidate that degrades below the floor where it serves: refused,
    // saying where, the adapter it built removed, nothing registered.
    write_file(fixture.training / "datasets" / "degraded.jsonl",
               "{\"mock\": {\"answer\": \"something else entirely\"}}\n");
    const std::string degraded = trained_on(fixture, "degraded");
    const std::string at_v1 = read_file(fixture.base.config_path);
    CHECK(fixture.run({"train", "promote", degraded, "--as", "q4", "--adapter", "--suite", suite},
                      &out, &err) == 1);
    CHECK(err.find("the eval gate failed for run " + degraded +
                   ", scored on tiny-Q4_K_M.gguf (Q4_K_M), the base 'q4' serves (50%: 1/2 "
                   "passed)") != std::string::npos);
    CHECK(err.find("the adapter was removed, and the config and the version ledger are "
                   "unchanged") != std::string::npos);
    CHECK(read_file(fixture.base.config_path) == at_v1);
    CHECK_FALSE(std::filesystem::exists(fixture.training / "versions" / "q4" / "v2.lora.gguf"));
    CHECK(ledger_of(fixture, "q4").active_version == 1);
    CHECK(ledger_of(fixture, "q4").versions.size() == 1);

    // --force skips the gate, as it says: nothing scored, the version
    // carrying no eval, and said.
    REQUIRE(fixture.run({"train", "promote", degraded, "--as", "q4", "--adapter", "--force"}, &out,
                        &err) == 0);
    CHECK(err.find("promoting despite the gate (--force): run " + degraded +
                   " was not scored on tiny-Q4_K_M.gguf (Q4_K_M), the base 'q4' serves") !=
          std::string::npos);
    CHECK(err.find("[promote] scoring") == std::string::npos);
    const apogee::training::VersionLedger forced = ledger_of(fixture, "q4");
    REQUIRE(forced.find(2) != nullptr);
    CHECK_FALSE(forced.find(2)->eval_passed.has_value());
    CHECK(forced.find(2)->eval_scored_on.empty());
}

TEST_CASE(
    "the doctor's door for a promoted adapter whose base changed promotes its run again, and "
    "runs as printed",
    "[commands][train][check][adapters][doors]") {
    const RunFixture fixture;
    const std::filesystem::path based = add_base_entry(fixture, "based");
    const std::filesystem::path q4 = add_q4_entry(fixture, "q4");
    const std::string suite = (fixture.home / "suite.jsonl").string();
    const std::string id = fixture.trained();
    (void)fixture.evaluated(id);
    std::string out;
    std::string err;
    REQUIRE(fixture.run({"train", "promote", id, "--as", "based", "--adapter"}, &out, &err) == 0);
    const std::string unscored = fixture.trained(3);
    REQUIRE(fixture.run({"train", "promote", unscored, "--as", "q4", "--adapter", "--suite", suite},
                        &out, &err) == 0);

    // Both bases rewritten under their adapters.
    write_file(based, "GGUF other base weights");
    apogee::testing::GgufBuilder header;
    header.magic().u32(3).u64(1).u64(2);
    header.string_kv("general.architecture", "qwen2");
    header.u32_kv("general.file_type", 15);
    header.tensor("token_embd.weight");
    write_file(q4, header.bytes());

    const auto adapter_row = [&fixture](const std::string& name) {
        std::string json;
        std::string ignored;
        (void)fixture.run({"check", "--output-format", "json"}, &json, &ignored);
        const nlohmann::json document = nlohmann::json::parse(json);
        for (const nlohmann::json& row : document.at("rows")) {
            if (row.at("section") == "Adapters" && row.at("name") == name) {
                return row;
            }
        }
        return nlohmann::json{};
    };
    // Each door as a person types it after `apogee`: before ` to `, its
    // blank filled.
    const auto door_of = [](std::string remedy, const std::string& blank,
                            const std::string& filled) {
        remedy.resize(remedy.find(" to "));
        if (const std::size_t at = remedy.find(blank); at != std::string::npos) {
            remedy.replace(at, blank.size(), filled);
        }
        std::vector<std::string> words;
        std::istringstream in{remedy};
        for (std::string word; in >> word;) {
            words.push_back(word);
        }
        REQUIRE(words.front() == "apogee");
        words.erase(words.begin());
        return words;
    };

    // The run's own eval gates the full-precision base's: the door is the
    // promote alone.
    const nlohmann::json changed = adapter_row("based: v1.lora.gguf");
    REQUIRE(changed.is_object());
    CHECK(changed["status"] == "fail");
    CHECK(changed["detail"].get<std::string>().find("than the base " + based.string() +
                                                    " holds now") != std::string::npos);
    const std::string remedy = changed["remedy"];
    CHECK(remedy == "apogee train promote " + id +
                        " --as based --adapter to promote its run again over these weights");
    REQUIRE(fixture.run(door_of(remedy, {}, {}), &out, &err) == 0);
    CHECK(adapter_row("based: v2.lora.gguf")["status"] == "ok");

    // A quantized base's gate scores there, and nothing names its suite: the
    // door says to, and runs with the blank filled.
    const std::string q4_remedy = adapter_row("q4: v1.lora.gguf")["remedy"];
    CHECK(q4_remedy == "apogee train promote " + unscored +
                           " --as q4 --adapter --suite <path|name> to promote its run again "
                           "over these weights");
    REQUIRE(fixture.run(door_of(q4_remedy, "<path|name>", suite), &out, &err) == 0);
    CHECK(adapter_row("q4: v2.lora.gguf")["status"] == "ok");
}

TEST_CASE("train promote completes its --adapter flag",
          "[commands][train][promote][adapter][completion]") {
    // ADR 0007: a new flag is offered the moment it exists.
    const Fixture fixture;
    const std::ostringstream captured;
    std::streambuf* old_out = std::cout.rdbuf(captured.rdbuf());
    int code = -1;
    {
        apogee::commands::RootCommand root{apogee::commands::default_registry()};
        std::vector<const char*> argv{"apogee", "__complete", "train", "promote", "r1", "--ad"};
        code = root.run(static_cast<int>(argv.size()), argv.data());
    }
    std::cout.rdbuf(old_out);
    CHECK(code == 0);
    CHECK(captured.str().find("--adapter\n") != std::string::npos);
}

TEST_CASE("train promote completes its --suite and --judge flags",
          "[commands][train][promote][serving][completion]") {
    // ADR 0007: the gate's two flags (53c), offered the moment they exist --
    // and their values answered as train eval's are.
    const Fixture fixture;
    for (const auto& [typed, offered] :
         {std::pair<std::string, std::string>{"--su", "--suite\n"}, {"--ju", "--judge\n"}}) {
        const std::ostringstream captured;
        std::streambuf* old_out = std::cout.rdbuf(captured.rdbuf());
        int code = -1;
        {
            apogee::commands::RootCommand root{apogee::commands::default_registry()};
            std::vector<const char*> argv{"apogee",  "__complete", "train",
                                          "promote", "r1",         typed.c_str()};
            code = root.run(static_cast<int>(argv.size()), argv.data());
        }
        std::cout.rdbuf(old_out);
        CHECK(code == 0);
        CHECK(captured.str().find(offered) != std::string::npos);
    }
    const std::ostringstream captured;
    std::streambuf* old_out = std::cout.rdbuf(captured.rdbuf());
    {
        apogee::commands::RootCommand root{apogee::commands::default_registry()};
        std::vector<const char*> argv{"apogee", "__complete", "train", "promote",
                                      "r1",     "--judge",    ""};
        (void)root.run(static_cast<int>(argv.size()), argv.data());
    }
    std::cout.rdbuf(old_out);
    CHECK(captured.str().find("local") != std::string::npos);
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
    // Nothing half-built left in the store either.
    CHECK_FALSE(std::filesystem::exists(fixture.home / "models" / "tiny" / "gguf"));
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
    CHECK(std::filesystem::exists(fixture.base.version_file("staged", 1)));
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
    CHECK(std::filesystem::exists(fixture.base.version_file("tuned", 1)));
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
    "train regime list and show read the shipped foundation -- seeded, edited, under a "
    "training.regimes entry -- as text and as documents, and a named spec that does not parse "
    "is said",
    "[commands][train][regime][regimes]") {
    const PipelineFixture fixture;
    std::string out;
    std::string err;
    const std::filesystem::path spec = fixture.training / "regimes" / "foundation.yaml";
    REQUIRE(std::filesystem::exists(spec));  // seeded with the data directory
    const std::string description =
        "The four general kits as one gated pipeline -- instruction-following, structured "
        "output, summarization, then reasoning.";

    REQUIRE(fixture.run({"train", "regime", "list"}, &out, &err) == 0);
    CHECK(
        out.starts_with("NAME             SOURCE   KITS  DESCRIPTION\n"
                        "foundation       seeded   4     " +
                        description + "\n"));
    CHECK(out.find("\nharness-helpers  seeded   3     The harness chores") != std::string::npos);
    CHECK(out.find("\nknowledge        seeded   1     The knowledge-capture clerk") !=
          std::string::npos);
    REQUIRE(fixture.run({"train", "regime", "show", "foundation"}, &out, &err) == 0);
    CHECK(out == "foundation  seeded  " + spec.string() + "\n" + description +
                     "\n\nkits, in order:\n"
                     "  1. instruction-following\n"
                     "  2. structured-output\n"
                     "  3. summarization\n"
                     "  4. reasoning\n"
                     "count:       each kit's own synth.count\n"
                     "iters:       each kit's own train.iters\n"
                     "temperature: each kit's own\n"
                     "teacher:     yours: --teacher <backend>\n"
                     "student:     yours: --student <snapshot>\n");

    // The documents: the list idiom, and one regime whole.
    REQUIRE(fixture.run({"train", "regime", "list", "--output-format", "json"}, &out, &err) == 0);
    nlohmann::json list = nlohmann::json::parse(out);
    CHECK(list["object"] == "list");
    REQUIRE(list["data"].size() == 3);
    CHECK(list["data"][0]["name"] == "foundation");
    CHECK(list["data"][0]["source"] == "seeded");
    CHECK(list["data"][0]["path"] == spec.string());
    CHECK(list["data"][0]["kits"].size() == 4);
    CHECK(list["problems"].empty());
    REQUIRE(fixture.run({"train", "regime", "show", "foundation", "--output-format", "json"}, &out,
                        &err) == 0);
    const nlohmann::json shown = nlohmann::json::parse(out);
    CHECK(shown["object"] == "training.regime");
    CHECK(shown["description"] == description);
    CHECK(shown["kits"][3] == "reasoning");
    CHECK(shown["teacher"] == "");
    CHECK(shown["student"] == "");
    CHECK(fixture.run({"train", "regime", "show", "nope"}, &out, &err) == 1);
    CHECK(err.find("no regime named 'nope' (there are: foundation, harness-helpers, knowledge)") !=
          std::string::npos);
    CHECK(fixture.run({"train", "regime", "list", "--output-format", "stream-json"}, &out, &err) !=
          0);  // a read is text or a document

    // Without its file, the compiled-in text; edited, the user's; under a
    // config entry of its name, the entry -- and a file of the user's own.
    std::filesystem::remove(spec);
    REQUIRE(fixture.run({"train", "regime", "list"}, &out, &err) == 0);
    CHECK(out.find("foundation       shipped  4") != std::string::npos);
    write_file(spec, "description: mine now\nkits: [reasoning]\n");
    write_file(fixture.training / "regimes" / "quick.yaml", "kits: [alpha]\ncount: 2\n");
    REQUIRE(fixture.run({"train", "regime", "list"}, &out, &err) == 0);
    CHECK(out.find("foundation       user     1     mine now\n") != std::string::npos);
    CHECK(out.find("quick            user     1") != std::string::npos);
    write_file(fixture.base.base.config_path,
               read_file(fixture.base.base.config_path) +
                   "training:\n  regimes:\n    foundation:\n      kits: [alpha, beta]\n");
    REQUIRE(fixture.run({"train", "regime", "show", "foundation"}, &out, &err) == 0);
    CHECK(out.starts_with("foundation  user  (training.regimes entry)\n"));
    CHECK(out.find("  2. beta\n") != std::string::npos);

    // A spec of the user's that does not parse: listed as skipped, and a run
    // naming it is told why rather than that nothing is there.
    write_file(fixture.training / "regimes" / "quick.yaml", "kits: [unterminated\n");
    REQUIRE(fixture.run({"train", "regime", "list"}, &out, &err) == 0);
    CHECK(err.find("[regime] skipped: " + (fixture.training / "regimes" / "quick.yaml").string()) !=
          std::string::npos);
    CHECK(out.find("quick") == std::string::npos);
    CHECK(fixture.run(
              {"train", "regime", "run", "quick", "--teacher", "teacher", "--student", "tiny"},
              &out, &err) == 1);
    CHECK(err.find("regime 'quick': " + (fixture.training / "regimes" / "quick.yaml").string()) !=
          std::string::npos);
}

TEST_CASE(
    "train regime run foundation runs the four bundled kits as one gated pipeline on the mock "
    "trainer: the echoing student fails the first kit's evals under a hard gate, and a soft "
    "gate runs all four",
    "[commands][train][regime][regimes]") {
    std::string out;
    std::string err;
    {
        const PipelineFixture fixture{"training:\n  trainer: mock\n"};
        CHECK(fixture.run({"train", "regime", "run", "foundation", "--teacher", "teacher",
                           "--student", "tiny", "--count", "2", "--no-promote"},
                          &out, &err) == 2);  // the gate's failure, as a pipeline's
        CHECK(err.find("[regime] foundation as") != std::string::npos);
        CHECK(err.find("kits instruction-following -> structured-output -> summarization -> "
                       "reasoning") != std::string::npos);
        CHECK(err.find("stage 'instruction-following' failed the cumulative eval gate") !=
              std::string::npos);
        REQUIRE(fixture.pipelines().size() == 1);
        const apogee::training::PipelineRunManifest stopped =
            fixture.pipeline(fixture.pipelines().front().id);
        REQUIRE(stopped.stages.size() == 4);
        REQUIRE(stopped.stages[0].eval.has_value());
        CHECK(stopped.stages[0].eval->total == 24);  // the deepened floor, gating
        CHECK_FALSE(stopped.stages[0].cumulative_passed);
        CHECK_FALSE(stopped.stages[1].eval.has_value());
    }
    const PipelineFixture fixture{"training:\n  trainer: mock\n  gate_mode: soft\n"};
    REQUIRE(fixture.run({"train", "regime", "run", "foundation", "--teacher", "teacher",
                         "--student", "tiny", "--count", "2", "--no-promote"},
                        &out, &err) == 0);
    REQUIRE(fixture.pipelines().size() == 1);
    const apogee::training::PipelineRunManifest ran =
        fixture.pipeline(fixture.pipelines().front().id);
    REQUIRE(ran.stages.size() == 4);
    CHECK(ran.stages[0].name == "instruction-following");
    CHECK(ran.stages[3].name == "reasoning");
    REQUIRE(ran.stages[3].eval.has_value());
    CHECK(ran.stages[3].eval->total == 96);  // every kit's evals, cumulatively
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
    CHECK(after.find(
              "  nightly-model:\n    type: llamacpp\n    model_path: " +
              apogee::harness::yaml_scalar(fixture.base.version_file("nightly-model", 1).string()) +
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

// --- M19: the environment's Python, chosen and said ------------------------------------

#include <CLI/CLI.hpp>

#include <map>

#include "platform/platform.h"
#include "training/python_env.h"

namespace {

/// A machine for `train setup`: its own home, a PATH of fake interpreters
/// that say their versions, and a runner standing in for Python and pip --
/// every command recorded, a `venv` made as Python makes one.
struct SetupMachine {
    apogee::testing::TempDir home{"setup-home-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard apogee_home{"APOGEE_HOME", home.path().string()};
    apogee::testing::TempDir bin{"setup-path-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard path{"PATH", bin.path().string()};
    std::map<std::string, std::string> versions{
        {"python3", "3.14.5"}, {"python3.13", "3.13.13"}, {"python3.12", "3.12.13"}};
    std::vector<apogee::platform::ChildCommand> commands;

    SetupMachine() {
        for (const auto& [name, version] : versions) {
            std::ofstream{bin.path() / name} << "#!/bin/sh\n";
            std::filesystem::permissions(bin.path() / name, std::filesystem::perms::owner_all);
        }
    }

    [[nodiscard]] apogee::commands::SetupRequest request(
        std::vector<apogee::training::RequirementSet> with) {
        apogee::commands::SetupRequest out;
        out.with = std::move(with);
        out.host = apogee::training::HostShape{.apple_silicon = true, .nvidia_smi = false};
        out.runner = [this](const apogee::platform::ChildCommand& command,
                            const apogee::harness::CancellationToken&) {
            commands.push_back(command);
            apogee::training::CommandResult result;
            result.exit_code = 0;
            const std::string name = std::filesystem::path{command.program}.filename().string();
            if (!command.arguments.empty() && command.arguments[0] == "-c") {
                result.out = versions.at(name) + "\n";
            } else if (command.arguments.size() >= 3 && command.arguments[1] == "venv") {
                const std::filesystem::path venv{command.arguments[2]};
                std::filesystem::create_directories(venv / "bin");
                std::ofstream{venv / "bin" / "python"} << "#!fake";
                std::ofstream{venv / "pyvenv.cfg"} << "version = " << versions.at(name) << "\n";
            }
            return result;
        };
        return out;
    }

    /// Runs a setup; the exit code (0 for success) and every line said.
    int run(const apogee::harness::Config& config, const apogee::commands::SetupRequest& request,
            std::string* said) const {
        const std::ostringstream captured;
        std::streambuf* old_out = std::cout.rdbuf(captured.rdbuf());
        std::streambuf* old_err = std::cerr.rdbuf(captured.rdbuf());
        int code = 0;
        try {
            apogee::commands::run_train_setup(config, request);
        } catch (const CLI::RuntimeError& e) {
            code = e.get_exit_code();
        }
        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);
        *said = captured.str();
        return code;
    }

    [[nodiscard]] std::vector<std::string> pips() const {
        std::vector<std::string> out;
        for (const apogee::platform::ChildCommand& command : commands) {
            if (command.arguments.size() >= 5 && command.arguments[1] == "pip") {
                out.push_back(command.arguments[4]);
            }
        }
        return out;
    }
};

}  // namespace

TEST_CASE("train setup makes the environment on the newest Python every set runs on, and says why",
          "[commands][train][setup][m19]") {
    if (apogee::platform::host_target().starts_with("windows")) {
        SKIP("the scripted interpreters are POSIX files on a POSIX PATH");
    }
    SetupMachine machine;
    std::string said;
    REQUIRE(machine.run({}, machine.request({apogee::training::RequirementSet::Prepare}), &said) ==
            0);
    INFO(said);
    CHECK(said.find("from " + (machine.bin.path() / "python3.12").string() +
                    " (Python 3.12.13) -- the newest Python every set this host can install runs "
                    "on (>=3.11,<3.13); passed over python3 (3.14.5), python3.13 (3.13.13): the "
                    "'voice' set runs on >=3.10,<3.13") != std::string::npos);
    CHECK(said.find("(Python 3.12.13); sets: prepare") != std::string::npos);
    CHECK(machine.pips() == std::vector<std::string>{"datasets>=3.0"});
}

TEST_CASE("an environment on 3.14 refuses voice before pip; --rebuild asks, then reinstalls all",
          "[commands][train][setup][m19]") {
    if (apogee::platform::host_target().starts_with("windows")) {
        SKIP("the scripted interpreters are POSIX files on a POSIX PATH");
    }
    using apogee::training::RequirementSet;
    // The user's environment: made from python3 (3.14.5), five sets in it.
    SetupMachine machine;
    std::string said;
    apogee::commands::SetupRequest made =
        machine.request({RequirementSet::Prepare, RequirementSet::Mlx, RequirementSet::Convert,
                         RequirementSet::MlxVlm, RequirementSet::Video});
    made.python = "python3";
    REQUIRE(machine.run({}, made, &said) == 0);
    CHECK(said.find("(Python 3.14.5) -- as named") != std::string::npos);

    // Voice: refused before pip, the last line the rebuild that works.
    machine.commands.clear();
    CHECK(machine.run({}, machine.request({RequirementSet::Voice}), &said) != 0);
    CHECK(said.find("is on Python 3.14.5, and the 'voice' set runs on >=3.10,<3.13 -- rebuild it "
                    "on a Python that fits: apogee train setup --rebuild --with voice") !=
          std::string::npos);
    CHECK(machine.pips().empty());

    // --python names a new environment's Python; with one there, it asks for --rebuild.
    apogee::commands::SetupRequest named = machine.request({});
    named.python = "python3.12";
    CHECK(machine.run({}, named, &said) != 0);
    CHECK(said.find("add --rebuild to replace it") != std::string::npos);

    // Nobody to ask (the suite's stdin is not a terminal): refused, naming --yes.
    apogee::commands::SetupRequest rebuild = machine.request({RequirementSet::Voice});
    rebuild.rebuild = true;
    CHECK(machine.run({}, rebuild, &said) != 0);
    CHECK(said.find("rerun with --yes: apogee train setup --rebuild --yes") != std::string::npos);
    CHECK(machine.pips().empty());

    // With --yes: made again on 3.12, every recorded set reinstalled, then voice.
    rebuild.yes = true;
    REQUIRE(machine.run({}, rebuild, &said) == 0);
    INFO(said);
    CHECK(said.find("rebuilding") != std::string::npos);
    CHECK(said.find("(Python 3.12.13)") != std::string::npos);
    CHECK(said.find("reinstalling prepare, mlx, convert, mlx-vlm, video") != std::string::npos);
    CHECK(machine.pips() == std::vector<std::string>{"datasets>=3.0", "mlx-lm>=0.21", "torch>=2.11",
                                                     "mlx-vlm>=0.6.1", "torch>=2.6", "torch>=2.6"});
    CHECK(said.find("(Python 3.12.13); sets: prepare, mlx, convert, mlx-vlm, video, voice") !=
          std::string::npos);
}

TEST_CASE("train setup --with mlx-video says the commit pip builds it from, and installs it",
          "[commands][train][setup][mlx_video]") {
    if (apogee::platform::host_target().starts_with("windows")) {
        SKIP("the scripted interpreters are POSIX files on a POSIX PATH");
    }
    using apogee::training::RequirementSet;
    // 53k: mlx-video comes from one reviewed GitHub commit, never PyPI --
    // said as it installs, the requirement pip is handed the commit's.
    SetupMachine machine;
    std::string said;
    // No git on this PATH: refused before pip, naming what pip fetches it with.
    CHECK(machine.run({}, machine.request({RequirementSet::MlxVideo}), &said) != 0);
    CHECK(said.find("which pip fetches with git, and git is not on PATH") != std::string::npos);
    CHECK(machine.pips().empty());
    // With git: installed, and said.
    std::ofstream{machine.bin.path() / "git"} << "#!/bin/sh\n";
    std::filesystem::permissions(machine.bin.path() / "git", std::filesystem::perms::owner_all);
    REQUIRE(machine.run({}, machine.request({RequirementSet::MlxVideo}), &said) == 0);
    INFO(said);
    CHECK(said.find("installing the 'mlx-video' set: mlx-video @ "
                    "git+https://github.com/Blaizzy/mlx-video.git@"
                    "87db56a51758fefb748a359b90a5283bb8ba4837 torch>=2.6 ...\n  mlx-video is not "
                    "on PyPI: pip builds it with git from https://github.com/Blaizzy/mlx-video at "
                    "commit 87db56a51758 (2026-05-13), the one this Apogee reviewed on "
                    "2026-10-21\ninstalled\n") != std::string::npos);
    CHECK(machine.pips() ==
          std::vector<std::string>{"mlx-video @ git+https://github.com/Blaizzy/mlx-video.git@"
                                   "87db56a51758fefb748a359b90a5283bb8ba4837"});
    CHECK(said.find("sets: mlx-video") != std::string::npos);
}
