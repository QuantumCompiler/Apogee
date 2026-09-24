#include "training/convert.h"

#include <string_view>
#include <utility>

#include "harness/assets.h"
#include "harness/layout.h"
#include "training/script_runner.h"

namespace apogee::training {

const std::vector<std::string>& converter_out_types() {
    static const std::vector<std::string> types{"f16", "bf16", "f32", "q8_0", "auto"};
    return types;
}

std::filesystem::path converter_script() {
    return harness::training_scripts_dir() / "convert" / "convert_hf_to_gguf.py";
}

std::string converter_unavailable(const PythonEnv& env, const std::filesystem::path& script) {
    if (!env.exists()) {
        return "the GGUF converter runs in Apogee's Python environment, which has not been "
               "created yet -- run 'apogee train setup --with convert' (a one-time install of "
               "torch and transformers)";
    }
    if (!env.status().has(RequirementSet::Convert)) {
        return "the GGUF converter needs the 'convert' requirement set in the Python environment "
               "-- run 'apogee train setup --with convert'";
    }
    std::error_code code;
    if (!std::filesystem::is_regular_file(script, code)) {
        return "the vendored converter is missing: " + script.string() +
               " -- run 'apogee check --fix' to seed it";
    }
    // The rest of the tree too. The script finds its `gguf` package beside
    // it and, when that is gone, falls back to the PyPI one without a word --
    // which lags the pin, and then fails on the first tensor it cannot name.
    constexpr std::string_view tree_prefix = "convert/";
    for (const harness::BundledScript& file : harness::bundled_converter_files()) {
        const std::filesystem::path path =
            script.parent_path() / std::string{file.name.substr(tree_prefix.size())};
        if (!std::filesystem::is_regular_file(path, code)) {
            return "the vendored converter is incomplete: " + path.string() +
                   " is missing -- run 'apogee check --fix' to seed it";
        }
    }
    return {};
}

std::vector<std::string> converter_arguments(std::string_view out_type,
                                             const std::filesystem::path& input,
                                             const std::filesystem::path& output,
                                             ConverterOutput writes) {
    std::vector<std::string> arguments{"--outtype", std::string{out_type}, "--outfile",
                                       output.string(), input.string()};
    if (writes == ConverterOutput::Projector) {
        arguments.emplace_back("--mmproj");
    }
    return arguments;
}

std::string explain_converter_failure(std::string error) {
    if (error.find("is not supported") != std::string::npos ||
        error.find("Failed to detect model architecture") != std::string::npos) {
        error +=
            "\nThe converter shipped with this build's llama.cpp does not know this model's "
            "architecture; a newer llama.cpp revision may.";
    } else if (error.find("Can not map tensor") != std::string::npos) {
        error +=
            "\nThe converter shipped with this build's llama.cpp knows this architecture but not "
            "every tensor this model carries; a newer llama.cpp revision may.";
    }
    return error;
}

Converter script_converter(std::filesystem::path interpreter, std::filesystem::path script,
                           std::string out_type, ConverterOutput writes) {
    return [interpreter = std::move(interpreter), script = std::move(script),
            out_type = std::move(out_type),
            writes](const std::filesystem::path& input, const std::filesystem::path& gguf,
                    const MessageSink& on_message, const harness::CancellationToken& cancellation) {
        ScriptRequest request;
        request.interpreter = interpreter;
        request.script = script;
        request.arguments = converter_arguments(out_type, input, gguf, writes);
        request.environment.emplace_back("PYTHONDONTWRITEBYTECODE", "1");
        const ScriptOutcome outcome = run_script(
            request,
            [&on_message](const ScriptEvent& event) {
                if (on_message && event.kind != ScriptEvent::Kind::Error) {
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
        return explain_converter_failure(outcome.describe());
    };
}

}  // namespace apogee::training
