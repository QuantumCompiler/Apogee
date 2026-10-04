#pragma once

#include <filesystem>
#include <memory>
#include <string_view>

#include "training/script_trainer.h"

/// The Apple Silicon trainer: `train_mlx.py` over `mlx_lm` -- `mlx_lm.lora`
/// for the run, `mlx_lm.fuse` for the merge, the `mlx_lm` API for adapter
/// inference. Needs the `mlx` requirement set (`apogee train setup --trainer
/// mlx`).
namespace apogee::training {

inline constexpr std::string_view kMlxScriptName = "train_mlx.py";

/// The MLX trainer over the seeded driver under `scripts_dir`.
[[nodiscard]] std::unique_ptr<Trainer> make_mlx_trainer(const std::filesystem::path& interpreter,
                                                        const std::filesystem::path& scripts_dir,
                                                        Spawner spawn = default_spawner());

[[nodiscard]] TrainerCapabilities mlx_capabilities();

}  // namespace apogee::training
