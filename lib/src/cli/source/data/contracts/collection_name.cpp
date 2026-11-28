#include "contracts/collection_name.h"

#include <algorithm>
#include <array>
#include <filesystem>

#include "contracts/config.h"
#include "contracts/config_edit.h"
#include "contracts/config_yaml.h"

namespace apogee::harness {

bool name_leaves_folder(std::string_view name) {
    if (name.empty() || name == "..") {
        return true;
    }
    if (name.find_first_of(std::string_view{"/\\\0", 3}) != std::string_view::npos) {
        return true;
    }
    return std::filesystem::path{std::string{name}}.has_root_name();
}

bool windows_device_name(std::string_view name) {
    std::string stem{name.substr(0, name.find('.'))};
    std::ranges::transform(stem, stem.begin(), [](char c) {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    });
    constexpr std::array<std::string_view, 4> kWords{"con", "prn", "aux", "nul"};
    if (std::ranges::find(kWords, stem) != kWords.end()) {
        return true;
    }
    return stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) &&
           stem[3] >= '1' && stem[3] <= '9';
}

namespace {

/// The config's add rule as a collection's words: `/` is the read rule's.
constexpr std::string_view kNewNameRule =
    "a name is letters, digits, '_', '-' and '.', first neither '-' nor '.', and not null";

}  // namespace

std::string quoted_collection_name(std::string_view name) {
    std::string out = "'";
    for (const char c : name) {
        const unsigned byte = static_cast<unsigned char>(c);
        if (c == '\n') {
            out += "\\n";
        } else if (c == '\t') {
            out += "\\t";
        } else if (c == '\r') {
            out += "\\r";
        } else if (byte < 0x20U || byte == 0x7FU) {
            constexpr std::string_view kHex = "0123456789abcdef";
            out += "\\x";
            out += kHex[byte >> 4U];
            out += kHex[byte & 0x0FU];
        } else {
            out += c;
        }
    }
    return out + "'";
}

std::string collection_create_error(std::string_view name,
                                    const std::vector<std::string>& existing) {
    if (std::string refused = collection_read_error(name); !refused.empty()) {
        return refused;
    }
    const std::string quoted = quoted_collection_name(name);
    if (!yaml_plain_key(name)) {
        return "collection name " + quoted + " is not one Apogee can create -- " +
               std::string{kNewNameRule};
    }
    if (name.size() > kNewCollectionNameMax) {
        return "collection name " + quoted + " is " + std::to_string(name.size()) +
               " bytes -- a new name is at most " + std::to_string(kNewCollectionNameMax);
    }
    if (windows_device_name(name)) {
        return "collection name " + quoted +
               " is a device name on Windows -- a new name is none of con, prn, aux, nul, "
               "com1-com9 and lpt1-lpt9, before any '.'";
    }
    if (const std::optional<std::string> clash = fold_collision(existing, name);
        clash.has_value()) {
        return "collection name " + quoted + " differs only in case from the collection " +
               quoted_collection_name(*clash) +
               " -- names that differ only in case are one collection";
    }
    return {};
}

std::vector<std::string> collection_references(const Config& config) {
    std::vector<std::string> names = config.embedding_names();
    const auto add = [&names](const std::string& name) {
        if (!name.empty() && std::ranges::find(names, name) == names.end()) {
            names.push_back(name);
        }
    };
    add(config.auto_rag);
    add(config.knowledge.db);
    for (const auto& agent : config.agents) {
        add(agent.second.collection);
    }
    for (const auto& graph : config.graphs) {
        for (const std::string& member : graph.second.collections) {
            add(member);
        }
    }
    return names;
}

std::string collection_read_error(std::string_view name) {
    if (name.empty()) {
        return "a collection needs a name";
    }
    if (name_leaves_folder(name)) {
        return "'" + std::string{name} + "' is not a plain collection name";
    }
    return {};
}

}  // namespace apogee::harness
