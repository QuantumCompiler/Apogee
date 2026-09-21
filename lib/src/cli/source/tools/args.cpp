#include "tools/args.h"

#include <charconv>

namespace apogee::tools {

std::string trim(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t' ||
                                   text[begin] == '\n' || text[begin] == '\r')) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\n' ||
                           text[end - 1] == '\r')) {
        --end;
    }
    return std::string{text.substr(begin, end - begin)};
}

std::string Arguments::string(std::string_view key) const {
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return {};
    }
    if (it->is_string()) {
        return trim(it->get<std::string>());
    }
    if (it->is_number_integer()) {
        return std::to_string(it->get<std::int64_t>());
    }
    return {};
}

std::optional<std::int64_t> Arguments::integer(std::string_view key) const {
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return std::nullopt;
    }
    if (it->is_number_integer()) {
        return it->get<std::int64_t>();
    }
    if (it->is_number_float()) {
        return static_cast<std::int64_t>(it->get<double>());
    }
    if (it->is_string()) {
        const std::string text = trim(it->get<std::string>());
        std::int64_t value = 0;
        const char* begin = text.data();
        const char* end = begin + text.size();
        if (!text.empty() && std::from_chars(begin, end, value).ec == std::errc{} &&
            std::from_chars(begin, end, value).ptr == end) {
            return value;
        }
    }
    return std::nullopt;
}

bool Arguments::has(std::string_view key) const {
    const auto it = object.find(key);
    return it != object.end() && !it->is_null();
}

std::optional<Arguments> parse_arguments(std::string_view arguments, std::string_view example,
                                         agent::ToolOutcome& error) {
    // An empty argument string is an empty object: a tool with no required
    // arguments is often called with nothing at all.
    if (trim(arguments).empty()) {
        return Arguments{nlohmann::json::object()};
    }
    nlohmann::json parsed = nlohmann::json::parse(arguments, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        error = tools::error("arguments must be a JSON object like " + std::string{example});
        return std::nullopt;
    }
    return Arguments{std::move(parsed)};
}

agent::ToolOutcome ok(std::string text) {
    return agent::ToolOutcome{std::move(text), false};
}

agent::ToolOutcome error(std::string text) {
    return agent::ToolOutcome{"Error: " + std::move(text), true};
}

}  // namespace apogee::tools
