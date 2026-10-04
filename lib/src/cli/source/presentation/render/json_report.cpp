#include "render/json_report.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

namespace apogee::render {
namespace {

std::string trim(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return std::string{text.substr(begin, end - begin)};
}

std::vector<std::string> sorted_keys(const nlohmann::json& object) {
    std::vector<std::string> keys;
    for (const auto& [key, unused] : object.items()) {
        keys.push_back(key);
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

/// Alphabetical, with the reserved key moved to the end when present.
std::vector<std::string> top_level_keys(const nlohmann::json& object) {
    std::vector<std::string> keys = sorted_keys(object);
    const auto it = std::find(keys.begin(), keys.end(), std::string{kHumanSummaryKey});
    if (it != keys.end()) {
        keys.erase(it);
        keys.emplace_back(kHumanSummaryKey);
    }
    return keys;
}

/// The key whose string value is longest -- the "main" field of an object
/// rendered inline inside a list.
std::string primary_key(const nlohmann::json& object) {
    std::string best;
    std::size_t longest = 0;
    for (const std::string& key : sorted_keys(object)) {
        const nlohmann::json& value = object.at(key);
        if (value.is_string() && value.get_ref<const std::string&>().size() > longest) {
            longest = value.get_ref<const std::string&>().size();
            best = key;
        }
    }
    return best;
}

std::string number_text(const nlohmann::json& value) {
    if (value.is_number_integer()) {
        return std::to_string(value.get<std::int64_t>());
    }
    std::ostringstream out;
    out << value.get<double>();
    return out.str();
}

std::string yes_no(bool value) {
    return value ? "Yes" : "No";
}

void write_markdown(std::ostringstream& out, const nlohmann::json& value, int depth);

void write_markdown_list(std::ostringstream& out, const nlohmann::json& list) {
    for (const nlohmann::json& item : list) {
        if (item.is_object()) {
            out << "- ";
            const std::string primary = primary_key(item);
            if (!primary.empty()) {
                out << item.at(primary).get<std::string>();
            }
            std::vector<std::string> meta;
            for (const std::string& key : sorted_keys(item)) {
                if (key == primary) {
                    continue;
                }
                const nlohmann::json& field = item.at(key);
                if (field.is_string()) {
                    meta.push_back(key_to_title(key) + ": **" + field.get<std::string>() + "**");
                } else if (field.is_number()) {
                    meta.push_back(key_to_title(key) + ": **" + number_text(field) + "**");
                } else if (field.is_boolean()) {
                    meta.push_back(key_to_title(key) + ": **" + yes_no(field.get<bool>()) + "**");
                }
            }
            for (std::size_t i = 0; i < meta.size(); ++i) {
                out << (i == 0 ? " -- " : ", ") << meta[i];
            }
            out << "\n";
        } else if (item.is_string()) {
            out << "- " << item.get<std::string>() << "\n";
        } else {
            out << "- " << item.dump() << "\n";
        }
    }
    out << "\n";
}

void write_markdown(std::ostringstream& out, const nlohmann::json& value, int depth) {
    if (value.is_object()) {
        const std::vector<std::string> keys =
            depth == 0 ? top_level_keys(value) : sorted_keys(value);
        for (const std::string& key : keys) {
            const std::string title = key_to_title(key);
            if (depth == 0) {
                out << "## " << title << "\n\n";
            } else if (depth == 1) {
                out << "### " << title << "\n\n";
            } else {
                out << std::string(static_cast<std::size_t>(depth - 2) * 2, ' ') << "**" << title
                    << "**: ";
            }
            write_markdown(out, value.at(key), depth + 1);
        }
    } else if (value.is_array()) {
        write_markdown_list(out, value);
    } else if (value.is_string()) {
        out << value.get<std::string>() << (depth > 2 ? "\n" : "\n\n");
    } else if (value.is_boolean()) {
        out << yes_no(value.get<bool>()) << "\n\n";
    } else if (value.is_number()) {
        out << number_text(value) << "\n\n";
    } else if (value.is_null()) {
        out << "\n";
    } else {
        out << value.dump() << "\n\n";
    }
}

void write_text(std::ostringstream& out, const nlohmann::json& value, int depth) {
    const std::string indent(static_cast<std::size_t>(depth) * 2, ' ');
    if (value.is_object()) {
        const std::vector<std::string> keys =
            depth == 0 ? top_level_keys(value) : sorted_keys(value);
        for (const std::string& key : keys) {
            out << indent << key_to_title(key) << ":\n";
            write_text(out, value.at(key), depth + 1);
        }
        if (depth == 0) {
            out << "\n";
        }
    } else if (value.is_array()) {
        for (const nlohmann::json& item : value) {
            if (item.is_object()) {
                out << indent << "- ";
                const std::string primary = primary_key(item);
                if (!primary.empty()) {
                    out << item.at(primary).get<std::string>();
                }
                std::vector<std::string> meta;
                for (const std::string& key : sorted_keys(item)) {
                    if (key != primary && item.at(key).is_string()) {
                        meta.push_back(key_to_title(key) + ": " + item.at(key).get<std::string>());
                    }
                }
                for (std::size_t i = 0; i < meta.size(); ++i) {
                    out << (i == 0 ? " (" : ", ") << meta[i] << (i + 1 == meta.size() ? ")" : "");
                }
                out << "\n";
            } else if (item.is_string()) {
                out << indent << "- " << item.get<std::string>() << "\n";
            } else {
                out << indent << "- " << item.dump() << "\n";
            }
        }
        out << "\n";
    } else if (value.is_string()) {
        out << indent << value.get<std::string>() << "\n\n";
    } else if (value.is_boolean()) {
        out << indent << yes_no(value.get<bool>()) << "\n\n";
    } else if (value.is_number()) {
        out << indent << number_text(value) << "\n\n";
    } else {
        out << indent << value.dump() << "\n\n";
    }
}

}  // namespace

std::string key_to_title(std::string_view key) {
    std::vector<std::string> words;
    std::string current;
    for (std::size_t i = 0; i < key.size(); ++i) {
        const char c = key[i];
        if (c == '_' || c == '-') {
            if (!current.empty()) {
                words.push_back(current);
                current.clear();
            }
        } else if (i > 0 && std::isupper(static_cast<unsigned char>(c)) != 0 &&
                   std::isupper(static_cast<unsigned char>(key[i - 1])) == 0) {
            words.push_back(current);
            current.clear();
            current.push_back(c);
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        words.push_back(current);
    }
    std::string out;
    for (std::string& word : words) {
        if (word.empty()) {
            continue;
        }
        word[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(word[0])));
        for (std::size_t i = 1; i < word.size(); ++i) {
            word[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(word[i])));
        }
        out += out.empty() ? "" : " ";
        out += word;
    }
    return out;
}

std::string render_markdown(const nlohmann::json& report) {
    std::ostringstream out;
    write_markdown(out, report, 0);
    return trim(out.str());
}

std::string render_text(const nlohmann::json& report) {
    std::ostringstream out;
    write_text(out, report, 0);
    return trim(out.str());
}

std::string strip_code_fence(std::string_view text) {
    std::string trimmed = trim(text);
    if (!trimmed.starts_with("```")) {
        return trimmed;
    }
    const std::size_t newline = trimmed.find('\n');
    if (newline != std::string::npos) {
        trimmed = trimmed.substr(newline + 1);
    }
    const std::size_t closing = trimmed.rfind("```");
    if (closing != std::string::npos) {
        trimmed = trimmed.substr(0, closing);
    }
    return trim(trimmed);
}

std::string render_report(std::string_view raw, ReportFormat format) {
    const nlohmann::json parsed = nlohmann::json::parse(strip_code_fence(raw), nullptr, false);
    if (parsed.is_discarded()) {
        return std::string{raw};  // not JSON: returned unchanged, never lost
    }
    const std::string rendered =
        format == ReportFormat::Text ? render_text(parsed) : render_markdown(parsed);
    return rendered.empty() ? std::string{raw} : rendered;
}

}  // namespace apogee::render
