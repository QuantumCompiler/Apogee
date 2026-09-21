#include "training/kit.h"

#include <nlohmann/json.hpp>

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <system_error>

namespace apogee::training {
namespace {

[[noreturn]] void fail(std::string_view origin, std::string_view message) {
    throw std::runtime_error(std::string{origin} + ": " + std::string{message});
}

std::string scalar(const YAML::Node& node, std::string_view origin, std::string_view key) {
    if (!node.IsDefined() || node.IsNull()) {
        return {};
    }
    if (!node.IsScalar()) {
        fail(origin, std::string{key} + ": expected a scalar");
    }
    return node.Scalar();
}

int integer(const YAML::Node& node, std::string_view origin, std::string_view key, int fallback) {
    if (!node.IsDefined() || node.IsNull()) {
        return fallback;
    }
    try {
        return node.as<int>();
    } catch (const YAML::Exception&) {
        fail(origin, std::string{key} + ": expected an integer");
    }
}

double number(const YAML::Node& node, std::string_view origin, std::string_view key,
              double fallback) {
    if (!node.IsDefined() || node.IsNull()) {
        return fallback;
    }
    try {
        return node.as<double>();
    } catch (const YAML::Exception&) {
        fail(origin, std::string{key} + ": expected a number");
    }
}

bool blank(std::string_view text) {
    return std::ranges::all_of(text, [](unsigned char c) { return std::isspace(c) != 0; });
}

}  // namespace

Kit parse_kit(std::string_view text, std::string_view origin) {
    YAML::Node root;
    try {
        root = YAML::Load(std::string{text});
    } catch (const YAML::Exception& e) {
        fail(origin, std::string{"not valid YAML -- "} + e.what());
    }
    if (!root.IsDefined() || root.IsNull()) {
        fail(origin, "the kit is empty");
    }
    if (!root.IsMap()) {
        fail(origin, "expected a mapping at the top level");
    }

    Kit kit;
    kit.name = scalar(root["name"], origin, "name");
    kit.description = scalar(root["description"], origin, "description");
    kit.skill = scalar(root["skill"], origin, "skill");

    if (const YAML::Node synth = root["synth"]; synth.IsDefined() && !synth.IsNull()) {
        if (!synth.IsMap()) {
            fail(origin, "synth: expected a mapping");
        }
        kit.synth.system = scalar(synth["system"], origin, "synth.system");
        if (const YAML::Node seeds = synth["seeds"]; seeds.IsDefined() && !seeds.IsNull()) {
            if (!seeds.IsSequence()) {
                fail(origin, "synth.seeds: expected a list");
            }
            for (const YAML::Node& seed : seeds) {
                kit.synth.seeds.push_back(scalar(seed, origin, "synth.seeds"));
            }
        }
        kit.synth.count = integer(synth["count"], origin, "synth.count", kDefaultSynthCount);
        kit.synth.per_seed = integer(synth["per_seed"], origin, "synth.per_seed", kDefaultPerSeed);
        kit.synth.temperature =
            number(synth["temperature"], origin, "synth.temperature", kDefaultSynthTemperature);
    }

    if (const YAML::Node train = root["train"]; train.IsDefined() && !train.IsNull()) {
        if (!train.IsMap()) {
            fail(origin, "train: expected a mapping");
        }
        kit.train.iters = integer(train["iters"], origin, "train.iters", 0);
        kit.train.batch_size = integer(train["batch_size"], origin, "train.batch_size", 0);
        kit.train.num_layers = integer(train["num_layers"], origin, "train.num_layers", 0);
    }

    if (const YAML::Node eval = root["eval"]; eval.IsDefined() && !eval.IsNull()) {
        if (!eval.IsSequence()) {
            fail(origin, "eval: expected a list of {prompt, expected} items");
        }
        for (const YAML::Node& item : eval) {
            if (!item.IsMap()) {
                fail(origin, "eval: each item must be a mapping with prompt and expected");
            }
            EvalItem parsed;
            parsed.prompt = scalar(item["prompt"], origin, "eval.prompt");
            parsed.expected = scalar(item["expected"], origin, "eval.expected");
            kit.eval.push_back(std::move(parsed));
        }
    }
    return kit;
}

Kit load_kit(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        throw std::runtime_error("could not read kit " + path.string());
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    Kit kit = parse_kit(text, path.string());
    if (kit.name.empty()) {
        kit.name = path.stem().string();
    }
    kit.path = path;
    return kit;
}

std::string validate_kit(const Kit& kit) {
    if (kit.name.empty()) {
        return "the kit has no name";
    }
    if (blank(kit.synth.system)) {
        return "synth.system is required (the teacher's generation prompt)";
    }
    if (kit.eval.empty()) {
        return "at least one eval item is required (it gates the trained model)";
    }
    for (std::size_t i = 0; i < kit.eval.size(); ++i) {
        if (blank(kit.eval[i].prompt)) {
            return "eval item " + std::to_string(i + 1) + " has an empty prompt";
        }
    }
    if (kit.synth.count <= 0) {
        return "synth.count must be positive";
    }
    if (kit.synth.per_seed <= 0) {
        return "synth.per_seed must be positive";
    }
    if (kit.synth.temperature < 0.0 || kit.synth.temperature > 2.0) {
        return "synth.temperature must be between 0 and 2";
    }
    return {};
}

std::vector<KitSummary> list_kits(const std::filesystem::path& dir) {
    std::vector<KitSummary> kits;
    std::error_code code;
    if (!std::filesystem::is_directory(dir, code)) {
        return kits;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir, code)) {
        if (code) {
            break;
        }
        if (!entry.is_regular_file(code) ||
            (entry.path().extension() != ".yaml" && entry.path().extension() != ".yml")) {
            continue;
        }
        KitSummary summary;
        summary.name = entry.path().stem().string();
        summary.path = entry.path();
        try {
            const Kit kit = load_kit(entry.path());
            summary.description = kit.description;
            summary.eval_items = kit.eval.size();
            summary.error = validate_kit(kit);
        } catch (const std::exception& e) {
            summary.error = e.what();
        }
        kits.push_back(std::move(summary));
    }
    std::ranges::sort(kits,
                      [](const KitSummary& a, const KitSummary& b) { return a.name < b.name; });
    return kits;
}

std::optional<std::filesystem::path> find_kit(const std::filesystem::path& dir,
                                              std::string_view name_or_path) {
    std::error_code code;
    const std::filesystem::path as_path{std::string{name_or_path}};
    if ((as_path.has_parent_path() || as_path.extension() == ".yaml" ||
         as_path.extension() == ".yml") &&
        std::filesystem::is_regular_file(as_path, code)) {
        return as_path;
    }
    for (const char* extension : {".yaml", ".yml"}) {
        const std::filesystem::path candidate = dir / (std::string{name_or_path} + extension);
        if (std::filesystem::is_regular_file(candidate, code)) {
            return candidate;
        }
    }
    return std::nullopt;
}

std::string eval_suite_text(const Kit& kit) {
    std::string out;
    for (const EvalItem& item : kit.eval) {
        nlohmann::ordered_json line{{"prompt", item.prompt}, {"expected", item.expected}};
        out += line.dump();
        out += '\n';
    }
    return out;
}

void write_eval_suite(const Kit& kit, const std::filesystem::path& path) {
    std::error_code code;
    std::filesystem::create_directories(path.parent_path(), code);
    std::ofstream out{path, std::ios::binary};
    if (!out) {
        throw std::runtime_error("could not write " + path.string());
    }
    out << eval_suite_text(kit);
}

}  // namespace apogee::training
