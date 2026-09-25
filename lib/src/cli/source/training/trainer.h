#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "harness/cancellation.h"

/// The trainer contract: what a fine-tuning run asks of whatever executes
/// it, and the JSONL progress protocol that execution speaks back.
///
/// Execution is Python -- `mlx_lm` on Apple Silicon, `transformers`/`peft`
/// on CUDA -- driven through the script runner; the C++ side owns everything
/// around it: the run directory, the manifest, the status line, the gate,
/// the promotion. The interface is what lets the whole chain run on the real
/// binary with no Python at all (the mock trainer), and it is what the
/// pipeline item composes.
///
/// **The protocol is a contract on both sides.** `parse_progress_line` is
/// pinned against a golden fixture, and the shipped drivers are run under
/// stub modules to prove they emit the fixture's shapes. Three outcomes are
/// distinct by construction, each a silent failure in the reference
/// implementation: an `{"error"}` line is its own event (there it decoded
/// as a blank progress tick), a non-JSON line is a message (there it was
/// dropped), and the child's exit code is carried (there it was discarded,
/// so a crashed trainer was a complete run).
namespace apogee::training {

/// The adapter directory inside a run's directory.
inline constexpr std::string_view kAdapterDirName = "adapters";
/// The fused SafeTensors checkpoint inside a run's directory.
inline constexpr std::string_view kFusedDirName = "fused";
/// Where `train_mlx.py` lays out `{train,valid}.jsonl`.
inline constexpr std::string_view kDataDirName = "data";

struct TrainRequest {
    /// The SafeTensors snapshot: `config.json` and `*.safetensors`.
    std::filesystem::path model_dir;
    std::filesystem::path dataset;
    /// `lora` or `qlora`.
    std::string method = "lora";
    /// Zero means the driver's default.
    int iters = 0;
    int batch_size = 0;
    int num_layers = 0;
    bool grad_checkpoint = false;
    /// Completion-only loss: the prompt tokens are excluded.
    bool mask_prompt = false;
    std::string run_id;
    /// `training/runs/<run_id>`; the adapter lands in `adapters/` beneath.
    std::filesystem::path output_dir;
};

struct TrainerCapabilities {
    std::vector<std::string> methods;
    bool grad_checkpoint = false;
    bool mask_prompt = false;
};

/// One line of the progress protocol, classified.
struct ProgressEvent {
    enum class Kind : std::uint8_t {
        /// `{"iteration", "total_iters", "loss", "lr", "throughput"}`.
        Iteration,
        /// `{"message"}`, a driver's terminal record, or a non-JSON line.
        Message,
        /// `{"error"}` -- fatal; the driver exits non-zero after it.
        Error,
    };
    Kind kind = Kind::Message;
    int iteration = 0;
    int total_iters = 0;
    double loss = 0.0;
    double lr = 0.0;
    double throughput = 0.0;
    /// The message or error text; for an iteration, empty.
    std::string text;
};

/// Classifies one complete line. `{"iteration": …}` with the numeric fields
/// is an Iteration; `{"error"}` an Error; everything else -- `{"message"}`,
/// a record, a stack-trace line -- a Message carrying the text.
[[nodiscard]] ProgressEvent parse_progress_line(std::string_view line);

using ProgressSink = std::function<void(const ProgressEvent&)>;
using MessageSink = std::function<void(std::string_view)>;

struct TrainOutcome {
    bool ok = false;
    bool cancelled = false;
    /// Why not, with the driver's stderr tail when there is one.
    std::string error;
    /// The last iteration's loss and number.
    double final_loss = 0.0;
    int iterations = 0;
    std::filesystem::path adapter_dir;
};

struct CandidateReply {
    bool ok = false;
    std::string text;
    std::string error;
};

/// Adapter-only inference for eval: the base plus the adapter answers one
/// prompt, no fuse, no GGUF. An empty adapter is the untuned base -- the
/// pairwise baseline.
class CandidateRunner {
public:
    CandidateRunner() = default;
    virtual ~CandidateRunner() = default;
    CandidateRunner(const CandidateRunner&) = delete;
    CandidateRunner& operator=(const CandidateRunner&) = delete;
    CandidateRunner(CandidateRunner&&) = delete;
    CandidateRunner& operator=(CandidateRunner&&) = delete;

    [[nodiscard]] virtual CandidateReply run(std::string_view prompt,
                                             const harness::CancellationToken& cancellation) = 0;
};

class Trainer {
public:
    Trainer() = default;
    virtual ~Trainer() = default;
    Trainer(const Trainer&) = delete;
    Trainer& operator=(const Trainer&) = delete;
    Trainer(Trainer&&) = delete;
    Trainer& operator=(Trainer&&) = delete;

    /// `mlx`, `peft`, `mock` -- what the manifest records.
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual TrainerCapabilities capabilities() const = 0;

    /// Runs the fine-tune to completion, delivering every progress event.
    [[nodiscard]] virtual TrainOutcome train(const TrainRequest& request,
                                             const ProgressSink& on_progress,
                                             const harness::CancellationToken& cancellation) = 0;

    /// Merges `adapter` into `base`, writing a SafeTensors model to `out`.
    /// The error text, or empty.
    [[nodiscard]] virtual std::string fuse(const std::filesystem::path& base,
                                           const std::filesystem::path& adapter,
                                           const std::filesystem::path& out,
                                           const MessageSink& on_message,
                                           const harness::CancellationToken& cancellation) = 0;

    /// Inference over `base` plus `adapter` (empty: the untuned base).
    [[nodiscard]] virtual std::unique_ptr<CandidateRunner> candidate_runner(
        const std::filesystem::path& base, const std::filesystem::path& adapter) = 0;
};

/// What `--trainer auto` looks at.
struct HostShape {
    bool apple_silicon = false;
    /// `nvidia-smi` is on PATH.
    bool nvidia_smi = false;
};

[[nodiscard]] HostShape detect_host();

struct TrainerChoice {
    /// `mlx`, `peft` or `mock`; empty with `error` set on a refusal.
    std::string name;
    std::string error;
};

/// The rule: `auto` (or empty) picks `mlx` on Apple Silicon, `peft` where
/// `nvidia-smi` is on PATH, and refuses naming both when neither fits; a
/// known name is taken as given; anything else is refused by name.
[[nodiscard]] TrainerChoice select_trainer(std::string_view requested, const HostShape& host);

/// The names `select_trainer` takes: `auto`, then each trainer by name.
[[nodiscard]] std::span<const std::string_view> trainer_names() noexcept;

/// Whether `dir` is something a trainer can take: a directory holding
/// `config.json` and at least one `*.safetensors` shard. The error text
/// names the fix -- a GGUF, or a name that is not a directory, is refused
/// pointing at `apogee models pull <owner>/<repo> --safetensors`, because
/// only full-precision weights are trainable.
[[nodiscard]] std::string validate_student(const std::filesystem::path& dir);

}  // namespace apogee::training
