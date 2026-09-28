#include "agent/readable.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace apogee::agent {
namespace {

// ---------------------------------------------------------------------------
// Characters
// ---------------------------------------------------------------------------

constexpr std::string_view kReplacement = "\xEF\xBF\xBD";  // U+FFFD

void append_code_point(std::string& out, std::uint32_t code) {
    if (code == 0 || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
        out += kReplacement;
    } else if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xC0U | (code >> 6U));
        out += static_cast<char>(0x80U | (code & 0x3FU));
    } else if (code < 0x10000) {
        out += static_cast<char>(0xE0U | (code >> 12U));
        out += static_cast<char>(0x80U | ((code >> 6U) & 0x3FU));
        out += static_cast<char>(0x80U | (code & 0x3FU));
    } else {
        out += static_cast<char>(0xF0U | (code >> 18U));
        out += static_cast<char>(0x80U | ((code >> 12U) & 0x3FU));
        out += static_cast<char>(0x80U | ((code >> 6U) & 0x3FU));
        out += static_cast<char>(0x80U | (code & 0x3FU));
    }
}

bool continues_character(char byte) {
    return (static_cast<unsigned char>(byte) & 0xC0U) == 0x80U;
}

std::string lowered(std::string_view text) {
    std::string out{text};
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool starts_with_ci(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && lowered(text.substr(0, prefix.size())) == prefix;
}

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

/// Windows-1252's 0x80-0x9F, where it differs from ISO-8859-1's controls.
constexpr std::array<std::uint16_t, 32> kWindows1252{
    0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
    0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD, 0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
    0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178};

// ---------------------------------------------------------------------------
// Entities
// ---------------------------------------------------------------------------

constexpr std::array<std::pair<std::string_view, std::uint32_t>, 52> kEntities{{
    {"amp", '&'},       {"lt", '<'},        {"gt", '>'},        {"quot", '"'},
    {"apos", '\''},     {"nbsp", ' '},      {"ensp", ' '},      {"emsp", ' '},
    {"thinsp", ' '},    {"ndash", 0x2013},  {"mdash", 0x2014},  {"hellip", 0x2026},
    {"lsquo", 0x2018},  {"rsquo", 0x2019},  {"sbquo", 0x201A},  {"ldquo", 0x201C},
    {"rdquo", 0x201D},  {"bdquo", 0x201E},  {"laquo", 0x00AB},  {"raquo", 0x00BB},
    {"lsaquo", 0x2039}, {"rsaquo", 0x203A}, {"copy", 0x00A9},   {"reg", 0x00AE},
    {"trade", 0x2122},  {"middot", 0x00B7}, {"bull", 0x2022},   {"deg", 0x00B0},
    {"times", 0x00D7},  {"divide", 0x00F7}, {"plusmn", 0x00B1}, {"euro", 0x20AC},
    {"pound", 0x00A3},  {"yen", 0x00A5},    {"cent", 0x00A2},   {"sect", 0x00A7},
    {"para", 0x00B6},   {"larr", 0x2190},   {"rarr", 0x2192},   {"uarr", 0x2191},
    {"darr", 0x2193},   {"harr", 0x2194},   {"iexcl", 0x00A1},  {"iquest", 0x00BF},
    {"frac12", 0x00BD}, {"frac14", 0x00BC}, {"frac34", 0x00BE}, {"micro", 0x00B5},
    {"prime", 0x2032},  {"Prime", 0x2033},  {"hearts", 0x2665}, {"check", 0x2713},
}};

/// HTML's names for U+00A0 to U+00FF, in order: the Latin-1 letters and
/// signs older pages spell by name.
constexpr std::array<std::string_view, 96> kLatin1Entities{
    "nbsp",   "iexcl",  "cent",   "pound",  "curren", "yen",    "brvbar", "sect",   "uml",
    "copy",   "ordf",   "laquo",  "not",    "shy",    "reg",    "macr",   "deg",    "plusmn",
    "sup2",   "sup3",   "acute",  "micro",  "para",   "middot", "cedil",  "sup1",   "ordm",
    "raquo",  "frac14", "frac12", "frac34", "iquest", "Agrave", "Aacute", "Acirc",  "Atilde",
    "Auml",   "Aring",  "AElig",  "Ccedil", "Egrave", "Eacute", "Ecirc",  "Euml",   "Igrave",
    "Iacute", "Icirc",  "Iuml",   "ETH",    "Ntilde", "Ograve", "Oacute", "Ocirc",  "Otilde",
    "Ouml",   "times",  "Oslash", "Ugrave", "Uacute", "Ucirc",  "Uuml",   "Yacute", "THORN",
    "szlig",  "agrave", "aacute", "acirc",  "atilde", "auml",   "aring",  "aelig",  "ccedil",
    "egrave", "eacute", "ecirc",  "euml",   "igrave", "iacute", "icirc",  "iuml",   "eth",
    "ntilde", "ograve", "oacute", "ocirc",  "otilde", "ouml",   "divide", "oslash", "ugrave",
    "uacute", "ucirc",  "uuml",   "yacute", "thorn",  "yuml"};

/// `text` with its character references decoded: the named ones above,
/// and every numeric one. An unknown name stays as written; soft hyphens
/// and zero-width joiners go, since they are layout, not text.
std::string decode_entities(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] != '&') {
            out += text[i++];
            continue;
        }
        const std::size_t semicolon = text.find(';', i + 1);
        if (semicolon == std::string_view::npos || semicolon - i > 12) {
            out += text[i++];
            continue;
        }
        const std::string_view name = text.substr(i + 1, semicolon - i - 1);
        if (name.size() > 1 && name[0] == '#') {
            std::uint32_t code = 0;
            bool hex = name[1] == 'x' || name[1] == 'X';
            bool valid = name.size() > (hex ? 2U : 1U);
            for (std::size_t k = hex ? 2 : 1; k < name.size() && valid; ++k) {
                const char c = name[k];
                const int digit = std::isdigit(static_cast<unsigned char>(c)) != 0 ? c - '0'
                                  : hex && std::isxdigit(static_cast<unsigned char>(c)) != 0
                                      ? std::tolower(static_cast<unsigned char>(c)) - 'a' + 10
                                      : -1;
                if (digit < 0 || code > 0x10FFFF) {
                    valid = false;
                } else {
                    code = code * (hex ? 16U : 10U) + static_cast<std::uint32_t>(digit);
                }
            }
            if (valid) {
                if (code == 0xAD || code == 0x200B || code == 0x200C || code == 0x200D) {
                    // soft hyphen and zero-width characters: nothing to read
                } else if (code == 0xA0) {
                    out += ' ';
                } else if (code >= 0x80 && code <= 0x9F) {
                    append_code_point(out, kWindows1252.at(code - 0x80));
                } else {
                    append_code_point(out, code);
                }
                i = semicolon + 1;
                continue;
            }
        } else if (name == "shy" || name == "zwj" || name == "zwnj") {
            i = semicolon + 1;
            continue;
        } else {
            bool found = false;
            for (const auto& [entity, code] : kEntities) {
                if (name == entity) {
                    append_code_point(out, code);
                    found = true;
                    break;
                }
            }
            for (std::size_t k = 0; !found && k < kLatin1Entities.size(); ++k) {
                if (name == kLatin1Entities.at(k)) {
                    append_code_point(out, static_cast<std::uint32_t>(0xA0 + k));
                    found = true;
                }
            }
            if (found) {
                i = semicolon + 1;
                continue;
            }
        }
        out += text[i++];
    }
    return out;
}

// ---------------------------------------------------------------------------
// The tree
// ---------------------------------------------------------------------------

/// One node of the page: an element, or a run of text (`tag` empty).
/// Kept flat, by index: the tree is walked, never rearranged.
struct Node {
    std::string tag;
    std::string text;
    std::vector<std::pair<std::string, std::string>> attributes;
    std::vector<std::size_t> children;
    std::size_t parent = 0;

    [[nodiscard]] std::string_view attribute(std::string_view name) const {
        for (const auto& [key, value] : attributes) {
            if (key == name) {
                return value;
            }
        }
        return {};
    }

    [[nodiscard]] bool has(std::string_view name) const {
        return std::any_of(attributes.begin(), attributes.end(),
                           [name](const auto& attribute) { return attribute.first == name; });
    }
};

constexpr std::array<std::string_view, 16> kVoid{
    "area", "base", "br",    "col",    "embed", "hr",  "img",    "input",
    "link", "meta", "param", "source", "track", "wbr", "keygen", "frame"};

/// Elements whose content is not markup and is never read: skipped whole
/// while parsing.
constexpr std::array<std::string_view, 7> kRawSkipped{"script",   "style",  "noscript", "template",
                                                      "textarea", "iframe", "xmp"};

/// Starting one of these closes an open paragraph.
constexpr std::array<std::string_view, 30> kClosesParagraph{
    "address",    "article", "aside",   "blockquote", "details", "div",  "dl",  "fieldset",
    "figcaption", "figure",  "footer",  "form",       "h1",      "h2",   "h3",  "h4",
    "h5",         "h6",      "header",  "hr",         "main",    "menu", "nav", "ol",
    "p",          "pre",     "section", "table",      "ul",      "li"};

/// Deeper than this, a page is pathological (or hostile): further elements
/// are flattened into the one at the limit.
constexpr std::size_t kMaxDepth = 256;

template <std::size_t N>
bool one_of(std::string_view name, const std::array<std::string_view, N>& names) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

class Tree {
public:
    explicit Tree(std::string_view html) {
        nodes_.push_back(Node{"#document", {}, {}, {}, 0});
        stack_.push_back(0);
        parse(html);
    }

    [[nodiscard]] const Node& node(std::size_t index) const {
        return nodes_.at(index);
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return nodes_.size();
    }

    /// The first element named `tag` under `from`, depth first.
    [[nodiscard]] std::optional<std::size_t> find(std::string_view tag,
                                                  std::size_t from = 0) const {
        for (const std::size_t child : nodes_.at(from).children) {
            if (nodes_.at(child).tag == tag) {
                return child;
            }
            if (const std::optional<std::size_t> found = find(tag, child); found.has_value()) {
                return found;
            }
        }
        return std::nullopt;
    }

    /// Whether `ancestor` is `index` or above it.
    [[nodiscard]] bool within(std::size_t index, std::size_t ancestor) const {
        for (std::size_t at = index;; at = nodes_.at(at).parent) {
            if (at == ancestor) {
                return true;
            }
            if (at == 0) {
                return false;
            }
        }
    }

private:
    void parse(std::string_view html) {
        std::size_t i = 0;
        while (i < html.size()) {
            if (html[i] != '<') {
                const std::size_t next = html.find('<', i);
                text(html.substr(
                    i, next == std::string_view::npos ? std::string_view::npos : next - i));
                i = next == std::string_view::npos ? html.size() : next;
                continue;
            }
            if (html.substr(i).starts_with("<!--")) {
                const std::size_t end = html.find("-->", i + 4);
                i = end == std::string_view::npos ? html.size() : end + 3;
                continue;
            }
            if (i + 1 < html.size() && (html[i + 1] == '!' || html[i + 1] == '?')) {
                const std::size_t end = html.find('>', i);
                i = end == std::string_view::npos ? html.size() : end + 1;
                continue;
            }
            const bool closing = i + 1 < html.size() && html[i + 1] == '/';
            const std::size_t name_start = i + (closing ? 2 : 1);
            if (name_start >= html.size() ||
                std::isalpha(static_cast<unsigned char>(html[name_start])) == 0) {
                text(html.substr(i, 1));  // a '<' that starts no tag is text
                ++i;
                continue;
            }
            std::size_t name_end = name_start;
            while (name_end < html.size() && !is_space(html[name_end]) && html[name_end] != '>' &&
                   html[name_end] != '/') {
                ++name_end;
            }
            const std::string name = lowered(html.substr(name_start, name_end - name_start));
            if (closing) {
                const std::size_t end = html.find('>', name_end);
                i = end == std::string_view::npos ? html.size() : end + 1;
                close(name);
                continue;
            }
            Node element;
            element.tag = name;
            bool self_closing = false;
            i = attributes(html, name_end, element, self_closing);
            if (one_of(name, kRawSkipped)) {
                // Gone, but it still parts the words either side: "a" and "b"
                // around a script are two words, not "ab".
                i = skip_raw(html, i, name);
                text(" ");
                continue;
            }
            if (name == "title") {
                const std::size_t end = find_closing(html, i, name);
                element.text = decode_entities(html.substr(i, end - i));
                open(std::move(element), /*push=*/false);
                i = skip_raw(html, i, name);
                continue;
            }
            open(std::move(element), !self_closing && !one_of(name, kVoid));
        }
    }

    /// Reads a start tag's attributes from `at`; returns where the tag ends.
    static std::size_t attributes(std::string_view html, std::size_t at, Node& element,
                                  bool& self_closing) {
        while (at < html.size()) {
            while (at < html.size() && is_space(html[at])) {
                ++at;
            }
            if (at >= html.size()) {
                break;
            }
            if (html[at] == '>') {
                return at + 1;
            }
            if (html[at] == '/') {
                self_closing = at + 1 < html.size() && html[at + 1] == '>';
                ++at;
                continue;
            }
            const std::size_t name_start = at;
            while (at < html.size() && !is_space(html[at]) && html[at] != '=' && html[at] != '>' &&
                   html[at] != '/') {
                ++at;
            }
            std::string name = lowered(html.substr(name_start, at - name_start));
            while (at < html.size() && is_space(html[at])) {
                ++at;
            }
            std::string value;
            if (at < html.size() && html[at] == '=') {
                ++at;
                while (at < html.size() && is_space(html[at])) {
                    ++at;
                }
                if (at < html.size() && (html[at] == '"' || html[at] == '\'')) {
                    const char quote = html[at];
                    const std::size_t end = html.find(quote, at + 1);
                    const std::size_t stop = end == std::string_view::npos ? html.size() : end;
                    value = decode_entities(html.substr(at + 1, stop - at - 1));
                    at = stop == html.size() ? stop : stop + 1;
                } else {
                    const std::size_t start = at;
                    while (at < html.size() && !is_space(html[at]) && html[at] != '>') {
                        ++at;
                    }
                    value = decode_entities(html.substr(start, at - start));
                }
            }
            if (!name.empty() && element.attributes.size() < 64) {
                element.attributes.emplace_back(std::move(name), std::move(value));
            }
        }
        return html.size();
    }

    /// Where `</name` next starts, case-insensitively, or the end.
    static std::size_t find_closing(std::string_view html, std::size_t from,
                                    std::string_view name) {
        const std::string closing = "</" + std::string{name};
        for (std::size_t at = html.find("</", from); at != std::string_view::npos;
             at = html.find("</", at + 2)) {
            if (starts_with_ci(html.substr(at), closing)) {
                return at;
            }
        }
        return html.size();
    }

    static std::size_t skip_raw(std::string_view html, std::size_t from, std::string_view name) {
        const std::size_t at = find_closing(html, from, name);
        const std::size_t end = html.find('>', at);
        return end == std::string_view::npos ? html.size() : end + 1;
    }

    void text(std::string_view raw) {
        if (raw.empty()) {
            return;
        }
        const std::size_t parent = stack_.back();
        std::vector<std::size_t>& siblings = nodes_.at(parent).children;
        if (!siblings.empty() && nodes_.at(siblings.back()).tag.empty()) {
            nodes_.at(siblings.back()).text += decode_entities(raw);
            return;
        }
        nodes_.push_back(Node{{}, decode_entities(raw), {}, {}, parent});
        nodes_.at(parent).children.push_back(nodes_.size() - 1);
    }

    /// Pops through the nearest open `names`, looking no further than a
    /// `boundaries` element; false when there is none to pop.
    template <std::size_t N, std::size_t M>
    bool pop_through(const std::array<std::string_view, N>& names,
                     const std::array<std::string_view, M>& boundaries) {
        for (std::size_t k = stack_.size(); k-- > 1;) {
            const std::string& tag = nodes_.at(stack_[k]).tag;
            if (one_of(tag, names)) {
                stack_.resize(k);
                return true;
            }
            if (one_of(tag, boundaries)) {
                return false;
            }
        }
        return false;
    }

    /// The end tags HTML leaves implied: a new item closes the last one.
    void imply_ends(std::string_view name) {
        if (one_of(name, kClosesParagraph) && nodes_.at(stack_.back()).tag == "p") {
            stack_.pop_back();
        }
        if (name == "li") {
            (void)pop_through(std::array<std::string_view, 1>{"li"},
                              std::array<std::string_view, 3>{"ul", "ol", "menu"});
        } else if (name == "dt" || name == "dd") {
            (void)pop_through(std::array<std::string_view, 2>{"dt", "dd"},
                              std::array<std::string_view, 1>{"dl"});
        } else if (name == "tr") {
            (void)pop_through(std::array<std::string_view, 1>{"tr"},
                              std::array<std::string_view, 4>{"table", "thead", "tbody", "tfoot"});
        } else if (name == "td" || name == "th") {
            (void)pop_through(std::array<std::string_view, 2>{"td", "th"},
                              std::array<std::string_view, 2>{"tr", "table"});
        } else if (name == "thead" || name == "tbody" || name == "tfoot") {
            (void)pop_through(std::array<std::string_view, 3>{"thead", "tbody", "tfoot"},
                              std::array<std::string_view, 1>{"table"});
        } else if (name == "option") {
            (void)pop_through(std::array<std::string_view, 1>{"option"},
                              std::array<std::string_view, 2>{"select", "datalist"});
        }
    }

    void open(Node element, bool push) {
        imply_ends(element.tag);
        element.parent = stack_.back();
        nodes_.push_back(std::move(element));
        const std::size_t index = nodes_.size() - 1;
        nodes_.at(stack_.back()).children.push_back(index);
        if (push && stack_.size() < kMaxDepth) {
            stack_.push_back(index);
        }
    }

    void close(std::string_view name) {
        for (std::size_t k = stack_.size(); k-- > 1;) {
            if (nodes_.at(stack_[k]).tag == name) {
                stack_.resize(k);
                return;
            }
        }
        // A stray end tag closes nothing.
    }

    std::vector<Node> nodes_;
    std::vector<std::size_t> stack_;
};

// ---------------------------------------------------------------------------
// What is page furniture
// ---------------------------------------------------------------------------

/// Never content, wherever they are.
constexpr std::array<std::string_view, 17> kFurniture{
    "nav",   "aside", "svg",   "button", "select", "input",  "dialog", "canvas",  "object",
    "embed", "audio", "video", "map",    "head",   "option", "math",   "datalist"};

constexpr std::array<std::string_view, 12> kFurnitureRoles{
    "navigation",  "banner", "contentinfo", "complementary", "search",  "dialog",
    "alertdialog", "menu",   "menubar",     "toolbar",       "tablist", "status"};

/// Class and id fragments that mark banners, share bars, sidebars and the
/// rest -- matched as substrings of the element's own class and id.
constexpr std::array<std::string_view, 35> kFurnitureNames{
    "cookie",   "consent",         "gdpr",           "newsletter",    "subscribe", "advert",
    "sponsor",  "breadcrumb",      "sidebar",        "skip-link",     "skiplink",  "skip-to",
    "sr-only",  "visually-hidden", "visuallyhidden", "screen-reader", "share-",    "sharing",
    "social-",  "navbox",          "mw-editsection", "mw-jump-link",  "catlinks",  "printfooter",
    "noprint",  "related-",        "promo",          "headerlink",    "comments",  "disqus",
    "dropdown", "mw-portlet",      "interlanguage",  "ad-slot",       "ad-unit"};

/// Whole class names too short to match as fragments.
constexpr std::array<std::string_view, 7> kFurnitureClasses{"toc", "ad",     "ads",   "menu",
                                                            "nav", "navbar", "footer"};

// ---------------------------------------------------------------------------
// Reading the page
// ---------------------------------------------------------------------------

/// `target` with its path's `.` and `..` segments resolved (RFC 3986's
/// remove_dot_segments): what a relative link like `../x.html` means.
std::string without_dot_segments(std::string_view target) {
    const std::size_t query = target.find('?');
    const std::string_view path = target.substr(0, query);
    std::vector<std::string_view> segments;
    bool trailing = false;
    std::size_t at = path.empty() || path.front() != '/' ? 0 : 1;
    while (at <= path.size()) {
        const std::size_t end = std::min(path.find('/', at), path.size());
        const std::string_view segment = path.substr(at, end - at);
        trailing = end == path.size() && (segment == "." || segment == "..");
        if (segment == "..") {
            if (!segments.empty()) {
                segments.pop_back();
            }
        } else if (segment != ".") {
            segments.push_back(segment);
        }
        at = end + 1;
    }
    std::string out;
    for (const std::string_view segment : segments) {
        out += "/" + std::string{segment};
    }
    if (out.empty() || trailing) {
        out += "/";
    }
    if (query != std::string_view::npos) {
        out += target.substr(query);
    }
    return out;
}

class Reader {
public:
    Reader(const Tree& tree, const HttpUrl& page) : tree_{tree}, base_{page} {
        measure();
        if (const std::optional<std::size_t> head = tree_.find("head"); head.has_value()) {
            if (const std::optional<std::size_t> base = tree_.find("base", *head);
                base.has_value()) {
                if (std::optional<HttpUrl> declared =
                        resolve_redirect(page, tree_.node(*base).attribute("href"));
                    declared.has_value()) {
                    base_ = std::move(*declared);
                }
            }
        }
    }

    [[nodiscard]] std::string title() {
        if (const std::optional<std::size_t> title = tree_.find("title"); title.has_value()) {
            std::string text = collapsed(tree_.node(*title).text);
            if (!text.empty()) {
                return text;
            }
        }
        if (const std::optional<std::size_t> heading = tree_.find("h1"); heading.has_value()) {
            return collapsed(inline_text(*heading));
        }
        return {};
    }

    [[nodiscard]] std::string text() {
        root_ = choose_root();
        out_.clear();
        render(root_, /*in_section=*/is_section(tree_.node(root_).tag));
        return finish(out_);
    }

private:
    // --- Measuring and choosing ------------------------------------------

    /// Non-space characters under each node, furniture left out.
    void measure() {
        length_.assign(tree_.size(), 0);
        for (std::size_t k = tree_.size(); k-- > 0;) {
            const Node& node = tree_.node(k);
            if (node.tag.empty()) {
                length_[k] = static_cast<std::size_t>(std::count_if(
                    node.text.begin(), node.text.end(), [](char c) { return !is_space(c); }));
                continue;
            }
            if (always_furniture(node)) {
                continue;
            }
            for (const std::size_t child : node.children) {
                length_[k] += length_[child];
            }
        }
    }

    static bool is_section(std::string_view tag) {
        return tag == "article" || tag == "main" || tag == "section";
    }

    [[nodiscard]] static bool always_furniture(const Node& node) {
        if (node.tag == "aside" &&
            (lowered(node.attribute("class")).find("footnote") != std::string::npos ||
             lowered(node.attribute("role")).starts_with("doc-"))) {
            return false;  // a footnote is the text's, whatever element holds it
        }
        if (one_of(node.tag, kFurniture) || node.has("hidden") ||
            node.attribute("aria-hidden") == "true") {
            return true;
        }
        if (one_of(lowered(node.attribute("role")), kFurnitureRoles)) {
            return true;
        }
        const std::string style = lowered(node.attribute("style"));
        std::string compact;
        std::copy_if(style.begin(), style.end(), std::back_inserter(compact),
                     [](char c) { return !is_space(c); });
        return compact.find("display:none") != std::string::npos ||
               compact.find("visibility:hidden") != std::string::npos;
    }

    /// Whether `index` holds the page's content, so no name-based rule may
    /// drop it: a heading or a content landmark, or half the text.
    [[nodiscard]] bool holds_content(std::size_t index) const {
        if (length_[index] * 2 >= length_[root_] && length_[root_] > 0) {
            return true;
        }
        for (const char* tag : {"h1", "main", "article"}) {
            if (tree_.find(tag, index).has_value()) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool furniture(std::size_t index, bool in_section) const {
        const Node& node = tree_.node(index);
        if (always_furniture(node)) {
            return true;
        }
        // A page's own header and footer, not an article's.
        if ((node.tag == "header" || node.tag == "footer") && !in_section) {
            return true;
        }
        if (node.tag == "form" || !node.attribute("class").empty() ||
            !node.attribute("id").empty()) {
            if (node.tag == "form") {
                return !holds_content(index);
            }
            const std::string classes = lowered(node.attribute("class"));
            const std::string names = classes + " " + lowered(node.attribute("id"));
            bool matched = std::any_of(kFurnitureNames.begin(), kFurnitureNames.end(),
                                       [&names](std::string_view fragment) {
                                           return names.find(fragment) != std::string::npos;
                                       });
            if (!matched) {
                std::size_t at = 0;
                while (at < classes.size() && !matched) {
                    const std::size_t end = std::min(classes.find(' ', at), classes.size());
                    matched =
                        one_of(std::string_view{classes}.substr(at, end - at), kFurnitureClasses);
                    at = end + 1;
                }
            }
            return matched && !holds_content(index);
        }
        return false;
    }

    [[nodiscard]] std::size_t choose_root() const {
        std::size_t root = tree_.find("body").value_or(0);
        // The content landmark: `<main>` or `role="main"`, the largest.
        std::optional<std::size_t> main;
        for (std::size_t k = 0; k < tree_.size(); ++k) {
            const Node& node = tree_.node(k);
            if ((node.tag == "main" || lowered(node.attribute("role")) == "main") &&
                (!main.has_value() || length_[k] > length_[*main])) {
                main = k;
            }
        }
        if (main.has_value() && length_[*main] >= 200) {
            root = *main;
        }
        // Within it, the dominant article: one with at least half of all the
        // articles' text -- the story, not a list of teasers.
        std::size_t total = 0;
        std::optional<std::size_t> largest;
        for (std::size_t k = 0; k < tree_.size(); ++k) {
            if (tree_.node(k).tag != "article" || !tree_.within(k, root) ||
                (tree_.node(k).parent != 0 && inside_article(tree_.node(k).parent))) {
                continue;
            }
            total += length_[k];
            if (!largest.has_value() || length_[k] > length_[*largest]) {
                largest = k;
            }
        }
        if (largest.has_value() && length_[*largest] >= 200 && length_[*largest] * 2 >= total) {
            root = *largest;
        }
        return root;
    }

    [[nodiscard]] bool inside_article(std::size_t index) const {
        for (std::size_t at = index; at != 0; at = tree_.node(at).parent) {
            if (tree_.node(at).tag == "article") {
                return true;
            }
        }
        return false;
    }

    // --- Writing -----------------------------------------------------------

    static bool at_line_start(const std::string& out) {
        return out.empty() || out.back() == '\n';
    }

    /// Whether the line being written is only a list marker so far: the
    /// item's first paragraph belongs on it, not below it.
    static bool after_marker(const std::string& out) {
        const std::size_t start = out.rfind('\n') == std::string::npos ? 0 : out.rfind('\n') + 1;
        std::string_view line_text = std::string_view{out}.substr(start);
        while (!line_text.empty() && line_text.front() == ' ') {
            line_text.remove_prefix(1);
        }
        if (line_text == "- ") {
            return true;
        }
        std::size_t digits = 0;
        while (digits < line_text.size() &&
               std::isdigit(static_cast<unsigned char>(line_text[digits])) != 0) {
            ++digits;
        }
        return digits > 0 && line_text.substr(digits) == ". ";
    }

    static void block(std::string& out) {
        if (after_marker(out)) {
            return;
        }
        while (!out.empty() && out.back() == ' ') {
            out.pop_back();
        }
        if (out.empty()) {
            return;
        }
        if (out.back() != '\n') {
            out += '\n';
        }
        if (out.size() < 2 || out[out.size() - 2] != '\n') {
            out += '\n';
        }
    }

    static void line(std::string& out) {
        if (after_marker(out)) {
            return;
        }
        while (!out.empty() && out.back() == ' ') {
            out.pop_back();
        }
        if (!at_line_start(out)) {
            out += '\n';
        }
    }

    /// Text, its whitespace collapsed as a browser would.
    static void words(std::string& out, std::string_view text) {
        for (const char c : text) {
            if (is_space(c)) {
                if (!at_line_start(out) && out.back() != ' ') {
                    out += ' ';
                }
                continue;
            }
            out += c;
        }
    }

    [[nodiscard]] static std::string collapsed(std::string_view text) {
        std::string out;
        words(out, text);
        while (!out.empty() && (out.back() == ' ' || out.back() == '\n')) {
            out.pop_back();
        }
        return out;
    }

    /// `index` rendered as one line: what a heading, a link or a cell reads.
    [[nodiscard]] std::string inline_text(std::size_t index) {
        std::string saved;
        std::swap(saved, out_);
        const std::size_t list_depth = list_depth_;
        list_depth_ = 0;
        for (const std::size_t child : tree_.node(index).children) {
            render(child, true);
        }
        list_depth_ = list_depth;
        std::string text;
        std::swap(text, out_);
        out_ = std::move(saved);
        std::string flat;
        for (const char c : text) {
            flat += c == '\n' ? ' ' : c;
        }
        return collapsed(flat);
    }

    /// The raw text under `index`, for `<pre>`: whitespace kept, `<br>` a
    /// line break.
    void raw_text(std::size_t index, std::string& out) const {
        const Node& node = tree_.node(index);
        if (node.tag.empty()) {
            out += node.text;
            return;
        }
        if (node.tag == "br") {
            out += '\n';
            return;
        }
        for (const std::size_t child : node.children) {
            raw_text(child, out);
        }
    }

    [[nodiscard]] std::string link_target(std::string_view href) const {
        const std::string trimmed = collapsed(href);
        if (trimmed.empty() || trimmed.front() == '#') {
            return {};
        }
        std::optional<HttpUrl> url = resolve_redirect(base_, trimmed);
        if (!url.has_value()) {
            return {};
        }
        url->target = without_dot_segments(url->target);
        return url->str();
    }

    void render_link(std::size_t index) {
        const Node& node = tree_.node(index);
        std::string text = inline_text(index);
        if (text.empty()) {
            // An image link reads as its alt text.
            for (std::size_t k = index + 1; k < tree_.size() && tree_.within(k, index); ++k) {
                if (tree_.node(k).tag == "img") {
                    text = collapsed(tree_.node(k).attribute("alt"));
                    break;
                }
            }
        }
        if (text.empty()) {
            return;
        }
        const std::string target = link_target(node.attribute("href"));
        if (target.empty()) {
            // A link within the page, or to nothing a fetch can follow: its
            // words, unless they are one symbol -- a heading's pilcrow, a
            // footnote's back-arrow.
            const std::size_t characters = static_cast<std::size_t>(std::count_if(
                text.begin(), text.end(), [](char c) { return !continues_character(c); }));
            const bool symbol =
                characters == 1 && std::isalnum(static_cast<unsigned char>(text.front())) == 0;
            if (!symbol) {
                words(out_, text);
            }
            return;
        }
        if (!at_line_start(out_) && out_.back() != ' ' && out_.back() != '(' &&
            out_.back() != '[') {
            out_ += ' ';
        }
        std::string escaped;
        for (const char c : text) {
            escaped += c == ']' ? ')' : c == '[' ? '(' : c;
        }
        out_ += "[" + escaped + "](" + target + ")";
    }

    void render_table(std::size_t index, bool in_section) {
        std::vector<std::vector<std::string>> rows;
        std::vector<bool> heading;
        bool layout = false;
        std::size_t widest = 0;
        const auto row = [&](std::size_t tr) {
            std::vector<std::string> cells;
            bool all_heading = true;
            for (const std::size_t cell : tree_.node(tr).children) {
                const std::string& tag = tree_.node(cell).tag;
                if (tag != "td" && tag != "th") {
                    continue;
                }
                std::string text = inline_text(cell);
                if (text.size() > 300 || tree_.find("table", cell).has_value()) {
                    layout = true;
                }
                std::string escaped;
                for (const char c : text) {
                    escaped += c == '|' ? "\\|" : std::string(1, c);
                }
                cells.push_back(std::move(escaped));
                all_heading = all_heading && tag == "th";
            }
            if (!cells.empty()) {
                widest = std::max(widest, cells.size());
                rows.push_back(std::move(cells));
                heading.push_back(all_heading);
            }
        };
        for (const std::size_t child : tree_.node(index).children) {
            const std::string& tag = tree_.node(child).tag;
            if (tag == "tr") {
                row(child);
            } else if (tag == "thead" || tag == "tbody" || tag == "tfoot") {
                for (const std::size_t tr : tree_.node(child).children) {
                    if (tree_.node(tr).tag == "tr") {
                        row(tr);
                    }
                }
            } else if (tag == "caption") {
                block(out_);
                out_ += inline_text(child);
            }
        }
        if (layout || widest < 2) {
            // A table laid out as a page, or a single column: its cells as
            // the paragraphs they are.
            for (const std::size_t child : tree_.node(index).children) {
                if (tree_.node(child).tag != "caption") {
                    render(child, in_section);
                }
            }
            return;
        }
        block(out_);
        for (std::size_t r = 0; r < rows.size(); ++r) {
            out_ += "|";
            for (const std::string& cell : rows[r]) {
                out_ += " " + cell + " |";
            }
            out_ += "\n";
            if (r == 0 && heading[0]) {
                out_ += "|";
                for (std::size_t c = 0; c < rows[0].size(); ++c) {
                    out_ += " --- |";
                }
                out_ += "\n";
            }
        }
        block(out_);
    }

    void render_children(std::size_t index, bool in_section) {
        for (const std::size_t child : tree_.node(index).children) {
            render(child, in_section);
        }
    }

    void render(std::size_t index, bool in_section) {
        const Node& node = tree_.node(index);
        if (node.tag.empty()) {
            words(out_, node.text);
            return;
        }
        if (index != root_ && furniture(index, in_section)) {
            // A space where it stood keeps the words either side apart.
            if (!at_line_start(out_) && out_.back() != ' ') {
                out_ += ' ';
            }
            return;
        }
        const std::string& tag = node.tag;
        const bool section = in_section || is_section(tag);

        if (tag.size() == 2 && tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6') {
            const std::string text = inline_text(index);
            if (!text.empty()) {
                block(out_);
                out_ += std::string(static_cast<std::size_t>(tag[1] - '0'), '#') + " " + text;
                block(out_);
            }
            return;
        }
        if (tag == "a") {
            render_link(index);
            return;
        }
        if (tag == "br") {
            line(out_);
            return;
        }
        if (tag == "img" || tag == "hr" || tag == "title") {
            return;
        }
        if (tag == "pre") {
            std::string code;
            raw_text(index, code);
            while (!code.empty() && (code.front() == '\n' || code.front() == '\r')) {
                code.erase(0, 1);
            }
            while (!code.empty() && is_space(code.back())) {
                code.pop_back();
            }
            if (!code.empty()) {
                block(out_);
                out_ += "```\n" + code + "\n```";
                block(out_);
            }
            return;
        }
        if (tag == "code" || tag == "kbd" || tag == "samp" || tag == "tt") {
            const std::string text = inline_text(index);
            if (!text.empty()) {
                if (!at_line_start(out_) && out_.back() != ' ') {
                    out_ += ' ';
                }
                out_ += "`" + text + "`";
            }
            return;
        }
        if (tag == "table") {
            render_table(index, section);
            return;
        }
        if (tag == "ul" || tag == "ol" || tag == "menu") {
            if (list_depth_ == 0) {
                block(out_);
            } else {
                line(out_);
            }
            ++list_depth_;
            std::size_t number = 0;
            for (const std::size_t child : node.children) {
                if (tree_.node(child).tag == "li") {
                    ++number;
                    line(out_);
                    out_ += std::string(2 * (list_depth_ - 1), ' ') +
                            (tag == "ol" ? std::to_string(number) + ". " : std::string{"- "});
                    const std::size_t mark = out_.size();
                    render_children(child, section);
                    if (out_.size() == mark) {
                        // An item with nothing readable in it.
                        out_.resize(out_.rfind('\n') == std::string::npos ? 0
                                                                          : out_.rfind('\n') + 1);
                    }
                } else {
                    render(child, section);
                }
            }
            --list_depth_;
            if (list_depth_ == 0) {
                block(out_);
            } else {
                line(out_);
            }
            return;
        }
        if (tag == "blockquote") {
            std::string saved;
            std::swap(saved, out_);
            render_children(index, section);
            std::string quoted = finish(out_);
            out_ = std::move(saved);
            if (!quoted.empty()) {
                block(out_);
                std::size_t at = 0;
                while (at <= quoted.size()) {
                    const std::size_t end = std::min(quoted.find('\n', at), quoted.size());
                    out_ += "> " + quoted.substr(at, end - at) + "\n";
                    at = end + 1;
                }
                block(out_);
            }
            return;
        }
        if (tag == "li" || tag == "dt" || tag == "dd" || tag == "tr" || tag == "summary" ||
            tag == "figcaption" || tag == "caption") {
            line(out_);
            render_children(index, section);
            line(out_);
            return;
        }
        const bool is_block = tag == "p" || tag == "div" || tag == "section" || tag == "article" ||
                              tag == "main" || tag == "header" || tag == "footer" ||
                              tag == "figure" || tag == "address" || tag == "details" ||
                              tag == "dl" || tag == "body" || tag == "center" ||
                              tag == "fieldset" || tag == "form";
        if (is_block) {
            // Inside a list item a paragraph is a line, not a blank-line break
            // that would end the item.
            list_depth_ > 0 ? line(out_) : block(out_);
            render_children(index, section);
            list_depth_ > 0 ? line(out_) : block(out_);
            return;
        }
        render_children(index, section);
    }

    /// Trailing spaces off every line, no more than one blank line in a row,
    /// and none at either end.
    [[nodiscard]] static std::string finish(const std::string& text) {
        std::string out;
        std::size_t blank = 0;
        std::size_t at = 0;
        while (at <= text.size()) {
            const std::size_t end = std::min(text.find('\n', at), text.size());
            std::string_view line_text = std::string_view{text}.substr(at, end - at);
            while (!line_text.empty() && is_space(line_text.back())) {
                line_text.remove_suffix(1);
            }
            if (line_text.empty()) {
                ++blank;
            } else {
                if (!out.empty()) {
                    out += blank > 0 ? "\n\n" : "\n";
                }
                out += line_text;
                blank = 0;
            }
            at = end + 1;
        }
        return out;
    }

    const Tree& tree_;
    HttpUrl base_;
    std::vector<std::size_t> length_;
    std::size_t root_ = 0;
    std::string out_;
    std::size_t list_depth_ = 0;
};

/// The end of the page that starts at `start`: `start + limit` or earlier.
std::size_t page_end(std::string_view text, std::size_t start, std::size_t limit) {
    if (text.size() - start <= limit) {
        return text.size();
    }
    const std::string_view window = text.substr(start, limit);
    for (const std::string_view seam :
         {std::string_view{"\n\n"}, std::string_view{"\n"}, std::string_view{" "}}) {
        const std::size_t at = window.rfind(seam);
        if (at != std::string_view::npos && at >= limit / 2) {
            return start + at;
        }
    }
    std::size_t cut = start + limit;
    while (cut > start && continues_character(text[cut])) {
        --cut;
    }
    return cut;
}

/// Past any whitespace at `at`.
std::size_t skip_space(std::string_view text, std::size_t at) {
    while (at < text.size() && is_space(text[at])) {
        ++at;
    }
    return at;
}

}  // namespace

std::string charset_of(std::string_view content_type) {
    const std::string lower = lowered(content_type);
    const std::size_t at = lower.find("charset=");
    if (at == std::string::npos) {
        return {};
    }
    std::string value = lower.substr(at + 8);
    value = value.substr(0, value.find_first_of("; \t"));
    std::erase(value, '"');
    std::erase(value, '\'');
    return value;
}

std::string meta_charset(std::string_view html) {
    const std::string head = lowered(html.substr(0, 4096));
    for (std::size_t at = head.find("<meta"); at != std::string::npos;
         at = head.find("<meta", at + 5)) {
        const std::size_t end = head.find('>', at);
        const std::string_view tag = std::string_view{head}.substr(
            at, end == std::string::npos ? std::string::npos : end - at);
        const std::size_t charset = tag.find("charset=");
        if (charset == std::string_view::npos) {
            continue;
        }
        std::string value{tag.substr(charset + 8)};
        while (!value.empty() && (value.front() == '"' || value.front() == '\'')) {
            value.erase(0, 1);
        }
        value = value.substr(0, value.find_first_of("\"'; \t/"));
        if (!value.empty()) {
            return value;
        }
    }
    return {};
}

std::string as_utf8(std::string_view bytes, std::string_view charset) {
    std::string out;
    out.reserve(bytes.size());
    const std::string name = lowered(charset);
    if (name == "iso-8859-1" || name == "latin1" || name == "latin-1" || name == "windows-1252" ||
        name == "cp1252" || name == "us-ascii" || name == "ascii") {
        for (const char c : bytes) {
            const auto byte = static_cast<unsigned char>(c);
            if (byte >= 0x80 && byte <= 0x9F) {
                append_code_point(out, kWindows1252.at(byte - 0x80));
            } else {
                append_code_point(out, byte);
            }
        }
        return out;
    }
    for (std::size_t i = 0; i < bytes.size();) {
        const auto lead = static_cast<unsigned char>(bytes[i]);
        std::size_t length = 0;
        std::uint32_t minimum = 0;
        if (lead < 0x80) {
            out += bytes[i++];
            continue;
        }
        if ((lead & 0xE0U) == 0xC0U) {
            length = 2;
            minimum = 0x80;
        } else if ((lead & 0xF0U) == 0xE0U) {
            length = 3;
            minimum = 0x800;
        } else if ((lead & 0xF8U) == 0xF0U) {
            length = 4;
            minimum = 0x10000;
        }
        bool valid = length > 0 && i + length <= bytes.size();
        std::uint32_t code = valid ? lead & (0xFFU >> (length + 1)) : 0;
        for (std::size_t k = 1; valid && k < length; ++k) {
            const auto next = static_cast<unsigned char>(bytes[i + k]);
            valid = (next & 0xC0U) == 0x80U;
            code = (code << 6U) | (next & 0x3FU);
        }
        valid = valid && code >= minimum && code <= 0x10FFFF && !(code >= 0xD800 && code <= 0xDFFF);
        if (valid) {
            out.append(bytes.substr(i, length));
            i += length;
        } else {
            out += kReplacement;
            ++i;
        }
    }
    return out;
}

ReadablePage extract_readable(std::string_view html, const HttpUrl& page) {
    const Tree tree{html};
    Reader reader{tree, page};
    ReadablePage readable;
    readable.title = reader.title();
    readable.text = reader.text();
    return readable;
}

TextPage page_of(std::string_view text, std::size_t offset, std::size_t limit,
                 std::size_t first_limit) {
    TextPage page;
    limit = std::max<std::size_t>(limit, 1);
    const std::size_t first = skip_space(text, 0);
    // The first page may be smaller than the rest: a lookup reads only the
    // start of a page, and a model reading on wants more of it at once.
    const auto limit_at = [&](std::size_t at) {
        return at == first && first_limit > 0 ? first_limit : limit;
    };
    if (offset > text.size()) {
        page.count = 0;
        return page;
    }
    while (offset < text.size() && continues_character(text[offset])) {
        ++offset;
    }
    const std::size_t start = skip_space(text, offset);
    if (start >= text.size() && offset > 0) {
        page.count = 0;  // nothing left to read from there
        return page;
    }
    // Pages are counted from the start, so a page is always "N of M"; an
    // offset that is not a page's own start is counted from where it lands.
    std::size_t before = 0;
    for (std::size_t at = first; at < start;
         at = skip_space(text, page_end(text, at, limit_at(at)))) {
        ++before;
    }
    const std::size_t end = page_end(text, start, limit_at(start));
    std::string_view slice = text.substr(start, end - start);
    while (!slice.empty() && is_space(slice.back())) {
        slice.remove_suffix(1);
    }
    page.text = std::string{slice};
    page.number = before + 1;
    const std::size_t next = skip_space(text, end);
    page.next = next < text.size() ? next : 0;
    page.count = page.number;
    for (std::size_t at = next; at < text.size();
         at = skip_space(text, page_end(text, at, limit_at(at)))) {
        ++page.count;
    }
    return page;
}

}  // namespace apogee::agent
