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
/// **Prefixes** (42b): a kit whose prompts begin with a fixed text -- the
/// product's own instruction for a chore, or a system prompt carried into
/// the turn -- names each such text once under `prefixes:`, and every
/// prompt, eval item and synthesised example alike, begins with one: an
/// eval item says which by `prefix:`, and the teacher names one per example
/// and writes only what follows it. The text is kept byte for byte, never
/// copied into each prompt or asked of the teacher.
namespace apogee::training {

inline constexpr int kDefaultSynthCount = 200;
inline constexpr int kDefaultPerSeed = 8;
inline constexpr double kDefaultSynthTemperature = 0.9;

struct EvalItem {
    /// The whole prompt, its prefix's text included.
    std::string prompt;
    /// A substring the answer must contain; empty means a judge decides.
    std::string expected;
};

/// A fixed text a kit's prompts begin with, by name.
struct KitPrefix {
    std::string name;
    std::string text;
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
    /// In the order the kit declares them; empty for most kits.
    std::vector<KitPrefix> prefixes;
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

/// The prefix of `kit` named `name`, or nullptr.
[[nodiscard]] const KitPrefix* find_prefix(const Kit& kit, std::string_view name) noexcept;

/// The rule a kit must satisfy: a non-blank `synth.system`, at least one
/// eval item (it gates the trained model), every prompt non-blank, sane
/// numbers. Empty when valid. (That every eval item of a kit with prefixes
/// begins with one is the parser's rule.)
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
