#include "harness/context_windows.h"

#include <array>
#include <cctype>
#include <string>
#include <utility>

namespace apogee::harness {
namespace {

/// Prefix → window, longest match wins.
///
/// Prefixes rather than exact ids so a dated pin (`claude-sonnet-5-20260101`)
/// resolves through its family without a row per release — otherwise this table
/// is stale the week after every vendor ships.
///
/// Kept deliberately short: these are fallbacks for a config entry that omitted
/// `context_size`, not a model catalogue. When a user runs something not listed,
/// the answer is "unknown" and they set `context_size` themselves — which is
/// honest, and better than guessing a window and warning at the wrong point.
constexpr std::array<std::pair<std::string_view, std::int64_t>, 14> kWindows{{
    // Anthropic
    {"claude-opus", 200000},
    {"claude-sonnet", 200000},
    {"claude-haiku", 200000},
    {"claude-fable", 200000},
    {"claude-", 200000},

    // OpenAI
    {"gpt-5", 400000},
    {"gpt-4.1", 1047576},
    {"gpt-4o", 128000},
    {"gpt-4", 8192},
    {"o3", 200000},
    {"o4", 200000},

    // Google
    {"gemini-2.5", 1048576},
    {"gemini-1.5-flash", 1048576},
    {"gemini-", 1048576},
}};

std::string fold(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (const char c : value) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

}  // namespace

std::int64_t context_window_for(std::string_view model) noexcept {
    if (model.empty()) {
        return 0;
    }
    const std::string needle = fold(model);

    std::size_t best_length = 0;
    std::int64_t best_window = 0;
    for (const auto& [prefix, window] : kWindows) {
        if (needle.size() < prefix.size()) {
            continue;
        }
        if (needle.compare(0, prefix.size(), prefix) != 0) {
            continue;
        }
        // Longest prefix wins: "claude-sonnet" must beat "claude-".
        if (prefix.size() > best_length) {
            best_length = prefix.size();
            best_window = window;
        }
    }
    return best_window;
}

std::int64_t resolve_context_window(std::int64_t configured, std::string_view model) noexcept {
    if (configured > 0) {
        return configured;
    }
    return context_window_for(model);
}

}  // namespace apogee::harness
