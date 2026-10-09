#include "contracts/jsonc.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace apogee::harness::jsonc {
namespace {

using Json = nlohmann::ordered_json;

/// Deeper than any config, shallow enough that a hostile file cannot run the
/// recursive reader off the stack.
constexpr std::size_t kMaxDepth = 256;

/// Enough rounds for any edit a verb makes; one that has not converged by
/// then is a bug, refused rather than looped on.
constexpr std::size_t kMaxRounds = 4096;

constexpr std::string_view kByteOrderMark = "\xEF\xBB\xBF";

[[nodiscard]] bool is_space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

class Reader {
public:
    explicit Reader(std::string_view text) : text_(text) {
        if (text_.starts_with(kByteOrderMark)) {
            pos_ = kByteOrderMark.size();
        }
    }

    std::optional<Value> document() {
        skip_trivia();
        if (pos_ >= text_.size()) {
            return std::nullopt;
        }
        Value root = value(0);
        skip_trivia();
        if (pos_ < text_.size()) {
            fail("unexpected text after the document's closing bracket");
        }
        return root;
    }

private:
    [[noreturn]] void fail(const std::string& what) const {
        std::size_t line = 1;
        std::size_t column = 1;
        for (std::size_t i = 0; i < pos_ && i < text_.size(); ++i) {
            if (text_[i] == '\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
        }
        throw JsoncError("line " + std::to_string(line) + ", column " + std::to_string(column) +
                         ": " + what);
    }

    void skip_trivia() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (is_space(c)) {
                ++pos_;
                continue;
            }
            if (c == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '/') {
                while (pos_ < text_.size() && text_[pos_] != '\n') {
                    ++pos_;
                }
                continue;
            }
            if (c == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '*') {
                const std::size_t close = text_.find("*/", pos_ + 2);
                if (close == std::string_view::npos) {
                    fail("a /* comment that never closes");
                }
                pos_ = close + 2;
                continue;
            }
            return;
        }
    }

    Value value(std::size_t depth) {
        if (depth > kMaxDepth) {
            fail("nested too deeply");
        }
        if (pos_ >= text_.size()) {
            fail("expected a value, found the end of the file");
        }
        switch (text_[pos_]) {
            case '{':
                return object(depth);
            case '[':
                return array(depth);
            case '"': {
                Value node;
                node.kind = Kind::String;
                node.begin = pos_;
                (void)string();
                node.end = pos_;
                return node;
            }
            case 't':
                return word("true", Kind::Boolean);
            case 'f':
                return word("false", Kind::Boolean);
            case 'n':
                return word("null", Kind::Null);
            default:
                break;
        }
        if (text_[pos_] == '-' || (text_[pos_] >= '0' && text_[pos_] <= '9')) {
            return number();
        }
        fail(std::string{"unexpected '"} + text_[pos_] + "' where a value belongs");
    }

    Value word(std::string_view spelled, Kind kind) {
        if (text_.substr(pos_, spelled.size()) != spelled) {
            fail("unexpected '" + std::string{text_.substr(pos_, 1)} + "' where a value belongs");
        }
        Value node;
        node.kind = kind;
        node.begin = pos_;
        pos_ += spelled.size();
        node.end = pos_;
        return node;
    }

    Value number() {
        Value node;
        node.kind = Kind::Number;
        node.begin = pos_;
        const auto digits = [&] {
            const std::size_t from = pos_;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
                ++pos_;
            }
            return pos_ > from;
        };
        if (text_[pos_] == '-') {
            ++pos_;
        }
        if (pos_ < text_.size() && text_[pos_] == '0') {
            ++pos_;
        } else if (!digits()) {
            fail("a number needs digits");
        }
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            if (!digits()) {
                fail("a number's fraction needs digits");
            }
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
                ++pos_;
            }
            if (!digits()) {
                fail("a number's exponent needs digits");
            }
        }
        node.end = pos_;
        return node;
    }

    /// Reads a string at `pos_` (its opening quote), returning it decoded.
    std::string string() {
        std::string out;
        ++pos_;
        while (true) {
            if (pos_ >= text_.size()) {
                fail("a string that never closes");
            }
            const char c = text_[pos_];
            if (c == '"') {
                ++pos_;
                return out;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                fail("a control character inside a string -- write it as an escape");
            }
            if (c != '\\') {
                out.push_back(c);
                ++pos_;
                continue;
            }
            if (pos_ + 1 >= text_.size()) {
                fail("a string that never closes");
            }
            const char escape = text_[pos_ + 1];
            pos_ += 2;
            switch (escape) {
                case '"':
                case '\\':
                case '/':
                    out.push_back(escape);
                    break;
                case 'b':
                    out.push_back('\b');
                    break;
                case 'f':
                    out.push_back('\f');
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                case 'u':
                    append_code_point(out);
                    break;
                default:
                    pos_ -= 2;
                    fail(std::string{"an unknown escape \\"} + escape + " in a string");
            }
        }
    }

    std::uint32_t hex4() {
        if (pos_ + 4 > text_.size()) {
            fail("a \\u escape needs four hex digits");
        }
        std::uint32_t out = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            const char c = text_[pos_ + i];
            out <<= 4U;
            if (c >= '0' && c <= '9') {
                out |= static_cast<std::uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                out |= static_cast<std::uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                out |= static_cast<std::uint32_t>(c - 'A' + 10);
            } else {
                fail("a \\u escape needs four hex digits");
            }
        }
        pos_ += 4;
        return out;
    }

    void append_code_point(std::string& out) {
        std::uint32_t code = hex4();
        if (code >= 0xD800 && code <= 0xDBFF) {
            if (text_.substr(pos_, 2) != "\\u") {
                fail("a lone surrogate in a \\u escape");
            }
            pos_ += 2;
            const std::uint32_t low = hex4();
            if (low < 0xDC00 || low > 0xDFFF) {
                fail("a lone surrogate in a \\u escape");
            }
            code = 0x10000 + ((code - 0xD800) << 10U) + (low - 0xDC00);
        } else if (code >= 0xDC00 && code <= 0xDFFF) {
            fail("a lone surrogate in a \\u escape");
        }
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6U)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3FU)));
        } else if (code < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (code >> 12U)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3FU)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (code >> 18U)));
            out.push_back(static_cast<char>(0x80 | ((code >> 12U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3FU)));
        }
    }

    Value object(std::size_t depth) {
        Value node;
        node.kind = Kind::Object;
        node.begin = pos_;
        ++pos_;
        skip_trivia();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            node.end = pos_;
            return node;
        }
        while (true) {
            if (pos_ >= text_.size() || text_[pos_] != '"') {
                fail(pos_ < text_.size() && text_[pos_] == '}'
                         ? "a trailing comma before }"
                         : "expected a member name in double quotes");
            }
            const std::size_t key_begin = pos_;
            std::string key = string();
            if (std::ranges::find(node.keys, key) != node.keys.end()) {
                pos_ = key_begin;
                fail("the key \"" + key + "\" appears twice in one object");
            }
            skip_trivia();
            if (pos_ >= text_.size() || text_[pos_] != ':') {
                fail("expected ':' after a member name");
            }
            ++pos_;
            skip_trivia();
            node.keys.push_back(std::move(key));
            node.key_begins.push_back(key_begin);
            node.children.push_back(value(depth + 1));
            skip_trivia();
            if (pos_ < text_.size() && text_[pos_] == ',') {
                ++pos_;
                skip_trivia();
                continue;
            }
            if (pos_ < text_.size() && text_[pos_] == '}') {
                ++pos_;
                node.end = pos_;
                return node;
            }
            fail("expected ',' or '}' after a member");
        }
    }

    Value array(std::size_t depth) {
        Value node;
        node.kind = Kind::Array;
        node.begin = pos_;
        ++pos_;
        skip_trivia();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            node.end = pos_;
            return node;
        }
        while (true) {
            if (pos_ < text_.size() && text_[pos_] == ']') {
                fail("a trailing comma before ]");
            }
            node.children.push_back(value(depth + 1));
            skip_trivia();
            if (pos_ < text_.size() && text_[pos_] == ',') {
                ++pos_;
                skip_trivia();
                continue;
            }
            if (pos_ < text_.size() && text_[pos_] == ']') {
                ++pos_;
                node.end = pos_;
                return node;
            }
            fail("expected ',' or ']' after an item");
        }
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

std::string decode_string(std::string_view text, std::size_t begin) {
    // The reader decodes as it validates; re-reading one string is cheap.
    std::string_view rest = text.substr(begin);
    std::size_t close = 1;
    while (close < rest.size() && rest[close] != '"') {
        close += rest[close] == '\\' ? 2 : 1;
    }
    const Json decoded = Json::parse(rest.substr(0, close + 1));
    return decoded.get<std::string>();
}

// ---------------------------------------------------------------------------
// Rendering.
// ---------------------------------------------------------------------------

[[nodiscard]] std::string scalar_text(const Json& value) {
    return value.dump(-1, ' ', false, Json::error_handler_t::replace);
}

[[nodiscard]] bool is_container(const Json& value) {
    return value.is_object() || value.is_array();
}

/// On one line, for a value spliced into an object or array written on one.
[[nodiscard]] std::string render_inline(const Json& value) {
    if (value.is_object()) {
        if (value.empty()) {
            return "{}";
        }
        std::string out = "{";
        bool first = true;
        for (const auto& [key, member] : value.items()) {
            out += first ? "" : ", ";
            first = false;
            out += scalar_text(Json(key)) + ": " + render_inline(member);
        }
        return out + "}";
    }
    if (value.is_array()) {
        std::string out = "[";
        bool first = true;
        for (const Json& item : value) {
            out += first ? "" : ", ";
            first = false;
            out += render_inline(item);
        }
        return out + "]";
    }
    return scalar_text(value);
}

// ---------------------------------------------------------------------------
// Text geometry.
// ---------------------------------------------------------------------------

[[nodiscard]] std::size_t line_start(std::string_view text, std::size_t pos) {
    const std::size_t newline = text.rfind('\n', pos == 0 ? 0 : pos - 1);
    if (pos == 0 || newline == std::string_view::npos) {
        return 0;
    }
    return newline + 1;
}

/// Where `pos`'s line ends: before its terminator (`\n` or `\r\n`), or at
/// the end of the text.
[[nodiscard]] std::size_t line_end(std::string_view text, std::size_t pos) {
    std::size_t newline = text.find('\n', pos);
    if (newline == std::string_view::npos) {
        return text.size();
    }
    if (newline > pos && text[newline - 1] == '\r') {
        --newline;
    }
    return newline;
}

/// The whitespace that opens `pos`'s line.
[[nodiscard]] std::string line_indent(std::string_view text, std::size_t pos) {
    const std::size_t start = line_start(text, pos);
    std::size_t end = start;
    while (end < text.size() && (text[end] == ' ' || text[end] == '\t')) {
        ++end;
    }
    return std::string{text.substr(start, end - start)};
}

[[nodiscard]] bool only_space_before(std::string_view text, std::size_t pos) {
    const std::size_t start = line_start(text, pos);
    return std::ranges::all_of(text.substr(start, pos - start),
                               [](char c) { return c == ' ' || c == '\t'; });
}

[[nodiscard]] bool blank(std::string_view span) {
    return std::ranges::all_of(span, is_space);
}

[[nodiscard]] bool same_line(std::string_view text, std::size_t from, std::size_t to) {
    return text.substr(from, to - from).find('\n') == std::string_view::npos;
}

/// The first byte at or after `pos` that is not whitespace or a comment.
[[nodiscard]] std::size_t next_significant(std::string_view text, std::size_t pos) {
    while (pos < text.size()) {
        if (is_space(text[pos])) {
            ++pos;
        } else if (text.substr(pos, 2) == "//") {
            pos = line_end(text, pos);
        } else if (text.substr(pos, 2) == "/*") {
            const std::size_t close = text.find("*/", pos + 2);
            pos = close == std::string_view::npos ? text.size() : close + 2;
        } else {
            break;
        }
    }
    return pos;
}

/// The comma that follows a member or item ending at `end`, if any.
[[nodiscard]] std::optional<std::size_t> comma_after(std::string_view text, std::size_t end) {
    const std::size_t next = next_significant(text, end);
    if (next < text.size() && text[next] == ',') {
        return next;
    }
    return std::nullopt;
}

/// When a member or item ending at `end` closes its line -- its comma, then
/// at most a `//` comment, then the terminator -- one past that terminator.
[[nodiscard]] std::optional<std::size_t> rest_of_line(std::string_view text, std::size_t end,
                                                      bool want_comma) {
    std::size_t pos = end;
    const auto spaces = [&] {
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) {
            ++pos;
        }
    };
    spaces();
    if (want_comma) {
        if (pos >= text.size() || text[pos] != ',') {
            return std::nullopt;
        }
        ++pos;
        spaces();
    }
    if (text.substr(pos, 2) == "//") {
        pos = line_end(text, pos);
    }
    if (pos < text.size() && text[pos] == '\r') {
        ++pos;
    }
    if (pos >= text.size()) {
        return text.size();
    }
    if (text[pos] != '\n') {
        return std::nullopt;
    }
    return pos + 1;
}

struct Splice {
    std::size_t begin = 0;
    std::size_t end = 0;
    std::string text;
};

using Splices = std::vector<Splice>;

void apply_splices(std::string& text, const Splices& splices) {
    // Back to front, so each splice's offsets are still the original's. Two
    // at one place land in their listed order: the later one goes in first.
    std::vector<std::size_t> order(splices.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::ranges::sort(order, [&](std::size_t a, std::size_t b) {
        if (splices[a].begin != splices[b].begin) {
            return splices[a].begin > splices[b].begin;
        }
        return a > b;
    });
    for (const std::size_t i : order) {
        const Splice& splice = splices[i];
        text.replace(splice.begin, splice.end - splice.begin, splice.text);
    }
}

// ---------------------------------------------------------------------------
// The edits.
// ---------------------------------------------------------------------------

class Patcher {
public:
    Patcher(std::string_view text, std::string newline)
        : text_(text), newline_(std::move(newline)) {}

    std::optional<Splices> diff(const Value& node, const Json& target) const {
        if (node.kind == Kind::Object && target.is_object()) {
            return diff_object(node, target);
        }
        if (node.kind == Kind::Array && target.is_array()) {
            return diff_array(node, target);
        }
        if (to_json(node, text_) == target) {
            return std::nullopt;
        }
        return Splices{Splice{node.begin, node.end, render(target, indent_at(node.begin))}};
    }

private:
    std::string indent_at(std::size_t pos) const {
        return line_indent(text_, pos);
    }

    std::string member(const std::string& key, const Json& value, std::string_view indent) const {
        return scalar_text(Json(key)) + ": " + render(value, indent);
    }

    std::string render(const Json& value, std::string_view indent) const {
        return jsonc::render(value, indent, newline_);
    }

    [[nodiscard]] bool multi_line(const Value& node) const {
        return !same_line(text_, node.begin, node.end);
    }

    std::optional<Splices> diff_object(const Value& node, const Json& target) const {
        const auto index_of = [&](const std::string& key) -> std::optional<std::size_t> {
            const auto found = std::ranges::find(node.keys, key);
            if (found == node.keys.end()) {
                return std::nullopt;
            }
            return static_cast<std::size_t>(found - node.keys.begin());
        };
        for (std::size_t i = 0; i < node.keys.size(); ++i) {
            if (!target.contains(node.keys[i])) {
                return remove(node, i, node.key_begins[i]);
            }
        }
        std::optional<std::size_t> previous;
        for (const auto& [key, value] : target.items()) {
            const std::optional<std::size_t> at = index_of(key);
            if (!at.has_value()) {
                return insert_member(node, previous, key, value);
            }
            previous = at;
        }
        for (std::size_t i = 0; i < node.keys.size(); ++i) {
            if (auto splices = diff(node.children[i], target.at(node.keys[i]))) {
                return splices;
            }
        }
        return std::nullopt;
    }

    std::optional<Splices> diff_array(const Value& node, const Json& target) const {
        const std::size_t have = node.children.size();
        const std::size_t want = target.size();
        const auto item_equals = [&](std::size_t mine, std::size_t theirs) {
            return to_json(node.children[mine], text_) == target[theirs];
        };
        if (want > have) {
            bool prefix = true;
            for (std::size_t i = 0; i < have && prefix; ++i) {
                prefix = item_equals(i, i);
            }
            if (prefix) {
                return append_item(node, target[have]);
            }
        }
        if (want + 1 == have) {
            for (std::size_t drop = 0; drop < have; ++drop) {
                bool rest = true;
                for (std::size_t i = 0, j = 0; i < have && rest; ++i) {
                    if (i == drop) {
                        continue;
                    }
                    rest = item_equals(i, j++);
                }
                if (rest) {
                    return remove(node, drop, node.children[drop].begin);
                }
            }
        }
        if (want == have) {
            for (std::size_t i = 0; i < have; ++i) {
                if (auto splices = diff(node.children[i], target[i])) {
                    return splices;
                }
            }
            return std::nullopt;
        }
        return Splices{Splice{node.begin, node.end, render(target, indent_at(node.begin))}};
    }

    /// Removes member or item `i` of `node`, which starts at `start` (its
    /// key for a member, the value for an item).
    Splices remove(const Value& node, std::size_t i, std::size_t start) const {
        const std::size_t count = node.children.size();
        const bool last = i + 1 == count;
        const Value& gone = node.children[i];
        const std::string empty = node.kind == Kind::Object ? "{}" : "[]";
        const auto begin_of = [&](std::size_t index) {
            return node.kind == Kind::Object ? node.key_begins[index] : node.children[index].begin;
        };

        const std::optional<std::size_t> line_after = rest_of_line(text_, gone.end, !last);
        if (only_space_before(text_, start) && line_after.has_value()) {
            const std::size_t from = line_start(text_, start);
            if (count == 1) {
                // Nothing but whitespace left between the brackets: back to
                // the empty form an insert expands from.
                if (blank(text_.substr(node.begin + 1, from - node.begin - 1)) &&
                    blank(text_.substr(*line_after, node.end - 1 - *line_after))) {
                    return Splices{Splice{node.begin, node.end, empty}};
                }
                return Splices{Splice{from, *line_after, ""}};
            }
            if (!last) {
                return Splices{Splice{from, *line_after, ""}};
            }
            const std::optional<std::size_t> comma = comma_after(text_, node.children[i - 1].end);
            if (comma.has_value()) {
                return Splices{Splice{*comma, *comma + 1, ""}, Splice{from, *line_after, ""}};
            }
        }
        if (count == 1) {
            return Splices{Splice{node.begin, node.end, empty}};
        }
        if (!last) {
            return Splices{Splice{start, begin_of(i + 1), ""}};
        }
        return Splices{Splice{node.children[i - 1].end, gone.end, ""}};
    }

    Splices insert_member(const Value& node, std::optional<std::size_t> after,
                          const std::string& key, const Json& value) const {
        if (node.children.empty()) {
            return into_empty(node,
                              [&](std::string_view indent) { return member(key, value, indent); });
        }
        if (!multi_line(node)) {
            const std::string spliced = scalar_text(Json(key)) + ": " + render_inline(value);
            if (after.has_value()) {
                const std::size_t end = node.children[*after].end;
                return Splices{Splice{end, end, ", " + spliced}};
            }
            return Splices{Splice{node.key_begins[0], node.key_begins[0], spliced + ", "}};
        }
        if (!after.has_value()) {
            const std::size_t first = node.key_begins[0];
            const std::string indent = indent_at(first);
            if (same_line(text_, node.begin, first)) {
                return Splices{
                    Splice{first, first, member(key, value, indent) + "," + newline_ + indent}};
            }
            const std::size_t brace_end = line_end(text_, node.begin);
            return Splices{
                Splice{brace_end, brace_end, newline_ + indent + member(key, value, indent) + ","}};
        }
        const std::size_t index = *after;
        const std::string indent = indent_at(node.key_begins[index]);
        return after_sibling(node, index, node.key_begins, member(key, value, indent), indent);
    }

    Splices append_item(const Value& node, const Json& value) const {
        if (node.children.empty()) {
            if (!is_container(value) &&
                blank(text_.substr(node.begin + 1, node.end - node.begin - 2))) {
                return Splices{Splice{node.begin, node.end, "[" + render_inline(value) + "]"}};
            }
            return into_empty(node, [&](std::string_view indent) { return render(value, indent); });
        }
        const Value& last = node.children.back();
        if (!multi_line(node) || !only_space_before(text_, last.begin)) {
            return Splices{Splice{last.end, last.end, ", " + render_inline(value)}};
        }
        std::vector<std::size_t> begins;
        begins.reserve(node.children.size());
        for (const Value& item : node.children) {
            begins.push_back(item.begin);
        }
        const std::string indent = indent_at(last.begin);
        return after_sibling(node, node.children.size() - 1, begins, render(value, indent), indent);
    }

    /// A member or item written on its own line after sibling `index`.
    Splices after_sibling(const Value& node, std::size_t index,
                          const std::vector<std::size_t>& begins, const std::string& added,
                          const std::string& indent) const {
        const Value& sibling = node.children[index];
        if (const std::optional<std::size_t> comma = comma_after(text_, sibling.end)) {
            const std::size_t next = begins[index + 1];
            if (same_line(text_, *comma, next)) {
                return Splices{Splice{next, next, added + ", "}};
            }
            const std::size_t end = line_end(text_, *comma);
            return Splices{Splice{end, end, newline_ + indent + added + ","}};
        }
        const std::size_t close = node.end - 1;
        if (same_line(text_, sibling.end, close)) {
            return Splices{Splice{sibling.end, sibling.end, "," + newline_ + indent + added}};
        }
        const std::size_t end = line_end(text_, sibling.end);
        if (end == sibling.end) {
            return Splices{Splice{end, end, "," + newline_ + indent + added}};
        }
        return Splices{Splice{sibling.end, sibling.end, ","},
                       Splice{end, end, newline_ + indent + added}};
    }

    /// The first member or item of an empty object or array: `{}` opens onto
    /// lines of its own; brackets already apart (holding comments, say) take
    /// it on a line above the closing one.
    template <typename Render>
    Splices into_empty(const Value& node, Render&& rendered) const {
        const std::size_t close = node.end - 1;
        const char open = text_[node.begin];
        const char shut = text_[close];
        if (blank(text_.substr(node.begin + 1, close - node.begin - 1)) &&
            same_line(text_, node.begin, close)) {
            const std::string outer = indent_at(node.begin);
            const std::string inner = outer + "  ";
            return Splices{Splice{
                node.begin, node.end,
                std::string{open} + newline_ + inner + rendered(inner) + newline_ + outer + shut}};
        }
        if (only_space_before(text_, close)) {
            const std::string inner = indent_at(close) + "  ";
            const std::size_t from = line_start(text_, close);
            return Splices{Splice{from, from, inner + rendered(inner) + newline_}};
        }
        const std::string inner = indent_at(node.begin) + "  ";
        return Splices{Splice{
            close, close, newline_ + inner + rendered(inner) + newline_ + indent_at(node.begin)}};
    }

    std::string_view text_;
    std::string newline_;
};

}  // namespace

bool looks_like_jsonc(std::string_view content) noexcept {
    if (content.starts_with(kByteOrderMark)) {
        content.remove_prefix(kByteOrderMark.size());
    }
    const std::size_t first = content.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return false;
    }
    const std::string_view rest = content.substr(first);
    return rest.front() == '{' || rest.starts_with("//") || rest.starts_with("/*");
}

std::optional<Value> parse_document(std::string_view text) {
    return Reader{text}.document();
}

Value parse(std::string_view text) {
    std::optional<Value> root = parse_document(text);
    if (!root.has_value()) {
        throw JsoncError("line 1, column 1: no value -- the file holds only comments");
    }
    return std::move(*root);
}

Json to_json(const Value& node, std::string_view text) {
    switch (node.kind) {
        case Kind::Object: {
            Json out = Json::object();
            for (std::size_t i = 0; i < node.keys.size(); ++i) {
                out[node.keys[i]] = to_json(node.children[i], text);
            }
            return out;
        }
        case Kind::Array: {
            Json out = Json::array();
            for (const Value& item : node.children) {
                out.push_back(to_json(item, text));
            }
            return out;
        }
        case Kind::String:
            return decode_string(text, node.begin);
        case Kind::Number:
            return Json::parse(text.substr(node.begin, node.end - node.begin));
        case Kind::Boolean:
            return text[node.begin] == 't';
        case Kind::Null:
            break;
    }
    return nullptr;
}

Json parse_json(std::string_view text) {
    const std::optional<Value> root = parse_document(text);
    if (!root.has_value()) {
        return nullptr;
    }
    return to_json(*root, text);
}

std::string render(const Json& value, std::string_view indent, std::string_view newline) {
    if (!is_container(value)) {
        return scalar_text(value);
    }
    if (value.empty()) {
        return value.is_object() ? "{}" : "[]";
    }
    if (value.is_array() && std::ranges::none_of(value, is_container)) {
        return render_inline(value);
    }
    const std::string inner = std::string{indent} + "  ";
    std::string out = value.is_object() ? "{" : "[";
    bool first = true;
    if (value.is_object()) {
        for (const auto& [key, member] : value.items()) {
            out += first ? "" : ",";
            first = false;
            out += std::string{newline} + inner + scalar_text(Json(key)) + ": " +
                   render(member, inner, newline);
        }
    } else {
        for (const Json& item : value) {
            out += first ? "" : ",";
            first = false;
            out += std::string{newline} + inner + render(item, inner, newline);
        }
    }
    out += std::string{newline} + std::string{indent} + (value.is_object() ? "}" : "]");
    return out;
}

std::string indent_at(std::string_view text, std::size_t pos) {
    return line_indent(text, pos);
}

std::string insert_lines(std::string_view text, const Value& object,
                         std::optional<std::size_t> after, const std::string& key,
                         std::vector<std::string> lines, std::size_t comma_at, const Json& value) {
    const std::string newline = newline_of(text);
    const std::size_t count = object.children.size();
    std::string out{text};
    const std::size_t close = object.end - 1;
    const bool apart = !same_line(text, object.begin, object.end);
    if (count > 0 && !apart) {
        // One line: the member as a value, which patch() places the same way.
        const Json written = to_json(object, text);
        Json edited = Json::object();
        std::size_t index = 0;
        if (!after.has_value()) {
            edited[key] = value;
        }
        for (const auto& [name, member] : written.items()) {
            edited[name] = member;
            if (after.has_value() && index++ == *after) {
                edited[key] = value;
            }
        }
        const std::string region{text.substr(object.begin, object.end - object.begin)};
        return out.replace(object.begin, object.end - object.begin, patch(region, edited));
    }
    const bool follows = after.has_value() ? *after + 1 < count : count > 0;
    if (follows) {
        lines.back().insert(comma_at, ",");
    }
    std::string block;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        block += (i == 0 ? "" : newline) + lines[i];
    }
    Splices splices;
    if (count == 0) {
        if (blank(text.substr(object.begin + 1, close - object.begin - 1)) && !apart) {
            splices.push_back(Splice{object.begin + 1, close,
                                     newline + block + newline + line_indent(text, object.begin)});
        } else if (only_space_before(text, close)) {
            const std::size_t from = line_start(text, close);
            splices.push_back(Splice{from, from, block + newline});
        } else {
            splices.push_back(
                Splice{close, close, newline + block + newline + line_indent(text, object.begin)});
        }
    } else if (!after.has_value()) {
        const std::size_t end = line_end(text, object.begin);
        splices.push_back(Splice{end, end, newline + block});
    } else {
        const Value& sibling = object.children[*after];
        if (const std::optional<std::size_t> comma = comma_after(text, sibling.end)) {
            const std::size_t end = line_end(text, *comma);
            splices.push_back(Splice{end, end, newline + block});
        } else if (same_line(text, sibling.end, close)) {
            splices.push_back(Splice{sibling.end, sibling.end, "," + newline + block});
        } else {
            const std::size_t end = line_end(text, sibling.end);
            splices.push_back(Splice{sibling.end, sibling.end, ","});
            splices.push_back(Splice{end, end, newline + block});
        }
    }
    apply_splices(out, splices);
    return out;
}

std::string newline_of(std::string_view text) {
    const std::size_t newline = text.find('\n');
    if (newline != std::string_view::npos && newline > 0 && text[newline - 1] == '\r') {
        return "\r\n";
    }
    return "\n";
}

std::string patch(std::string_view text, const Json& target) {
    std::string out{text};
    const std::string newline = newline_of(text);
    for (std::size_t round = 0; round < kMaxRounds; ++round) {
        const std::optional<Value> root = parse_document(out);
        if (!root.has_value()) {
            // Nothing but comments: the value goes after them.
            if (!out.empty() && out.back() != '\n') {
                out += newline;
            }
            out += render(target, "", newline) + newline;
            continue;
        }
        const std::optional<Splices> splices = Patcher{out, newline}.diff(*root, target);
        if (!splices.has_value()) {
            return out;
        }
        apply_splices(out, *splices);
    }
    throw JsoncError("an edit that did not settle -- the file was left as it was");
}

}  // namespace apogee::harness::jsonc
