#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace apogee::testing {

/// The run of bytes `after` added to `before`, when that is all that changed:
/// every byte of `before` kept, in place, around one inserted run. Nullopt
/// when anything else moved -- the check a format-preserving edit must pass.
[[nodiscard]] inline std::optional<std::string> inserted(std::string_view before,
                                                         std::string_view after) {
    if (after.size() < before.size()) {
        return std::nullopt;
    }
    std::size_t prefix = 0;
    while (prefix < before.size() && before[prefix] == after[prefix]) {
        ++prefix;
    }
    std::size_t suffix = 0;
    while (suffix < before.size() - prefix &&
           before[before.size() - 1 - suffix] == after[after.size() - 1 - suffix]) {
        ++suffix;
    }
    if (prefix + suffix != before.size()) {
        return std::nullopt;
    }
    return std::string{after.substr(prefix, after.size() - before.size())};
}

}  // namespace apogee::testing
