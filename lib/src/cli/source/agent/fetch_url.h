#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

#include "agent/tool.h"

/// `fetch_url` — read the text of a web page.
///
/// The durable half of Ommi's web tooling. Its *search* half is deliberately
/// not ported (user decision 2026-08-26): search meant scraping DuckDuckGo's
/// HTML results page with regexes, which breaks silently whenever the markup
/// changes and returns nothing rather than erroring. Web search comes from the
/// providers' own server-side tools instead — Anthropic's `web_search` is
/// already wired. Fetching a URL the model was *given* carries none of that
/// fragility: there is no result page to parse.
namespace apogee::agent {

/// What a fetch returned.
struct FetchResult {
    long status = 0;
    std::string body;
    /// Set when the fetch failed at the transport level.
    std::string error;
};

/// Fetches a URL.
///
/// **Injected**, so the tool is testable with no network at all — which is what
/// keeps the loop's conformance suite hermetic. The composition root wires this
/// to the real HTTP client; `agent/` includes no transport of its own.
using UrlFetcher = std::function<FetchResult(std::string_view url)>;

/// Strips HTML tags, scripts, styles, and entity escapes, collapsing whitespace.
///
/// Crude on purpose. The model wants the prose, and a real HTML parser would be
/// a dependency and a parsing-difference surface for a job where "roughly the
/// text" is entirely sufficient.
[[nodiscard]] std::string strip_html(std::string_view html);

/// Builds the tool. `max_bytes` caps the text handed back — an unbounded page
/// would blow the context window on one call.
[[nodiscard]] Tool make_fetch_url_tool(UrlFetcher fetcher, std::size_t max_bytes = 8000);

}  // namespace apogee::agent
