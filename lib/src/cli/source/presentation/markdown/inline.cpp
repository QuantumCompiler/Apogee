#include "markdown/inline.h"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>

namespace apogee::markdown {
namespace {

[[nodiscard]] bool is_space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

/// ASCII punctuation, the set CommonMark's flanking rules and escapes use.
/// Anything past ASCII counts as a letter: close enough for the flanking
/// rules, and it keeps `é*` from reading as punctuation.
[[nodiscard]] bool is_punct(char c) noexcept {
    const auto u = static_cast<unsigned char>(c);
    return (u >= 0x21 && u <= 0x2F) || (u >= 0x3A && u <= 0x40) || (u >= 0x5B && u <= 0x60) ||
           (u >= 0x7B && u <= 0x7E);
}

/// One piece of the line before emphasis is resolved: literal text, or a run
/// of `*`, `_` or `~~` that may open or close emphasis.
struct Node {
    std::string text;
    ansi::TextAttributes attributes;
    std::string link;
    /// `*`, `_` or `~` for a delimiter run; 0 for text.
    char delimiter = 0;
    /// Characters of the run still unmatched, shown as written at the end.
    std::size_t count = 0;
    /// The run's length as written, for the rule of three.
    std::size_t original = 0;
    bool can_open = false;
    bool can_close = false;
};

/// The characters around a delimiter run, and whether the run touches the
/// start or end of the line (which count as whitespace).
struct Neighbours {
    std::optional<char> before;
    std::optional<char> after;
};

void classify(Node& node, const Neighbours& around) {
    const bool space_before = !around.before.has_value() || is_space(*around.before);
    const bool space_after = !around.after.has_value() || is_space(*around.after);
    const bool punct_before = around.before.has_value() && is_punct(*around.before);
    const bool punct_after = around.after.has_value() && is_punct(*around.after);
    const bool left = !space_after && (!punct_after || space_before || punct_before);
    const bool right = !space_before && (!punct_before || space_after || punct_after);
    if (node.delimiter == '_') {
        // An underscore never opens or closes inside a word: snake_case_name.
        node.can_open = left && (!right || punct_before);
        node.can_close = right && (!left || punct_after);
    } else {
        node.can_open = left;
        node.can_close = right;
    }
}

void apply(ansi::TextAttributes& attributes, char delimiter, std::size_t used) {
    if (delimiter == '~') {
        attributes.strike = true;
    } else if (used == 2) {
        attributes.bold = true;
    } else {
        attributes.italic = true;
    }
}

[[nodiscard]] std::string plain(const std::vector<Span>& spans) {
    std::string out;
    for (const Span& span : spans) {
        out += span.text;
    }
    return out;
}

class Parser {
public:
    Parser(std::string_view text, const InlineOptions& options) : text_{text}, options_{options} {}

    [[nodiscard]] std::vector<Span> run() {
        for (std::size_t i = 0; i < text_.size();) {
            const char c = text_[i];
            if (c == '\\' && i + 1 < text_.size() && is_punct(text_[i + 1])) {
                pending_ += text_[i + 1];
                i += 2;
                continue;
            }
            if (c == '`') {
                code_span(i);
                continue;
            }
            if (c == '[' || (c == '!' && i + 1 < text_.size() && text_[i + 1] == '[')) {
                if (link(i)) {
                    continue;
                }
            }
            if (c == '<' && autolink(i)) {
                continue;
            }
            if ((c == 'h' || c == 'w') && bare_url(i)) {
                continue;
            }
            if (c == '*' || c == '_' || c == '~') {
                delimiter_run(i);
                continue;
            }
            pending_ += c;
            ++i;
        }
        flush_text();
        resolve_emphasis();
        return spans();
    }

private:
    [[nodiscard]] ansi::TextAttributes base() const {
        return options_.base;
    }

    void flush_text() {
        if (!pending_.empty()) {
            nodes_.push_back(Node{.text = std::move(pending_), .attributes = base()});
            pending_.clear();
        }
    }

    /// A run of backticks, closed by a run of the same length or shown as
    /// written. Its content is literal: no emphasis, no links.
    void code_span(std::size_t& i) {
        std::size_t run = 0;
        while (i + run < text_.size() && text_[i + run] == '`') {
            ++run;
        }
        for (std::size_t j = i + run; j < text_.size();) {
            if (text_[j] != '`') {
                ++j;
                continue;
            }
            std::size_t closing = 0;
            while (j + closing < text_.size() && text_[j + closing] == '`') {
                ++closing;
            }
            if (closing == run) {
                std::string content{text_.substr(i + run, j - i - run)};
                // One space either side is padding (``` `` `a` `` ```), unless
                // the span is nothing but spaces.
                if (content.size() >= 2 && content.front() == ' ' && content.back() == ' ' &&
                    content.find_first_not_of(' ') != std::string::npos) {
                    content = content.substr(1, content.size() - 2);
                }
                flush_text();
                ansi::TextAttributes attributes = base();
                attributes.color = kCodeColor;
                nodes_.push_back(Node{.text = std::move(content), .attributes = attributes});
                i = j + closing;
                return;
            }
            j += closing;
        }
        pending_.append(run, '`');
        i += run;
    }

    /// `[text](url)` or `![alt](url)`, or false with nothing consumed.
    bool link(std::size_t& i) {
        const bool image = text_[i] == '!';
        const std::size_t open = image ? i + 1 : i;
        std::optional<std::size_t> close;
        int depth = 0;
        for (std::size_t k = open; k < text_.size(); ++k) {
            const char c = text_[k];
            if (c == '\\') {
                ++k;
            } else if (c == '[') {
                ++depth;
            } else if (c == ']' && --depth == 0) {
                close = k;
                break;
            }
        }
        if (!close.has_value() || *close + 1 >= text_.size() || text_[*close + 1] != '(') {
            return false;
        }
        std::size_t p = *close + 2;
        while (p < text_.size() && text_[p] == ' ') {
            ++p;
        }
        std::string destination;
        if (p < text_.size() && text_[p] == '<') {
            const std::size_t end = text_.find('>', p + 1);
            if (end == std::string_view::npos) {
                return false;
            }
            destination = std::string{text_.substr(p + 1, end - p - 1)};
            p = end + 1;
        } else {
            int parens = 0;
            while (p < text_.size()) {
                const char c = text_[p];
                if (c == '\\' && p + 1 < text_.size()) {
                    destination += text_[p + 1];
                    p += 2;
                    continue;
                }
                if (is_space(c) || (c == ')' && parens == 0)) {
                    break;
                }
                parens += c == '(' ? 1 : (c == ')' ? -1 : 0);
                destination += c;
                ++p;
            }
        }
        while (p < text_.size() && text_[p] == ' ') {
            ++p;
        }
        if (p < text_.size() && (text_[p] == '"' || text_[p] == '\'' || text_[p] == '(')) {
            const char closer = text_[p] == '(' ? ')' : text_[p];
            const std::size_t end = text_.find(closer, p + 1);
            if (end == std::string_view::npos) {
                return false;
            }
            p = end + 1;
            while (p < text_.size() && text_[p] == ' ') {
                ++p;
            }
        }
        if (p >= text_.size() || text_[p] != ')') {
            return false;
        }
        const std::string_view inner = text_.substr(open + 1, *close - open - 1);
        flush_text();
        if (image) {
            ansi::TextAttributes attributes = base();
            attributes.dim = true;
            nodes_.push_back(Node{.text = "[image: " + std::string{inner} + "]",
                                  .attributes = attributes,
                                  .link = options_.hyperlinks ? destination : std::string{}});
        } else {
            InlineOptions inner_options = options_;
            inner_options.base.underline = true;
            const std::vector<Span> spans = render_inline(inner, inner_options);
            for (const Span& span : spans) {
                nodes_.push_back(Node{.text = span.text,
                                      .attributes = span.attributes,
                                      .link = options_.hyperlinks ? destination : std::string{}});
            }
            const std::string shown = plain(spans);
            if (!options_.hyperlinks && !destination.empty() && destination != shown &&
                destination != "mailto:" + shown) {
                ansi::TextAttributes attributes = base();
                attributes.dim = true;
                nodes_.push_back(Node{.text = " (" + destination + ")", .attributes = attributes});
            }
        }
        i = p + 1;
        return true;
    }

    /// `<https://…>` and `<mailto:…>`.
    bool autolink(std::size_t& i) {
        const std::size_t end = text_.find('>', i + 1);
        if (end == std::string_view::npos) {
            return false;
        }
        const std::string_view inner = text_.substr(i + 1, end - i - 1);
        const bool scheme = inner.starts_with("http://") || inner.starts_with("https://") ||
                            inner.starts_with("mailto:");
        if (!scheme || inner.find_first_of(" \t<") != std::string_view::npos) {
            return false;
        }
        push_url(inner, std::string{inner});
        i = end + 1;
        return true;
    }

    /// A URL written bare, as GFM links it: at a word's start, running to
    /// whitespace, without the sentence's closing punctuation.
    bool bare_url(std::size_t& i) {
        if (i > 0) {
            const char before = text_[i - 1];
            if (!is_space(before) && before != '(' && before != '"' && before != '\'' &&
                before != '*' && before != '_' && before != '~') {
                return false;
            }
        }
        const std::string_view rest = text_.substr(i);
        const bool www = rest.starts_with("www.");
        if (!www && !rest.starts_with("http://") && !rest.starts_with("https://")) {
            return false;
        }
        std::size_t end = i;
        while (end < text_.size() && !is_space(text_[end]) && text_[end] != '<') {
            ++end;
        }
        std::size_t opens = 0;
        std::size_t closes = 0;
        for (std::size_t k = i; k < end; ++k) {
            opens += text_[k] == '(' ? 1 : 0;
            closes += text_[k] == ')' ? 1 : 0;
        }
        while (end > i) {
            const char last = text_[end - 1];
            if (last == ')' && closes > opens) {
                --closes;
                --end;
            } else if (last == '.' || last == ',' || last == ':' || last == ';' || last == '!' ||
                       last == '?' || last == '"' || last == '\'' || last == '*' || last == '_' ||
                       last == '~') {
                --end;
            } else {
                break;
            }
        }
        const std::string_view url = text_.substr(i, end - i);
        if (url.size() <= (www ? 4U : 8U)) {
            return false;
        }
        flush_text();
        push_url(url, www ? "http://" + std::string{url} : std::string{url});
        i = end;
        return true;
    }

    void push_url(std::string_view shown, std::string target) {
        flush_text();
        ansi::TextAttributes attributes = base();
        attributes.underline = true;
        nodes_.push_back(Node{.text = std::string{shown},
                              .attributes = attributes,
                              .link = options_.hyperlinks ? std::move(target) : std::string{}});
    }

    void delimiter_run(std::size_t& i) {
        const char c = text_[i];
        std::size_t run = 0;
        while (i + run < text_.size() && text_[i + run] == c) {
            ++run;
        }
        // Strikethrough is two tildes, exactly: a single one is "about".
        if (c == '~' && run != 2) {
            pending_.append(run, c);
            i += run;
            return;
        }
        flush_text();
        Node node{.attributes = base(), .delimiter = c, .count = run, .original = run};
        Neighbours around;
        if (i > 0) {
            around.before = text_[i - 1];
        }
        if (i + run < text_.size()) {
            around.after = text_[i + run];
        }
        classify(node, around);
        nodes_.push_back(std::move(node));
        i += run;
    }

    /// CommonMark's "process emphasis": each closer, left to right, matched
    /// with the nearest opener of its kind that the rule of three allows.
    void resolve_emphasis() {
        for (std::size_t c = 0; c < nodes_.size(); ++c) {
            while (nodes_[c].delimiter != 0 && nodes_[c].can_close && nodes_[c].count > 0) {
                std::optional<std::size_t> found;
                for (std::size_t o = c; o-- > 0;) {
                    const Node& opener = nodes_[o];
                    const Node& closer = nodes_[c];
                    if (opener.delimiter != closer.delimiter || !opener.can_open ||
                        opener.count == 0) {
                        continue;
                    }
                    const bool either_way = opener.can_close || closer.can_open;
                    if (either_way && (opener.original + closer.original) % 3 == 0 &&
                        (opener.original % 3 != 0 || closer.original % 3 != 0)) {
                        continue;
                    }
                    found = o;
                    break;
                }
                if (!found.has_value()) {
                    break;
                }
                Node& opener = nodes_[*found];
                Node& closer = nodes_[c];
                const std::size_t used =
                    closer.delimiter == '~' || (opener.count >= 2 && closer.count >= 2) ? 2 : 1;
                for (std::size_t k = *found + 1; k < c; ++k) {
                    apply(nodes_[k].attributes, closer.delimiter, used);
                    // Delimiters inside a matched pair can no longer match
                    // across it.
                    nodes_[k].can_open = false;
                    nodes_[k].can_close = false;
                }
                opener.count -= used;
                closer.count -= used;
            }
        }
    }

    [[nodiscard]] std::vector<Span> spans() const {
        std::vector<Span> out;
        for (const Node& node : nodes_) {
            std::string text =
                node.delimiter != 0 ? std::string(node.count, node.delimiter) : node.text;
            if (text.empty()) {
                continue;
            }
            if (!out.empty() && out.back().attributes == node.attributes &&
                out.back().link == node.link) {
                out.back().text += text;
            } else {
                out.push_back(Span{std::move(text), node.attributes, node.link});
            }
        }
        return out;
    }

    std::string_view text_;
    const InlineOptions& options_;
    std::vector<Node> nodes_;
    std::string pending_;
};

}  // namespace

std::vector<Span> render_inline(std::string_view text, const InlineOptions& options) {
    return Parser{text, options}.run();
}

}  // namespace apogee::markdown
