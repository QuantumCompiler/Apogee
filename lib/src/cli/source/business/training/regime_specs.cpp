#include "training/regime_specs.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <system_error>
#include <utility>

#include "contracts/assets.h"
#include "contracts/errors.h"

namespace apogee::training {
namespace {

[[nodiscard]] std::optional<std::string> read_file(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        return std::nullopt;
    }
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// The `.yaml` and `.yml` files directly under `dir`, by name.
[[nodiscard]] std::map<std::string, std::filesystem::path> spec_files(
    const std::filesystem::path& dir) {
    std::map<std::string, std::filesystem::path> out;
    std::error_code code;
    for (std::filesystem::directory_iterator it{dir, code};
         !code && it != std::filesystem::directory_iterator{}; it.increment(code)) {
        const std::string extension = it->path().extension().string();
        if ((extension == ".yaml" || extension == ".yml") && it->is_regular_file(code)) {
            out.emplace(it->path().stem().string(), it->path());
        }
    }
    return out;
}

/// `path` read as the regime `name`, or its problem said.
[[nodiscard]] std::optional<RegimeEntry> read_spec(const std::filesystem::path& path,
                                                   const std::string& name,
                                                   std::vector<std::string>& problems) {
    const std::optional<std::string> bytes = read_file(path);
    if (!bytes.has_value()) {
        problems.push_back(path.string() + ": could not be read");
        return std::nullopt;
    }
    try {
        RegimeEntry entry;
        entry.spec = harness::parse_regime_spec(*bytes, path.string(), name);
        if (entry.spec.name != name) {
            problems.push_back(path.string() + ": names itself '" + entry.spec.name +
                               "' -- a file under training/regimes/ runs by its file's name");
            return std::nullopt;
        }
        entry.path = path;
        const harness::BundledRegime* shipped = harness::find_bundled_regime(name);
        entry.source = shipped != nullptr && shipped->text == *bytes ? RegimeSource::Seeded
                                                                     : RegimeSource::User;
        return entry;
    } catch (const harness::ConfigError& e) {
        problems.emplace_back(e.what());
        return std::nullopt;
    }
}

[[nodiscard]] nlohmann::json fields(const RegimeEntry& entry) {
    const harness::RegimeSpec& spec = entry.spec;
    return nlohmann::json{
        {"name", spec.name},
        {"description", spec.description},
        {"source", to_string(entry.source)},
        {"config", entry.config},
        {"path", entry.path.empty() ? nlohmann::json{} : nlohmann::json(entry.path.string())},
        {"kits", spec.kits},
        {"count", spec.count},
        {"iters", spec.iters},
        {"temperature", spec.temperature},
        {"teacher", spec.teacher},
        {"student", spec.student},
        {"promote_as", spec.promote_as}};
}

}  // namespace

std::string_view to_string(RegimeSource source) noexcept {
    switch (source) {
        case RegimeSource::Shipped:
            return "shipped";
        case RegimeSource::Seeded:
            return "seeded";
        case RegimeSource::User:
            return "user";
    }
    return "shipped";
}

const RegimeEntry* RegimeBook::find(std::string_view name) const noexcept {
    for (const RegimeEntry& entry : regimes) {
        if (entry.spec.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

std::string RegimeBook::names() const {
    std::string out;
    for (const RegimeEntry& entry : regimes) {
        out += (out.empty() ? "" : ", ") + entry.spec.name;
    }
    return out;
}

RegimeBook regime_book(const harness::Config& config, const std::filesystem::path& dir) {
    RegimeBook book;
    std::map<std::string, std::filesystem::path> files = spec_files(dir);
    const auto entry_of = [&config](const std::string& name) -> std::optional<RegimeEntry> {
        const auto found = config.training.regimes.find(name);
        if (found == config.training.regimes.end()) {
            return std::nullopt;
        }
        RegimeEntry entry;
        entry.spec = found->second;
        entry.spec.name = name;
        entry.source = RegimeSource::User;
        entry.config = true;
        return entry;
    };

    std::vector<std::string> shipped;
    for (const harness::BundledRegime& regime : harness::bundled_regimes()) {
        shipped.emplace_back(regime.name);
    }
    std::ranges::sort(shipped);
    for (const std::string& name : shipped) {
        // A shipped name's file stands in for it, read or not: taken out of
        // the files left over, so one that does not parse is said once.
        std::optional<std::filesystem::path> file;
        if (const auto found = files.find(name); found != files.end()) {
            file = found->second;
            files.erase(found);
        }
        if (std::optional<RegimeEntry> entry = entry_of(name); entry.has_value()) {
            book.regimes.push_back(std::move(*entry));
            continue;
        }
        if (file.has_value()) {
            if (std::optional<RegimeEntry> entry = read_spec(*file, name, book.problems);
                entry.has_value()) {
                book.regimes.push_back(std::move(*entry));
            }
            continue;
        }
        try {
            RegimeEntry entry;
            entry.spec = harness::parse_regime_spec(harness::find_bundled_regime(name)->text,
                                                    "shipped regime " + name, name);
            entry.source = RegimeSource::Shipped;
            book.regimes.push_back(std::move(entry));
        } catch (const harness::ConfigError& e) {
            book.problems.emplace_back(e.what());
        }
    }
    for (const auto& [name, spec] : config.training.regimes) {
        if (book.find(name) != nullptr) {
            continue;
        }
        files.erase(name);
        book.regimes.push_back(*entry_of(name));
    }
    for (const auto& [name, path] : files) {
        if (std::optional<RegimeEntry> entry = read_spec(path, name, book.problems);
            entry.has_value()) {
            book.regimes.push_back(std::move(*entry));
        }
    }
    return book;
}

nlohmann::json to_json(const RegimeEntry& entry) {
    nlohmann::json out = fields(entry);
    out["object"] = "training.regime";
    return out;
}

nlohmann::json to_json(const RegimeBook& book) {
    nlohmann::json data = nlohmann::json::array();
    for (const RegimeEntry& entry : book.regimes) {
        data.push_back(fields(entry));
    }
    return nlohmann::json{
        {"object", "list"}, {"data", std::move(data)}, {"problems", book.problems}};
}

}  // namespace apogee::training
