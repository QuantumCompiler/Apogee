#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// The model calls `apogee mcp serve` may make for another MCP host (54b),
/// declared here as plain data so the loader, the editor's completion and
/// the server read one table.
///
/// **Consent is config, per tool.** A call exists only behind a backend
/// entry's `mcp_serve:` list, which defaults to empty: listing `complete`
/// there is the person's consent for a host to run that entry's model. A
/// tool no entry lists is absent from `tools/list`, never refused at call --
/// a host cannot pass a flag that opens it. The 47a discipline (an entry's
/// `server_tools:`) mirrored: there a vendor runs tools on the person's
/// behalf, here a host runs Apogee's models.
///
/// **Eligibility is the provider's word.** Only an entry whose provider says
/// it is local and unmetered may list one (`LLMProvider::
/// generation_is_metered`, never a list of types; unknown is metered): a
/// served generation runs on another host's initiative, which never spends.
/// The editor asks it at write, the server again at its start and at each
/// call. (A served `search_documents` embedding its query is no listed call:
/// it keeps the per-question spend rule a chat's search keeps, 2026-09-13.)
namespace apogee::harness {

/// The one-shot completion: a prompt in, the entry's answer out -- the core
/// `apogee complete -m <backend>` runs.
inline constexpr std::string_view kServedComplete = "complete";

/// The most tokens a served `complete` generates in one call (54b): a
/// backend's own lower `max_tokens:` lowers it, and a host's `max_tokens`
/// asks for fewer, never more.
inline constexpr std::int64_t kServedMaxTokens = 1024;

/// Every tool an entry's `mcp_serve:` may list, in the order they are
/// offered: what `set-backend --mcp-serve` completes from.
[[nodiscard]] std::span<const std::string_view> mcp_serve_tool_names() noexcept;

/// Why `names` cannot be an entry's `mcp_serve:`, or empty when they can: a
/// name `apogee mcp serve` cannot run, naming the ones it can. Whether the
/// entry may serve at all is its provider's to say
/// (`mcp_serve_ineligible`), asked where a provider can be built.
[[nodiscard]] std::string mcp_serve_refusal(const std::vector<std::string>& names);

/// The rule, in the words every door refuses by: why backend `name` cannot
/// serve `tools` -- its provider bills each call (`metered`), or `unknown`
/// says why that could not be asked, and unknown counts as billed.
[[nodiscard]] std::string mcp_serve_ineligible(std::string_view name,
                                               const std::vector<std::string>& tools, bool metered,
                                               std::string_view unknown);

/// The bound a served `complete` on an entry holds to: `kServedMaxTokens`,
/// or the entry's own `max_tokens` when that is lower.
[[nodiscard]] std::int64_t served_max_tokens(const std::optional<std::int64_t>& entry_max_tokens);

}  // namespace apogee::harness
