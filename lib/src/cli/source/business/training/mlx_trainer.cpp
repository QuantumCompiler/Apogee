#include "training/mlx_trainer.h"

#include <utility>

namespace apogee::training {

TrainerCapabilities mlx_capabilities() {
    return TrainerCapabilities{
        .methods = {"lora", "qlora"}, .grad_checkpoint = true, .mask_prompt = true};
}

std::unique_ptr<Trainer> make_mlx_trainer(const std::filesystem::path& interpreter,
                                          const std::filesystem::path& scripts_dir, Spawner spawn) {
    ScriptTrainerSpec spec;
    spec.name = "mlx";
    spec.interpreter = interpreter;
    spec.script = scripts_dir / kMlxScriptName;
    spec.capabilities = mlx_capabilities();
    spec.spawn = std::move(spawn);
    return std::make_unique<ScriptTrainer>(std::move(spec));
}

}  // namespace apogee::training
