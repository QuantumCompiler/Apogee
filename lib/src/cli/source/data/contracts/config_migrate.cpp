#include "contracts/config_migrate.h"

#include <nlohmann/json.hpp>

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "contracts/config.h"
#include "contracts/config_edit.h"
#include "contracts/config_yaml.h"
#include "contracts/jsonc.h"

namespace apogee::harness {
namespace {

using Json = nlohmann::ordered_json;

// ---------------------------------------------------------------------------
// Where a YAML file's comments are.
// ---------------------------------------------------------------------------

enum class LineKind : std::uint8_t {
    Blank,
    Comment,       ///< a line that is only a comment
    Content,       ///< a line with YAML on it, perhaps a comment after
    Continuation,  ///< inside a block scalar: content, whatever it looks like
};

struct SourceLine {
    LineKind role = LineKind::Blank;
    std::size_t indent = 0;
    /// After the `#`: a Comment line's text, or a Content line's trailing one.
    std::string comment;
    bool trailing = false;
};

[[noreturn]] void refuse(std::size_t line, const std::string& what) {
    throw ConfigError("line " + std::to_string(line + 1) + ": " + what +
                      " -- migrate carries a file exactly or not at all; rewrite that by hand "
                      "and run it again");
}

std::vector<std::string_view> split_lines(std::string_view text) {
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        const std::size_t next = end == std::string_view::npos ? text.size() : end + 1;
        end = end == std::string_view::npos ? text.size() : end;
        std::string_view line = text.substr(start, end - start);
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        lines.push_back(line);
        start = next;
    }
    return lines;
}

[[nodiscard]] bool is_gap(char c) {
    return c == ' ' || c == '\t';
}

/// A block scalar's header token: `|` or `>`, a chomping and an indentation
/// indicator in either order.
[[nodiscard]] bool block_header(std::string_view token) {
    if (token.empty() || (token.front() != '|' && token.front() != '>')) {
        return false;
    }
    token.remove_prefix(1);
    if (token.size() > 2) {
        return false;
    }
    return std::ranges::all_of(
        token, [](char c) { return c == '+' || c == '-' || (c >= '1' && c <= '9'); });
}

/// Reads one line with YAML on it: where a trailing comment starts (outside
/// any quoted scalar), and whether a block scalar opens, whose content then
/// sits indented past the returned column. Refuses what it cannot read one
/// line at a time.
std::optional<std::size_t> scan_content(std::string_view line, std::size_t number,
                                        SourceLine& out) {
    std::size_t pos = out.indent;
    bool at_start = true;  // where a scalar may begin
    int depth = 0;         // inside flow brackets
    std::size_t token_begin = pos;
    std::optional<std::size_t> key_column;
    std::size_t dash_column = out.indent;
    std::size_t value_end = pos;
    for (; pos < line.size(); ++pos) {
        const char c = line[pos];
        if (is_gap(c)) {
            continue;
        }
        if (c == '#' && (pos == 0 || is_gap(line[pos - 1]))) {
            out.trailing = true;
            out.comment = std::string{line.substr(pos + 1)};
            break;
        }
        value_end = pos + 1;
        if (at_start && (c == '"' || c == '\'')) {
            token_begin = pos;
            std::size_t j = pos + 1;
            while (j < line.size()) {
                if (c == '"' && line[j] == '\\') {
                    j += 2;
                    continue;
                }
                if (line[j] == c) {
                    if (c == '\'' && j + 1 < line.size() && line[j + 1] == '\'') {
                        j += 2;
                        continue;
                    }
                    break;
                }
                ++j;
            }
            if (j >= line.size()) {
                refuse(number, "a quoted value continues onto the next line");
            }
            pos = j;
            value_end = pos + 1;
            at_start = false;
            continue;
        }
        if (at_start && (c == '&' || c == '!')) {
            // An anchor or a tag: the scalar after it may still be quoted.
            while (pos + 1 < line.size() && !is_gap(line[pos + 1])) {
                ++pos;
            }
            continue;
        }
        if (c == '[' || c == '{') {
            ++depth;
            at_start = true;
            continue;
        }
        if ((c == ']' || c == '}') && depth > 0) {
            --depth;
            at_start = false;
            continue;
        }
        if (c == ',' && depth > 0) {
            at_start = true;
            continue;
        }
        if (c == ':' && (pos + 1 == line.size() || is_gap(line[pos + 1]))) {
            if (depth == 0 && !key_column.has_value()) {
                key_column = token_begin;
            }
            at_start = true;
            continue;
        }
        if (c == '-' && at_start && depth == 0 &&
            (pos + 1 == line.size() || is_gap(line[pos + 1]))) {
            dash_column = pos;
            continue;
        }
        if (at_start) {
            token_begin = pos;
        }
        at_start = false;
    }
    if (depth > 0) {
        refuse(number, "a flow list or map continues onto the next line");
    }
    // A block scalar opens when the value is its header alone.
    std::size_t header = value_end;
    while (header > out.indent && !is_gap(line[header - 1])) {
        --header;
    }
    if (block_header(line.substr(header, value_end - header))) {
        return key_column.value_or(dash_column);
    }
    return std::nullopt;
}

std::vector<SourceLine> classify(const std::vector<std::string_view>& lines) {
    std::vector<SourceLine> out(lines.size());
    std::optional<std::size_t> block_parent;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string_view line = lines[i];
        SourceLine& role = out[i];
        const std::size_t first = line.find_first_not_of(" \t");
        const bool blank = first == std::string_view::npos;
        role.indent = blank ? 0 : first;
        if (block_parent.has_value()) {
            if (blank || role.indent > *block_parent) {
                role.role = LineKind::Continuation;
                continue;
            }
            block_parent.reset();
        }
        if (blank) {
            role.role = LineKind::Blank;
            continue;
        }
        if (line[first] == '#') {
            role.role = LineKind::Comment;
            role.comment = std::string{line.substr(first + 1)};
            continue;
        }
        role.role = LineKind::Content;
        block_parent = scan_content(line, i, role);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Writing it as JSONC.
// ---------------------------------------------------------------------------

[[nodiscard]] std::string json_text(const Json& value) {
    return value.dump(-1, ' ', false, Json::error_handler_t::replace);
}

[[nodiscard]] std::string scalar_text(const YAML::Node& node) {
    const Json value = json_of_yaml_scalar(node.Scalar(), node.Tag() == "?");
    if (value.is_number()) {
        return node.Scalar();  // the token as written: a JSON number by construction
    }
    return json_text(value);
}

[[nodiscard]] std::string inline_text(const YAML::Node& node) {
    std::string out;
    bool first = true;
    switch (node.Type()) {
        case YAML::NodeType::Map:
            out = "{";
            for (const auto& pair : node) {
                out += first ? "" : ", ";
                first = false;
                out += json_text(Json(pair.first.Scalar())) + ": " + inline_text(pair.second);
            }
            return out + "}";
        case YAML::NodeType::Sequence:
            out = "[";
            for (const auto& item : node) {
                out += first ? "" : ", ";
                first = false;
                out += inline_text(item);
            }
            return out + "]";
        case YAML::NodeType::Scalar:
            return scalar_text(node);
        case YAML::NodeType::Null:
        case YAML::NodeType::Undefined:
            break;
    }
    return "null";
}

[[nodiscard]] std::optional<std::size_t> line_of(const YAML::Node& node) {
    const YAML::Mark mark = node.Mark();
    if (mark.is_null() || mark.line < 0) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(mark.line);
}

class Migrator {
public:
    /// With `sections`, a top-level key YAML leaves empty but for the
    /// commented examples indented under it -- the template's `backends:`
    /// -- becomes an empty object holding those comments, where a reader
    /// expects them, rather than `null` with its comments after it.
    Migrator(std::vector<SourceLine> roles, bool sections)
        : roles_(std::move(roles)), flushed_(roles_.size(), false), sections_(sections) {}

    /// The top-level keys written as objects that YAML read as null.
    [[nodiscard]] const std::vector<std::string>& emptied() const noexcept {
        return emptied_;
    }

    void document(const YAML::Node& root) {
        if (root.IsMap() && root.size() > 0) {
            // The file's header -- its leading comments up to the last blank
            // line before the first key -- stays above the brace.
            const std::optional<std::size_t> first = line_of(root.begin()->first);
            std::optional<std::size_t> header_end;
            for (std::size_t i = 0; first.has_value() && i < *first; ++i) {
                if (roles_[i].role == LineKind::Blank) {
                    header_end = i + 1;
                }
            }
            if (header_end.has_value()) {
                leading(*header_end, "");
            }
            open("{");
            members(root, "", std::nullopt);
            return;
        }
        if (!root.IsDefined() || root.IsNull()) {
            leading(roles_.size(), "");
            open("{}");
            return;
        }
        refuse(0, "the top level is not a mapping");
    }

    [[nodiscard]] std::string text(std::string_view newline) const {
        std::string out;
        for (const std::string& line : out_) {
            out += line;
            out += newline;
        }
        return out;
    }

    [[nodiscard]] std::size_t carried() const noexcept {
        return carried_;
    }

private:
    void open(std::string line) {
        out_.push_back(std::move(line));
    }

    void append(std::string_view text) {
        out_.back() += text;
    }

    /// The blank and comment lines before `line`, each on a line of its own
    /// at `indent`. Lines with YAML on them belong to values already written.
    void leading(std::size_t line, const std::string& indent) {
        for (; next_ < line && next_ < roles_.size(); ++next_) {
            const SourceLine& role = roles_[next_];
            if (role.role == LineKind::Blank) {
                open("");
            } else if (role.role == LineKind::Comment) {
                open(indent + "//" + role.comment);
                ++carried_;
            }
        }
    }

    /// Whether the first line after `line` with anything on it is a comment
    /// indented past `column`.
    [[nodiscard]] bool commented_under(std::size_t line, std::size_t column) const {
        for (std::size_t i = line + 1; i < roles_.size(); ++i) {
            if (roles_[i].role == LineKind::Blank) {
                continue;
            }
            return roles_[i].role == LineKind::Comment && roles_[i].indent > column;
        }
        return false;
    }

    void past(std::size_t line) {
        next_ = std::max(next_, line + 1);
    }

    /// A container's closing comments: those at or past its members' column
    /// before anything shallower -- the YAML spelling of "still inside".
    void tail(std::size_t column, const std::string& indent) {
        std::size_t take = next_;
        for (std::size_t i = next_; i < roles_.size(); ++i) {
            if (roles_[i].role == LineKind::Blank) {
                continue;
            }
            if (roles_[i].role == LineKind::Comment && roles_[i].indent >= column) {
                take = i + 1;
                continue;
            }
            break;
        }
        leading(take, indent);
    }

    /// `line`'s trailing comment, after what was just written.
    void trailing(std::optional<std::size_t> line) {
        if (!line.has_value() || *line >= roles_.size() || flushed_[*line] ||
            !roles_[*line].trailing) {
            return;
        }
        flushed_[*line] = true;
        append(" //" + roles_[*line].comment);
        ++carried_;
    }

    void value(const YAML::Node& node, const std::string& indent,
               std::optional<std::size_t> owner) {
        switch (node.Type()) {
            case YAML::NodeType::Map:
                if (node.size() == 0 || node.Style() == YAML::EmitterStyle::Flow) {
                    append(inline_text(node));
                    return;
                }
                append("{");
                members(node, indent, owner);
                return;
            case YAML::NodeType::Sequence:
                if (node.size() == 0 || node.Style() == YAML::EmitterStyle::Flow) {
                    append(inline_text(node));
                    return;
                }
                append("[");
                items(node, indent, owner);
                return;
            case YAML::NodeType::Scalar:
                append(scalar_text(node));
                return;
            case YAML::NodeType::Null:
            case YAML::NodeType::Undefined:
                break;
        }
        append("null");
    }

    void members(const YAML::Node& node, const std::string& indent,
                 std::optional<std::size_t> owner) {
        const std::string inner = indent + "  ";
        const std::optional<std::size_t> first = line_of(node.begin()->first);
        if (owner.has_value() && first.has_value() && *first > *owner) {
            trailing(owner);
        }
        const std::size_t column =
            first.has_value() ? static_cast<std::size_t>(node.begin()->first.Mark().column) : 0;
        std::size_t remaining = node.size();
        for (const auto& pair : node) {
            const std::optional<std::size_t> line = line_of(pair.first);
            if (!pair.first.IsScalar()) {
                refuse(line.value_or(0), "a key that is not a plain name");
            }
            if (line.has_value()) {
                leading(*line, inner);
                past(*line);
            }
            open(inner + json_text(Json(pair.first.Scalar())) + ": ");
            if (sections_ && !owner.has_value() && line.has_value() && pair.second.IsNull() &&
                commented_under(*line, column)) {
                append("{");
                past(*line);
                tail(column + 1, inner + "  ");
                open(inner + "}");
                emptied_.push_back(pair.first.Scalar());
            } else {
                value(pair.second, inner, line);
            }
            if (--remaining > 0) {
                append(",");
            }
            // After the comma, never before it: `"key": 1, // note`.
            trailing(line);
            if (pair.second.IsScalar()) {
                trailing(line_of(pair.second));
            }
        }
        tail(column, inner);
        open(indent + "}");
    }

    void items(const YAML::Node& node, const std::string& indent,
               std::optional<std::size_t> owner) {
        const std::string inner = indent + "  ";
        const std::optional<std::size_t> first = line_of(node[0]);
        if (owner.has_value() && first.has_value() && *first > *owner) {
            trailing(owner);
        }
        const std::size_t column = first.has_value() ? roles_[*first].indent : 0;
        std::size_t remaining = node.size();
        for (const auto& item : node) {
            const std::optional<std::size_t> line = line_of(item);
            if (line.has_value()) {
                leading(*line, inner);
                past(*line);
            }
            open(inner);
            value(item, inner, line);
            if (--remaining > 0) {
                append(",");
            }
            trailing(line);
        }
        tail(column, inner);
        open(indent + "]");
    }

    std::vector<SourceLine> roles_;
    std::vector<bool> flushed_;
    std::vector<std::string> out_;
    std::size_t next_ = 0;
    std::size_t carried_ = 0;
    bool sections_ = false;
    std::vector<std::string> emptied_;
};

// ---------------------------------------------------------------------------
// Upgrade.
// ---------------------------------------------------------------------------

/// One option the template has and the config lacks, located in both.
struct Gap {
    std::string path;                       ///< dotted
    const jsonc::Value* live = nullptr;     ///< the object it goes into
    const jsonc::Value* written = nullptr;  ///< the template's object holding it
    std::size_t index = 0;                  ///< its place in `written`
};

void find_gaps(std::string_view tmpl, const jsonc::Value& written, const jsonc::Value& live,
               const std::string& prefix, std::vector<Gap>& out, bool first_only) {
    for (std::size_t i = 0; i < written.keys.size(); ++i) {
        if (first_only && !out.empty()) {
            return;
        }
        const std::string path = prefix.empty() ? written.keys[i] : prefix + "." + written.keys[i];
        const auto found = std::ranges::find(live.keys, written.keys[i]);
        if (found == live.keys.end()) {
            out.push_back(Gap{path, &live, &written, i});
            continue;
        }
        const jsonc::Value& inner =
            live.children[static_cast<std::size_t>(found - live.keys.begin())];
        if (written.children[i].kind == jsonc::Kind::Object && inner.kind == jsonc::Kind::Object) {
            find_gaps(tmpl, written.children[i], inner, path, out, first_only);
        }
    }
}

[[nodiscard]] std::size_t start_of_line(std::string_view text, std::size_t pos) {
    const std::size_t newline = pos == 0 ? std::string_view::npos : text.rfind('\n', pos - 1);
    return newline == std::string_view::npos ? 0 : newline + 1;
}

[[nodiscard]] std::string_view trimmed(std::string_view line) {
    const std::size_t first = line.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) {
        return {};
    }
    const std::size_t last = line.find_last_not_of(" \t\r");
    return line.substr(first, last - first + 1);
}

/// Where the inserted option's lines start, in the config: its siblings'
/// indentation, or one step in from an empty object's braces.
[[nodiscard]] std::string member_indent(std::string_view text, const jsonc::Value& object,
                                        std::optional<std::size_t> after) {
    if (!object.children.empty()) {
        return jsonc::indent_at(text, object.key_begins[after.value_or(0)]);
    }
    const std::size_t close = object.end - 1;
    if (trimmed(text.substr(start_of_line(text, close), close - start_of_line(text, close)))
            .empty()) {
        return jsonc::indent_at(text, close) + "  ";
    }
    return jsonc::indent_at(text, object.begin) + "  ";
}

}  // namespace

namespace {

Migration convert(std::string_view yaml, bool sections) {
    YAML::Node root;
    try {
        root = YAML::Load(std::string{yaml});
    } catch (const YAML::Exception& e) {
        throw ConfigError(std::string{"not valid YAML: "} + e.what());
    }
    const std::vector<std::string_view> lines = split_lines(yaml);
    std::vector<SourceLine> roles = classify(lines);
    const std::size_t comments =
        static_cast<std::size_t>(std::ranges::count_if(roles, [](const SourceLine& line) {
            return line.role == LineKind::Comment || line.trailing;
        }));

    Migrator migrator{std::move(roles), sections};
    migrator.document(root);
    Migration out{migrator.text(jsonc::newline_of(yaml)), migrator.carried()};

    // Lossless or nothing: the same values, the same comments.
    Json expected = json_of_yaml(yaml, Json{});
    if (expected.is_null()) {
        expected = Json::object();
    }
    for (const std::string& key : migrator.emptied()) {
        expected[key] = Json::object();
    }
    if (jsonc::parse_json(out.text) != expected || out.comments != comments) {
        throw ConfigError(
            "the conversion did not carry the file exactly -- migrate refuses rather than lose "
            "anything; convert it by hand");
    }
    return out;
}

}  // namespace

Migration migrate_config_text(std::string_view yaml) {
    // An empty section is an empty object to every reader of the config; a
    // file whose reader says otherwise keeps its nulls.
    Migration sectioned = convert(yaml, true);
    try {
        (void)parse_config(sectioned.text, "<migrated config>");
        return sectioned;
    } catch (const ConfigError&) {
        return convert(yaml, false);
    }
}

MigrationReport migrate_config_file(const std::filesystem::path& path) {
    std::error_code code;
    if (!std::filesystem::is_regular_file(path, code)) {
        throw ConfigEditError(path.string() + ": no config file there to migrate");
    }
    const std::string original = read_config_file(path);
    if (jsonc::looks_like_jsonc(original)) {
        throw ConfigEditError(path.string() + " is already JSON -- there is nothing to migrate");
    }
    std::filesystem::path target = path;
    target.replace_extension(".json");
    std::filesystem::path backup = path;
    backup += ".bak";
    if (target != path && std::filesystem::exists(target, code)) {
        throw ConfigEditError("both " + path.string() + " and " + target.string() +
                              " exist, and which one is current is yours to say -- move one "
                              "aside, then run it again");
    }
    if (std::filesystem::exists(backup, code)) {
        throw ConfigEditError(backup.string() +
                              " already exists -- move it aside so the original can be kept "
                              "there, then run it again");
    }
    (void)parse_config(original, path.string());
    const Migration migration = migrate_config_text(original);
    (void)parse_config(migration.text, "<migrated config>");

    // The JSONC first: an interruption before the move leaves the original
    // where it was, and the move itself is one rename.
    if (target == path) {
        write_file_atomically(backup, original);
        write_file_atomically(path, migration.text);
    } else {
        write_file_atomically(target, migration.text);
        std::filesystem::rename(path, backup, code);
        if (code) {
            std::error_code cleanup;
            std::filesystem::remove(target, cleanup);
            throw ConfigEditError(path.string() + ": cannot move it to " + backup.string() + ": " +
                                  code.message());
        }
    }
    MigrationReport report{path, target, backup, migration.comments, 0};
    const Json migrated = jsonc::parse_json(migration.text);
    report.keys = migrated.is_object() ? migrated.size() : 0;
    return report;
}

std::vector<std::string> missing_template_options(std::string_view content,
                                                  std::string_view template_text) {
    std::vector<std::string> out;
    if (!jsonc::looks_like_jsonc(content)) {
        return out;
    }
    try {
        const std::optional<jsonc::Value> live = jsonc::parse_document(content);
        const jsonc::Value written = jsonc::parse(template_text);
        if (!live.has_value() || live->kind != jsonc::Kind::Object) {
            return out;
        }
        std::vector<Gap> gaps;
        find_gaps(template_text, written, *live, "", gaps, false);
        for (const Gap& gap : gaps) {
            out.push_back(gap.path);
        }
    } catch (const jsonc::JsoncError&) {
        // A config that does not parse is check's Config failure, not a gap.
    }
    return out;
}

std::vector<std::string> missing_template_options(std::string_view content) {
    return missing_template_options(content, config_template());
}

namespace {

Upgrade upgrade(std::string_view content, std::string_view template_text) {
    Upgrade out{std::string{content}, {}};
    const jsonc::Value written = jsonc::parse(template_text);
    // Each round inserts one option the file lacked; more rounds than the
    // template has options would be an insertion that did not land.
    const auto options = static_cast<std::size_t>(std::ranges::count(template_text, ':'));
    for (std::size_t round = 0; round <= options; ++round) {
        const jsonc::Value live = jsonc::parse(out.text);
        std::vector<Gap> gaps;
        find_gaps(template_text, written, live, "", gaps, true);
        if (gaps.empty()) {
            return out;
        }
        const Gap& gap = gaps.front();
        const jsonc::Value& object = *gap.live;
        const jsonc::Value& from = *gap.written;
        const std::string& key = from.keys[gap.index];

        // After the option the template puts before it that the file has.
        std::optional<std::size_t> after;
        for (std::size_t j = gap.index; j-- > 0 && !after.has_value();) {
            const auto found = std::ranges::find(object.keys, from.keys[j]);
            if (found != object.keys.end()) {
                after = static_cast<std::size_t>(found - object.keys.begin());
            }
        }

        // The option's lines in the template: the comments directly above
        // it, the member, and a comment after its value on its last line.
        const std::size_t key_begin = from.key_begins[gap.index];
        const jsonc::Value& value = from.children[gap.index];
        const std::size_t first_line = start_of_line(template_text, key_begin);
        const std::string template_indent = jsonc::indent_at(template_text, key_begin);
        std::vector<std::string> lines;
        std::size_t cursor = first_line;
        while (cursor > 0) {
            const std::size_t above = start_of_line(template_text, cursor - 1);
            const std::string_view line = template_text.substr(above, cursor - 1 - above);
            if (!trimmed(line).starts_with("//")) {
                if (trimmed(line).empty() && after.has_value()) {
                    lines.insert(lines.begin(), "");
                }
                break;
            }
            lines.insert(
                lines.begin(),
                std::string{line.ends_with('\r') ? line.substr(0, line.size() - 1) : line});
            cursor = above;
        }
        std::size_t member_line = first_line;
        while (member_line < value.end) {
            const std::size_t end =
                std::min<std::size_t>(template_text.find('\n', member_line), value.end);
            std::string_view line = template_text.substr(member_line, end - member_line);
            if (line.ends_with('\r')) {
                line.remove_suffix(1);
            }
            lines.emplace_back(line);
            member_line = end + 1;
        }
        std::size_t pos = value.end;
        while (pos < template_text.size() &&
               (template_text[pos] == ' ' || template_text[pos] == '\t')) {
            ++pos;
        }
        if (pos < template_text.size() && template_text[pos] == ',') {
            ++pos;
        }
        std::string comment;
        std::size_t gap_end = pos;
        while (gap_end < template_text.size() &&
               (template_text[gap_end] == ' ' || template_text[gap_end] == '\t')) {
            ++gap_end;
        }
        if (template_text.substr(gap_end, 2) == "//") {
            std::size_t end = template_text.find('\n', gap_end);
            end = end == std::string_view::npos ? template_text.size() : end;
            if (end > gap_end && template_text[end - 1] == '\r') {
                --end;
            }
            comment = std::string{template_text.substr(pos, end - pos)};
        }

        // Re-indented to the file's own.
        const std::string indent = member_indent(out.text, object, after);
        for (std::string& line : lines) {
            if (line.starts_with(template_indent)) {
                line = indent + line.substr(template_indent.size());
            }
        }
        const std::size_t comma_at = lines.back().size();
        lines.back() += comment;
        out.text = jsonc::insert_lines(out.text, object, after, key, std::move(lines), comma_at,
                                       jsonc::to_json(value, template_text));
        out.added.push_back(gap.path);
    }
    throw ConfigEditError("the upgrade did not settle -- the file was left as it was");
}

}  // namespace

Upgrade upgrade_config_text(std::string_view content, std::string_view template_text) {
    if (!jsonc::looks_like_jsonc(content)) {
        return Upgrade{std::string{content}, {}};
    }
    try {
        return upgrade(content, template_text);
    } catch (const jsonc::JsoncError& e) {
        throw ConfigEditError(std::string{"the upgrade could not be applied -- the file was left "
                                          "as it was: "} +
                              e.what());
    }
}

}  // namespace apogee::harness
