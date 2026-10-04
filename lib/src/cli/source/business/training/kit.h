#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// Training kits: one YAML file per skill, bundling a teacher synthesis
/// spec (the generation system prompt, seed topics that diversify batches,
/// the default example count, the sampling temperature) and an inline eval
/// suite (`{prompt, expected}` items) that gates a model trained on that
/// skill. Four ship compiled in and are seeded under `training/kits/`; a
/// user drops a `<name>.yaml` of the same shape beside them.
///
/// The format is Ommi's, byte for byte in the bundled four, so a kit written
/// for either project runs on both.
namespace apogee::training {

inline constexpr int kDefaultSynthCount = 200;
inline constexpr int kDefaultPerSeed = 8;
inline constexpr double kDefaultSynthTemperature = 0.9;

struct EvalItem {
    std::string prompt;
    /// A substring the answer must contain; empty means a judge decides.
    std::string expected;
};

struct KitSynthSpec {
    /// The teacher's generation system prompt. Required.
    std::string system;
    /// Topic hints, cycled batch by batch for diversity.
    std::vector<std::string> seeds;
    int count = kDefaultSynthCount;
    /// Examples asked of the teacher per call.
    int per_seed = kDefaultPerSeed;
    double temperature = kDefaultSynthTemperature;
};

/// Per-stage training defaults, read by the pipeline item. Zero means unset.
struct KitTrainDefaults {
    int iters = 0;
    int batch_size = 0;
    int num_layers = 0;
};

struct Kit {
    std::string name;
    std::string description;
    std::string skill;
    KitSynthSpec synth;
    KitTrainDefaults train;
    std::vector<EvalItem> eval;
    /// Where it was loaded from; empty for a parsed text.
    std::filesystem::path path;
};

/// Parses a kit. `origin` names the source in errors. The name defaults to
/// nothing -- `load_kit` fills it from the file stem. Throws
/// std::runtime_error naming what is wrong.
[[nodiscard]] Kit parse_kit(std::string_view text, std::string_view origin);

/// `parse_kit` over a file, the name defaulting to the file stem.
[[nodiscard]] Kit load_kit(const std::filesystem::path& path);

/// The rule a kit must satisfy: a non-blank `synth.system`, at least one
/// eval item (it gates the trained model), every prompt non-blank, sane
/// numbers. Empty when valid.
[[nodiscard]] std::string validate_kit(const Kit& kit);

struct KitSummary {
    std::string name;
    std::filesystem::path path;
    std::string description;
    std::size_t eval_items = 0;
    /// Why it did not load or validate, or empty.
    std::string error;
};

/// Every `*.yaml` under `dir`, sorted by name. A missing directory lists as
/// empty.
[[nodiscard]] std::vector<KitSummary> list_kits(const std::filesystem::path& dir);

/// A kit by name under `dir`, or by path when `name_or_path` names a file.
[[nodiscard]] std::optional<std::filesystem::path> find_kit(const std::filesystem::path& dir,
                                                            std::string_view name_or_path);

/// The inline eval suite as JSONL text -- one `{"prompt", "expected"}` per
/// line, the shape `apogee train eval --suite` reads.
[[nodiscard]] std::string eval_suite_text(const Kit& kit);

/// Writes `eval_suite_text` to `path`, creating the directory.
void write_eval_suite(const Kit& kit, const std::filesystem::path& path);

}  // namespace apogee::training
