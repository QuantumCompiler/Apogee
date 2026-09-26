#include "agent/fetch_url.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cctype>
#include <charconv>
#include <utility>

#include "harness/host.h"

namespace apogee::agent {
namespace {

bool starts_with_ci(std::string_view text, std::string_view prefix) {
    if (text.size() < prefix.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(text[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

/// Skips an entire element including its content, for tags whose text is markup
/// rather than prose.
std::size_t skip_element(std::string_view html, std::size_t open_start, std::string_view tag) {
    const std::string closing = "</" + std::string{tag};
    std::size_t i = open_start;
    while (i < html.size()) {
        if (html[i] == '<' && starts_with_ci(html.substr(i), closing)) {
            const std::size_t close = html.find('>', i);
            return close == std::string_view::npos ? html.size() : close + 1;
        }
        ++i;
    }
    return html.size();
}

void append_entity(std::string& out, std::string_view name) {
    static const std::array<std::pair<std::string_view, std::string_view>, 6> kEntities{{
        {"amp", "&"},
        {"lt", "<"},
        {"gt", ">"},
        {"quot", "\""},
        {"apos", "'"},
        {"nbsp", " "},
    }};
    for (const auto& [entity, replacement] : kEntities) {
        if (name == entity) {
            out += replacement;
            return;
        }
    }
    if (!name.empty() && name.front() == '#') {
        // Numeric entities are dropped rather than decoded: getting UTF-8
        // encoding right here is not worth it for prose the model will read
        // past anyway.
        out += ' ';
        return;
    }
    out += ' ';
}

}  // namespace

std::string strip_html(std::string_view html) {
    std::string out;
    out.reserve(html.size() / 2);

    for (std::size_t i = 0; i < html.size();) {
        if (html[i] == '<') {
            // script and style contain code, not prose. Skipping the whole
            // element still leaves a word boundary behind: without it,
            // "a<script>…</script>b" becomes "ab" and two unrelated words
            // merge into one the model then treats as a term.
            if (starts_with_ci(html.substr(i), "<script")) {
                if (!out.empty() && out.back() != ' ' && out.back() != '\n') {
                    out.push_back(' ');
                }
                i = skip_element(html, i, "script");
                continue;
            }
            if (starts_with_ci(html.substr(i), "<style")) {
                if (!out.empty() && out.back() != ' ' && out.back() != '\n') {
                    out.push_back(' ');
                }
                i = skip_element(html, i, "style");
                continue;
            }
            const std::size_t close = html.find('>', i);
            if (close == std::string_view::npos) {
                break;
            }
            // A tag boundary is a word boundary; without this, "a</b>b" becomes
            // "ab".
            if (!out.empty() && out.back() != ' ' && out.back() != '\n') {
                out.push_back(' ');
            }
            i = close + 1;
            continue;
        }
        if (html[i] == '&') {
            const std::size_t semicolon = html.find(';', i);
            if (semicolon != std::string_view::npos && semicolon - i <= 10) {
                append_entity(out, html.substr(i + 1, semicolon - i - 1));
                i = semicolon + 1;
                continue;
            }
        }

        const char c = html[i];
        if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
            if (!out.empty() && out.back() != ' ' && out.back() != '\n') {
                out.push_back(' ');
            }
            ++i;
            continue;
        }
        out.push_back(c);
        ++i;
    }

    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

/// A path and query as a transport will send it: every byte that is not
/// printable ASCII, or that a URL may not carry raw, percent-encoded.
/// Existing `%XX` escapes pass through; the fragment never reaches a server.
std::string encode_target(std::string_view rest) {
    const std::size_t fragment = rest.find('#');
    if (fragment != std::string_view::npos) {
        rest = rest.substr(0, fragment);
    }
    std::string out;
    if (rest.empty() || rest.front() != '/') {
        out.push_back('/');
    }
    constexpr std::string_view kHex = "0123456789ABCDEF";
    for (const char c : rest) {
        const auto byte = static_cast<unsigned char>(c);
        const bool raw = byte > 0x20 && byte < 0x7f &&
                         std::string_view{"\"<>\\^`{|}"}.find(c) == std::string_view::npos;
        if (raw) {
            out.push_back(c);
            continue;
        }
        out.push_back('%');
        out.push_back(kHex[byte >> 4U]);
        out.push_back(kHex[byte & 0x0FU]);
    }
    return out;
}

bool is_redirect(long status) {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

/// The `url` argument, or empty.
std::string url_argument(std::string_view arguments) {
    const nlohmann::json parsed = nlohmann::json::parse(arguments, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return {};
    }
    const auto it = parsed.find("url");
    return it != parsed.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

}  // namespace

std::string HttpUrl::str() const {
    std::string out = scheme + "://";
    out += host.find(':') == std::string::npos ? host : "[" + host + "]";
    if (!port.empty()) {
        out += ":" + port;
    }
    out += target;
    return out;
}

std::optional<HttpUrl> parse_http_url(std::string_view url) {
    url = trim(url);
    HttpUrl parsed;
    if (starts_with_ci(url, "https://")) {
        parsed.scheme = "https";
        url.remove_prefix(8);
    } else if (starts_with_ci(url, "http://")) {
        parsed.scheme = "http";
        url.remove_prefix(7);
    } else {
        return std::nullopt;
    }

    const std::size_t authority_end = url.find_first_of("/?#");
    const std::string_view authority = url.substr(0, authority_end);
    const std::string_view rest =
        authority_end == std::string_view::npos ? std::string_view{} : url.substr(authority_end);

    // Userinfo is where the classic host confusion lives, and a model has no
    // business sending credentials in a URL. A backslash is a path separator
    // to a browser and not to curl; a percent escape hides a host's spelling.
    if (authority.find_first_of("@\\%") != std::string_view::npos) {
        return std::nullopt;
    }

    std::string_view host = authority;
    std::string_view port;
    if (!host.empty() && host.front() == '[') {
        const std::size_t close = host.find(']');
        if (close == std::string_view::npos) {
            return std::nullopt;
        }
        const std::string_view after = host.substr(close + 1);
        host = host.substr(0, close + 1);
        if (!after.empty()) {
            if (after.front() != ':') {
                return std::nullopt;
            }
            port = after.substr(1);
            if (port.empty()) {
                return std::nullopt;
            }
        }
    } else if (const std::size_t colon = host.find(':'); colon != std::string_view::npos) {
        port = host.substr(colon + 1);
        host = host.substr(0, colon);
        if (port.empty()) {
            return std::nullopt;
        }
    }
    if (!port.empty()) {
        unsigned value = 0;
        const auto [end, error] = std::from_chars(port.data(), port.data() + port.size(), value);
        if (error != std::errc{} || end != port.data() + port.size() || value == 0 ||
            value > 65535 || port.size() > 5) {
            return std::nullopt;
        }
        parsed.port = std::string{port};
    }

    const std::optional<std::string> canonical = harness::canonical_host(host);
    if (!canonical.has_value()) {
        return std::nullopt;
    }
    parsed.host = *canonical;
    parsed.target = encode_target(rest);
    return parsed;
}

std::optional<HttpUrl> resolve_redirect(const HttpUrl& base, std::string_view location) {
    location = trim(location);
    if (location.empty()) {
        return std::nullopt;
    }
    if (starts_with_ci(location, "http://") || starts_with_ci(location, "https://")) {
        return parse_http_url(location);
    }
    // Any other scheme -- `ftp:`, `file:`, `javascript:` -- is not followed.
    const std::size_t colon = location.find(':');
    const std::size_t first_separator = location.find_first_of("/?#");
    if (colon != std::string_view::npos && colon < first_separator) {
        return std::nullopt;
    }
    if (location.starts_with("//")) {
        return parse_http_url(base.scheme + ":" + std::string{location});
    }
    std::string origin = base.scheme + "://";
    origin += base.host.find(':') == std::string::npos ? base.host : "[" + base.host + "]";
    if (!base.port.empty()) {
        origin += ":" + base.port;
    }
    if (location.front() == '/') {
        return parse_http_url(origin + std::string{location});
    }
    const std::string_view base_path =
        std::string_view{base.target}.substr(0, base.target.find('?'));
    if (location.front() == '?') {
        return parse_http_url(origin + std::string{base_path} + std::string{location});
    }
    const std::string_view directory = base_path.substr(0, base_path.rfind('/') + 1);
    return parse_http_url(origin + std::string{directory} + std::string{location});
}

Tool make_fetch_url_tool(UrlFetcher fetcher, std::size_t max_bytes) {
    Tool tool;
    tool.name = std::string{kFetchUrlToolName};
    tool.description =
        "Fetch a web page and return its text content with HTML markup removed. Use it to "
        "read a URL you already have -- from the user, or from a search result. It cannot "
        "search: give it a URL, not a query. A website the user has not allowed is asked "
        "about first, and may be refused.";
    tool.parameters_schema = nlohmann::json{
        {"type", "object"},
        {"properties",
         {{"url", {{"type", "string"}, {"description", "The absolute URL to fetch."}}}}},
        {"required",
         nlohmann::json::array(
             {"url"})}}.dump();
    // Reading a page changes nothing on this machine, so it does not `write`.
    // But a URL can carry anything the model has read out of the machine, so
    // it is `outbound`: gated per website, where `writes` is gated per tool.
    tool.writes = false;
    tool.outbound = true;
    tool.describe_target = [](std::string_view arguments) -> std::string {
        const std::optional<HttpUrl> url = parse_http_url(url_argument(arguments));
        return url.has_value() ? url->host : std::string{};
    };
    tool.describe_detail = [](std::string_view arguments) -> std::string {
        const std::optional<HttpUrl> url = parse_http_url(url_argument(arguments));
        return url.has_value() ? url->str() : std::string{};
    };

    tool.run_gated = [fetcher = std::move(fetcher), max_bytes](
                         std::string_view arguments, const TargetGate& gate) -> ToolOutcome {
        const nlohmann::json parsed = nlohmann::json::parse(arguments, nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object()) {
            return ToolOutcome{R"(Error: arguments must be a JSON object like {"url": "..."})",
                               true};
        }
        const std::string url = parsed.value("url", std::string{});
        if (url.empty()) {
            return ToolOutcome{R"(Error: missing "url".)", true};
        }
        if (!starts_with_ci(trim(url), "http://") && !starts_with_ci(trim(url), "https://")) {
            return ToolOutcome{"Error: '" + url + "' is not an http or https URL.", true};
        }
        std::optional<HttpUrl> current = parse_http_url(url);
        if (!current.has_value()) {
            return ToolOutcome{"Error: '" + url +
                                   "' is not a URL fetch_url can read: it needs a plain host "
                                   "name, no user name or password, and a port from 1 to 65535.",
                               true};
        }
        if (!fetcher) {
            return ToolOutcome{"Error: fetching is not available in this session.", true};
        }

        const std::string requested = current->str();
        for (int hop = 0;; ++hop) {
            const std::string address = current->str();
            const FetchResult result = fetcher(address);
            if (!result.error.empty()) {
                return ToolOutcome{"Error fetching " + address + ": " + result.error, true};
            }
            if (is_redirect(result.status)) {
                if (hop >= kMaxRedirects) {
                    return ToolOutcome{"Error: " + requested + " redirected more than " +
                                           std::to_string(kMaxRedirects) + " times; stopped at " +
                                           address + ".",
                                       true};
                }
                if (result.location.empty()) {
                    return ToolOutcome{"Error fetching " + address + ": HTTP " +
                                           std::to_string(result.status) +
                                           " with no Location to follow.",
                                       true};
                }
                std::optional<HttpUrl> next = resolve_redirect(*current, result.location);
                if (!next.has_value()) {
                    return ToolOutcome{"Error: " + address + " redirected to '" + result.location +
                                           "', which is not an http or https URL fetch_url "
                                           "can follow.",
                                       true};
                }
                // A hop is a host: a redirect to a new one is asked about like
                // a direct fetch, or the guard is one 302 away from useless.
                // The same host was just allowed, so it is not asked again.
                if (next->host != current->host && !gate(next->host, next->str())) {
                    return ToolOutcome{"Error: " + address + " redirected to " + next->str() +
                                           ", and permission to reach " + next->host +
                                           " was not given, so nothing was fetched from it. "
                                           "Do not retry it; continue without it or ask what "
                                           "to do instead.",
                                       true};
                }
                current = std::move(next);
                continue;
            }
            if (result.status < 200 || result.status >= 300) {
                return ToolOutcome{
                    "Error fetching " + address + ": HTTP " + std::to_string(result.status), true};
            }

            std::string text = strip_html(result.body);
            if (text.size() > max_bytes) {
                text.resize(max_bytes);
                // Saying so matters: a model handed silently truncated text will
                // confidently answer about the part it never saw.
                text += "\n\n[truncated]";
            }
            if (text.empty()) {
                return ToolOutcome{"The page at " + address + " contained no readable text.",
                                   false};
            }
            if (address != requested) {
                text = "[" + requested + " redirected to " + address + "]\n\n" + text;
            }
            return ToolOutcome{text, false};
        }
    };

    return tool;
}

}  // namespace apogee::agent
