#include "contracts/mcp_serve.h"

#include <algorithm>
#include <array>

namespace apogee::harness {
namespace {

/// `complete` alone (54b): each further call is a deliberate later listing.
constexpr std::array<std::string_view, 1> kServedTools{kServedComplete};

[[nodiscard]] std::string joined(const std::vector<std::string>& names) {
    std::string out;
    for (const std::string& name : names) {
        out += (out.empty() ? "" : ", ") + name;
    }
    return out;
}

}  // namespace

std::span<const std::string_view> mcp_serve_tool_names() noexcept {
    return kServedTools;
}

std::string mcp_serve_refusal(const std::vector<std::string>& names) {
    for (const std::string& name : names) {
        if (std::ranges::find(kServedTools, name) == kServedTools.end()) {
            std::string known;
            for (const std::string_view tool : kServedTools) {
                known += (known.empty() ? "" : ", ") + std::string{tool};
            }
            return "'" + name + "' is not a call apogee mcp serve makes (known: " + known + ")";
        }
    }
    return {};
}

std::string mcp_serve_ineligible(std::string_view name, const std::vector<std::string>& tools,
                                 bool metered, std::string_view unknown) {
    const std::string why = unknown.empty() && metered
                                ? "its provider bills each call"
                                : "whether its provider bills each call could not be asked (" +
                                      std::string{unknown.empty() ? "it did not say" : unknown} +
                                      "), and unknown counts as billed";
    return "'" + std::string{name} + "' cannot serve " + joined(tools) + ": " + why +
           " -- a served call runs on another host's initiative, so only a backend local and "
           "unmetered by its own provider's word may serve one";
}

std::int64_t served_max_tokens(const std::optional<std::int64_t>& entry_max_tokens) {
    if (entry_max_tokens.has_value() && *entry_max_tokens > 0 &&
        *entry_max_tokens < kServedMaxTokens) {
        return *entry_max_tokens;
    }
    return kServedMaxTokens;
}

}  // namespace apogee::harness
