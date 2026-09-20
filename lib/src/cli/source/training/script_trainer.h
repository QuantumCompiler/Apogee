#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "harness/cancellation.h"
#include "training/script_runner.h"
#include "training/trainer.h"

/// A trainer that is a shipped Python driver under the environment's
/// interpreter. `train_mlx.py` and `train_peft.py` share ONE argv contract
/// -- `--mode train|fuse|infer` with the same flags -- so one class drives
/// both; `mlx_trainer.h` and `peft_trainer.h` are the two specs.
///
/// Every run goes through `script_runner`: stdout framed by the one framer,
/// each line classified into the progress protocol, the exit code carried,
/// stderr a bounded tail folded into the failure, cancellation terminating
/// the child. The driver never writes bytecode into the seeded tree
/// (`PYTHONDONTWRITEBYTECODE`), so `apogee check`'s drift row stays honest
/// after a run.
namespace apogee::training {

/// The token budget for one eval answer.
inline constexpr int kCandidateMaxTokens = 256;

struct ScriptTrainerSpec {
    /// `mlx` or `peft`.
    std::string name;
    /// The environment's interpreter -- never the system Python.
    std::filesystem::path interpreter;
    /// The seeded driver, `training/scripts/<name>.py`.
    std::filesystem::path script;
    TrainerCapabilities capabilities;
    /// Injectable, so every test here is hermetic.
    Spawner spawn = default_spawner();
};

class ScriptTrainer final : public Trainer {
public:
    explicit ScriptTrainer(ScriptTrainerSpec spec);

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] TrainerCapabilities capabilities() const override;
    [[nodiscard]] TrainOutcome train(const TrainRequest& request, const ProgressSink& on_progress,
                                     const harness::CancellationToken& cancellation) override;
    [[nodiscard]] std::string fuse(const std::filesystem::path& base,
                                   const std::filesystem::path& adapter,
                                   const std::filesystem::path& out, const MessageSink& on_message,
                                   const harness::CancellationToken& cancellation) override;
    [[nodiscard]] std::unique_ptr<CandidateRunner> candidate_runner(
        const std::filesystem::path& base, const std::filesystem::path& adapter) override;

    [[nodiscard]] const ScriptTrainerSpec& spec() const noexcept {
        return spec_;
    }

private:
    ScriptTrainerSpec spec_;
};

/// The argv each mode sends, exposed because they are the contract the
/// drivers' argparse must accept -- a test pins them.
[[nodiscard]] std::vector<std::string> train_arguments(const TrainRequest& request);
[[nodiscard]] std::vector<std::string> fuse_arguments(const std::filesystem::path& base,
                                                      const std::filesystem::path& adapter,
                                                      const std::filesystem::path& out);
[[nodiscard]] std::vector<std::string> infer_arguments(const std::filesystem::path& base,
                                                       const std::filesystem::path& adapter,
                                                       std::string_view prompt, int max_tokens);

/// The request every driver run is made from: the spec's interpreter and
/// script, `arguments`, and the environment guard against bytecode.
[[nodiscard]] ScriptRequest script_request(const ScriptTrainerSpec& spec,
                                           std::vector<std::string> arguments);

}  // namespace apogee::training
