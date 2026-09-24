#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "training/promote.h"
#include "training/python_env.h"

/// The vendored `convert_hf_to_gguf.py`, run under Apogee's own Python
/// environment: SafeTensors in, GGUF out.
///
/// **One implementation, two callers.** `train promote` converts a fused run
/// with it and `models convert` converts a pulled snapshot, and both reach
/// the script through `script_converter` -- a second way to run it would be a
/// second set of arguments to keep in step with an upstream script.
namespace apogee::training {

/// The precisions a user would pick, in the script's own spelling. Its
/// ternary types (tq1_0, tq2_0) are left out: they are for models trained
/// ternary, and anything else converts to garbage without complaint.
[[nodiscard]] const std::vector<std::string>& converter_out_types();

/// Where the seeded script lives: `training/scripts/convert/`.
[[nodiscard]] std::filesystem::path converter_script();

/// Why the converter cannot run, naming the one command that fixes it -- or
/// empty when it can: the environment, its `convert` set, and every file of
/// the vendored tree `script` heads. The environment is not created or filled
/// here: the requirement set is torch and transformers, gigabytes a user
/// installs on purpose.
[[nodiscard]] std::string converter_unavailable(const PythonEnv& env,
                                                const std::filesystem::path& script);

/// The script's arguments: `--outtype <type> --outfile <output> <input>`.
[[nodiscard]] std::vector<std::string> converter_arguments(std::string_view out_type,
                                                           const std::filesystem::path& input,
                                                           const std::filesystem::path& output);

/// A failed run's error, with one plain line added when it names an
/// architecture or a tensor the script does not know: the fix (a newer
/// llama.cpp) is not the user's to guess from a Python traceback.
[[nodiscard]] std::string explain_converter_failure(std::string error);

/// The script under `interpreter`, as a Converter. Its stdout lines go to the
/// sink; its stderr is captured as a tail and folded into the error, never
/// inherited, and explained (`explain_converter_failure`).
[[nodiscard]] Converter script_converter(std::filesystem::path interpreter,
                                         std::filesystem::path script,
                                         std::string out_type = "f16");

}  // namespace apogee::training
