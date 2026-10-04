#include "training/peft_trainer.h"

#include <utility>

namespace apogee::training {

TrainerCapabilities peft_capabilities() {
    return TrainerCapabilities{
        .methods = {"lora", "qlora"}, .grad_checkpoint = true, .mask_prompt = true};
}

std::unique_ptr<Trainer> make_peft_trainer(const std::filesystem::path& interpreter,
                                           const std::filesystem::path& scripts_dir,
                                           Spawner spawn) {
    ScriptTrainerSpec spec;
    spec.name = "peft";
    spec.interpreter = interpreter;
    spec.script = scripts_dir / kPeftScriptName;
    spec.capabilities = peft_capabilities();
    spec.spawn = std::move(spawn);
    return std::make_unique<ScriptTrainer>(std::move(spec));
}

}  // namespace apogee::training
