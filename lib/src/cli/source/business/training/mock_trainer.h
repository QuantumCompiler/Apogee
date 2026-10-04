#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>

#include "training/trainer.h"

/// An in-process trainer with no Python behind it, shipped in the binary as
/// `--trainer mock` -- the `mock` backend type's precedent. Scripted
/// iterations with a falling loss, a token adapter directory, a fuse that
/// copies, an echoing candidate, and at promote a minimal valid GGUF, so
/// the whole chain -- run, eval, promote, rollback -- runs on the real
/// binary in the lifecycle test with nothing installed. Every orchestration
/// test drives it too; the real drivers are proven separately under stub
/// modules.
///
/// **Scripted by the dataset**, the way the mock backend is scripted by its
/// file: when the dataset's first line is `{"mock": {...}}`, its `error`
/// fails the run after the first iteration, its `fuse_error` fails the
/// promote's fuse (carried in the adapter it wrote, so a later promote sees
/// it), its `iters` sets the count, and its `answer` is what the trained
/// candidate replies to every prompt instead of echoing it (carried in the
/// adapter too) -- which is how a pipeline stage that REGRESSES an earlier
/// stage's suite is driven through the real command line. A fused
/// checkpoint carries the answer forward, so a later stage trained from it
/// inherits the regression until its own dataset says otherwise.
namespace apogee::training {

/// The key the dataset's first line scripts the mock with.
inline constexpr std::string_view kMockScriptKey = "mock";

struct MockTrainerOptions {
    /// Iterations when the request asks for none.
    int iters = 5;
    double final_loss = 0.42;
    /// A pause per iteration, so a cancellation test can land mid-stream.
    std::chrono::milliseconds delay{0};
    /// When set, `train` fails with this after the first iteration.
    std::string error;
    /// When set, `fuse` fails with this.
    std::string fuse_error;
    /// When set, the trained candidate answers this to every prompt.
    std::string answer;
};

class MockTrainer final : public Trainer {
public:
    explicit MockTrainer(MockTrainerOptions options = {});

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] TrainerCapabilities capabilities() const override;
    [[nodiscard]] TrainOutcome train(const TrainRequest& request, const ProgressSink& on_progress,
                                     const harness::CancellationToken& cancellation) override;
    [[nodiscard]] std::string fuse(const std::filesystem::path& base,
                                   const std::filesystem::path& adapter,
                                   const std::filesystem::path& out, const MessageSink& on_message,
                                   const harness::CancellationToken& cancellation) override;
    /// Echoes the prompt; the untuned base (an empty adapter) prefixes it
    /// with `base: `, so a judge test can tell the two apart. An adapter
    /// (or a fused base) carrying a scripted `answer` replies with that.
    [[nodiscard]] std::unique_ptr<CandidateRunner> candidate_runner(
        const std::filesystem::path& base, const std::filesystem::path& adapter) override;

private:
    MockTrainerOptions options_;
};

/// The mock's converter: a minimal, well-formed GGUF at `gguf` -- the magic,
/// version 3, `general.architecture` from the fused `config.json`, one
/// tensor -- that the header reader parses. The error text, or empty.
[[nodiscard]] std::string write_mock_gguf(const std::filesystem::path& fused_dir,
                                          const std::filesystem::path& gguf);

}  // namespace apogee::training
