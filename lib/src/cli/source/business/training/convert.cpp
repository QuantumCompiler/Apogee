#include "training/convert.h"

#include <regex>
#include <string_view>
#include <utility>

#include "contracts/assets.h"
#include "contracts/layout.h"
#include "training/script_runner.h"

namespace apogee::training {

const std::vector<std::string>& converter_out_types() {
    static const std::vector<std::string> types{"f16", "bf16", "f32", "q8_0", "auto"};
    return types;
}

std::filesystem::path converter_script() {
    return harness::training_scripts_dir() / "convert" / "convert_hf_to_gguf.py";
}

std::string converter_unavailable(const PythonEnv& env, const std::filesystem::path& script,
                                  std::span<const std::string_view> retired) {
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
        // The bundled name is written with '/'; the message names the file
        // the way this platform writes paths.
        std::filesystem::path path =
            script.parent_path() / std::string{file.name.substr(tree_prefix.size())};
        path.make_preferred();
        if (!std::filesystem::is_regular_file(path, code)) {
            return "the vendored converter is incomplete: " + path.string() +
                   " is missing -- run 'apogee check --fix' to seed it";
        }
    }
    // An earlier Apogee's copy, left by skip-if-present seeding: it would run
    // an older llama.cpp's converter than this build's runtime, and refuse
    // the models the update was for.
    if (harness::inspect_converter_tree(script.parent_path(), retired).stale > 0) {
        return "the converter under " + script.parent_path().string() +
               " is from an earlier Apogee (an older llama.cpp than this build's) -- run "
               "'apogee check --fix' to update it";
    }
    return {};
}

std::vector<std::string> converter_arguments(std::string_view out_type,
                                             const std::filesystem::path& input,
                                             const std::filesystem::path& output,
                                             ConverterOutput writes, bool dense_modules) {
    std::vector<std::string> arguments{"--outtype", std::string{out_type}, "--outfile",
                                       output.string(), input.string()};
    if (writes == ConverterOutput::Projector) {
        arguments.emplace_back("--mmproj");
    }
    if (dense_modules && writes == ConverterOutput::Model) {
        arguments.emplace_back("--sentence-transformers-dense-modules");
    }
    return arguments;
}

std::string fold_name_list(const std::string& error, std::size_t most) {
    // A name a line: a dotted identifier, then its list's comma or full stop,
    // and on the last one perhaps the run's ` -- stderr: ...` tail.
    static const std::regex kName{R"(^\s*[A-Za-z_][A-Za-z0-9_.]*[,.]?(\s+--\s.*)?$)"};
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= error.size()) {
        const std::size_t end = error.find('\n', start);
        lines.push_back(
            error.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    std::size_t names = 0;
    for (const std::string& line : lines) {
        names += std::regex_match(line, kName) ? 1 : 0;
    }
    if (names <= most) {
        return error;
    }
    std::string out;
    std::string tail;
    std::size_t seen = 0;
    for (const std::string& line : lines) {
        if (!std::regex_match(line, kName)) {
            out += (out.empty() ? "" : "\n") + line;
            continue;
        }
        ++seen;
        if (seen <= most) {
            out += (out.empty() ? "" : "\n") + line;
            if (seen == most) {
                out += "\n... and " + std::to_string(names - most) + " more";
            }
        } else if (const std::size_t dash = line.find(" -- "); dash != std::string::npos) {
            tail = line.substr(dash);
        }
    }
    return out + tail;
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
                           std::string out_type, ConverterOutput writes, bool dense_modules) {
    return [interpreter = std::move(interpreter), script = std::move(script),
            out_type = std::move(out_type), writes, dense_modules](
               const std::filesystem::path& input, const std::filesystem::path& gguf,
               const MessageSink& on_message, const harness::CancellationToken& cancellation) {
        ScriptRequest request;
        request.interpreter = interpreter;
        request.script = script;
        request.arguments = converter_arguments(out_type, input, gguf, writes, dense_modules);
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
        return explain_converter_failure(fold_name_list(outcome.describe()));
    };
}

}  // namespace apogee::training
