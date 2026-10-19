#include "contracts/review_skill.h"

#include <nlohmann/json.hpp>

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <system_error>

#include "contracts/assets.h"

namespace apogee::harness {
namespace {

constexpr std::array<std::string_view, 2> kC{".c", ".h"};
constexpr std::array<std::string_view, 9> kCpp{".cc",  ".cpp", ".cxx", ".c++", ".hh",
                                               ".hpp", ".hxx", ".h++", ".ipp"};
constexpr std::array<std::string_view, 1> kCSharp{".cs"};
constexpr std::array<std::string_view, 1> kGo{".go"};
constexpr std::array<std::string_view, 1> kJava{".java"};
constexpr std::array<std::string_view, 4> kJavaScript{".js", ".jsx", ".mjs", ".cjs"};
constexpr std::array<std::string_view, 2> kKotlin{".kt", ".kts"};
constexpr std::array<std::string_view, 1> kPhp{".php"};
constexpr std::array<std::string_view, 2> kPython{".py", ".pyi"};
constexpr std::array<std::string_view, 1> kRuby{".rb"};
constexpr std::array<std::string_view, 1> kRust{".rs"};
constexpr std::array<std::string_view, 2> kScala{".scala", ".sc"};
constexpr std::array<std::string_view, 3> kShell{".sh", ".bash", ".zsh"};
constexpr std::array<std::string_view, 1> kSql{".sql"};
constexpr std::array<std::string_view, 1> kSwift{".swift"};
constexpr std::array<std::string_view, 4> kTypeScript{".ts", ".tsx", ".mts", ".cts"};

constexpr std::array<ReviewLanguage, 16> kLanguages{{
    {"c", kC},
    {"cpp", kCpp},
    {"csharp", kCSharp},
    {"go", kGo},
    {"java", kJava},
    {"javascript", kJavaScript},
    {"kotlin", kKotlin},
    {"php", kPhp},
    {"python", kPython},
    {"ruby", kRuby},
    {"rust", kRust},
    {"scala", kScala},
    {"shell", kShell},
    {"sql", kSql},
    {"swift", kSwift},
    {"typescript", kTypeScript},
}};

[[nodiscard]] const ReviewLanguage* find_language(std::string_view name) {
    for (const ReviewLanguage& language : kLanguages) {
        if (language.name == name) {
            return &language;
        }
    }
    return nullptr;
}

[[nodiscard]] std::string lowered(std::string_view text) {
    std::string out{text};
    std::ranges::transform(out, out.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

[[noreturn]] void refuse(std::string_view origin, const std::string& why) {
    throw std::runtime_error(std::string{origin} + ": " + why);
}

/// A YAML list of strings at `node`, or a refusal naming `where`.
[[nodiscard]] std::vector<std::string> strings_at(const YAML::Node& node, std::string_view origin,
                                                  std::string_view where) {
    std::vector<std::string> out;
    if (!node || node.IsNull()) {
        return out;
    }
    if (!node.IsSequence()) {
        refuse(origin, std::string{where} + " is a list of strings");
    }
    for (const YAML::Node& item : node) {
        if (!item.IsScalar() || item.Scalar().empty()) {
            refuse(origin, std::string{where} + " is a list of strings, none empty");
        }
        out.push_back(item.Scalar());
    }
    return out;
}

/// The extension of `path`'s last segment, lowercase with its dot; empty
/// for none.
[[nodiscard]] std::string extension_of(std::string_view path) {
    const std::size_t slash = path.find_last_of('/');
    const std::string_view leaf = slash == std::string_view::npos ? path : path.substr(slash + 1);
    const std::size_t dot = leaf.find_last_of('.');
    if (dot == std::string_view::npos || dot == 0) {
        return {};
    }
    return lowered(leaf.substr(dot));
}

[[nodiscard]] std::optional<std::string> read_file(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        return std::nullopt;
    }
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// Every `.md` file directly under `dir`, by its name without `.md`.
[[nodiscard]] std::map<std::string, std::filesystem::path> skill_files(
    const std::filesystem::path& dir) {
    std::map<std::string, std::filesystem::path> files;
    std::error_code code;
    for (std::filesystem::directory_iterator it{dir, code};
         !code && it != std::filesystem::directory_iterator{}; it.increment(code)) {
        if (it->path().extension() == ".md" && it->is_regular_file(code)) {
            files.emplace(it->path().stem().string(), it->path());
        }
    }
    return files;
}

[[nodiscard]] nlohmann::json fields(const ReviewSkillEntry& entry) {
    const ReviewSkill& skill = entry.skill;
    nlohmann::json out{
        {"name", skill.name},
        {"description", skill.description},
        {"source", to_string(entry.source)},
        {"applies",
         {{"extensions", skill.applies.extensions},
          {"paths", skill.applies.paths},
          {"languages", skill.applies.languages}}},
        {"everywhere", skill.applies.everywhere()},
    };
    if (!entry.path.empty()) {
        out["path"] = entry.path.string();
    }
    return out;
}

}  // namespace

std::span<const ReviewLanguage> review_skill_languages() noexcept {
    return kLanguages;
}

std::string_view to_string(ReviewSkillSource source) noexcept {
    switch (source) {
        case ReviewSkillSource::Shipped:
            return "shipped";
        case ReviewSkillSource::Seeded:
            return "seeded";
        case ReviewSkillSource::User:
            return "user";
    }
    return "user";
}

ReviewSkill parse_review_skill(std::string_view text, std::string_view origin,
                               std::string_view name) {
    // The front matter: a `---` line first, its closing `---` line, then
    // the prompt.
    const std::string_view opening = text.starts_with("---\r\n") ? "---\r\n" : "---\n";
    if (!text.starts_with(opening)) {
        refuse(origin, "a review skill opens with a --- front matter holding its description");
    }
    const std::string_view rest = text.substr(opening.size());
    std::size_t close = std::string_view::npos;
    for (std::size_t at = 0; at < rest.size();) {
        const std::size_t end = rest.find('\n', at);
        std::string_view line =
            rest.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at);
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        if (line == "---") {
            close = at;
            break;
        }
        if (end == std::string_view::npos) {
            break;
        }
        at = end + 1;
    }
    if (close == std::string_view::npos) {
        refuse(origin, "the front matter is never closed with a --- line");
    }
    const std::string front{rest.substr(0, close)};
    const std::size_t body_start = rest.find('\n', close);
    std::string_view body =
        body_start == std::string_view::npos ? std::string_view{} : rest.substr(body_start + 1);
    while (!body.empty() && std::isspace(static_cast<unsigned char>(body.front())) != 0) {
        body.remove_prefix(1);
    }
    while (!body.empty() && std::isspace(static_cast<unsigned char>(body.back())) != 0) {
        body.remove_suffix(1);
    }

    YAML::Node root;
    try {
        root = YAML::Load(front);
    } catch (const YAML::Exception& e) {
        refuse(origin, std::string{"the front matter is not YAML: "} + e.what());
    }
    if (!root.IsMap()) {
        refuse(origin, "the front matter is a map: description, and applies if any");
    }
    ReviewSkill skill;
    skill.name = std::string{name};
    for (const auto& item : root) {
        const std::string key = item.first.as<std::string>();
        if (key != "name" && key != "description" && key != "applies") {
            refuse(origin, "unknown key '" + key + "' (a skill's are name, description, applies)");
        }
    }
    if (const YAML::Node named = root["name"]; named && !named.IsNull()) {
        if (!named.IsScalar() || named.Scalar() != name) {
            refuse(origin, "name: says '" + (named.IsScalar() ? named.Scalar() : std::string{}) +
                               "', and the file is '" + std::string{name} + "'");
        }
    }
    const YAML::Node description = root["description"];
    if (!description || !description.IsScalar() || description.Scalar().empty()) {
        refuse(origin, "description: is missing -- one line saying what the lens hunts");
    }
    skill.description = description.Scalar();

    if (const YAML::Node applies = root["applies"]; applies && !applies.IsNull()) {
        if (!applies.IsMap()) {
            refuse(origin, "applies: is a map of extensions, paths and languages");
        }
        for (const auto& item : applies) {
            const std::string key = item.first.as<std::string>();
            if (key != "extensions" && key != "paths" && key != "languages") {
                refuse(origin,
                       "unknown applies key '" + key + "' (they are extensions, paths, languages)");
            }
        }
        for (const std::string& extension :
             strings_at(applies["extensions"], origin, "applies.extensions")) {
            if (!extension.starts_with('.') || extension.size() < 2) {
                refuse(origin, "applies.extensions: '" + extension +
                                   "' is an extension with its dot (.py)");
            }
            skill.applies.extensions.push_back(lowered(extension));
        }
        for (const std::string& glob : strings_at(applies["paths"], origin, "applies.paths")) {
            if (glob.starts_with('/') || glob.find("..") != std::string::npos ||
                glob.find('\\') != std::string::npos) {
                refuse(origin, "applies.paths: '" + glob +
                                   "' is a glob relative to the repository, with / and no ..");
            }
            skill.applies.paths.push_back(glob);
        }
        for (const std::string& language :
             strings_at(applies["languages"], origin, "applies.languages")) {
            if (find_language(language) == nullptr) {
                std::string known;
                for (const ReviewLanguage& each : kLanguages) {
                    known += (known.empty() ? "" : ", ") + std::string{each.name};
                }
                refuse(origin, "applies.languages: '" + language +
                                   "' is not a language a skill can name (" + known + ")");
            }
            skill.applies.languages.push_back(language);
        }
    }
    if (body.empty()) {
        refuse(origin, "the skill has no prompt below its front matter");
    }
    skill.prompt = std::string{body};
    return skill;
}

bool review_glob_match(std::string_view glob, std::string_view path) {
    if (glob.empty()) {
        return path.empty();
    }
    if (glob.starts_with("**")) {
        std::string_view rest = glob.substr(2);
        if (rest.starts_with('/')) {
            // `**/` is zero or more whole segments.
            rest.remove_prefix(1);
            if (review_glob_match(rest, path)) {
                return true;
            }
            for (std::size_t at = 0; at < path.size(); ++at) {
                if (path[at] == '/' && review_glob_match(rest, path.substr(at + 1))) {
                    return true;
                }
            }
            return false;
        }
        for (std::size_t at = 0; at <= path.size(); ++at) {
            if (review_glob_match(rest, path.substr(at))) {
                return true;
            }
        }
        return false;
    }
    if (glob.front() == '*') {
        for (std::size_t at = 0; at <= path.size(); ++at) {
            if (review_glob_match(glob.substr(1), path.substr(at))) {
                return true;
            }
            if (at < path.size() && path[at] == '/') {
                return false;  // one star never crosses a segment
            }
        }
        return false;
    }
    if (path.empty()) {
        return false;
    }
    if (glob.front() == '?') {
        return path.front() != '/' && review_glob_match(glob.substr(1), path.substr(1));
    }
    return glob.front() == path.front() && review_glob_match(glob.substr(1), path.substr(1));
}

bool skill_applies(const ReviewSkill& skill, std::string_view path) {
    if (skill.applies.everywhere()) {
        return true;
    }
    const std::string extension = extension_of(path);
    if (!extension.empty()) {
        if (std::ranges::find(skill.applies.extensions, extension) !=
            skill.applies.extensions.end()) {
            return true;
        }
        for (const std::string& name : skill.applies.languages) {
            if (const ReviewLanguage* language = find_language(name);
                language != nullptr &&
                std::ranges::find(language->extensions, extension) != language->extensions.end()) {
                return true;
            }
        }
    }
    return std::ranges::any_of(skill.applies.paths, [path](const std::string& glob) {
        return review_glob_match(glob, path);
    });
}

const ReviewSkillEntry* ReviewSkillBook::find(std::string_view name) const noexcept {
    for (const ReviewSkillEntry& entry : skills) {
        if (entry.skill.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

std::string ReviewSkillBook::names() const {
    std::string out;
    for (const ReviewSkillEntry& entry : skills) {
        out += (out.empty() ? "" : ", ") + entry.skill.name;
    }
    return out;
}

std::vector<const ReviewSkillEntry*> ReviewSkillBook::applicable(
    const std::vector<std::string>& paths) const {
    std::vector<const ReviewSkillEntry*> out;
    for (const ReviewSkillEntry& entry : skills) {
        if (std::ranges::any_of(paths, [&entry](const std::string& path) {
                return skill_applies(entry.skill, path);
            })) {
            out.push_back(&entry);
        }
    }
    return out;
}

ReviewSkillBook review_skill_book(const std::filesystem::path& dir) {
    ReviewSkillBook book;
    std::map<std::string, std::filesystem::path> files = skill_files(dir);
    std::vector<const BundledReviewSkill*> shipped;
    for (const BundledReviewSkill& skill : bundled_review_skills()) {
        shipped.push_back(&skill);
    }
    std::ranges::sort(shipped, {}, &BundledReviewSkill::name);
    for (const BundledReviewSkill* bundled : shipped) {
        const std::string name{bundled->name};
        // A shipped skill's file stands in for it, read or not: taken out of
        // the files left over, so one that does not parse is said once.
        std::optional<std::filesystem::path> file;
        if (const auto found = files.find(name); found != files.end()) {
            file = found->second;
            files.erase(found);
        }
        ReviewSkillEntry entry;
        try {
            if (file.has_value()) {
                const std::optional<std::string> bytes = read_file(*file);
                if (!bytes.has_value()) {
                    book.problems.push_back(file->string() + ": could not be read");
                    continue;
                }
                entry.skill = parse_review_skill(*bytes, file->string(), name);
                entry.source =
                    *bytes == bundled->text ? ReviewSkillSource::Seeded : ReviewSkillSource::User;
                entry.path = *file;
            } else {
                entry.skill = parse_review_skill(bundled->text, "shipped " + name, name);
                entry.source = ReviewSkillSource::Shipped;
            }
        } catch (const std::runtime_error& e) {
            book.problems.emplace_back(e.what());
            continue;
        }
        book.skills.push_back(std::move(entry));
    }
    for (const auto& [name, path] : files) {
        const std::optional<std::string> bytes = read_file(path);
        if (!bytes.has_value()) {
            book.problems.push_back(path.string() + ": could not be read");
            continue;
        }
        try {
            book.skills.push_back(
                ReviewSkillEntry{.skill = parse_review_skill(*bytes, path.string(), name),
                                 .source = ReviewSkillSource::User,
                                 .path = path});
        } catch (const std::runtime_error& e) {
            book.problems.emplace_back(e.what());
        }
    }
    return book;
}

const std::string& review_finding_schema_text() {
    static const std::string text = [] {
        const BundledAgent* agent = find_bundled_agent("code-review");
        const nlohmann::json agent_schema =
            agent == nullptr ? nlohmann::json{}
                             : nlohmann::json::parse(agent->schema, nullptr, false);
        nlohmann::json findings = nlohmann::json::object();
        if (agent_schema.is_object() && agent_schema.contains("properties") &&
            agent_schema["properties"].contains("findings")) {
            findings = agent_schema["properties"]["findings"];
        }
        findings["description"] =
            "What this lens found, most severe first. Empty when it found nothing: silence is "
            "a complete, valid review.";
        const nlohmann::json schema{
            {"$schema", "http://json-schema.org/draft-07/schema#"},
            {"title", "ReviewFindings"},
            {"type", "object"},
            {"additionalProperties", false},
            {"properties", {{"findings", findings}}},
            {"required", {"findings"}},
        };
        return schema.dump(2);
    }();
    return text;
}

nlohmann::json to_json(const ReviewSkillEntry& entry) {
    nlohmann::json out = fields(entry);
    out["object"] = "review.skill";
    out["prompt"] = entry.skill.prompt;
    return out;
}

nlohmann::json to_json(const ReviewSkillBook& book) {
    nlohmann::json data = nlohmann::json::array();
    for (const ReviewSkillEntry& entry : book.skills) {
        data.push_back(fields(entry));
    }
    return nlohmann::json{
        {"object", "list"}, {"data", std::move(data)}, {"problems", book.problems}};
}

}  // namespace apogee::harness
