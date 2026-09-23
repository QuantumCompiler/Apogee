#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "platform/child_process.h"
#include "platform/platform.h"
#include "support/env_guard.h"
#include "support/fake_child.h"
#include "training/mlx_trainer.h"
#include "training/peft_trainer.h"
#include "training/script_runner.h"
#include "training/script_trainer.h"
#include "training/trainer.h"

/// The JSONL progress protocol as a contract on BOTH sides.
///
/// The C++ side: `parse_progress_line` against the golden fixture, event by
/// event -- an `{"error"}` an Error, a non-JSON line a Message, an empty
/// line skipped -- and the script trainer over a scripted child carrying
/// the exit code and the stderr tail on the outcome, with the argv each
/// mode sends pinned. The Python side: the shipped `train_mlx.py` and
/// `train_peft.py` run under stub `mlx_lm` / `transformers` modules on the
/// host's bare python3 (skipped by name without one), proving each mode
/// emits the fixture's shapes; `train_peft.py`'s missing-deps error; on
/// Apple Silicon its hard-exit as the FIRST line; and the guard-before-import
/// rule on both, by grep.
namespace {

using apogee::testing::FakeChild;
using apogee::training::ProgressEvent;
using apogee::training::ScriptEvent;
using apogee::training::ScriptOutcome;
using apogee::training::ScriptRequest;
using apogee::training::ScriptTrainer;
using apogee::training::ScriptTrainerSpec;
using apogee::training::Spawner;
using apogee::training::TrainOutcome;
using apogee::training::TrainRequest;

const std::filesystem::path kFixture =
    std::filesystem::path{APOGEE_TESTS_DIR} / "training" / "fixtures" / "progress.jsonl";
const std::filesystem::path kStubs =
    std::filesystem::path{APOGEE_TESTS_DIR} / "training" / "scripts";
const std::filesystem::path kAssets = std::filesystem::path{APOGEE_ASSETS_DIR} / "training";

std::string read(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    REQUIRE(in.good());
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

void write(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << content;
}

std::vector<std::string> lines_of(std::string_view text) {
    std::vector<std::string> lines;
    std::string line;
    std::istringstream in{std::string{text}};
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    return lines;
}

struct Captured {
    std::vector<std::string> arguments;

    Spawner spawner(std::string stdout_script, int exit_status = 0,
                    std::string stderr_script = {}) {
        return [this, stdout_script, exit_status, stderr_script](
                   const apogee::platform::ChildCommand& command,
                   std::string&) -> std::unique_ptr<apogee::platform::ChildProcess> {
            arguments = command.arguments;
            auto child = std::make_unique<FakeChild>();
            child->stdout_script = stdout_script;
            child->stderr_script = stderr_script;
            child->exit_status = exit_status;
            child->chunk_size = 5;
            return child;
        };
    }
};

ScriptTrainerSpec spec(Spawner spawn) {
    ScriptTrainerSpec out;
    out.name = "mlx";
    out.interpreter = "/venv/bin/python";
    out.script = "/scripts/train_mlx.py";
    out.capabilities = apogee::training::mlx_capabilities();
    out.spawn = std::move(spawn);
    return out;
}

TrainRequest request() {
    TrainRequest out;
    out.model_dir = "/snap/tiny";
    out.dataset = "/data/set.jsonl";
    out.method = "qlora";
    out.iters = 3;
    out.batch_size = 2;
    out.num_layers = 8;
    out.grad_checkpoint = true;
    out.mask_prompt = true;
    out.run_id = "r1";
    out.output_dir = "/runs/r1";
    return out;
}

std::string python3() {
    return apogee::platform::find_on_path("python3");
}

/// Runs a shipped driver on the host's python3 with the stub package dir on
/// PYTHONPATH, collecting every event.
struct DriverRun {
    ScriptOutcome outcome;
    std::vector<ProgressEvent> events;
    nlohmann::json record;
};

DriverRun run_driver(const std::filesystem::path& script, std::vector<std::string> arguments,
                     const std::filesystem::path& stubs,
                     std::vector<std::pair<std::string, std::string>> environment = {}) {
    ScriptRequest request;
    request.interpreter = python3();
    request.script = script;
    request.arguments = std::move(arguments);
    request.environment = std::move(environment);
    request.environment.emplace_back("PYTHONPATH", stubs.string());
    request.environment.emplace_back("PYTHONDONTWRITEBYTECODE", "1");
    DriverRun run;
    run.outcome = apogee::training::run_script(
        request,
        [&run](const ScriptEvent& event) {
            if (event.kind == ScriptEvent::Kind::Record) {
                run.record = event.record;
                run.events.push_back(apogee::training::parse_progress_line(event.text));
            } else {
                ProgressEvent progress;
                progress.kind = event.kind == ScriptEvent::Kind::Error
                                    ? ProgressEvent::Kind::Error
                                    : ProgressEvent::Kind::Message;
                progress.text = event.text;
                run.events.push_back(progress);
            }
        },
        {});
    return run;
}

int iterations_in(const std::vector<ProgressEvent>& events) {
    int count = 0;
    for (const ProgressEvent& event : events) {
        count += event.kind == ProgressEvent::Kind::Iteration ? 1 : 0;
    }
    return count;
}

bool has_message(const std::vector<ProgressEvent>& events, std::string_view text) {
    for (const ProgressEvent& event : events) {
        if (event.kind == ProgressEvent::Kind::Message &&
            event.text.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE(
    "the golden fixture parses event by event: iterations, a message, a non-JSON line, "
    "a record, an error",
    "[training][trainer][protocol]") {
    const std::vector<std::string> lines = lines_of(read(kFixture));
    REQUIRE(lines.size() == 8);
    std::vector<ProgressEvent> events;
    for (const std::string& line : lines) {
        if (line.empty()) {
            continue;  // what the framer never delivers
        }
        events.push_back(apogee::training::parse_progress_line(line));
    }
    REQUIRE(events.size() == 7);
    CHECK(events[0].kind == ProgressEvent::Kind::Message);
    CHECK(events[0].text == "loading model from /snap/tiny");
    CHECK(events[1].kind == ProgressEvent::Kind::Iteration);
    CHECK(events[1].iteration == 1);
    CHECK(events[1].total_iters == 3);
    CHECK(events[1].loss == 2.5);
    CHECK(events[1].lr == 0.0001);
    CHECK(events[1].throughput == 4.2);
    CHECK(events[2].kind == ProgressEvent::Kind::Message);
    CHECK(events[2].text == "Traceback line that is not JSON at all");
    CHECK(events[3].kind == ProgressEvent::Kind::Iteration);
    CHECK(events[3].iteration == 2);
    CHECK(events[4].kind == ProgressEvent::Kind::Iteration);
    CHECK(events[4].iteration == 3);
    CHECK(events[4].lr == 0.00009);
    // A driver's terminal record is a message carrying the raw line.
    CHECK(events[5].kind == ProgressEvent::Kind::Message);
    CHECK(events[5].text.find("fused_dir") != std::string::npos);
    CHECK(events[6].kind == ProgressEvent::Kind::Error);
    CHECK(events[6].text == "the driver gave up");

    // An iteration with a non-numeric field is not an iteration.
    CHECK(apogee::training::parse_progress_line(R"({"iteration": "one"})").kind ==
          ProgressEvent::Kind::Message);
}

TEST_CASE(
    "the script trainer delivers the fixture's events, carries the final loss, and an "
    "error line makes the run not ok",
    "[training][trainer][protocol]") {
    Captured captured;
    ScriptTrainer trainer{spec(captured.spawner(read(kFixture), 1, "stderr tail here\n"))};
    std::vector<ProgressEvent> events;
    const TrainOutcome outcome =
        trainer.train(request(), [&events](const ProgressEvent& e) { events.push_back(e); }, {});
    CHECK_FALSE(outcome.ok);
    CHECK_FALSE(outcome.cancelled);
    CHECK(outcome.error.find("the driver gave up") != std::string::npos);
    CHECK(outcome.error.find("stderr tail here") != std::string::npos);
    CHECK(outcome.final_loss == 1.2);
    CHECK(outcome.iterations == 3);
    CHECK(iterations_in(events) == 3);
    CHECK(events.front().kind == ProgressEvent::Kind::Message);
    CHECK(events.back().kind == ProgressEvent::Kind::Error);
    CHECK(has_message(events, "Traceback line"));
    CHECK(outcome.adapter_dir == std::filesystem::path{"/runs/r1/adapters"});

    // The argv contract, as the driver's argparse must accept it.
    CHECK(captured.arguments ==
          std::vector<std::string>{
              "/scripts/train_mlx.py", "--mode", "train", "--model", "/snap/tiny", "--dataset",
              "/data/set.jsonl", "--method", "qlora", "--output-dir",
              // Spelled the way the trainer spells
              // them: a joined path, so Windows'
              // separator is expected, not a bug.
              (std::filesystem::path{"/runs/r1"} / "adapters").string(), "--data-dir",
              (std::filesystem::path{"/runs/r1"} / "data").string(), "--iters", "3", "--batch-size",
              "2", "--num-layers", "8", "--grad-checkpoint", "--mask-prompt"});
}

TEST_CASE(
    "a clean exit is ok, a bad exit with no error line is a failed run named with its "
    "code, and cancellation is reported",
    "[training][trainer][protocol]") {
    Captured captured;
    const std::string clean =
        R"({"iteration": 1, "total_iters": 1, "loss": 0.7, "lr": 0.001, "throughput": 1.0})"
        "\n";
    ScriptTrainer ok{spec(captured.spawner(clean, 0))};
    const TrainOutcome fine = ok.train(request(), {}, {});
    CHECK(fine.ok);
    CHECK(fine.final_loss == 0.7);

    // The reference discarded the exit status, so a crashed trainer read as
    // a complete run. Here the code is the outcome.
    ScriptTrainer crashed{spec(captured.spawner(clean, 137, "Killed\n"))};
    const TrainOutcome dead = crashed.train(request(), {}, {});
    CHECK_FALSE(dead.ok);
    CHECK(dead.error.find("137") != std::string::npos);
    CHECK(dead.error.find("Killed") != std::string::npos);

    const apogee::harness::CancellationToken token = apogee::harness::CancellationToken::create();
    token.cancel();
    ScriptTrainer stopped{spec(captured.spawner(clean, 0))};
    const TrainOutcome cancelled = stopped.train(request(), {}, token);
    CHECK_FALSE(cancelled.ok);
    CHECK(cancelled.cancelled);
}

TEST_CASE(
    "fuse and infer send their argv, infer reads the text record, and a driver that "
    "prints no record is an error",
    "[training][trainer][protocol]") {
    Captured captured;
    ScriptTrainer fusing{
        spec(captured.spawner("{\"message\": \"merging\"}\n{\"fused_dir\": \"/f\"}\n"))};
    std::vector<std::string> messages;
    CHECK(fusing
              .fuse("/snap/tiny", "/runs/r1/adapters", "/runs/r1/fused",
                    [&messages](std::string_view text) { messages.emplace_back(text); }, {})
              .empty());
    CHECK(messages == std::vector<std::string>{"merging"});
    CHECK(captured.arguments == std::vector<std::string>{"/scripts/train_mlx.py", "--mode", "fuse",
                                                         "--model", "/snap/tiny", "--adapter-path",
                                                         "/runs/r1/adapters", "--output-dir",
                                                         "/runs/r1/fused"});
    ScriptTrainer fuse_failed{spec(captured.spawner("{\"error\": \"OOM\"}\n", 1))};
    CHECK(fuse_failed.fuse("/b", "/a", "/o", {}, {}).find("OOM") != std::string::npos);

    ScriptTrainer answering{spec(captured.spawner("{\"text\": \"Paris\"}\n"))};
    const auto runner = answering.candidate_runner("/snap/tiny", "/runs/r1/adapters");
    const apogee::training::CandidateReply reply = runner->run("capital of France?", {});
    CHECK(reply.ok);
    CHECK(reply.text == "Paris");
    CHECK(captured.arguments ==
          std::vector<std::string>{"/scripts/train_mlx.py", "--mode", "infer", "--model",
                                   "/snap/tiny", "--adapter-path", "/runs/r1/adapters", "--prompt",
                                   "capital of France?", "--max-tokens", "256"});
    // The untuned base: an empty adapter path, sent as such.
    const auto base = answering.candidate_runner("/snap/tiny", {});
    (void)base->run("x", {});
    CHECK(captured.arguments[6] == "");

    ScriptTrainer silent{spec(captured.spawner("{\"message\": \"thinking\"}\n"))};
    const apogee::training::CandidateReply none = silent.candidate_runner("/b", "/a")->run("q", {});
    CHECK_FALSE(none.ok);
    CHECK(none.error.find("no {\"text\"} record") != std::string::npos);

    // Every driver run refuses to leave bytecode in the seeded tree.
    const ScriptRequest formed = apogee::training::script_request(spec({}), {"--mode", "train"});
    CHECK(formed.interpreter == std::filesystem::path{"/venv/bin/python"});
    CHECK(formed.environment ==
          std::vector<std::pair<std::string, std::string>>{{"PYTHONDONTWRITEBYTECODE", "1"}});
}

TEST_CASE(
    "the environment guards precede the first ML import in both drivers, and the "
    "Apple-Silicon guard precedes every import in train_peft.py",
    "[training][trainer][guards]") {
    for (const char* name : {"train_mlx.py", "train_peft.py"}) {
        INFO(name);
        const std::string text = read(kAssets / name);
        const std::size_t offline = text.find("os.environ[\"HF_HUB_OFFLINE\"] = \"1\"");
        const std::size_t tokenizers =
            text.find("os.environ.setdefault(\"TOKENIZERS_PARALLELISM\"");
        const std::size_t kmp = text.find("os.environ.setdefault(\"KMP_DUPLICATE_LIB_OK\"");
        std::size_t ml_import = std::string::npos;
        for (const char* module :
             {"import mlx_lm", "from mlx_lm", "import torch", "from transformers", "from peft",
              "import bitsandbytes", "from transformers import"}) {
            const std::size_t at = text.find(module);
            if (at != std::string::npos) {
                ml_import = std::min(ml_import, at);
            }
        }
        REQUIRE(offline != std::string::npos);
        REQUIRE(tokenizers != std::string::npos);
        REQUIRE(kmp != std::string::npos);
        REQUIRE(ml_import != std::string::npos);
        CHECK(offline < ml_import);
        CHECK(tokenizers < ml_import);
        CHECK(kmp < ml_import);
        CHECK(text.find("os.environ[\"TRANSFORMERS_OFFLINE\"] = \"1\"") < ml_import);
        CHECK(text.find("os.environ[\"HF_DATASETS_OFFLINE\"] = \"1\"") < ml_import);
        CHECK(text.find("os.environ[\"HF_HUB_DISABLE_TELEMETRY\"] = \"1\"") < ml_import);
    }
    // bitsandbytes aborts inside C-extension init on Apple Silicon: the
    // guard must run before any import that could pull it in -- before
    // argparse, even.
    const std::string peft = read(kAssets / "train_peft.py");
    const std::size_t guard = peft.find("platform.machine() == \"arm64\"");
    REQUIRE(guard != std::string::npos);
    CHECK(guard < peft.find("import argparse"));
    CHECK(guard < peft.find("import torch"));
    CHECK(peft.find("APOGEE_TRAINING_STUB_MODULES") < peft.find("import argparse"));
}

TEST_CASE(
    "train_mlx.py emits the protocol under the stub mlx_lm: train lays out the data "
    "directory and forwards the flags, fuse records, infer answers",
    "[training][trainer][python]") {
    // Both halves matter. python3 can be on PATH -- it is on the Windows
    // runner images -- while the platform still cannot start it: child_process
    // has no Windows implementation, so the spawn fails with "this build
    // cannot spawn child processes". Checking only the interpreter let these
    // three cases run and fail there (2026-09-22). supports_child_processes()
    // is the same guard tools/shell_test.cpp uses.
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    if (python3().empty()) {
        SKIP("no python3 on PATH");
    }
    const apogee::testing::TempDir root{"mlx-driver-" + std::to_string(std::random_device{}())};
    const std::filesystem::path stubs = kStubs / "stub_mlx_lm";
    const std::filesystem::path record = root.path() / "record.txt";
    const std::filesystem::path dataset = root.path() / "set.jsonl";
    write(dataset, "{\"messages\": []}\n{\"messages\": []}\n{\"messages\": []}\n\n");
    // What mlx_lm.lora prints: the newer shape without /total, a Val line,
    // a warning.
    write(root.path() / "lora.txt",
          "Loading pretrained model\n"
          "Iter 1: Val loss 2.900, Val took 0.4s\n"
          "Iter 5: Train loss 2.345, Learning Rate 1.000e-05, It/sec 2.340, Tokens/sec 100\n"
          "Iter 10/10: Train loss 1.234, Learning Rate 9.000e-06, It/sec 2.5\n"
          "Saved adapter weights\n");
    const std::filesystem::path run_dir = root.path() / "runs" / "r1";
    TrainRequest req;
    req.model_dir = root.path() / "snap";
    req.dataset = dataset;
    req.method = "lora";
    req.iters = 10;
    req.mask_prompt = true;
    req.grad_checkpoint = true;
    req.output_dir = run_dir;
    const DriverRun train =
        run_driver(kAssets / "train_mlx.py", apogee::training::train_arguments(req), stubs,
                   {{"STUB_LORA_OUTPUT", (root.path() / "lora.txt").string()},
                    {"STUB_RECORD", record.string()}});
    INFO(train.outcome.describe());
    REQUIRE(train.outcome.ok);
    CHECK(iterations_in(train.events) == 2);
    const ProgressEvent* first = nullptr;
    for (const ProgressEvent& event : train.events) {
        if (event.kind == ProgressEvent::Kind::Iteration) {
            first = &event;
            break;
        }
    }
    REQUIRE(first != nullptr);
    CHECK(first->iteration == 5);
    CHECK(first->total_iters == 10);  // from --iters, since the line had no /total
    CHECK(first->loss == 2.345);
    CHECK(first->lr == 1e-5);
    CHECK(first->throughput == 2.34);
    CHECK(has_message(train.events, "Val loss"));
    CHECK(has_message(train.events, "3 training example(s)"));
    // mlx_lm.lora wants a DIRECTORY of {train,valid}.jsonl, laid out from
    // the dataset: every line trains, validation is a copy of the first
    // tenth (at least one).
    CHECK(read(run_dir / "data" / "train.jsonl") ==
          "{\"messages\": []}\n{\"messages\": []}\n{\"messages\": []}\n");
    CHECK(read(run_dir / "data" / "valid.jsonl") == "{\"messages\": []}\n");
    const std::string recorded = read(record);
    CHECK(recorded.find("lora: --model " + (root.path() / "snap").string() + " --train --data " +
                        (run_dir / "data").string() + " --adapter-path " +
                        (run_dir / "adapters").string() + " --iters 10") != std::string::npos);
    CHECK(recorded.find("--grad-checkpoint") != std::string::npos);
    CHECK(recorded.find("--mask-prompt") != std::string::npos);

    // A failing mlx_lm.lora is an error line and a non-zero exit.
    const DriverRun failed =
        run_driver(kAssets / "train_mlx.py", apogee::training::train_arguments(req), stubs,
                   {{"STUB_LORA_OUTPUT", (root.path() / "lora.txt").string()}, {"STUB_EXIT", "3"}});
    CHECK_FALSE(failed.outcome.ok);
    CHECK(failed.outcome.exit_code == 3);
    CHECK(failed.outcome.error.find("exited with code 3") != std::string::npos);

    // An empty dataset is refused before anything runs.
    write(root.path() / "empty.jsonl", "\n");
    TrainRequest empty = req;
    empty.dataset = root.path() / "empty.jsonl";
    const DriverRun refused =
        run_driver(kAssets / "train_mlx.py", apogee::training::train_arguments(empty), stubs);
    CHECK_FALSE(refused.outcome.ok);
    CHECK(refused.outcome.error.find("empty") != std::string::npos);

    // fuse: mlx_lm.fuse's argv, its output as messages, the terminal record.
    const std::filesystem::path fused = run_dir / "fused";
    const DriverRun fuse = run_driver(
        kAssets / "train_mlx.py",
        apogee::training::fuse_arguments(root.path() / "snap", run_dir / "adapters", fused), stubs,
        {{"STUB_RECORD", record.string()}});
    INFO(fuse.outcome.describe());
    REQUIRE(fuse.outcome.ok);
    CHECK(fuse.record["fused_dir"] == fused.string());
    CHECK(std::filesystem::exists(fused / "model.safetensors"));
    CHECK(has_message(fuse.events, "Fusing... done"));

    // infer: the API with the adapter, the chat template applied when the
    // tokenizer has one, the answer as the text record.
    const DriverRun infer = run_driver(
        kAssets / "train_mlx.py",
        apogee::training::infer_arguments(root.path() / "snap", run_dir / "adapters", "hello", 32),
        stubs, {{"STUB_RECORD", record.string()}, {"STUB_CHAT_TEMPLATE", "1"}});
    INFO(infer.outcome.describe());
    REQUIRE(infer.outcome.ok);
    CHECK(infer.record["text"] == "stub answer to [<user>hello<assistant>] max=32");
    CHECK(read(record).find("load: " + (root.path() / "snap").string() +
                            " adapter=" + (run_dir / "adapters").string()) != std::string::npos);
    const DriverRun plain =
        run_driver(kAssets / "train_mlx.py",
                   apogee::training::infer_arguments(root.path() / "snap", {}, "hello", 8), stubs,
                   {{"STUB_RECORD", record.string()}});
    REQUIRE(plain.outcome.ok);
    CHECK(plain.record["text"] == "stub answer to [hello] max=8");
    CHECK(read(record).find("adapter=None") != std::string::npos);

    // Without mlx_lm importable: the install hint, not a stack trace. The
    // stub_missing package raises ImportError whatever the host has.
    const DriverRun missing = run_driver(
        kAssets / "train_mlx.py", apogee::training::train_arguments(req), kStubs / "stub_missing");
    CHECK_FALSE(missing.outcome.ok);
    CHECK(missing.outcome.error.find("apogee train setup --trainer mlx") != std::string::npos);
}

TEST_CASE(
    "train_peft.py emits the protocol under the stub transformers: the exact prompt "
    "mask, the flags, fuse and infer; its hard-exit on Apple Silicon is the first line",
    "[training][trainer][python]") {
    // Both halves matter. python3 can be on PATH -- it is on the Windows
    // runner images -- while the platform still cannot start it: child_process
    // has no Windows implementation, so the spawn fails with "this build
    // cannot spawn child processes". Checking only the interpreter let these
    // three cases run and fail there (2026-09-22). supports_child_processes()
    // is the same guard tools/shell_test.cpp uses.
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    if (python3().empty()) {
        SKIP("no python3 on PATH");
    }
    const apogee::testing::TempDir root{"peft-driver-" + std::to_string(std::random_device{}())};
    const std::filesystem::path stubs = kStubs / "stub_transformers";
    const std::filesystem::path record = root.path() / "record.txt";
    const std::filesystem::path dataset = root.path() / "set.jsonl";
    write(dataset,
          "{\"messages\": [{\"role\": \"user\", \"content\": \"two words\"}, "
          "{\"role\": \"assistant\", \"content\": \"three more words\"}]}\n"
          "{\"prompt\": \"a b\", \"completion\": \" c\"}\n"
          "{\"nothing\": 1}\n");
    const std::filesystem::path run_dir = root.path() / "runs" / "r1";
    TrainRequest req;
    req.model_dir = root.path() / "snap";
    req.dataset = dataset;
    req.method = "qlora";
    req.iters = 3;
    req.batch_size = 1;
    req.mask_prompt = true;
    req.grad_checkpoint = true;
    req.output_dir = run_dir;
    const std::vector<std::pair<std::string, std::string>> stub_env{
        {"APOGEE_TRAINING_STUB_MODULES", "1"},
        {"STUB_RECORD", record.string()},
        {"STUB_CHAT_TEMPLATE", "1"}};

#if defined(__APPLE__) && defined(__aarch64__)
    // The guard, verbatim: the first line out is the error, before any
    // import could abort the interpreter.
    const DriverRun guarded =
        run_driver(kAssets / "train_peft.py", apogee::training::train_arguments(req), stubs);
    CHECK_FALSE(guarded.outcome.ok);
    CHECK(guarded.outcome.exit_code == 1);
    REQUIRE_FALSE(guarded.events.empty());
    CHECK(guarded.events.front().kind == ProgressEvent::Kind::Error);
    CHECK(guarded.events.front().text.find("Apple Silicon") != std::string::npos);
    CHECK(guarded.events.front().text.find("--trainer mlx") != std::string::npos);
    CHECK(guarded.events.size() == 1);
#endif

    const DriverRun train = run_driver(kAssets / "train_peft.py",
                                       apogee::training::train_arguments(req), stubs, stub_env);
    INFO(train.outcome.describe());
    REQUIRE(train.outcome.ok);
    CHECK(iterations_in(train.events) == 3);
    const ProgressEvent* last = nullptr;
    for (const ProgressEvent& event : train.events) {
        if (event.kind == ProgressEvent::Kind::Iteration) {
            last = &event;
        }
    }
    REQUIRE(last != nullptr);
    CHECK(last->iteration == 3);
    CHECK(last->total_iters == 3);
    CHECK(last->loss == 1.5);
    CHECK(last->lr == 2e-4);
    CHECK(last->throughput == 1.5);
    CHECK(has_message(train.events, "4-bit (QLoRA)"));
    CHECK(has_message(train.events, "1 line(s) skipped"));
    CHECK(has_message(train.events, "2 example(s) with the prompt masked"));
    CHECK(std::filesystem::exists(run_dir / "adapters" / "model.safetensors"));
    CHECK(std::filesystem::exists(run_dir / "adapters" / "tokenizer.json"));
    const std::string recorded = read(record);
    CHECK(recorded.find("quantized=True") != std::string::npos);
    CHECK(recorded.find("lora: r=16 alpha=32") != std::string::npos);
    CHECK(recorded.find("gradient_checkpointing") != std::string::npos);
    // The mask is exact: the prompt is the template with the generation
    // prompt appended -- "<user> two words <assistant>" is four words, the
    // full text "<user> two words <assistant> three more words" seven, so
    // the first four labels are -100 and the last three are the tokens.
    CHECK(recorded.find("\"labels\": [[-100, -100, -100, -100, 5, 4, 5]]") != std::string::npos);

    // Without --mask-prompt the labels are the tokens.
    TrainRequest unmasked = req;
    unmasked.mask_prompt = false;
    unmasked.output_dir = root.path() / "runs" / "r2";
    std::filesystem::remove(record);
    const DriverRun plain = run_driver(
        kAssets / "train_peft.py", apogee::training::train_arguments(unmasked), stubs, stub_env);
    REQUIRE(plain.outcome.ok);
    CHECK(read(record).find("\"labels\": [[6, 3, 5, 11, 5, 4, 5]]") != std::string::npos);

    // fuse: merge_and_unload, the fused tree, the record.
    const std::filesystem::path fused = run_dir / "fused";
    const DriverRun fuse = run_driver(
        kAssets / "train_peft.py",
        apogee::training::fuse_arguments(root.path() / "snap", run_dir / "adapters", fused), stubs,
        stub_env);
    INFO(fuse.outcome.describe());
    REQUIRE(fuse.outcome.ok);
    CHECK(fuse.record["fused_dir"] == fused.string());
    CHECK(std::filesystem::exists(fused / "model.safetensors"));
    CHECK(has_message(fuse.events, "merge_and_unload"));
    CHECK(read(record).find("adapter: " + (run_dir / "adapters").string()) != std::string::npos);

    // infer: the template applied, the new tokens decoded, the record.
    const DriverRun infer =
        run_driver(kAssets / "train_peft.py",
                   apogee::training::infer_arguments(root.path() / "snap", run_dir / "adapters",
                                                     "hi there", 16),
                   stubs, stub_env);
    INFO(infer.outcome.describe());
    REQUIRE(infer.outcome.ok);
    CHECK(infer.record["text"] == "stub decoded 7 8 9");

    // Missing packages: the install hint naming the command, exit 1.
    const DriverRun missing =
        run_driver(kAssets / "train_peft.py", apogee::training::train_arguments(req),
                   kStubs / "stub_missing", {{"APOGEE_TRAINING_STUB_MODULES", "1"}});
    CHECK_FALSE(missing.outcome.ok);
    CHECK(missing.outcome.exit_code == 1);
    CHECK(missing.outcome.error.find("missing Python packages") != std::string::npos);
    CHECK(missing.outcome.error.find("apogee train setup --trainer peft") != std::string::npos);
}
