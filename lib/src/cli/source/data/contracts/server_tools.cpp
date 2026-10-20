#include "contracts/server_tools.h"

#include <algorithm>

namespace apogee::harness {

const std::vector<ServerToolSet>& server_tool_sets() {
    // Reviewed against each vendor's own documentation (ADR 0009's
    // discipline): Anthropic's Messages API server tools, OpenAI's Responses
    // API built-in tools, Gemini's built-in tools.
    static const std::vector<ServerToolSet> sets{
        ServerToolSet{.type = BackendType::Anthropic,
                      .vendor = "Anthropic",
                      .names = {kServerWebSearch, kServerCodeExecution},
                      .reviewed = "2026-10-18"},
        ServerToolSet{.type = BackendType::OpenAI,
                      .vendor = "OpenAI",
                      .names = {kServerWebSearch, kServerCodeExecution},
                      .reviewed = "2026-10-18"},
        ServerToolSet{.type = BackendType::Google,
                      .vendor = "Google",
                      .names = {kServerWebSearch, kServerCodeExecution},
                      .reviewed = "2026-10-18"},
    };
    return sets;
}

const ServerToolSet* server_tool_set(BackendType type) noexcept {
    for (const ServerToolSet& set : server_tool_sets()) {
        if (set.type == type) {
            return &set;
        }
    }
    return nullptr;
}

std::vector<std::string_view> server_tool_names() {
    std::vector<std::string_view> names;
    for (const ServerToolSet& set : server_tool_sets()) {
        for (const std::string_view name : set.names) {
            if (std::ranges::find(names, name) == names.end()) {
                names.push_back(name);
            }
        }
    }
    return names;
}

std::string server_tools_refusal(BackendType type, const std::vector<std::string>& names) {
    if (names.empty()) {
        return {};
    }
    const ServerToolSet* set = server_tool_set(type);
    if (set == nullptr) {
        return "a " + std::string{to_string(type)} +
               " backend runs no server-side tools -- only the anthropic, openai and google "
               "APIs do";
    }
    std::string known;
    for (const std::string_view name : set->names) {
        known += (known.empty() ? "" : ", ") + std::string{name};
    }
    for (const std::string& name : names) {
        if (std::ranges::find(set->names, name) == set->names.end()) {
            return "'" + name + "' is not a server-side tool " + std::string{set->vendor} +
                   " runs (known: " + known + ")";
        }
    }
    return {};
}

std::string server_tool_line(std::string_view tool, std::string_view vendor,
                             std::string_view what) {
    std::string line = std::string{tool} + ", run by " + std::string{vendor};
    if (!what.empty()) {
        line += ": " + std::string{what};
    }
    return line;
}

std::string narration_excerpt(std::string_view text, std::size_t limit) {
    std::size_t start = text.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) {
        return {};
    }
    std::string_view line = text.substr(start);
    bool cut = false;
    if (const std::size_t newline = line.find_first_of("\r\n"); newline != std::string_view::npos) {
        line = line.substr(0, newline);
        cut = true;
    }
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
        line.remove_suffix(1);
    }
    if (line.size() > limit) {
        std::size_t end = limit;
        // Back off a continuation byte so a character is never split.
        while (end > 0 && (static_cast<unsigned char>(line[end]) & 0xC0U) == 0x80U) {
            --end;
        }
        line = line.substr(0, end);
        cut = true;
    }
    std::string out{line};
    if (cut) {
        out += "…";
    }
    return out;
}

}  // namespace apogee::harness
