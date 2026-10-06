#include "symphony/definition.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>

#include "agentloop/structured.h"
#include "contracts/assets.h"
#include "contracts/paths.h"

namespace apogee::symphony {
namespace {

constexpr std::string_view kOpen = "{{";
constexpr std::string_view kClose = "}}";

bool same_folded(std::string_view lhs, std::string_view rhs) {
    return lhs.size() == rhs.size() &&
           std::equal(lhs.begin(), lhs.end(), rhs.begin(), [](char a, char b) {
               return std::tolower(static_cast<unsigned char>(a)) ==
                      std::tolower(static_cast<unsigned char>(b));
           });
}

std::string trimmed(std::string_view text) {
    const std::size_t begin = text.find_first_not_of(" \t");
    if (begin == std::string_view::npos) {
        return {};
    }
    const std::size_t end = text.find_last_not_of(" \t");
    return std::string{text.substr(begin, end - begin + 1)};
}

std::optional<std::string> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

/// A spec file read through the one parser, or why not.
struct Read {
    std::optional<harness::SymphonySpec> spec;
    std::string error;
};

Read read_spec(const std::filesystem::path& path, std::string_view fallback_name,
               const std::vector<std::string>& backends) {
    Read out;
    const std::optional<std::string> text = read_file(path);
    if (!text.has_value()) {
        out.error = path.string() + ": cannot be read";
        return out;
    }
    try {
        out.spec = harness::parse_symphony_spec(*text, path.string(), fallback_name, backends);
    } catch (const harness::ConfigError& e) {
        out.error = e.what();
    }
    return out;
}

/// A file under the symphonies directory is played by its name: one that
/// calls itself something else would answer to a name it does not show.
Read read_named(const std::filesystem::path& path, const std::string& name,
                const std::vector<std::string>& backends) {
    Read out = read_spec(path, name, backends);
    if (out.spec.has_value() && !same_folded(out.spec->name, name)) {
        out.error = path.string() + ": names itself '" + out.spec->name +
                    "' -- a file under symphonies/ plays by its file's name ('" + name +
                    "'); make the two agree";
        out.spec.reset();
    }
    return out;
}

/// The spec files under `dir`, by stem, sorted -- starters' among them.
std::vector<std::pair<std::string, std::filesystem::path>> spec_files(
    const std::filesystem::path& dir) {
    std::vector<std::pair<std::string, std::filesystem::path>> out;
    std::error_code code;
    if (!std::filesystem::is_directory(dir, code)) {
        return out;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir, code)) {
        const std::filesystem::path& path = entry.path();
        const std::string extension = path.extension().string();
        if ((extension != ".yaml" && extension != ".yml") || !entry.is_regular_file(code)) {
            continue;
        }
        out.emplace_back(path.stem().string(), path);
    }
    std::ranges::sort(out, [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
    return out;
}

bool looks_like_path(std::string_view text) {
    const auto ends_with = [&](std::string_view suffix) {
        return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
    };
    return text.find('/') != std::string_view::npos || text.find('\\') != std::string_view::npos ||
           ends_with(".yaml") || ends_with(".yml");
}

}  // namespace

std::string_view to_string(Source source) noexcept {
    switch (source) {
        case Source::Shipped:
            return "shipped";
        case Source::Config:
            return "config";
        case Source::File:
            return "file";
    }
    return "config";
}

std::vector<Segment> parse_template(std::string_view text, std::string& error) {
    std::vector<Segment> out;
    std::string literal;
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t open = text.find(kOpen, at);
        if (open == std::string_view::npos) {
            literal += text.substr(at);
            break;
        }
        literal += text.substr(at, open - at);
        const std::size_t close = text.find(kClose, open + kOpen.size());
        if (close == std::string_view::npos) {
            error = "an unclosed '{{' -- a variable is {{input}} or {{<an earlier stage>}}";
            return {};
        }
        const std::string name = trimmed(text.substr(open + kOpen.size(), close - open - 2));
        if (name.empty() || !harness::is_symphony_name(name)) {
            error = "'{{" + std::string{text.substr(open + kOpen.size(), close - open - 2)} +
                    "}}' is not a variable -- a variable is {{input}} or {{<an earlier stage>}}, "
                    "and a prompt has no other use for '{{'";
            return {};
        }
        if (!literal.empty()) {
            out.push_back(Segment{.variable = false, .text = std::move(literal)});
            literal.clear();
        }
        out.push_back(Segment{.variable = true, .text = name});
        at = close + kClose.size();
    }
    if (!literal.empty()) {
        out.push_back(Segment{.variable = false, .text = std::move(literal)});
    }
    return out;
}

std::string render_template(std::string_view text,
                            const std::map<std::string, std::string>& values) {
    std::string error;
    const std::vector<Segment> segments = parse_template(text, error);
    if (!error.empty()) {
        return std::string{text};
    }
    std::string out;
    for (const Segment& segment : segments) {
        if (!segment.variable) {
            out += segment.text;
            continue;
        }
        const auto value = std::ranges::find_if(
            values, [&](const auto& entry) { return same_folded(entry.first, segment.text); });
        out += value == values.end() ? "{{" + segment.text + "}}" : value->second;
    }
    return out;
}

std::vector<std::string> template_variables(std::string_view text) {
    std::string error;
    std::vector<std::string> out;
    for (const Segment& segment : parse_template(text, error)) {
        if (segment.variable && std::ranges::find(out, segment.text) == out.end()) {
            out.push_back(segment.text);
        }
    }
    return out;
}

namespace {

/// What is wrong with stage `index`'s template; `reads_input` set when it
/// names the input.
void check_template(const harness::SymphonySpec& spec, std::size_t index, const std::string& at,
                    std::vector<std::string>& problems, bool& reads_input) {
    std::string error;
    const std::vector<Segment> segments = parse_template(spec.stages[index].prompt, error);
    if (!error.empty()) {
        problems.push_back(at + ": " + error);
    }
    for (const Segment& segment : segments) {
        if (!segment.variable) {
            continue;
        }
        if (same_folded(segment.text, kInputVariable)) {
            reads_input = true;
            continue;
        }
        const auto named = std::ranges::find_if(
            spec.stages, [&](const auto& other) { return same_folded(other.name, segment.text); });
        std::string problem = at + ": {{" + segment.text + "}}";
        if (named == spec.stages.end()) {
            problem += " names no stage -- a variable is {{input}} or {{<an earlier stage>}}";
        } else if (const auto position =
                       static_cast<std::size_t>(std::distance(spec.stages.begin(), named));
                   position == index) {
            problem += " is this stage's own answer, which it has not given yet";
        } else if (position > index) {
            problem += " is stage " + std::to_string(position + 1) +
                       "'s answer, not yet played when stage " + std::to_string(index + 1) +
                       " runs";
        } else {
            continue;
        }
        problems.push_back(std::move(problem));
    }
}

/// What is wrong with a stage's schema, or empty.
std::string schema_problem(const std::string& text) {
    const nlohmann::json schema = nlohmann::json::parse(text, nullptr, false);
    if (schema.is_discarded() || !schema.is_object()) {
        return "its schema is not a JSON object";
    }
    if (const agentloop::ValidationResult checked = agentloop::validate_schema(schema);
        !checked.ok) {
        return "its schema is not a valid JSON Schema -- " +
               (checked.errors.empty() ? std::string{} : checked.errors.front());
    }
    return {};
}

}  // namespace

std::vector<std::string> validate(const harness::SymphonySpec& spec) {
    std::vector<std::string> problems;
    bool reads_input = false;
    for (std::size_t index = 0; index < spec.stages.size(); ++index) {
        const harness::SymphonyStage& stage = spec.stages[index];
        std::string at = "stage ";
        at += std::to_string(index + 1);
        at += " (" + stage.name + ")";
        check_template(spec, index, at, problems, reads_input);
        if (!stage.schema.empty()) {
            if (const std::string problem = schema_problem(stage.schema); !problem.empty()) {
                problems.push_back(at);
                problems.back() += ": ";
                problems.back() += problem;
            }
        }
    }
    const bool takes_image = std::ranges::any_of(
        spec.stages, [](const harness::SymphonyStage& stage) { return stage.image; });
    if (!reads_input && !takes_image) {
        problems.emplace_back(
            "no stage reads the input -- name {{input}} in a prompt, or the play's input is "
            "thrown away");
    }
    if (spec.input.image && !takes_image) {
        problems.emplace_back(
            "the input takes an image and no stage is given it -- mark the stage that reads it "
            "'image: true'");
    }
    return problems;
}

Catalog catalog(const harness::Config& config, const std::filesystem::path& dir) {
    Catalog out;
    const std::vector<std::string> backends = config.backend_names();
    const std::vector<std::pair<std::string, std::filesystem::path>> files = spec_files(dir);
    const auto file_named = [&](std::string_view name) -> const std::filesystem::path* {
        const auto found = std::ranges::find_if(
            files, [&](const auto& entry) { return same_folded(entry.first, name); });
        return found == files.end() ? nullptr : &found->second;
    };
    std::set<std::string> taken;  // folded names already listed
    const auto fold = [](std::string_view name) {
        std::string folded{name};
        std::ranges::transform(folded, folded.begin(), [](char c) {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        });
        return folded;
    };
    const auto entry_for = [&](std::string_view name) -> std::optional<Definition> {
        const harness::SymphonySpec* entry = config.find_symphony(name);
        if (entry == nullptr) {
            return std::nullopt;
        }
        Definition definition;
        definition.spec = *entry;
        definition.source = Source::Config;
        definition.overrides = true;
        return definition;
    };

    // The starters first, each in its place even when an entry stands in.
    for (const harness::BundledSymphony& starter : harness::bundled_symphonies()) {
        const std::string name{starter.name};
        taken.insert(fold(name));
        if (std::optional<Definition> entry = entry_for(name); entry.has_value()) {
            out.definitions.push_back(std::move(*entry));
            continue;
        }
        Definition definition;
        definition.source = Source::Shipped;
        if (const std::filesystem::path* seeded = file_named(name); seeded != nullptr) {
            Read read = read_named(*seeded, name, backends);
            if (!read.spec.has_value()) {
                out.problems.push_back(read.error);
                continue;
            }
            definition.spec = std::move(*read.spec);
            definition.path = *seeded;
        } else {
            try {
                definition.spec = harness::parse_symphony_spec(
                    starter.text, "<shipped symphony '" + name + "'>", name, backends);
            } catch (const harness::ConfigError& e) {
                out.problems.emplace_back(e.what());
                continue;
            }
        }
        out.definitions.push_back(std::move(definition));
    }
    // Then the config's own, an entry standing in for a spec file marked so.
    for (const auto& [name, spec] : config.symphonies) {
        if (taken.contains(fold(name))) {
            continue;
        }
        taken.insert(fold(name));
        Definition definition;
        definition.spec = spec;
        definition.source = Source::Config;
        definition.overrides = file_named(name) != nullptr;
        out.definitions.push_back(std::move(definition));
    }
    // Then the user's spec files under the directory.
    for (const auto& [name, path] : files) {
        if (taken.contains(fold(name))) {
            continue;
        }
        taken.insert(fold(name));
        Read read = read_named(path, name, backends);
        if (!read.spec.has_value()) {
            out.problems.push_back(read.error);
            continue;
        }
        Definition definition;
        definition.spec = std::move(*read.spec);
        definition.source = Source::File;
        definition.path = path;
        out.definitions.push_back(std::move(definition));
    }
    return out;
}

Found find_definition(const harness::Config& config, const std::filesystem::path& dir,
                      std::string_view name_or_path) {
    Found out;
    const std::string wanted = trimmed(name_or_path);
    if (wanted.empty()) {
        out.error = "a symphony's name or a spec file's path is required";
        return out;
    }
    std::error_code code;
    if (looks_like_path(wanted)) {
        const std::filesystem::path path = harness::expand_env_and_home(wanted);
        if (!std::filesystem::is_regular_file(path, code)) {
            out.error = "no spec file at " + path.string();
            return out;
        }
        Read read = read_spec(path, path.stem().string(), config.backend_names());
        if (!read.spec.has_value()) {
            out.error = read.error;
            return out;
        }
        Definition definition;
        definition.spec = std::move(*read.spec);
        definition.source = Source::File;
        definition.path = path;
        out.definition = std::move(definition);
        return out;
    }
    Catalog all = catalog(config, dir);
    for (Definition& definition : all.definitions) {
        if (same_folded(definition.spec.name, wanted)) {
            out.definition = std::move(definition);
            return out;
        }
    }
    // A broken file of that name says why, rather than "no such symphony".
    for (const std::string& problem : all.problems) {
        const std::filesystem::path file = dir / (wanted + ".yaml");
        if (problem.starts_with(file.string()) ||
            problem.find("'" + wanted + "'") != std::string::npos) {
            out.error = problem;
            return out;
        }
    }
    std::string known;
    for (const Definition& definition : all.definitions) {
        known += known.empty() ? "" : ", ";
        known += definition.spec.name;
    }
    out.error = "no symphony named '" + wanted + "'" +
                (known.empty() ? std::string{} : " (there are: " + known + ")");
    return out;
}

std::filesystem::path directory_for(const std::filesystem::path& config_path) {
    return harness::home_for_config(config_path) / "symphonies";
}

std::string role_chain(const harness::SymphonySpec& spec) {
    std::string out;
    for (const harness::SymphonyStage& stage : spec.stages) {
        out += out.empty() ? "" : " → ";
        out += stage.role;
    }
    return out;
}

}  // namespace apogee::symphony
