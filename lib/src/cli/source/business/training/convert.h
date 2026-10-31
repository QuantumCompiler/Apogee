#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/assets.h"
#include "training/promote.h"
#include "training/python_env.h"

/// The vendored `convert_hf_to_gguf.py`, run under Apogee's own Python
/// environment: SafeTensors in, GGUF out.
///
/// **One implementation, two callers.** `train promote` converts a fused run
/// with it and `models convert` converts a pulled snapshot, and both reach
/// the script through `script_converter` -- a second way to run it would be a
/// second set of arguments to keep in step with an upstream script.
///
/// **Its sibling** (53b): the vendored `convert_lora_to_gguf.py`, beside it
/// in the same tree and over the same `conversion/` and `gguf-py/`, turns a
/// PEFT adapter into a GGUF LoRA for `train promote --adapter`, reached the
/// same way through `script_lora_converter` -- over the one script runner.
namespace apogee::training {

/// The precisions a user would pick, in the script's own spelling. Its
/// ternary types (tq1_0, tq2_0) are left out: they are for models trained
/// ternary, and anything else converts to garbage without complaint.
[[nodiscard]] const std::vector<std::string>& converter_out_types();

/// Where the seeded script lives: `training/scripts/convert/`.
[[nodiscard]] std::filesystem::path converter_script();

/// Where the seeded LoRA converter lives (53b): beside `converter_script()`,
/// `training/scripts/convert/convert_lora_to_gguf.py`. `converter_unavailable`
/// asks the same questions of it, the tree being one.
[[nodiscard]] std::filesystem::path lora_converter_script();

/// Why the converter cannot run, naming the one command that fixes it -- or
/// empty when it can: the environment, its `convert` set, and every file of
/// the vendored tree `script` heads -- present, and not an earlier Apogee's
/// copy (`retired`, as `harness::inspect_converter_tree` takes it). The environment is not created
/// or filled here: the requirement set is torch and transformers, gigabytes a user installs on
/// purpose.
[[nodiscard]] std::string converter_unavailable(
    const PythonEnv& env, const std::filesystem::path& script,
    std::span<const std::string_view> retired = harness::bundled_converter_retired());

/// Which file a run of the script writes: the model, or -- for a model with
/// a vision or audio encoder -- the projector that lets it read images or
/// sound (`--mmproj`), a separate GGUF a llamacpp backend loads beside the
/// model (`mmproj_path`).
enum class ConverterOutput : std::uint8_t { Model, Projector };

/// The script's arguments: `--outtype <type> --outfile <output> <input>`,
/// then `--mmproj` for a projector, and `--sentence-transformers-dense-modules`
/// for an embedding model whose dense layers are there -- the converter leaves
/// them out without it (M21); the caller, who reads the snapshot, says so.
[[nodiscard]] std::vector<std::string> converter_arguments(
    std::string_view out_type, const std::filesystem::path& input,
    const std::filesystem::path& output, ConverterOutput writes = ConverterOutput::Model,
    bool dense_modules = false);

/// A failure that lists names one a line -- mlx's strict weight load names
/// every parameter it could not place, hundreds of them -- cut to its first
/// `most` names and a count of the rest, everything else kept as written
/// (M21). Any other error is returned unchanged.
[[nodiscard]] std::string fold_name_list(const std::string& error, std::size_t most = 20);

/// A failed run's error, with one plain line added when it names an
/// architecture or a tensor the script does not know: the fix (a newer
/// llama.cpp) is not the user's to guess from a Python traceback.
[[nodiscard]] std::string explain_converter_failure(std::string error);

/// The script under `interpreter`, as a Converter. Its stdout lines go to the
/// sink; its stderr is captured as a tail and folded into the error, never
/// inherited, and explained (`explain_converter_failure`).
[[nodiscard]] Converter script_converter(std::filesystem::path interpreter,
                                         std::filesystem::path script, std::string out_type = "f16",
                                         ConverterOutput writes = ConverterOutput::Model,
                                         bool dense_modules = false);

/// The LoRA script's arguments (53b): `--outtype <type> --base <base>
/// --outfile <output> <adapter>`. The base is always named, so the script
/// reads the base's `config.json` from disk and never asks the Hub for it.
[[nodiscard]] std::vector<std::string> lora_converter_arguments(
    std::string_view out_type, const std::filesystem::path& adapter,
    const std::filesystem::path& base, const std::filesystem::path& output);

/// The LoRA script under `interpreter`, as an AdapterConverter (53b), over the
/// one script runner: its stdout lines to the sink, its stderr a tail folded
/// into the error and explained, never inherited, and the Hub offline
/// (`HF_HUB_OFFLINE=1`) -- a conversion reads the files it is handed.
[[nodiscard]] AdapterConverter script_lora_converter(std::filesystem::path interpreter,
                                                     std::filesystem::path script,
                                                     std::string out_type = "f16");

}  // namespace apogee::training
