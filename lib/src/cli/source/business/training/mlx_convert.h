#pragma once

#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/cancellation.h"
#include "training/trainer.h"

/// `mlx_convert.py`, run under Apogee's own Python environment: a
/// full-weight SafeTensors snapshot in, an MLX model directory out, through
/// `mlx_lm`'s own `convert` (27b).
///
/// **A child, never a link** -- the GGUF converter's discipline, a different
/// engine: the driver is seeded beside the trainers, run by the environment's
/// interpreter through the one script runner (its JSONL framed by the one
/// framer, its stderr captured as a tail and folded into a failure, never
/// inherited), and ended when the caller's token fires. What it writes is the
/// caller's to verify and commit: this file never knows the store.
namespace apogee::training {

/// One precision `models convert --mlx` writes, spelled the way a community
/// build's name ends (`-4bit`, `-bf16`): `mlx_lm.convert`'s own choices --
/// bits in its affine scheme with its default group of 64, its `mxfp4`
/// mode, or a floating-point type unquantized -- and nothing it does not
/// expose.
struct MlxPrecision {
    std::string_view name;
    /// Bits a weight; 0 when unquantized.
    int bits = 0;
    /// `mlx-lm`'s quantization mode; empty for unquantized.
    std::string_view mode;
    /// `mlx-lm`'s dtype for an unquantized conversion; empty when quantized.
    std::string_view dtype;
    std::string_view summary;
};

/// Every precision, the default (`4bit`) first.
[[nodiscard]] std::span<const MlxPrecision> mlx_precisions() noexcept;

/// The precision named `name`, or null.
[[nodiscard]] const MlxPrecision* find_mlx_precision(std::string_view name) noexcept;

/// Every precision's name, in order -- for a flag's help and its check.
[[nodiscard]] std::vector<std::string> mlx_precision_names();

/// Where the seeded driver lives: `training/scripts/mlx_convert.py`.
[[nodiscard]] std::filesystem::path mlx_converter_script();

/// The driver's arguments: `--hf-path <input> --mlx-path <output>`, then
/// `--q-bits`, `--q-group-size` and `--q-mode` for a quantized precision, or
/// `--dtype` for one that is not.
[[nodiscard]] std::vector<std::string> mlx_converter_arguments(const MlxPrecision& precision,
                                                               const std::filesystem::path& input,
                                                               const std::filesystem::path& output);

/// Writes the MLX directory `output` from the snapshot `input`; the error, or
/// empty -- "cancelled" when the token fired.
using MlxConverter = std::function<std::string(
    const std::filesystem::path& input, const std::filesystem::path& output,
    const MessageSink& on_message, const harness::CancellationToken& cancellation)>;

/// The driver under `interpreter`, as an MlxConverter. Its `{"message"}`
/// lines go to the sink; its stderr is captured as a tail and folded into
/// the error, never inherited.
[[nodiscard]] MlxConverter script_mlx_converter(std::filesystem::path interpreter,
                                                std::filesystem::path script,
                                                const MlxPrecision& precision);

}  // namespace apogee::training
