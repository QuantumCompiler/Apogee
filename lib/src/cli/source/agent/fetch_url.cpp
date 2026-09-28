#include "agent/fetch_url.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cctype>
#include <charconv>
#include <utility>

#include "agent/readable.h"
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

/// Whether the first bytes of `body` read as text rather than binary: no
/// NUL byte, grep's own rule.
bool looks_like_text(std::string_view body) {
    return body.substr(0, 8192).find('\0') == std::string_view::npos;
}

/// Whether `body` opens as an HTML document, whatever its type says.
bool looks_like_html(std::string_view body) {
    std::string head;
    for (const char c : body.substr(0, 1024)) {
        head += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return head.find("<!doctype html") != std::string::npos ||
           head.find("<html") != std::string::npos || head.find("<body") != std::string::npos ||
           head.find("<head") != std::string::npos;
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

BodyKind classify_body(std::string_view content_type, std::string_view body, std::string& what) {
    std::string media;
    for (const char c : content_type.substr(0, content_type.find(';'))) {
        if (c != ' ' && c != '\t') {
            media += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    const auto refused = [&what](std::string description) {
        what = std::move(description);
        return BodyKind::Refused;
    };
    // By its bytes before its label: a PDF served as anything is a PDF.
    if (body.starts_with("%PDF-") || media == "application/pdf") {
        return refused("a PDF");
    }
    if (media == "text/html" || media == "application/xhtml+xml") {
        return BodyKind::Html;
    }
    if (media.starts_with("text/") || media.ends_with("+json") || media.ends_with("+xml")) {
        return BodyKind::Text;
    }
    for (const std::string_view text :
         {"application/json", "application/xml", "application/javascript",
          "application/x-javascript", "application/ecmascript", "application/yaml",
          "application/x-yaml", "application/toml"}) {
        if (media == text) {
            return BodyKind::Text;
        }
    }
    if (media.empty() || media == "application/octet-stream" || media == "binary/octet-stream") {
        // Unlabelled, or labelled as anything: read what it is.
        if (looks_like_text(body)) {
            return looks_like_html(body) ? BodyKind::Html : BodyKind::Text;
        }
        return refused("binary data" + (media.empty() ? std::string{} : " (" + media + ")"));
    }
    if (media.starts_with("image/")) {
        return refused("an image (" + media + ")");
    }
    if (media.starts_with("audio/")) {
        return refused("audio (" + media + ")");
    }
    if (media.starts_with("video/")) {
        return refused("a video (" + media + ")");
    }
    if (media.starts_with("font/")) {
        return refused("a font (" + media + ")");
    }
    if (media.find("zip") != std::string::npos || media.find("tar") != std::string::npos ||
        media.find("compressed") != std::string::npos || media.find("7z") != std::string::npos) {
        return refused("an archive (" + media + ")");
    }
    return refused(media);
}

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
        "Fetch a web page and read its main content as Markdown: its headings, lists, code, "
        "tables and links (absolute URLs you can fetch next), without the menus, banners and "
        "footers around it. One call returns at most " +
        std::to_string(max_bytes / 1024) +
        " KB of text: a longer page says so and gives the offset to read on from. It reads "
        "HTML and text (plain, Markdown, JSON, XML); a PDF, an image or any other file is "
        "refused. Use it to read a URL you already have -- from the user, or from a search "
        "result. It cannot search: give it a URL, not a query. A website the user has not "
        "allowed is asked about first, and may be refused.";
    tool.parameters_schema = nlohmann::json{
        {"type", "object"},
        {"properties",
         {{"url", {{"type", "string"}, {"description", "The absolute URL to fetch."}}},
          {"offset",
           {{"type", "integer"},
            {"minimum", 0},
            {"description",
             "Where to read on from in a long page: the offset the previous page ended "
             "with. Leave it out for the start."}}}}},
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
        std::size_t offset = 0;
        if (const auto it = parsed.find("offset"); it != parsed.end() && !it->is_null()) {
            std::int64_t value = -1;
            if (it->is_number_integer()) {
                value = it->get<std::int64_t>();
            } else if (it->is_string()) {
                const std::string text = it->get<std::string>();
                const auto [end, error] =
                    std::from_chars(text.data(), text.data() + text.size(), value);
                if (error != std::errc{} || end != text.data() + text.size()) {
                    value = -1;
                }
            }
            if (value < 0) {
                return ToolOutcome{
                    "Error: offset is the number a previous page of this URL "
                    "ended with, 0 or more.",
                    true};
            }
            offset = static_cast<std::size_t>(value);
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

            std::string what;
            const BodyKind kind = classify_body(result.content_type, result.body, what);
            if (kind == BodyKind::Refused) {
                return ToolOutcome{"Error: " + address + " is " + what +
                                       ", which fetch_url does not read: it reads web pages "
                                       "and text (HTML, plain text, Markdown, JSON, XML). "
                                       "Continue without it, or look for a web page with the "
                                       "same content.",
                                   true};
            }
            std::string charset = charset_of(result.content_type);
            if (charset.empty() && kind == BodyKind::Html) {
                charset = meta_charset(result.body);
            }
            const std::string body = as_utf8(result.body, charset);
            ReadablePage readable;
            if (kind == BodyKind::Html) {
                readable = extract_readable(body, *current);
            } else {
                readable.text = body;
            }

            std::string header;
            if (!readable.title.empty()) {
                header += "Title: " + readable.title + "\n";
            }
            header += "URL: " + address;
            if (address != requested) {
                header += " (redirected from " + requested + ")";
            }
            header += "\n";

            const TextPage page = page_of(readable.text, offset, max_bytes);
            if (page.count == 0) {
                return ToolOutcome{"Error: offset " + std::to_string(offset) +
                                       " is past the end of the text at " + address + " (" +
                                       std::to_string(readable.text.size()) +
                                       " bytes); read it from the start, without an offset.",
                                   true};
            }
            if (page.text.empty()) {
                return ToolOutcome{header +
                                       "\nThe page has no readable text. It may be built "
                                       "by JavaScript, which fetch_url does not run.",
                                   false};
            }
            std::string out = header;
            if (page.count > 1) {
                out += "Page " + std::to_string(page.number) + " of " + std::to_string(page.count) +
                       "\n";
            }
            out += "\n" + page.text;
            // Saying so matters: a model handed silently truncated text will
            // confidently answer about the part it never saw.
            if (page.next != 0) {
                out += "\n\n[Page " + std::to_string(page.number) + " of " +
                       std::to_string(page.count) +
                       ". The page continues: call fetch_url with the same url and offset " +
                       std::to_string(page.next) + ".]";
            } else if (page.count > 1) {
                out += "\n\n[Page " + std::to_string(page.number) + " of " +
                       std::to_string(page.count) + ": the end of the page.]";
            }
            return ToolOutcome{out, false};
        }
    };

    return tool;
}

}  // namespace apogee::agent
