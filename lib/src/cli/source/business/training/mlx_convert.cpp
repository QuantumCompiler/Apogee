#include "training/mlx_convert.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <regex>
#include <utility>

#include "contracts/layout.h"
#include "training/convert.h"
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

/// What an older seeded driver says when handed the mlx-vlm route it does
/// not know: argparse's own words.
constexpr std::string_view kUnknownRoute = "unrecognized arguments: --with";

/// What a seeded driver from before the mlx-embeddings route says when
/// handed it (53h): argparse refusing the choice -- and before mlx-video's
/// (53k), the same.
constexpr std::string_view kUnknownChoice = "argument --with: invalid choice";

/// mlx-video's Wan converter, the module the pinned commit holds it in
/// (53k, `python_env.cpp`): both its READMEs name others that commit lacks.
constexpr std::string_view kMlxVideoConverter = "mlx_video.models.wan_2.convert";

/// Whether `source` names `Model` as a word of its own -- an import or a
/// definition, never a longer name that ends in it (`LanguageModel`).
[[nodiscard]] bool names_model(const std::string& source) {
    constexpr std::string_view kWord = "Model";
    const auto word_char = [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
               c == '_';
    };
    for (std::size_t at = source.find(kWord); at != std::string::npos;
         at = source.find(kWord, at + 1)) {
        const std::size_t end = at + kWord.size();
        if ((at == 0 || !word_char(source[at - 1])) &&
            (end >= source.size() || !word_char(source[end]))) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::string read_text(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

}  // namespace

MlxConverterKind choose_mlx_converter(bool towers, bool text_only, bool embedding, bool drafter,
                                      bool video) noexcept {
    if (video) {
        // Wan's original layout is mlx-video's alone (53k): no language
        // model's converter builds a diffusion transformer.
        return MlxConverterKind::MlxVideo;
    }
    if (drafter) {
        // A draft head is built as the drafter mlx-vlm's speculative path
        // runs (53i): mlx-lm has no network for one, whatever is asked.
        return MlxConverterKind::MlxVlm;
    }
    if (embedding) {
        // An embedding model is built whole by mlx-embeddings, or not at all
        // (53h): mlx-lm builds no embedding model, and --text-only leaves
        // nothing out of one.
        return MlxConverterKind::MlxEmbeddings;
    }
    return towers && !text_only ? MlxConverterKind::MlxVlm : MlxConverterKind::MlxLm;
}

std::string_view to_string(MlxConverterKind converter) noexcept {
    switch (converter) {
        case MlxConverterKind::MlxLm:
            return "mlx-lm";
        case MlxConverterKind::MlxVlm:
            return "mlx-vlm";
        case MlxConverterKind::MlxEmbeddings:
            return "mlx-embeddings";
        case MlxConverterKind::MlxVideo:
            return "mlx-video";
    }
    return "mlx-lm";
}

std::string_view mlx_converter_package(MlxConverterKind converter) noexcept {
    switch (converter) {
        case MlxConverterKind::MlxLm:
            return "mlx_lm";
        case MlxConverterKind::MlxVlm:
            return "mlx_vlm";
        case MlxConverterKind::MlxEmbeddings:
            return "mlx_embeddings";
        case MlxConverterKind::MlxVideo:
            return "mlx_video";
    }
    return "mlx_lm";
}

std::string_view mlx_converter_entry(MlxConverterKind converter) noexcept {
    switch (converter) {
        case MlxConverterKind::MlxLm:
            return "mlx_lm.convert";
        case MlxConverterKind::MlxVlm:
            return "mlx_vlm.convert";
        case MlxConverterKind::MlxEmbeddings:
            return "mlx_embeddings.convert";
        case MlxConverterKind::MlxVideo:
            return kMlxVideoConverter;
    }
    return "mlx_lm.convert";
}

RequirementSet mlx_converter_set(MlxConverterKind converter) noexcept {
    switch (converter) {
        case MlxConverterKind::MlxLm:
            return RequirementSet::Mlx;
        case MlxConverterKind::MlxVlm:
            return RequirementSet::MlxVlm;
        case MlxConverterKind::MlxEmbeddings:
            return RequirementSet::MlxEmbeddings;
        case MlxConverterKind::MlxVideo:
            return RequirementSet::MlxVideo;
    }
    return RequirementSet::Mlx;
}

namespace {

/// The mlx-video module `dotted` names, as a file under the environment at
/// `venv`, with the torch its set brings beside it -- or empty.
[[nodiscard]] std::filesystem::path find_mlx_video_module(const std::filesystem::path& venv,
                                                          std::string_view dotted) {
    const std::filesystem::path package = find_site_package(venv, "mlx_video");
    if (package.empty() || find_site_package(venv, "torch").empty()) {
        return {};
    }
    // `mlx_video.models.wan_2.<module>`, as a file under the package.
    std::filesystem::path module = package.parent_path();
    for (std::string_view rest = dotted; !rest.empty();) {
        const std::size_t dot = rest.find('.');
        module /= std::string{rest.substr(0, dot)};
        rest = dot == std::string_view::npos ? std::string_view{} : rest.substr(dot + 1);
    }
    module += ".py";
    std::error_code code;
    return std::filesystem::is_regular_file(module, code) ? module : std::filesystem::path{};
}

}  // namespace

std::filesystem::path find_mlx_video_converter(const std::filesystem::path& venv) {
    return find_mlx_video_module(venv, kMlxVideoConverter);
}

std::filesystem::path find_mlx_video_generator(const std::filesystem::path& venv) {
    return find_mlx_video_module(venv, kMlxVideoGenerator);
}

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

bool mlx_converter_writes(MlxConverterKind converter, const MlxPrecision& precision) noexcept {
    if (converter != MlxConverterKind::MlxVideo) {
        return true;
    }
    // Its command line's own choices: `--bits 4|8` in affine groups, or a
    // `--dtype` -- bfloat16, float16, float32 -- unquantized.
    return precision.bits == 0 ||
           (precision.mode == "affine" && (precision.bits == 4 || precision.bits == 8));
}

std::vector<std::string> mlx_precision_names(MlxConverterKind converter) {
    std::vector<std::string> names;
    for (const MlxPrecision& precision : kPrecisions) {
        if (mlx_converter_writes(converter, precision)) {
            names.emplace_back(precision.name);
        }
    }
    return names;
}

std::filesystem::path mlx_converter_script() {
    return harness::training_scripts_dir() / "mlx_convert.py";
}

std::vector<std::string> mlx_converter_arguments(const MlxPrecision& precision,
                                                 const std::filesystem::path& input,
                                                 const std::filesystem::path& output,
                                                 MlxConverterKind converter) {
    std::vector<std::string> arguments;
    if (converter != MlxConverterKind::MlxLm) {
        arguments = {"--with", std::string{to_string(converter)}};
    }
    arguments.insert(arguments.end(), {"--hf-path", input.string(), "--mlx-path", output.string()});
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
                                  const MlxPrecision& precision, std::filesystem::path package,
                                  MlxConverterKind converter) {
    return [interpreter = std::move(interpreter), script = std::move(script), precision,
            package = std::move(package), converter](
               const std::filesystem::path& input, const std::filesystem::path& output,
               const MessageSink& on_message, const harness::CancellationToken& cancellation) {
        ScriptRequest request;
        request.interpreter = interpreter;
        request.script = script;
        request.arguments = mlx_converter_arguments(precision, input, output, converter);
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
        // A type the converter knows no model for: its own lists read now,
        // at failure time, to name the way out (M15).
        const std::string error = fold_name_list(outcome.describe());
        if (converter != MlxConverterKind::MlxLm &&
            (error.find(kUnknownRoute) != std::string::npos ||
             error.find(kUnknownChoice) != std::string::npos)) {
            // A seeded driver from before the route asked for: 27b's knows
            // no `--with` (53f), 53f's no mlx-embeddings (53h).
            return error + " -- the seeded converter at " + script.string() +
                   " is older than this build: 'apogee check --fix' brings an earlier Apogee's "
                   "up to this build's, and an edited one is kept, so delete it first";
        }
        return unsupported_model_type(error).empty()
                   ? error
                   : fold_model_type_failure(error, mlx_model_types(package), converter);
    };
}

std::vector<std::string> mlx_model_types(const std::filesystem::path& package) {
    std::vector<std::string> out;
    std::error_code code;
    if (package.empty() || !std::filesystem::is_directory(package, code)) {
        return out;
    }
    for (const auto& entry : std::filesystem::directory_iterator(package / "models", code)) {
        const std::filesystem::path& path = entry.path();
        const std::string stem = path.stem().string();
        if (stem.empty() || stem.front() == '_') {
            continue;
        }
        std::error_code type;
        if (entry.is_directory(type)) {
            // mlx-vlm's types are packages (53f): its loader imports
            // `models.<type>` and takes its `Model`, which the package's
            // `__init__.py` names; a helper package names none.
            if (names_model(read_text(path / "__init__.py"))) {
                out.push_back(path.filename().string());
            }
            continue;
        }
        // mlx-lm's are modules: a model's defines its `Model`; a helper
        // (`cache`, `base`) does not, and is no type.
        if (path.extension() == ".py" &&
            read_text(path).find("class Model(") != std::string::npos) {
            out.push_back(stem);
        }
    }
    // MODEL_REMAPPING's keys: `"<type>": "<module>"` pairs inside its braces.
    const std::string text = read_text(package / "utils.py");
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

std::string fold_model_type_failure(const std::string& error, const std::vector<std::string>& types,
                                    MlxConverterKind converter) {
    const std::string wanted = unsupported_model_type(error);
    if (wanted.empty()) {
        return error;
    }
    std::string near;
    for (const std::string& type : nearest_model_types(wanted, types)) {
        near += (near.empty() ? "" : ", ") + type;
    }
    return error + " -- if these weights are a type this " + std::string{to_string(converter)} +
           " knows, convert them as it: --model-type <type>" +
           (near.empty() ? std::string{} : " (nearest here: " + near + ")");
}

}  // namespace apogee::training
