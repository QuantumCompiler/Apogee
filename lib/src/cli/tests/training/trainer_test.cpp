#include "training/trainer.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "models/gguf_inspect.h"
#include "support/env_guard.h"
#include "training/mock_trainer.h"

/// The trainer contract on the mock: exactly `iters` events carrying the
/// total and a falling loss, a token adapter written, cancellation
/// mid-stream, an error surfacing from `train`, capabilities, the fuse
/// copy, the echoing candidate with the base distinguishable, and the
/// minimal GGUF the mock promotes to parsing under the header reader. Plus
/// the two pure rules: which trainer `auto` picks on each host shape, and
/// what a student may be.
namespace {

using apogee::training::HostShape;
using apogee::training::MockTrainer;
using apogee::training::MockTrainerOptions;
using apogee::training::ProgressEvent;
using apogee::training::select_trainer;
using apogee::training::TrainerChoice;
using apogee::training::TrainOutcome;
using apogee::training::TrainRequest;
using apogee::training::validate_student;

void write(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << content;
}

std::filesystem::path snapshot(const std::filesystem::path& root) {
    write(root / "snap" / "config.json",
          R"({"architectures": ["QwenForCausalLM"], "model_type": "qwen2"})");
    write(root / "snap" / "model.safetensors", "weights");
    return root / "snap";
}

TrainRequest request(const std::filesystem::path& root, int iters = 0) {
    TrainRequest out;
    out.model_dir = snapshot(root);
    out.dataset = root / "data.jsonl";
    write(out.dataset, "{}\n");
    out.method = "lora";
    out.iters = iters;
    out.run_id = "20260919-120000";
    out.output_dir = root / "runs" / out.run_id;
    return out;
}

}  // namespace

TEST_CASE(
    "auto picks the trainer from the host shape, names are taken as given, and the "
    "rest is refused by name",
    "[training][trainer][select]") {
    CHECK(select_trainer("auto", HostShape{.apple_silicon = true}).name == "mlx");
    CHECK(select_trainer("", HostShape{.apple_silicon = true}).name == "mlx");
    CHECK(select_trainer("auto", HostShape{.nvidia_smi = true}).name == "peft");
    // Apple Silicon wins when both are true: the machine IS one.
    CHECK(select_trainer("auto", HostShape{.apple_silicon = true, .nvidia_smi = true}).name ==
          "mlx");
    const TrainerChoice none = select_trainer("auto", HostShape{});
    CHECK(none.name.empty());
    CHECK(none.error.find("mlx") != std::string::npos);
    CHECK(none.error.find("peft") != std::string::npos);
    CHECK(none.error.find("nvidia-smi") != std::string::npos);

    CHECK(select_trainer("peft", HostShape{.apple_silicon = true}).name == "peft");
    CHECK(select_trainer("mlx", HostShape{}).name == "mlx");
    CHECK(select_trainer("mock", HostShape{}).name == "mock");
    const TrainerChoice bogus = select_trainer("bogus", HostShape{});
    CHECK(bogus.name.empty());
    CHECK(bogus.error.find("bogus") != std::string::npos);
}

TEST_CASE(
    "a student is a snapshot directory; a GGUF and anything else are refused naming "
    "--safetensors",
    "[training][trainer][student]") {
    const apogee::testing::TempDir root{"trainer-student-" +
                                        std::to_string(std::random_device{}())};
    CHECK(validate_student(snapshot(root.path())).empty());

    const std::string gguf = validate_student(root.path() / "model.gguf");
    CHECK(gguf.find("GGUF") != std::string::npos);
    CHECK(gguf.find("--safetensors") != std::string::npos);

    CHECK(validate_student(root.path() / "nope").find("not a snapshot directory") !=
          std::string::npos);

    write(root.path() / "noconfig" / "model.safetensors", "w");
    CHECK(validate_student(root.path() / "noconfig").find("config.json") != std::string::npos);

    write(root.path() / "noshard" / "config.json", "{}");
    CHECK(validate_student(root.path() / "noshard").find("safetensors shard") != std::string::npos);
}

TEST_CASE(
    "the mock emits exactly iters events with the total and a falling loss, and writes "
    "a token adapter",
    "[training][trainer][mock]") {
    const apogee::testing::TempDir root{"trainer-mock-" + std::to_string(std::random_device{}())};
    MockTrainer trainer{MockTrainerOptions{.iters = 4, .final_loss = 0.5}};
    CHECK(trainer.name() == "mock");
    std::vector<ProgressEvent> iterations;
    std::vector<std::string> messages;
    const TrainOutcome outcome =
        trainer.train(request(root.path()),
                      [&](const ProgressEvent& event) {
                          if (event.kind == ProgressEvent::Kind::Iteration) {
                              iterations.push_back(event);
                          } else {
                              messages.push_back(event.text);
                          }
                      },
                      {});
    REQUIRE(outcome.ok);
    CHECK_FALSE(outcome.cancelled);
    REQUIRE(iterations.size() == 4);
    for (std::size_t i = 0; i < iterations.size(); ++i) {
        CHECK(iterations[i].iteration == static_cast<int>(i) + 1);
        CHECK(iterations[i].total_iters == 4);
        if (i > 0) {
            CHECK(iterations[i].loss < iterations[i - 1].loss);
        }
    }
    CHECK(outcome.final_loss == iterations.back().loss);
    CHECK(outcome.iterations == 4);
    CHECK(outcome.adapter_dir == root.path() / "runs" / "20260919-120000" / "adapters");
    CHECK(std::filesystem::exists(outcome.adapter_dir / "adapter_config.json"));
    CHECK(std::filesystem::exists(outcome.adapter_dir / "adapters.safetensors"));
    CHECK_FALSE(messages.empty());

    // The request's iteration count wins over the option.
    std::size_t count = 0;
    const TrainOutcome asked =
        trainer.train(request(root.path(), 2),
                      [&count](const ProgressEvent& event) {
                          count += event.kind == ProgressEvent::Kind::Iteration ? 1 : 0;
                      },
                      {});
    CHECK(asked.ok);
    CHECK(count == 2);

    const apogee::training::TrainerCapabilities capabilities = trainer.capabilities();
    CHECK(capabilities.methods == std::vector<std::string>{"lora", "qlora"});
    CHECK(capabilities.grad_checkpoint);
    CHECK(capabilities.mask_prompt);
}

TEST_CASE(
    "cancellation mid-stream ends the mock's run as cancelled with no adapter, and a "
    "trainer error surfaces from train",
    "[training][trainer][mock]") {
    const apogee::testing::TempDir root{"trainer-cancel-" + std::to_string(std::random_device{}())};
    MockTrainer slow{MockTrainerOptions{.iters = 50, .delay = std::chrono::milliseconds{5}}};
    const apogee::harness::CancellationToken token = apogee::harness::CancellationToken::create();
    int seen = 0;
    const TrainOutcome cancelled = slow.train(
        request(root.path()),
        [&](const ProgressEvent& event) {
            if (event.kind == ProgressEvent::Kind::Iteration && ++seen == 3) {
                token.cancel();
            }
        },
        token);
    CHECK_FALSE(cancelled.ok);
    CHECK(cancelled.cancelled);
    CHECK(cancelled.iterations == 3);
    CHECK_FALSE(std::filesystem::exists(cancelled.adapter_dir));

    MockTrainer failing{MockTrainerOptions{.iters = 3, .error = "GPU on fire"}};
    bool error_event = false;
    const TrainOutcome failed =
        failing.train(request(root.path()),
                      [&error_event](const ProgressEvent& event) {
                          error_event = error_event || event.kind == ProgressEvent::Kind::Error;
                      },
                      {});
    CHECK_FALSE(failed.ok);
    CHECK_FALSE(failed.cancelled);
    CHECK(failed.error == "GPU on fire");
    CHECK(error_event);
}

TEST_CASE(
    "the mock's fuse copies the base config beside a token shard, its candidate echoes "
    "and the untuned base is distinguishable, and its GGUF parses",
    "[training][trainer][mock]") {
    const apogee::testing::TempDir root{"trainer-fuse-" + std::to_string(std::random_device{}())};
    MockTrainer trainer;
    const std::filesystem::path base = snapshot(root.path());
    const std::filesystem::path fused = root.path() / "fused";
    std::vector<std::string> messages;
    CHECK(trainer
              .fuse(base, root.path() / "adapters", fused,
                    [&messages](std::string_view text) { messages.emplace_back(text); }, {})
              .empty());
    CHECK(std::filesystem::exists(fused / "config.json"));
    CHECK(std::filesystem::exists(fused / "model.safetensors"));
    CHECK_FALSE(messages.empty());
    MockTrainer refusing{MockTrainerOptions{.fuse_error = "no fuse today"}};
    CHECK(refusing.fuse(base, root.path() / "adapters", root.path() / "f2", {}, {}) ==
          "no fuse today");

    const auto tuned = trainer.candidate_runner(base, root.path() / "adapters");
    const auto untuned = trainer.candidate_runner(base, {});
    CHECK(tuned->run("say hello", {}).text == "say hello");
    CHECK(untuned->run("say hello", {}).text == "base: say hello");
    const apogee::harness::CancellationToken token = apogee::harness::CancellationToken::create();
    token.cancel();
    CHECK_FALSE(tuned->run("x", token).ok);

    const std::filesystem::path gguf = root.path() / "versions" / "v1.gguf";
    CHECK(apogee::training::write_mock_gguf(fused, gguf).empty());
    const apogee::models::GgufInfo info = apogee::models::inspect_gguf(gguf);
    REQUIRE(info.parsed);
    CHECK(info.architecture == "qwen2");
    CHECK(info.tensors == 1);
    CHECK(info.text_tensors == 1);
    CHECK_FALSE(info.is_quantized());
    CHECK_FALSE(apogee::training::write_mock_gguf(root.path() / "nowhere", gguf).empty());
}

TEST_CASE(
    "the mock is scripted by the dataset's first line: an error, an iteration count, and "
    "a fuse failure carried in the adapter",
    "[training][trainer][mock]") {
    const apogee::testing::TempDir root{"trainer-script-" + std::to_string(std::random_device{}())};
    MockTrainer trainer;
    TrainRequest failing = request(root.path());
    write(failing.dataset, "{\"mock\": {\"error\": \"GPU on fire\", \"iters\": 3}}\n{}\n");
    const TrainOutcome failed = trainer.train(failing, {}, {});
    CHECK_FALSE(failed.ok);
    CHECK(failed.error == "GPU on fire");

    TrainRequest counted = request(root.path());
    counted.run_id = "counted";
    counted.output_dir = root.path() / "runs" / "counted";
    write(counted.dataset, "{\"mock\": {\"iters\": 2, \"fuse_error\": \"cannot fuse\"}}\n");
    int events = 0;
    const TrainOutcome ok = trainer.train(counted,
                                          [&events](const ProgressEvent& e) {
                                              events +=
                                                  e.kind == ProgressEvent::Kind::Iteration ? 1 : 0;
                                          },
                                          {});
    REQUIRE(ok.ok);
    CHECK(events == 2);
    // The fuse failure travels with the adapter, so a fresh mock sees it.
    MockTrainer later;
    CHECK(later.fuse(snapshot(root.path()), ok.adapter_dir, root.path() / "fused", {}, {}) ==
          "cannot fuse");
    // An ordinary dataset scripts nothing.
    TrainRequest plain = request(root.path());
    plain.run_id = "plain";
    plain.output_dir = root.path() / "runs" / "plain";
    CHECK(trainer.train(plain, {}, {}).ok);
}
