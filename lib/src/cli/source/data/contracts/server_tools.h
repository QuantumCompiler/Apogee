#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/config.h"

/// A vendor's server-side tools (47a): request blocks executed on the vendor's
/// own infrastructure -- its web search, its code execution -- which Apogee's
/// host gate can never see. Declared here as plain data, so the loader, the
/// editor's completion and each wire read one table and nothing above the
/// wire ever switches on a vendor.
///
/// They exist only behind a backend entry's `server_tools:` list, which
/// defaults to empty: enabling one is recorded consent for that vendor to
/// browse or execute on the user's behalf under the user's key (SPEC.md ->
/// Principles, 2026-10-16). The host gate's law is unchanged for every call
/// Apogee itself makes.
namespace apogee::harness {

/// The web search a vendor runs: Anthropic's `web_search`, OpenAI's
/// `web_search`, Google's `google_search`.
inline constexpr std::string_view kServerWebSearch = "web_search";
/// The code a vendor runs in its own sandbox: Anthropic's `code_execution`,
/// OpenAI's `code_interpreter`, Google's `code_execution`.
inline constexpr std::string_view kServerCodeExecution = "code_execution";

/// One vendor's server-side tools, kept current with the vendor as ADR 0009
/// keeps the carried model lists: `reviewed` moves whenever the table is
/// checked against the vendor's documentation, rows changed or not.
struct ServerToolSet {
    BackendType type = BackendType::Anthropic;
    /// The vendor, as a narration line names it: "Anthropic".
    std::string_view vendor{};
    /// The names an entry may list, in the order they are offered.
    std::vector<std::string_view> names{};
    /// When the table was last checked against the vendor's own docs.
    std::string_view reviewed{};
};

/// Every vendor whose API runs server-side tools, one row each.
[[nodiscard]] const std::vector<ServerToolSet>& server_tool_sets();

/// `type`'s row, or nullptr for a backend whose API runs none -- a local
/// model, a vendor CLI (whose own tools are the CLI's business), the mock.
[[nodiscard]] const ServerToolSet* server_tool_set(BackendType type) noexcept;

/// Every name any vendor offers, each once, in table order: what
/// `set-backend --server-tools` completes from.
[[nodiscard]] std::vector<std::string_view> server_tool_names();

/// Why `names` cannot be a `type` entry's `server_tools:`, or empty when they
/// can: a type that runs none, or a name its vendor does not offer, each
/// naming what would do.
[[nodiscard]] std::string server_tools_refusal(BackendType type,
                                               const std::vector<std::string>& names);

/// The one narration line a vendor-executed tool use is said by, in every
/// front end and in machine mode's `tool_status`: `web_search, run by
/// Anthropic: "apogee release notes"`. `what` empty leaves the colon out.
[[nodiscard]] std::string server_tool_line(std::string_view tool, std::string_view vendor,
                                           std::string_view what);

/// `text` as one line for a narration: its first line, at most `limit`
/// characters (an ellipsis said where cut), never splitting a UTF-8
/// character.
[[nodiscard]] std::string narration_excerpt(std::string_view text, std::size_t limit = 80);

}  // namespace apogee::harness
