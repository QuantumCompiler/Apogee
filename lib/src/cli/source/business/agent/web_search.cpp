#include "agent/web_search.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>

namespace apogee::agent {
namespace {

constexpr std::array<std::string_view, 4> kTimeRanges{"day", "week", "month", "year"};
constexpr std::size_t kTitleShown = 200;
constexpr std::size_t kSnippetShown = 300;
constexpr std::size_t kBoxShown = 500;

/// `text` cut to at most `limit` bytes, before a character rather than
/// through one, with an ellipsis when anything was cut.
std::string shortened(std::string text, std::size_t limit) {
    if (text.size() <= limit) {
        return text;
    }
    std::size_t cut = limit;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0U) == 0x80U) {
        --cut;
    }
    text.resize(cut);
    return text + "...";
}

/// Runs of whitespace as one space, and none at either end: a snippet's
/// line breaks are the page's layout, not its sense.
std::string one_line(std::string_view text) {
    std::string out;
    bool space = false;
    for (const char c : text) {
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            space = !out.empty();
            continue;
        }
        if (space) {
            out += ' ';
            space = false;
        }
        out += c;
    }
    return out;
}

std::string string_at(const nlohmann::json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

/// The `YYYY-MM-DD` a `publishedDate` starts with, or empty.
std::string date_of(const nlohmann::json& result) {
    const std::string published = string_at(result, "publishedDate");
    if (published.size() < 10) {
        return {};
    }
    for (std::size_t i = 0; i < 10; ++i) {
        const bool dash = i == 4 || i == 7;
        if (dash ? published[i] != '-'
                 : std::isdigit(static_cast<unsigned char>(published[i])) == 0) {
            return {};
        }
    }
    return published.substr(0, 10);
}

/// An answer, however the instance's version spells it: a string, or an
/// object carrying `answer` (current SearXNG), `content` or `text`.
std::string answer_text(const nlohmann::json& answer) {
    if (answer.is_string()) {
        return answer.get<std::string>();
    }
    if (!answer.is_object()) {
        return {};
    }
    for (const char* key : {"answer", "content", "text"}) {
        if (std::string text = string_at(answer, key); !text.empty()) {
            return text;
        }
    }
    return {};
}

/// `name (reason)` for a failing engine: a `[name, reason]` pair, as
/// SearXNG sends them, or a bare name.
std::string engine_failure(const nlohmann::json& entry) {
    if (entry.is_string()) {
        return entry.get<std::string>();
    }
    if (entry.is_array() && !entry.empty() && entry[0].is_string()) {
        std::string text = entry[0].get<std::string>();
        if (entry.size() > 1 && entry[1].is_string() && !entry[1].get<std::string>().empty()) {
            text += " (" + entry[1].get<std::string>() + ")";
        }
        return text;
    }
    return {};
}

/// Form encoding: letters, digits and `-._~` as they are, a space as `+`,
/// every other byte as `%XX`.
std::string form_encoded(std::string_view text) {
    static constexpr std::string_view kHex = "0123456789ABCDEF";
    std::string out;
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (std::isalnum(byte) != 0 || c == '-' || c == '.' || c == '_' || c == '~') {
            out += c;
        } else if (c == ' ') {
            out += '+';
        } else {
            out += '%';
            out += kHex[byte >> 4U];
            out += kHex[byte & 0x0FU];
        }
    }
    return out;
}

std::string trimmed(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return std::string{text.substr(begin, end - begin)};
}

std::string joined(const std::vector<std::string>& items) {
    std::string out;
    for (const std::string& item : items) {
        out += (out.empty() ? "" : ", ") + item;
    }
    return out;
}

}  // namespace

std::optional<SearchInstance> search_instance(const harness::SearchConfig& config,
                                              std::string& problem) {
    problem.clear();
    if (!config.configured()) {
        return std::nullopt;
    }
    // Absent means the one provider there is.
    const std::string provider = config.provider.empty() ? "searxng" : config.provider;
    if (provider != "searxng") {
        problem = "tools.search.provider '" + config.provider +
                  "' is not a search provider Apogee knows; the only one so far is searxng";
        return std::nullopt;
    }
    if (trimmed(config.url).empty()) {
        problem =
            "tools.search.url is not set: the address of your SearXNG, such as "
            "http://127.0.0.1:8888";
        return std::nullopt;
    }
    std::optional<HttpUrl> base = parse_http_url(trimmed(config.url));
    if (!base.has_value()) {
        problem = "tools.search.url '" + config.url +
                  "' is not an http or https address with a plain host name, such as "
                  "http://127.0.0.1:8888";
        return std::nullopt;
    }
    // The instance's address is where it lives; a query written into it
    // would be a second one beside the search's own.
    base->target = base->target.substr(0, base->target.find('?'));
    return SearchInstance{provider, std::move(*base),
                          static_cast<std::size_t>(std::max(config.results, 1))};
}

SearchResponse parse_searxng_response(std::string_view body) {
    SearchResponse response;
    const nlohmann::json parsed = nlohmann::json::parse(body, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object() || !parsed.contains("results") ||
        !parsed["results"].is_array()) {
        const std::string start = one_line(body.substr(0, 120));
        response.error = "the answer is not SearXNG's JSON" +
                         (start.empty() ? std::string{" (it was empty)"}
                                        : " (it began: " + shortened(start, 80) + ")");
        return response;
    }
    for (const nlohmann::json& item : parsed["results"]) {
        if (!item.is_object()) {
            continue;
        }
        SearchResult result;
        result.url = string_at(item, "url");
        if (result.url.empty()) {
            continue;
        }
        result.title = one_line(string_at(item, "title"));
        result.snippet = one_line(string_at(item, "content"));
        result.date = date_of(item);
        response.results.push_back(std::move(result));
    }
    if (const auto it = parsed.find("number_of_results"); it != parsed.end() && it->is_number()) {
        const double total = it->get<double>();
        response.total = total > 0 ? static_cast<std::size_t>(total) : 0;
    }
    if (const auto it = parsed.find("answers"); it != parsed.end() && it->is_array()) {
        for (const nlohmann::json& answer : *it) {
            if (std::string text = one_line(answer_text(answer)); !text.empty()) {
                response.answers.push_back(std::move(text));
            }
        }
    }
    if (const auto it = parsed.find("infoboxes"); it != parsed.end() && it->is_array()) {
        for (const nlohmann::json& box : *it) {
            if (!box.is_object()) {
                continue;
            }
            SearchInfobox infobox;
            infobox.title = one_line(string_at(box, "infobox"));
            infobox.content = one_line(string_at(box, "content"));
            infobox.url = string_at(box, "id");
            if (const auto urls = box.find("urls"); infobox.url.empty() && urls != box.end() &&
                                                    urls->is_array() && !urls->empty() &&
                                                    urls->front().is_object()) {
                infobox.url = string_at(urls->front(), "url");
            }
            if (!infobox.title.empty() || !infobox.content.empty()) {
                response.infoboxes.push_back(std::move(infobox));
            }
        }
    }
    if (const auto it = parsed.find("unresponsive_engines"); it != parsed.end() && it->is_array()) {
        for (const nlohmann::json& entry : *it) {
            if (std::string failure = engine_failure(entry); !failure.empty()) {
                response.unresponsive.push_back(std::move(failure));
            }
        }
    }
    return response;
}

std::string searxng_search_url(const HttpUrl& base, const SearchRequest& request) {
    HttpUrl url = base;
    std::string path = base.target.substr(0, base.target.find('?'));
    if (path.empty() || path.back() != '/') {
        path += '/';
    }
    url.target = path + "search?q=" + form_encoded(request.query) + "&format=json";
    if (!request.time_range.empty()) {
        url.target += "&time_range=" + form_encoded(request.time_range);
    }
    return url.str();
}

SearchProvider make_searxng_provider(HttpUrl base, UrlFetcher fetch) {
    return [base = std::move(base),
            fetch = std::move(fetch)](const SearchRequest& request) -> SearchResponse {
        SearchResponse response;
        const std::string instance = base.str();
        if (!fetch) {
            response.error = "searching is not available in this session";
            return response;
        }
        const FetchResult reply = fetch(searxng_search_url(base, request));
        if (!reply.error.empty()) {
            response.error = "could not reach SearXNG at " + instance +
                             " (tools.search.url): " + reply.error + ". Is it running?";
            return response;
        }
        if (reply.status == 403) {
            response.error = "SearXNG at " + instance +
                             " refused a JSON search (HTTP 403). Its JSON output is off by "
                             "default: add json under search.formats in its settings.yml, then "
                             "restart it";
            return response;
        }
        if (reply.status == 429) {
            response.error = "SearXNG at " + instance +
                             " refused the search as one request too many (HTTP 429): its bot "
                             "limiter is on. For an instance only you use, set server.limiter "
                             "to false in its settings.yml";
            return response;
        }
        if (reply.status >= 300 && reply.status < 400) {
            response.error = "SearXNG at " + instance + " redirected the search" +
                             (reply.location.empty() ? std::string{} : " to " + reply.location) +
                             ": set tools.search.url to the address it redirects to";
            return response;
        }
        if (reply.status != 200) {
            response.error = "SearXNG at " + instance + " answered HTTP " +
                             std::to_string(reply.status) + " to the search";
            return response;
        }
        response = parse_searxng_response(reply.body);
        if (!response.error.empty()) {
            response.error = "SearXNG at " + instance + " answered, but " + response.error;
            return response;
        }
        if (response.results.size() > request.count) {
            response.results.resize(request.count);
        }
        return response;
    };
}

std::string render_search(const SearchRequest& request, const SearchResponse& response) {
    std::string out;
    const bool nothing =
        response.results.empty() && response.answers.empty() && response.infoboxes.empty();
    if (nothing) {
        out = "No results for \"" + request.query + "\"" +
              (request.time_range.empty() ? std::string{} : " in the past " + request.time_range) +
              ". Try other words" +
              (request.time_range.empty() ? std::string{} : ", or a wider time_range") + ".";
    } else if (!response.results.empty()) {
        out = "Results for \"" + request.query + "\"";
        if (response.total > response.results.size()) {
            out += " (" + std::to_string(response.results.size()) + " of about " +
                   std::to_string(response.total) + ")";
        }
        out += ":\n";
        for (std::size_t i = 0; i < response.results.size(); ++i) {
            const SearchResult& result = response.results[i];
            out += "\n" + std::to_string(i + 1) + ". " +
                   (result.title.empty() ? result.url : shortened(result.title, kTitleShown)) +
                   "\n   " + result.url + "\n";
            std::string line = result.date;
            if (!result.snippet.empty()) {
                line += (line.empty() ? "" : " -- ") + shortened(result.snippet, kSnippetShown);
            }
            if (!line.empty()) {
                out += "   " + line + "\n";
            }
        }
    }
    for (const std::string& answer : response.answers) {
        out += (out.empty() ? "" : "\n") + std::string{"Answer: "} + shortened(answer, kBoxShown) +
               "\n";
    }
    for (const SearchInfobox& box : response.infoboxes) {
        out += (out.empty() ? "" : "\n") + std::string{"About "} + box.title + ": " +
               shortened(box.content, kBoxShown) +
               (box.url.empty() ? std::string{} : " (" + box.url + ")") + "\n";
    }
    if (!response.unresponsive.empty()) {
        out += "\nEngines that did not answer: " + joined(response.unresponsive) + ".";
    }
    if (!response.results.empty()) {
        out += "\nOpen a result with fetch_url to read it.";
    }
    while (!out.empty() && out.back() == '\n') {
        out.pop_back();
    }
    return out;
}

Tool make_web_search_tool(SearchProvider provider, std::string host, std::size_t count) {
    Tool tool;
    tool.name = std::string{kWebSearchToolName};
    // When to reach for it first, what it returns second (26p): a model reads
    // a description for whether a tool fits the question in front of it.
    tool.description =
        "Search the web for anything current, recent or that you cannot know from memory: the "
        "weather, news, prices, scores, schedules, releases, or anything after your training "
        "data. Returns the top " +
        std::to_string(count) +
        " results, each with its title, URL, date when known, and a snippet. Use it to find "
        "pages, then read one with fetch_url. time_range limits the results to the past day, "
        "week, month or year.";
    tool.parameters_schema = nlohmann::json{
        {"type", "object"},
        {"properties",
         {{"query", {{"type", "string"}, {"description", "What to search for"}}},
          {"time_range",
           {{"type", "string"},
            {"enum", nlohmann::json::array({"day", "week", "month", "year"})},
            {"description", "Only results from the past day, week, month or year"}}}}},
        {"required",
         nlohmann::json::array(
             {"query"})}}.dump();
    // Nothing on this machine changes, but a query can carry anything the
    // model has read: outbound, to the one host the user configured.
    tool.outbound = true;
    tool.describe_target = [host](std::string_view) { return host; };
    tool.describe_detail = [](std::string_view arguments) -> std::string {
        const nlohmann::json parsed = nlohmann::json::parse(arguments, nullptr, false);
        return parsed.is_object() ? "search: " + string_at(parsed, "query") : std::string{};
    };
    tool.run = [provider = std::move(provider), count](std::string_view arguments) -> ToolOutcome {
        const nlohmann::json parsed = nlohmann::json::parse(arguments, nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object()) {
            return ToolOutcome{R"(Error: arguments must be a JSON object like {"query": "..."})",
                               true};
        }
        SearchRequest request;
        request.query = trimmed(string_at(parsed, "query"));
        request.count = count;
        request.time_range = trimmed(string_at(parsed, "time_range"));
        if (request.query.empty()) {
            return ToolOutcome{R"(Error: missing "query": what to search for.)", true};
        }
        if (!request.time_range.empty() && std::find(kTimeRanges.begin(), kTimeRanges.end(),
                                                     request.time_range) == kTimeRanges.end()) {
            return ToolOutcome{
                "Error: time_range is day, week, month or year, not '" + request.time_range + "'.",
                true};
        }
        if (!provider) {
            return ToolOutcome{"Error: searching is not available in this session.", true};
        }
        const SearchResponse response = provider(request);
        if (!response.error.empty()) {
            // The instance itself failed -- unreachable, refusing, not
            // answering JSON -- and other words will not change that: a
            // small model otherwise rephrases the query until the turn's
            // step limit (found on Qwen3-VL-8B, 2026-09-28).
            return ToolOutcome{"Error: the search for \"" + request.query +
                                   "\" failed: " + response.error +
                                   ". Searching will not work until that is fixed, so do not "
                                   "search again: answer without it, and tell the user what "
                                   "to change.",
                               true, /*unavailable=*/true};
        }
        // Nothing found while engines failed is a failure, not an empty page:
        // the search never really ran.
        if (response.results.empty() && response.answers.empty() && response.infoboxes.empty() &&
            !response.unresponsive.empty()) {
            return ToolOutcome{"Error: the search for \"" + request.query +
                                   "\" found nothing, and the engines asked did not answer: " +
                                   joined(response.unresponsive) +
                                   ". Try again later, or check those engines in SearXNG.",
                               true};
        }
        return ToolOutcome{render_search(request, response), false};
    };
    return tool;
}

}  // namespace apogee::agent
