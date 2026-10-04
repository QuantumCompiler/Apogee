#include "commands/tool_vectors.h"

#include <nlohmann/json.hpp>

#include <exception>
#include <fstream>
#include <sstream>
#include <utility>

#include "contracts/config_edit.h"
#include "contracts/paths.h"

namespace apogee::commands {
namespace {

/// Bumped when the file's shape changes; a file of another version is
/// read as empty.
constexpr int kVersion = 1;

/// The vectors in the file at `path`; empty when it is missing, unreadable,
/// or not this version's shape.
agentloop::ToolVectors read_file(const std::filesystem::path& path) {
    agentloop::ToolVectors out;
    const std::ifstream in{path};
    if (!in) {
        return out;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    const nlohmann::json parsed = nlohmann::json::parse(buffer.str(), nullptr, false);
    if (!parsed.is_object() || parsed.value("version", 0) != kVersion) {
        return out;
    }
    const auto vectors = parsed.find("vectors");
    if (vectors == parsed.end() || !vectors->is_object()) {
        return out;
    }
    for (const auto& [key, value] : vectors->items()) {
        if (!value.is_array()) {
            continue;
        }
        std::vector<float> vector;
        vector.reserve(value.size());
        bool numbers = true;
        for (const nlohmann::json& number : value) {
            if (!number.is_number()) {
                numbers = false;
                break;
            }
            vector.push_back(number.get<float>());
        }
        if (numbers && !vector.empty()) {
            out.emplace(key, std::move(vector));
        }
    }
    return out;
}

}  // namespace

std::filesystem::path tool_vector_cache_path(const std::filesystem::path& config_path) {
    return harness::home_for_config(config_path) / "cache" / "tool-vectors.json";
}

ToolVectorCache::ToolVectorCache(std::filesystem::path file) : file_{std::move(file)} {}

void ToolVectorCache::read() {
    if (!read_) {
        read_ = true;
        vectors_ = read_file(file_);
    }
}

std::optional<std::vector<float>> ToolVectorCache::load(const std::string& key) {
    read();
    const auto found = vectors_.find(key);
    if (found == vectors_.end()) {
        return std::nullopt;
    }
    return found->second;
}

void ToolVectorCache::store(const agentloop::ToolVectors& made) {
    agentloop::ToolVectors merged = read_file(file_);
    for (const auto& [key, vector] : made) {
        merged.insert_or_assign(key, vector);
        vectors_.insert_or_assign(key, vector);
    }
    nlohmann::json vectors = nlohmann::json::object();
    for (const auto& [key, vector] : merged) {
        vectors[key] = vector;
    }
    try {
        std::filesystem::create_directories(file_.parent_path());
        harness::write_file_atomically(
            file_, nlohmann::json{{"version", kVersion}, {"vectors", vectors}}.dump());
    } catch (const std::exception&) {
        // Unwritten, it is only slower next time.
    }
}

}  // namespace apogee::commands
