#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "agent/tool.h"

/// Argument handling shared by every native tool.
///
/// A model sends optional arguments as explicit `null` as often as it omits
/// them, and numbers as strings when it is having a bad day. Every accessor
/// here treats `null` as "not supplied" and reports a wrong type as a fixable
/// error rather than leaking a JSON exception into the turn.
namespace apogee::tools {

/// The parsed argument object, or an error outcome the tool returns as-is.
struct Arguments {
    nlohmann::json object;

    /// The string at `key`, trimmed; empty when absent or null.
    [[nodiscard]] std::string string(std::string_view key) const;

    /// The integer at `key` (a JSON number, or a string of digits); nullopt
    /// when absent or null.
    [[nodiscard]] std::optional<std::int64_t> integer(std::string_view key) const;

    [[nodiscard]] bool has(std::string_view key) const;
};

/// Parses `arguments`. On failure, `error` is the outcome to return and the
/// result is nullopt. `example` is the shape named in the message.
[[nodiscard]] std::optional<Arguments> parse_arguments(std::string_view arguments,
                                                       std::string_view example,
                                                       agent::ToolOutcome& error);

[[nodiscard]] agent::ToolOutcome ok(std::string text);
[[nodiscard]] agent::ToolOutcome error(std::string text);

/// Trims ASCII whitespace at both ends.
[[nodiscard]] std::string trim(std::string_view text);

}  // namespace apogee::tools
