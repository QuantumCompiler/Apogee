#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "agent/tool.h"

/// `fetch_url` — read the text of a web page.
///
/// The durable half of Ommi's web tooling. Its *search* half -- scraping
/// DuckDuckGo's HTML results page with regexes, which breaks silently whenever
/// the markup changes and returns nothing rather than erroring -- is not
/// ported (user decision 2026-08-26). Search comes from the providers' own
/// server-side tools, and since 25e from `web_search` over the user's own
/// SearXNG, a JSON API rather than a page (`agent/web_search.h`). Fetching a
/// URL the model was *given* carries none of that fragility: there is no
/// result page to parse.
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
    /// The `Content-Type`, as sent: what the reader does with the body --
    /// HTML extracted, text passed through, anything else refused by name.
    std::string content_type;
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

/// How much of a page one call returns (25f). The first page is the smaller:
/// a lookup reads only the start of a page, and every byte is read by a
/// local model before it can act (a 12 KiB page was ~40 s on a hot M3 Max
/// running Qwen3.8-27B). A model reading on has decided it wants the page,
/// so it gets more at once -- a long document costs no more calls, and so no
/// more reasoning steps, than it did.
inline constexpr std::size_t kFetchFirstPageBytes = 6 * 1024;
inline constexpr std::size_t kFetchPageBytes = 12 * 1024;

/// What a response's body is, for the reader.
enum class BodyKind : std::uint8_t {
    /// Extracted: `text/html`, XHTML, or no type and markup that says so.
    Html,
    /// Passed through: `text/*`, JSON, XML, YAML and the like.
    Text,
    /// Refused by name: a PDF, an image, an archive, anything binary.
    Refused,
};

/// How `content_type` and the body's first bytes classify it; for a refusal,
/// `what` names it for the model ("a PDF", "an image (image/png)").
[[nodiscard]] BodyKind classify_body(std::string_view content_type, std::string_view body,
                                     std::string& what);

/// Builds the tool. `max_bytes` is one call's page of text, and
/// `first_bytes` the first page's (never more than `max_bytes`): a longer
/// page says so and gives the `offset` to read on from.
///
/// It reads a page as a model should (`agent/readable.h`): the main content
/// as Markdown, links absolute, the menus and banners around it left out.
/// The tool is `outbound`: every call goes through the permission gate with
/// its URL's host as the target and the URL as the detail, and every redirect
/// to a different host goes through it again before anything is fetched from
/// there.
[[nodiscard]] Tool make_fetch_url_tool(UrlFetcher fetcher, std::size_t max_bytes = kFetchPageBytes,
                                       std::size_t first_bytes = kFetchFirstPageBytes);

}  // namespace apogee::agent
