#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "contracts/config.h"

/// A turn's per-request settings, resolved the same way on every surface
/// (A4: moved out of `cli/helpers` so the admin plane resolves them without
/// reaching into the command line).
namespace apogee::commands {

/// Resolution order for a per-request setting: an explicit flag, then the
/// backend entry's own value, then nothing.
///
/// The flag always wins -- it is the most specific thing the user said.
/// A turn's thinking (26i): the mode and the budget each from the flag or
/// the session when set, else the backend's config, else on with no budget.
[[nodiscard]] harness::Thinking resolve_thinking(const std::optional<harness::ThinkingMode>& mode,
                                                 const std::optional<std::int64_t>& budget,
                                                 const harness::Config& config,
                                                 std::string_view backend_name);

[[nodiscard]] std::optional<double> resolve_temperature(const std::optional<double>& flag_value,
                                                        const harness::Config& config,
                                                        std::string_view backend_name);

[[nodiscard]] std::optional<std::int64_t> resolve_max_tokens(
    const std::optional<std::int64_t>& flag_value, const harness::Config& config,
    std::string_view backend_name);

/// The system prompt for a turn: the flag when given, else the backend
/// entry's `system_prompt`, else empty.
[[nodiscard]] std::string resolve_system_prompt(const std::string& flag_value,
                                                const harness::Config& config,
                                                std::string_view backend_name);

}  // namespace apogee::commands
