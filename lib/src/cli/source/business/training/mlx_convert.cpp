#include "training/mlx_convert.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <regex>
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
                                  const MlxPrecision& precision, std::filesystem::path mlx_lm) {
    return [interpreter = std::move(interpreter), script = std::move(script), precision,
            mlx_lm = std::move(mlx_lm)](
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
        // A type mlx-lm knows no converter for: its own lists read now, at
        // failure time, to name the way out (M15).
        const std::string error = outcome.describe();
        return unsupported_model_type(error).empty()
                   ? error
                   : fold_model_type_failure(error, mlx_model_types(mlx_lm));
    };
}

std::vector<std::string> mlx_model_types(const std::filesystem::path& mlx_lm) {
    std::vector<std::string> out;
    std::error_code code;
    if (mlx_lm.empty() || !std::filesystem::is_directory(mlx_lm, code)) {
        return out;
    }
    for (const auto& entry : std::filesystem::directory_iterator(mlx_lm / "models", code)) {
        const std::filesystem::path& path = entry.path();
        const std::string stem = path.stem().string();
        if (path.extension() != ".py" || stem.empty() || stem.front() == '_') {
            continue;
        }
        // A model's module defines its `Model`; a helper (`cache`, `base`)
        // does not, and is no type.
        std::ifstream module{path, std::ios::binary};
        const std::string source{std::istreambuf_iterator<char>{module},
                                 std::istreambuf_iterator<char>{}};
        if (source.find("class Model(") != std::string::npos) {
            out.push_back(stem);
        }
    }
    // MODEL_REMAPPING's keys: `"<type>": "<module>"` pairs inside its braces.
    std::ifstream in{mlx_lm / "utils.py", std::ios::binary};
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    if (const std::size_t named = text.find("MODEL_REMAPPING"); named != std::string::npos) {
        const std::size_t open = text.find('{', named);
        const std::size_t close = open == std::string::npos ? open : text.find('}', open);
        if (close != std::string::npos) {
            const std::string body = text.substr(open + 1, close - open - 1);
            static const std::regex pair{R"re(["']([A-Za-z0-9_.-]+)["']\s*:)re"};
            for (auto match = std::sregex_iterator(body.begin(), body.end(), pair);
                 match != std::sregex_iterator(); ++match) {
                out.push_back((*match)[1].str());
            }
        }
    }
    std::ranges::sort(out);
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::string unsupported_model_type(std::string_view error) {
    constexpr std::string_view kHead = "Model type ";
    constexpr std::string_view kTail = " not supported";
    const std::size_t head = error.find(kHead);
    if (head == std::string_view::npos) {
        return {};
    }
    const std::size_t start = head + kHead.size();
    const std::size_t tail = error.find(kTail, start);
    if (tail == std::string_view::npos || tail == start) {
        return {};
    }
    return std::string{error.substr(start, tail - start)};
}

std::vector<std::string> nearest_model_types(std::string_view wanted,
                                             const std::vector<std::string>& types,
                                             std::size_t most) {
    const auto shared = [wanted](std::string_view type) {
        std::size_t length = 0;
        while (length < wanted.size() && length < type.size() && wanted[length] == type[length]) {
            ++length;
        }
        return length;
    };
    std::vector<std::string> out;
    for (const std::string& type : types) {
        if (type != wanted && shared(type) >= 3) {
            out.push_back(type);
        }
    }
    std::ranges::stable_sort(out, [&shared](const std::string& a, const std::string& b) {
        const std::size_t left = shared(a);
        const std::size_t right = shared(b);
        return left != right ? left > right : a.size() < b.size();
    });
    if (out.size() > most) {
        out.resize(most);
    }
    return out;
}

std::string fold_model_type_failure(const std::string& error,
                                    const std::vector<std::string>& types) {
    const std::string wanted = unsupported_model_type(error);
    if (wanted.empty()) {
        return error;
    }
    std::string near;
    for (const std::string& type : nearest_model_types(wanted, types)) {
        near += (near.empty() ? "" : ", ") + type;
    }
    return error +
           " -- if these weights are a type this mlx-lm knows, convert them as it: "
           "--model-type <type>" +
           (near.empty() ? std::string{} : " (nearest here: " + near + ")");
}

}  // namespace apogee::training
