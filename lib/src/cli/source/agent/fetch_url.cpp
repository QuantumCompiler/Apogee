#include "agent/fetch_url.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cctype>
#include <utility>

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

Tool make_fetch_url_tool(UrlFetcher fetcher, std::size_t max_bytes) {
    Tool tool;
    tool.name = "fetch_url";
    tool.description =
        "Fetch a web page and return its text content with HTML markup removed. Use it to "
        "read a URL you already have -- from the user, or from a search result. It cannot "
        "search: give it a URL, not a query.";
    tool.parameters_schema = nlohmann::json{
        {"type", "object"},
        {"properties",
         {{"url", {{"type", "string"}, {"description", "The absolute URL to fetch."}}}}},
        {"required",
         nlohmann::json::array(
             {"url"})}}.dump();
    // Reading a page changes nothing, so it does not go through the permission
    // gate. Prompting for every read would train the user to approve without
    // looking, which makes the prompt worthless where it matters.
    tool.writes = false;

    tool.run = [fetcher = std::move(fetcher),
                max_bytes](std::string_view arguments) -> ToolOutcome {
        const nlohmann::json parsed = nlohmann::json::parse(arguments, nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object()) {
            return ToolOutcome{R"(Error: arguments must be a JSON object like {"url": "..."})",
                               true};
        }
        const std::string url = parsed.value("url", std::string{});
        if (url.empty()) {
            return ToolOutcome{R"(Error: missing "url".)", true};
        }
        if (!starts_with_ci(url, "http://") && !starts_with_ci(url, "https://")) {
            return ToolOutcome{"Error: '" + url + "' is not an http or https URL.", true};
        }
        if (!fetcher) {
            return ToolOutcome{"Error: fetching is not available in this session.", true};
        }

        const FetchResult result = fetcher(url);
        if (!result.error.empty()) {
            return ToolOutcome{"Error fetching " + url + ": " + result.error, true};
        }
        if (result.status < 200 || result.status >= 300) {
            return ToolOutcome{"Error fetching " + url + ": HTTP " + std::to_string(result.status),
                               true};
        }

        std::string text = strip_html(result.body);
        if (text.size() > max_bytes) {
            text.resize(max_bytes);
            // Saying so matters: a model handed silently truncated text will
            // confidently answer about the part it never saw.
            text += "\n\n[truncated]";
        }
        if (text.empty()) {
            return ToolOutcome{"The page at " + url + " contained no readable text.", false};
        }
        return ToolOutcome{text, false};
    };

    return tool;
}

}  // namespace apogee::agent
