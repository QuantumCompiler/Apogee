#pragma once

#include <cstddef>
#include <functional>
#include <optional>
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

inline constexpr std::string_view kFetchUrlToolName = "fetch_url";

/// The most redirects one call follows. Each hop to a new host is asked about
/// on its own, so this bounds a loop, not the guard.
inline constexpr int kMaxRedirects = 10;

/// What a fetch returned.
struct FetchResult {
    long status = 0;
    std::string body;
    /// Set when the fetch failed at the transport level.
    std::string error;
    /// Where a redirect points (its `Location`), as sent. The fetcher must
    /// NOT follow redirects itself: the tool follows them one hop at a time,
    /// so each new host goes through the permission gate like a direct fetch.
    std::string location;
};

/// Fetches ONE URL, without following redirects.
///
/// **Injected**, so the tool is testable with no network at all — which is what
/// keeps the loop's conformance suite hermetic. The composition root wires this
/// to the real HTTP client; `agent/` includes no transport of its own.
using UrlFetcher = std::function<FetchResult(std::string_view url)>;

/// An http(s) URL in the parts the gate and the fetch both use.
///
/// **The URL fetched is rebuilt from these parts** (`str()`), never passed on
/// as the model wrote it. That is what makes the host the gate asked about
/// the host the transport connects to: two URL parsers that disagree about
/// `http://allowed.example\@evil.example/` are the classic way round a host
/// check, and here there is only one parser -- this one -- plus a rebuilt
/// URL simple enough that no other can read it differently.
struct HttpUrl {
    std::string scheme;  ///< `http` or `https`
    std::string host;    ///< canonical (`harness::canonical_host`): the gate's key
    std::string port;    ///< digits, or empty for the scheme's default
    std::string target;  ///< path and query, from `/`; the fragment dropped

    [[nodiscard]] std::string str() const;
};

/// Parses an absolute http or https URL, or nullopt.
///
/// Refused: any other scheme, userinfo (`user@host`), a backslash or percent
/// escape in the authority, a host that is not a bare host name, a port that
/// is not 1-65535. A path or query byte that is a control character, a space,
/// non-ASCII, or one of `"<>\^`{|}` is percent-encoded rather than refused.
[[nodiscard]] std::optional<HttpUrl> parse_http_url(std::string_view url);

/// Where a redirect from `base` to `location` goes: an absolute URL, a
/// scheme-relative `//host/...`, an absolute path, a query, or a relative
/// path. Nullopt when it points at anything but http or https.
[[nodiscard]] std::optional<HttpUrl> resolve_redirect(const HttpUrl& base,
                                                      std::string_view location);

/// Strips HTML tags, scripts, styles, and entity escapes, collapsing whitespace.
///
/// Crude on purpose. The model wants the prose, and a real HTML parser would be
/// a dependency and a parsing-difference surface for a job where "roughly the
/// text" is entirely sufficient.
[[nodiscard]] std::string strip_html(std::string_view html);

/// Builds the tool. `max_bytes` caps the text handed back — an unbounded page
/// would blow the context window on one call.
///
/// The tool is `outbound`: every call goes through the permission gate with
/// its URL's host as the target and the URL as the detail, and every redirect
/// to a different host goes through it again before anything is fetched from
/// there.
[[nodiscard]] Tool make_fetch_url_tool(UrlFetcher fetcher, std::size_t max_bytes = 8000);

}  // namespace apogee::agent
