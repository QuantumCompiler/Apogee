#include "contracts/config_yaml.h"

#include <nlohmann/json.hpp>

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <string_view>

#include "contracts/config.h"

namespace apogee::harness {
namespace {

using Json = nlohmann::ordered_json;

/// The plain scalars YAML's core schema reads as null.
[[nodiscard]] bool null_word(std::string_view text) {
    return text.empty() || text == "~" || text == "null" || text == "Null" || text == "NULL";
}

/// JSON's number grammar, exactly: what can be written into a JSON file as
/// the same token.
[[nodiscard]] bool json_number(std::string_view text) {
    std::size_t i = 0;
    const auto digits = [&] {
        const std::size_t from = i;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            ++i;
        }
        return i > from;
    };
    if (i < text.size() && text[i] == '-') {
        ++i;
    }
    if (i < text.size() && text[i] == '0') {
        ++i;
    } else if (!digits()) {
        return false;
    }
    if (i < text.size() && text[i] == '.') {
        ++i;
        if (!digits()) {
            return false;
        }
    }
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
            ++i;
        }
        if (!digits()) {
            return false;
        }
    }
    return i == text.size();
}

/// A key YAML reads back as exactly its text when written plain.
[[nodiscard]] bool plain_key(std::string_view key) {
    if (key.empty() || null_word(key)) {
        return false;
    }
    return std::ranges::all_of(key,
                               [](char c) {
                                   return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                          (c >= '0' && c <= '9') || c == '_' || c == '-' ||
                                          c == '.' || c == '/';
                               }) &&
           key.front() != '-' && key.front() != '.';
}

/// `text` as a YAML double-quoted scalar.
[[nodiscard]] std::string double_quoted(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (byte < 0x20 || byte == 0x7F) {
                    std::array<char, 8> escape{};
                    (void)std::snprintf(escape.data(), escape.size(), "\\x%02X", byte);
                    out += escape.data();
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
    return out;
}

[[nodiscard]] std::string key_text(const std::string& key) {
    return plain_key(key) ? key : double_quoted(key);
}

[[nodiscard]] std::string scalar_text(const Json& value) {
    if (value.is_string()) {
        return double_quoted(value.get_ref<const std::string&>());
    }
    if (value.is_null()) {
        return "null";
    }
    return value.dump();
}

[[nodiscard]] bool scalars_only(const Json& list) {
    return std::ranges::none_of(
        list, [](const Json& item) { return item.is_object() || item.is_array(); });
}

[[nodiscard]] std::string flow_list(const Json& list) {
    std::string out = "[";
    bool first = true;
    for (const Json& item : list) {
        out += first ? "" : ", ";
        first = false;
        out += scalar_text(item);
    }
    return out + "]";
}

void write_sequence(std::string& out, const Json& list, const std::string& indent);

void write_mapping(std::string& out, const Json& object, const std::string& indent) {
    for (const auto& [key, value] : object.items()) {
        out += indent + key_text(key) + ":";
        if (value.is_null() || (value.is_object() && value.empty())) {
            out += "\n";
        } else if (value.is_object()) {
            out += "\n";
            write_mapping(out, value, indent + "  ");
        } else if (value.is_array() && (value.empty() || scalars_only(value))) {
            out += " " + flow_list(value) + "\n";
        } else if (value.is_array()) {
            out += "\n";
            write_sequence(out, value, indent + "  ");
        } else {
            out += " " + scalar_text(value) + "\n";
        }
    }
}

void write_sequence(std::string& out, const Json& list, const std::string& indent) {
    for (const Json& item : list) {
        if (item.is_object() && !item.empty()) {
            // The first member on the dash's line, the rest under it.
            std::string members;
            write_mapping(members, item, indent + "  ");
            out += indent + "- " + members.substr(indent.size() + 2);
        } else if (item.is_object()) {
            out += indent + "- {}\n";
        } else if (item.is_array() && (item.empty() || scalars_only(item))) {
            out += indent + "- " + flow_list(item) + "\n";
        } else if (item.is_array()) {
            out += indent + "-\n";
            write_sequence(out, item, indent + "  ");
        } else {
            out += indent + "- " + scalar_text(item) + "\n";
        }
    }
}

Json convert(const YAML::Node& node, const Json& hint) {
    switch (node.Type()) {
        case YAML::NodeType::Map: {
            Json out = Json::object();
            for (const auto& pair : node) {
                const std::string key = pair.first.Scalar();
                const Json& inner = hint.is_object() && hint.contains(key) ? hint.at(key) : Json{};
                out[key] = convert(pair.second, inner);
            }
            return out;
        }
        case YAML::NodeType::Sequence: {
            Json out = Json::array();
            std::size_t i = 0;
            for (const auto& item : node) {
                const Json& inner = hint.is_array() && i < hint.size() ? hint.at(i) : Json{};
                out.push_back(convert(item, inner));
                ++i;
            }
            return out;
        }
        case YAML::NodeType::Scalar:
            return json_of_yaml_scalar(node.Scalar(), node.Tag() == "?");
        case YAML::NodeType::Null:
        case YAML::NodeType::Undefined:
            break;
    }
    if (hint.is_object()) {
        return Json::object();
    }
    if (hint.is_array()) {
        return Json::array();
    }
    return nullptr;
}

}  // namespace

std::string yaml_of_json(const Json& value) {
    std::string out;
    if (value.is_object()) {
        write_mapping(out, value, "");
    }
    return out;
}

Json json_of_yaml_scalar(std::string_view scalar, bool plain) {
    if (!plain) {
        return std::string{scalar};
    }
    if (null_word(scalar)) {
        return nullptr;
    }
    if (scalar == "true" || scalar == "True" || scalar == "TRUE") {
        return true;
    }
    if (scalar == "false" || scalar == "False" || scalar == "FALSE") {
        return false;
    }
    if (json_number(scalar)) {
        return Json::parse(scalar);
    }
    return std::string{scalar};
}

Json json_of_yaml(std::string_view yaml, const Json& hint) {
    YAML::Node root;
    try {
        root = YAML::Load(std::string{yaml});
    } catch (const YAML::Exception& e) {
        throw ConfigError(std::string{"not valid YAML: "} + e.what());
    }
    return convert(root, hint);
}

}  // namespace apogee::harness
