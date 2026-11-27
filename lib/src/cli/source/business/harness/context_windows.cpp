#include "harness/context_windows.h"

#include <array>
#include <cctype>
#include <string>

namespace apogee::harness {
namespace {

/// One row of the table: an id, or the start of one, and its window.
struct Row {
    std::string_view id;
    std::int64_t window = 0;
    /// Matches `id` exactly, never as a prefix (57q): a family's bare id
    /// whose later members differ -- `gpt-4` is 8,192 tokens, `gpt-4-turbo`
    /// 128,000 -- so an id the table does not list answers unknown rather
    /// than the bare id's window.
    bool exact = false;
};

/// Prefix → window, longest match wins; an exact row only for its own id.
///
/// Prefixes rather than exact ids so a dated pin (`claude-sonnet-5-20260101`)
/// resolves through its family without a row per release — otherwise this table
/// is stale the week after every vendor ships.
///
/// Kept deliberately short: these are fallbacks for a config entry that omitted
/// `context_size`, not a model catalogue. When a user runs something not listed,
/// the answer is "unknown" and they set `context_size` themselves — which is
/// honest, and better than guessing a window and warning at the wrong point.
///
/// The GPT-4 family's rows reviewed 2026-11-08 against OpenAI's model pages
/// (gpt-4, gpt-4-turbo, gpt-4-turbo-preview -- whose snapshot is
/// gpt-4-0125-preview -- and gpt-4.5-preview); gpt-4-32k and
/// gpt-4-1106-preview are shut down and have no page, and keep the windows
/// they shipped with.
constexpr std::array<Row, 21> kWindows{{
    // Anthropic
    {.id = "claude-opus", .window = 200000},
    {.id = "claude-sonnet", .window = 200000},
    {.id = "claude-haiku", .window = 200000},
    {.id = "claude-fable", .window = 200000},
    {.id = "claude-", .window = 200000},

    // OpenAI
    {.id = "gpt-5", .window = 400000},
    {.id = "gpt-4.5", .window = 128000},
    {.id = "gpt-4.1", .window = 1047576},
    {.id = "gpt-4o", .window = 128000},
    {.id = "gpt-4-turbo", .window = 128000},
    {.id = "gpt-4-0125", .window = 128000},
    {.id = "gpt-4-1106", .window = 128000},
    {.id = "gpt-4-32k", .window = 32768},
    {.id = "gpt-4", .window = 8192, .exact = true},
    {.id = "gpt-4-0613", .window = 8192, .exact = true},
    {.id = "gpt-4-0314", .window = 8192, .exact = true},
    {.id = "o3", .window = 200000},
    {.id = "o4", .window = 200000},

    // Google
    {.id = "gemini-2.5", .window = 1048576},
    {.id = "gemini-1.5-flash", .window = 1048576},
    {.id = "gemini-", .window = 1048576},
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
    for (const Row& row : kWindows) {
        if (row.exact) {
            if (needle == row.id) {
                return row.window;
            }
            continue;
        }
        if (needle.size() < row.id.size()) {
            continue;
        }
        if (needle.compare(0, row.id.size(), row.id) != 0) {
            continue;
        }
        // Longest prefix wins: "claude-sonnet" must beat "claude-".
        if (row.id.size() > best_length) {
            best_length = row.id.size();
            best_window = row.window;
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
