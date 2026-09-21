#pragma once

#include <filesystem>
#include <memory>
#include <string_view>

#include "training/script_trainer.h"

/// The CUDA trainer: `train_peft.py` over transformers + peft (+
/// bitsandbytes for QLoRA). Needs the `peft` requirement set (`apogee train
/// setup --trainer peft`). The driver hard-exits on Apple Silicon before any
/// ML import, because bitsandbytes aborts inside its C-extension init there.
namespace apogee::training {

inline constexpr std::string_view kPeftScriptName = "train_peft.py";

/// The PEFT trainer over the seeded driver under `scripts_dir`.
[[nodiscard]] std::unique_ptr<Trainer> make_peft_trainer(const std::filesystem::path& interpreter,
                                                         const std::filesystem::path& scripts_dir,
                                                         Spawner spawn = default_spawner());

[[nodiscard]] TrainerCapabilities peft_capabilities();

}  // namespace apogee::training
