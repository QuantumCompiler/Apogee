#include "training/mlx_convert.h"

#include <algorithm>
#include <array>
#include <utility>

#include "contracts/layout.h"
#include "training/script_runner.h"

namespace apogee::training {
namespace {

/// `mlx-lm`'s default group for its affine scheme, written out so the
/// directory's `config.json` says what was asked rather than what a later
/// `mlx-lm` defaults to.
constexpr int kAffineGroup = 64;

constexpr std::array<MlxPrecision, 7> kPrecisions{{
    {.name = "4bit",
     .bits = 4,
     .mode = "affine",
     .dtype = "",
     .summary = "4 bits a weight, groups of 64 -- the community builds' usual choice"},
    {.name = "3bit", .bits = 3, .mode = "affine", .dtype = "", .summary = "3 bits a weight"},
    {.name = "6bit", .bits = 6, .mode = "affine", .dtype = "", .summary = "6 bits a weight"},
    {.name = "8bit",
     .bits = 8,
     .mode = "affine",
     .dtype = "",
     .summary = "8 bits a weight -- near the full weights' quality"},
    {.name = "mxfp4",
     .bits = 4,
     .mode = "mxfp4",
     .dtype = "",
     .summary = "the microscaling 4-bit float, groups of 32"},
    {.name = "bf16", .bits = 0, .mode = "", .dtype = "bfloat16", .summary = "unquantized, bf16"},
    {.name = "f16", .bits = 0, .mode = "", .dtype = "float16", .summary = "unquantized, f16"},
}};

}  // namespace

std::span<const MlxPrecision> mlx_precisions() noexcept {
    return kPrecisions;
}

const MlxPrecision* find_mlx_precision(std::string_view name) noexcept {
    for (const MlxPrecision& precision : kPrecisions) {
        if (precision.name == name) {
            return &precision;
        }
    }
    return nullptr;
}

std::vector<std::string> mlx_precision_names() {
    std::vector<std::string> names;
    names.reserve(kPrecisions.size());
    for (const MlxPrecision& precision : kPrecisions) {
        names.emplace_back(precision.name);
    }
    return names;
}

std::filesystem::path mlx_converter_script() {
    return harness::training_scripts_dir() / "mlx_convert.py";
}

std::vector<std::string> mlx_converter_arguments(const MlxPrecision& precision,
                                                 const std::filesystem::path& input,
                                                 const std::filesystem::path& output) {
    std::vector<std::string> arguments{"--hf-path", input.string(), "--mlx-path", output.string()};
    if (precision.bits > 0) {
        arguments.insert(arguments.end(), {"--q-bits", std::to_string(precision.bits)});
        if (precision.mode == "affine") {
            arguments.insert(arguments.end(), {"--q-group-size", std::to_string(kAffineGroup)});
        }
        arguments.insert(arguments.end(), {"--q-mode", std::string{precision.mode}});
    } else {
        arguments.insert(arguments.end(), {"--dtype", std::string{precision.dtype}});
    }
    return arguments;
}

MlxConverter script_mlx_converter(std::filesystem::path interpreter, std::filesystem::path script,
                                  const MlxPrecision& precision) {
    return [interpreter = std::move(interpreter), script = std::move(script), precision](
               const std::filesystem::path& input, const std::filesystem::path& output,
               const MessageSink& on_message, const harness::CancellationToken& cancellation) {
        ScriptRequest request;
        request.interpreter = interpreter;
        request.script = script;
        request.arguments = mlx_converter_arguments(precision, input, output);
        request.environment.emplace_back("PYTHONDONTWRITEBYTECODE", "1");
        const ScriptOutcome outcome = run_script(
            request,
            [&on_message](const ScriptEvent& event) {
                if (on_message && event.kind == ScriptEvent::Kind::Message) {
                    on_message(event.text);
                }
            },
            cancellation);
        if (outcome.ok) {
            return std::string{};
        }
        if (outcome.cancelled) {
            return std::string{"cancelled"};
        }
        return outcome.describe();
    };
}

}  // namespace apogee::training
