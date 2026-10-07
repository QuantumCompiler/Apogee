#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "agent/fetch_url.h"
#include "agent/tool.h"
#include "contracts/config.h"

/// `web_search` -- find pages, answered by a SearXNG instance the user runs
/// (25e).
///
/// **This revises the 2026-08-26 decision** that search comes only from the
/// providers' own server-side tools. That decision refused search that scrapes
/// DuckDuckGo's results page with regular expressions, which breaks silently
/// whenever the markup changes and returns nothing rather than failing. SearXNG
/// answers over a stable JSON API instead, keeps its own engines working
/// against upstream changes, and fails out loud: an HTTP error, or the engines
/// it names in `unresponsive_engines`. So the rule here is **never silently
/// empty** -- every outcome is results, or an error that says what to do.
///
/// The HTTP call arrives as a `UrlFetcher` from the composition root, as
/// `fetch_url`'s does: `agent/` includes no transport, and everything here is
/// tested with no network.
namespace apogee::agent {

inline constexpr std::string_view kWebSearchToolName = "web_search";

/// One result.
struct SearchResult {
    std::string title;
    std::string url;
    std::string snippet;
    /// `YYYY-MM-DD`, when the engine gave a date; empty otherwise.
    std::string date;
};

/// A box of facts an engine returned beside the results (Wikipedia's, say).
struct SearchInfobox {
    std::string title;
    std::string content;
    std::string url;
};

/// What a search came back with.
struct SearchResponse {
    std::vector<SearchResult> results;
    /// Direct answers, when an engine gave one.
    std::vector<std::string> answers;
    std::vector<SearchInfobox> infoboxes;
    /// Engines that failed, each as `name (reason)`.
    std::vector<std::string> unresponsive;
    /// How many results the instance says there are, or 0 when it does not.
    std::size_t total = 0;
    /// Set when the search failed: what went wrong and what to do about it.
    std::string error;
};

/// One search, as the tool asks it.
struct SearchRequest {
    std::string query;
    std::size_t count = 5;
    /// `day`, `week`, `month`, `year`, or empty for any time.
    std::string time_range;
};

/// Runs one search. **The seam**: SearXNG is the one provider so far, and a
/// keyed API would be a second implementation of this, never a reshaping of
/// the tool.
using SearchProvider = std::function<SearchResponse(const SearchRequest&)>;

/// A `tools.search` section, checked and ready to use.
struct SearchInstance {
    std::string provider;
    /// The instance's address, parsed by `parse_http_url`: its `host` is the
    /// one a search reaches.
    HttpUrl base;
    std::size_t results = 5;
};

/// The instance `config` names, or nullopt. With the section absent,
/// `problem` stays empty: search is simply off. With it present but unusable
/// -- an unknown provider, a URL `parse_http_url` refuses -- `problem` says
/// why. The one reading of the section: the tool's registration, the
/// permission checker's trust in its host, and `check` all call it.
[[nodiscard]] std::optional<SearchInstance> search_instance(const harness::SearchConfig& config,
                                                            std::string& problem);

/// Reads a SearXNG `format=json` body. Results, answers, infoboxes and the
/// engines that failed; `error` set when the body is not that JSON. Pure.
[[nodiscard]] SearchResponse parse_searxng_response(std::string_view body);

/// A provider asking the SearXNG instance at `base`, one GET per search
/// through `fetch`. Every way it can fail is an error naming what to change:
/// a refusal of JSON (HTTP 403) names SearXNG's `search.formats`, a rate
/// limit (429) its bot limiter, and a connection that fails names the
/// configured URL.
[[nodiscard]] SearchProvider make_searxng_provider(HttpUrl base, UrlFetcher fetch);

/// The URL a search for `request` asks at `base`: its path with `search`
/// appended, and the query form-encoded, `format=json` always.
[[nodiscard]] std::string searxng_search_url(const HttpUrl& base, const SearchRequest& request);

/// Builds the tool.
///
/// It is `outbound` -- a query can carry anything the model has read -- with
/// `host` as the target of every call. That host is trusted by configuration
/// (the user named the instance; `make_permission_checker` counts it as
/// listed), so a search never asks; every result the model then opens is an
/// ordinary `fetch_url`, asked per website.
[[nodiscard]] Tool make_web_search_tool(SearchProvider provider, std::string host,
                                        std::size_t count);

/// The tool's text for `response`: the results numbered, with their URL,
/// date and snippet, then any answers, infoboxes and failing engines. An
/// empty response says so and names the query.
[[nodiscard]] std::string render_search(const SearchRequest& request,
                                        const SearchResponse& response);

}  // namespace apogee::agent
