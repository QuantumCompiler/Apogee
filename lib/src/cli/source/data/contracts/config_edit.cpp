#include "contracts/config_edit.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <optional>
#include <random>
#include <span>
#include <sstream>
#include <system_error>
#include <utility>

#include "contracts/config_yaml.h"
#include "contracts/host.h"
#include "contracts/jsonc.h"
#include "contracts/layout.h"

namespace apogee::harness {
namespace {

/// The names an entry can have, as every refusal says them (55f): the keys
/// the JSONC bridge writes plain (`yaml_plain_key`), which the line editor
/// finds by their text in either format.
constexpr std::string_view kEntryNameRule =
    "a name is letters, digits, '_', '-', '.' and '/', first neither '-' nor '.', and not null";

}  // namespace

/// The line editor: every transform, defined once over the YAML text the
/// config was first written in. A JSONC config reaches it through the bridge
/// at the end of this file, which is what the declarations in the header
/// call.
namespace yaml_text {
namespace {

/// Entries under a map section sit at exactly this indent.
constexpr std::size_t kEntryIndent = 2;
/// Their fields sit at this one.
constexpr std::size_t kFieldIndent = 4;

/// A source line, terminator included.
///
/// Keeping "\n" (or "\r\n", or nothing on a final unterminated line) attached
/// to each line is what makes splicing byte-exact: untouched lines are copied
/// verbatim, so a CRLF file stays CRLF and a file with no trailing newline
/// keeps not having one. Normalising terminators would rewrite every line of a
/// Windows user's config on the first edit.
using Lines = std::vector<std::string>;

Lines split_lines(std::string_view content) {
    Lines lines;
    std::size_t start = 0;
    while (start < content.size()) {
        const std::size_t newline = content.find('\n', start);
        if (newline == std::string_view::npos) {
            lines.emplace_back(content.substr(start));
            break;
        }
        lines.emplace_back(content.substr(start, newline - start + 1));
        start = newline + 1;
    }
    return lines;
}

std::string join_lines(const Lines& lines) {
    std::string out;
    std::size_t total = 0;
    for (const std::string& line : lines) {
        total += line.size();
    }
    out.reserve(total);
    for (const std::string& line : lines) {
        out += line;
    }
    return out;
}

/// The line without its terminator or trailing horizontal whitespace.
std::string_view body(std::string_view line) {
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' ' ||
                             line.back() == '\t')) {
        line.remove_suffix(1);
    }
    return line;
}

/// The terminator this file uses, taken from the first terminated line so an
/// inserted line matches its neighbours. Defaults to "\n" for a single-line or
/// empty file.
std::string dominant_terminator(const Lines& lines) {
    for (const std::string& line : lines) {
        if (line.size() >= 2 && line[line.size() - 2] == '\r' && line.back() == '\n') {
            return "\r\n";
        }
        if (!line.empty() && line.back() == '\n') {
            return "\n";
        }
    }
    return "\n";
}

std::size_t indent_of(std::string_view line_body) {
    std::size_t count = 0;
    while (count < line_body.size() && line_body[count] == ' ') {
        ++count;
    }
    return count;
}

bool is_blank(std::string_view line_body) {
    return line_body.empty();
}

bool is_comment(std::string_view line_body) {
    const std::size_t indent = indent_of(line_body);
    return indent < line_body.size() && line_body[indent] == '#';
}

/// A line that begins a new top-level key: column 0, not blank, not a comment.
bool is_top_level(std::string_view line_body) {
    return !is_blank(line_body) && !is_comment(line_body) && line_body[0] != ' ' &&
           line_body[0] != '\t';
}

/// `<section>:` at column 0.
bool is_section_header(std::string_view line_body, std::string_view section) {
    if (!is_top_level(line_body)) {
        return false;
    }
    return line_body.size() == section.size() + 1 &&
           line_body.substr(0, section.size()) == section && line_body.back() == ':';
}

/// The name in a `  <name>:` entry line, or nullopt.
std::optional<std::string_view> entry_name(std::string_view line_body) {
    if (is_blank(line_body) || is_comment(line_body)) {
        return std::nullopt;
    }
    if (indent_of(line_body) != kEntryIndent || line_body.back() != ':') {
        return std::nullopt;
    }
    std::string_view name = line_body.substr(kEntryIndent, line_body.size() - kEntryIndent - 1);
    if (name.empty()) {
        return std::nullopt;
    }
    // A `  key: value` line is a field of a parent, not an entry.
    if (name.find_first_of(" \t:") != std::string_view::npos) {
        return std::nullopt;
    }
    return name;
}

bool equals_folded(std::string_view lhs, std::string_view rhs) {
    return lhs.size() == rhs.size() &&
           std::equal(lhs.begin(), lhs.end(), rhs.begin(), [](char a, char b) {
               return std::tolower(static_cast<unsigned char>(a)) ==
                      std::tolower(static_cast<unsigned char>(b));
           });
}

/// Half-open line range of a section's body, excluding its header.
struct SectionRange {
    bool found = false;
    std::size_t header = 0;  ///< index of the `<section>:` line
    std::size_t begin = 0;   ///< first body line
    std::size_t end = 0;     ///< one past the last body line
};

SectionRange find_section(const Lines& lines, std::string_view section) {
    SectionRange range;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (!is_section_header(body(lines[i]), section)) {
            continue;
        }
        range.found = true;
        range.header = i;
        range.begin = i + 1;
        range.end = lines.size();
        for (std::size_t j = range.begin; j < lines.size(); ++j) {
            if (is_top_level(body(lines[j]))) {
                range.end = j;
                break;
            }
        }
        return range;
    }
    return range;
}

/// Where the comment after a value on `line` starts -- the first `#` after
/// a space outside a quoted scalar, from `from` on -- or npos when there is
/// none. A path quoted for its `#` is not a comment; a quote mark inside a
/// plain scalar (`it's`) opens nothing. Every scan for a value's comment asks
/// this one (55f): a URL's fragment or a quoted path's `#` is the value's own.
std::size_t comment_start(std::string_view line, std::size_t from) {
    char quote = 0;
    for (std::size_t i = from; i < line.size(); ++i) {
        const char c = line[i];
        const char before = i > 0 ? line[i - 1] : ' ';
        if (quote == '"' && c == '\\') {
            ++i;  // an escaped character, a quote among them
        } else if (quote != 0) {
            quote = c == quote ? 0 : quote;
        } else if ((c == '"' || c == '\'') &&
                   std::string_view{" \t[{,:"}.find(before) != std::string_view::npos) {
            quote = c;
        } else if (c == '#' && (before == ' ' || before == '\t')) {
            return i;
        }
    }
    return std::string_view::npos;
}

/// The terminator `line` ends with, or none on a file's unterminated last.
std::string terminator_of(std::string_view line) {
    if (line.ends_with("\r\n")) {
        return "\r\n";
    }
    return line.ends_with('\n') ? "\n" : "";
}

/// The lines a field whose key sits at `indent` on line `key_index` owns, up to
/// `limit`: the key line, and after it its value's -- every line indented
/// deeper (a block list's items, a block scalar's text, a flow list carried
/// on) or, for a block list written at the key's own indent, a `- item` there
/// -- with a blank or comment line kept only when such a line follows it: one
/// trailing the value is the next field's.
std::pair<std::size_t, std::size_t> field_extent(const Lines& lines, std::size_t key_index,
                                                 std::size_t limit, std::size_t indent) {
    std::size_t last_content = key_index;
    for (std::size_t i = key_index + 1; i < limit; ++i) {
        const std::string_view line = body(lines[i]);
        if (is_blank(line) || is_comment(line)) {
            continue;
        }
        const std::size_t at = indent_of(line);
        if (at > indent || (at == indent && line.substr(at).starts_with("-"))) {
            last_content = i;
            continue;
        }
        break;
    }
    return {key_index, last_content + 1};
}

/// `original` -- a `key: value` line at `indent` -- with its value token
/// replaced by `rendered`, the trailing comment and the whitespace before it
/// kept. The comment is found by `comment_start`, so a `#` inside the value
/// goes with the value.
std::string with_value(const std::string& original, std::size_t indent, std::string_view rendered,
                       const std::string& terminator) {
    const std::size_t key_colon = original.find(':', indent);
    const std::size_t hash = comment_start(original, key_colon + 1);
    const std::size_t limit = (hash == std::string::npos) ? original.size() : hash;
    std::size_t value_start = key_colon + 1;
    while (value_start < limit && (original[value_start] == ' ' || original[value_start] == '\t')) {
        ++value_start;
    }
    std::size_t value_end = limit;
    while (value_end > value_start &&
           (original[value_end - 1] == ' ' || original[value_end - 1] == '\t' ||
            original[value_end - 1] == '\r' || original[value_end - 1] == '\n')) {
        --value_end;
    }
    std::string replacement = original.substr(0, value_start);
    if (value_start == key_colon + 1) {
        replacement += ' ';  // `key:value` had no space; give it one
    }
    replacement += rendered;
    replacement += original.substr(value_end);
    if (value_start == value_end && hash != std::string::npos) {
        // `key:   # comment` -- the value below the key, or none: the new one
        // goes after the colon, and the spacing before the comment stays.
        replacement = original.substr(0, key_colon + 1) + " " + std::string{rendered} +
                      original.substr(key_colon + 1);
    }
    if (replacement.empty() || replacement.back() != '\n') {
        replacement += terminator;
    }
    return replacement;
}

/// Sets the field whose key sits at `indent` on line `key` to `rendered`: the
/// value token replaced on the key's own line, its comment kept, and every
/// other line of the old value -- a block list's items, a block scalar's text
/// (`field_extent`, up to `limit`) -- taken with it (55f), so none is left
/// behind to fold into the field above. A one-line value is one token's edit.
void replace_field_value(Lines& lines, std::size_t key, std::size_t limit, std::size_t indent,
                         std::string_view rendered, const std::string& terminator) {
    const std::size_t end = field_extent(lines, key, limit, indent).second;
    lines[key] = with_value(lines[key], indent, rendered, terminator);
    lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(key) + 1,
                lines.begin() + static_cast<std::ptrdiff_t>(end));
}

/// Removes the field whose key sits at `indent` on line `key`, and every line
/// of its value with it (`field_extent`, up to `limit`).
void remove_field(Lines& lines, std::size_t key, std::size_t limit, std::size_t indent) {
    const std::size_t end = field_extent(lines, key, limit, indent).second;
    lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(key),
                lines.begin() + static_cast<std::ptrdiff_t>(end));
}

}  // namespace

// Public (declared in config_edit.h) so a test pinning the written bytes goes
// through the same rule: a Windows model_path carries a drive colon and is
// written quoted, and an expectation built from the raw path is wrong there.
std::string yaml_scalar(std::string_view value) {
    const bool needs_quotes =
        value.empty() || value.front() == ' ' || value.back() == ' ' ||
        value.find_first_of(":#{}[]&*!|>'\"%@`,\n\r\t") != std::string_view::npos ||
        equals_folded(value, "true") || equals_folded(value, "false") ||
        equals_folded(value, "null") || equals_folded(value, "yes") || equals_folded(value, "no");

    if (!needs_quotes) {
        return std::string{value};
    }
    std::string out = "\"";
    for (const char c : value) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

namespace {

std::string number_scalar(double value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

/// Renders one backend entry's lines (key line first), each terminated.
///
/// Field order is fixed rather than alphabetical: `type` first because it
/// determines which of the rest even apply, then identity, then tuning. A
/// hand-written entry keeps whatever order its author chose -- this order
/// applies only to entries Apogee writes.
Lines format_backend_entry(std::string_view name, const BackendConfig& backend,
                           std::string_view terminator) {
    Lines out;
    const std::string indent(kFieldIndent, ' ');

    auto field = [&](std::string_view key, const std::string& value) {
        out.push_back(indent + std::string{key} + ": " + value + std::string{terminator});
    };

    out.push_back(std::string(kEntryIndent, ' ') + std::string{name} + ":" +
                  std::string{terminator});
    field("type", std::string{to_string(backend.type)});

    if (!backend.api_key.empty()) {
        field("api_key", yaml_scalar(backend.api_key));
    }
    if (!backend.model.empty()) {
        field("model", yaml_scalar(backend.model));
    }
    if (!backend.model_path.empty()) {
        field("model_path", yaml_scalar(backend.model_path));
    }
    if (!backend.mmproj_path.empty()) {
        field("mmproj_path", yaml_scalar(backend.mmproj_path));
    }
    if (!backend.embedding_model.empty()) {
        field("embedding_model", yaml_scalar(backend.embedding_model));
    }
    if (backend.context_size.has_value()) {
        field("context_size", std::to_string(*backend.context_size));
    }
    if (backend.cache_type.has_value()) {
        field("cache_type", std::string{to_string(*backend.cache_type)});
    }
    if (backend.max_tokens.has_value()) {
        field("max_tokens", std::to_string(*backend.max_tokens));
    }
    if (backend.temperature.has_value()) {
        field("temperature", number_scalar(*backend.temperature));
    }
    if (backend.top_p.has_value()) {
        field("top_p", number_scalar(*backend.top_p));
    }
    if (backend.top_k.has_value()) {
        field("top_k", std::to_string(*backend.top_k));
    }
    if (backend.min_p.has_value()) {
        field("min_p", number_scalar(*backend.min_p));
    }
    if (backend.repeat_penalty.has_value()) {
        field("repeat_penalty", number_scalar(*backend.repeat_penalty));
    }
    if (backend.presence_penalty.has_value()) {
        field("presence_penalty", number_scalar(*backend.presence_penalty));
    }
    if (backend.seed.has_value()) {
        field("seed", std::to_string(*backend.seed));
    }
    if (backend.thinking.has_value()) {
        field("thinking", std::string{to_string(*backend.thinking)});
    }
    if (backend.thinking_budget.has_value()) {
        field("thinking_budget", std::to_string(*backend.thinking_budget));
    }
    if (!backend.system_prompt.empty()) {
        field("system_prompt", yaml_scalar(backend.system_prompt));
    }
    return out;
}

/// Renders one collection entry's lines (key line first), each terminated.
Lines format_embedding_entry(std::string_view name, const EmbeddingConfig& collection,
                             std::string_view terminator) {
    Lines out;
    const std::string indent(kFieldIndent, ' ');

    auto field = [&](std::string_view key, const std::string& value) {
        out.push_back(indent + std::string{key} + ": " + value + std::string{terminator});
    };

    out.push_back(std::string(kEntryIndent, ' ') + std::string{name} + ":" +
                  std::string{terminator});
    if (collection.chunk_size.has_value()) {
        field("chunk_size", std::to_string(*collection.chunk_size));
    }
    if (collection.chunk_overlap.has_value()) {
        field("chunk_overlap", std::to_string(*collection.chunk_overlap));
    }
    if (!collection.description.empty()) {
        field("description", yaml_scalar(collection.description));
    }
    if (!collection.backend.empty()) {
        field("backend", yaml_scalar(collection.backend));
    }
    if (!collection.retriever.empty()) {
        field("retriever", yaml_scalar(collection.retriever));
    }
    if (!collection.rerank.empty()) {
        field("rerank", yaml_scalar(collection.rerank));
    }
    return out;
}

/// The line range one entry occupies, given its key line.
///
/// Blank and comment lines are TENTATIVE: they extend the entry only when a
/// deeper-indented field follows before the next sibling key. That is what
/// keeps a comment block written above the *next* entry from being swallowed
/// when this one is deleted -- the flaw in doing this the obvious way.
std::pair<std::size_t, std::size_t> entry_extent(const Lines& lines, std::size_t key_index,
                                                 std::size_t section_end) {
    std::size_t last_content = key_index;
    for (std::size_t i = key_index + 1; i < section_end; ++i) {
        const std::string_view line = body(lines[i]);
        if (is_blank(line) || is_comment(line)) {
            continue;  // tentative -- only kept if a field follows
        }
        if (indent_of(line) >= kFieldIndent) {
            last_content = i;
            continue;
        }
        break;  // a sibling `  key:` line
    }
    return {key_index, last_content + 1};
}

std::optional<std::size_t> find_entry_line(const Lines& lines, const SectionRange& section,
                                           std::string_view name) {
    for (std::size_t i = section.begin; i < section.end; ++i) {
        const std::optional<std::string_view> candidate = entry_name(body(lines[i]));
        if (candidate.has_value() && equals_folded(*candidate, name)) {
            return i;
        }
    }
    return std::nullopt;
}

}  // namespace

std::vector<std::string> section_entry_names(std::string_view content, std::string_view section) {
    const Lines lines = split_lines(content);
    const SectionRange range = find_section(lines, section);
    std::vector<std::string> names;
    if (!range.found) {
        return names;
    }
    for (std::size_t i = range.begin; i < range.end; ++i) {
        if (const std::optional<std::string_view> name = entry_name(body(lines[i]));
            name.has_value()) {
            names.emplace_back(*name);
        }
    }
    return names;
}

std::optional<std::string> fold_collision(const std::vector<std::string>& existing,
                                          std::string_view candidate) {
    for (const std::string& name : existing) {
        if (name != candidate && equals_folded(name, candidate)) {
            return name;
        }
    }
    return std::nullopt;
}

namespace {

/// Appends `entry` (key line first, every line terminated) under `section:`,
/// creating the section at the end of the file when absent.
///
/// Shared by every "add an entry" helper. `noun` is what the entry is called
/// in error messages -- "backend", "collection" -- so the message a user reads
/// names the thing they typed rather than the mechanism underneath.
std::string append_entry(std::string_view content, std::string_view section, std::string_view noun,
                         std::string_view name, Lines entry, bool force) {
    if (name.empty()) {
        throw ConfigEditError(std::string{noun} + " name cannot be empty");
    }
    // Only a name both formats can edit (55f): another, added to a JSONC
    // config, would be written quoted by the bridge, and no later edit or
    // delete would find it.
    if (!yaml_plain_key(name)) {
        std::string refusal = std::string{noun} + " name '" + std::string{name} +
                              "' is not one both config formats can edit -- " +
                              std::string{kEntryNameRule};
        if (section == "backends") {
            refusal +=
                "; a backend already named otherwise is reached by 'apogee config "
                "rename-backend'";
        }
        throw ConfigEditError(refusal);
    }

    Lines lines = split_lines(content);
    const std::string terminator = dominant_terminator(lines);

    const std::vector<std::string> existing = section_entry_names(content, section);
    if (!force) {
        if (const std::optional<std::string> clash = fold_collision(existing, name);
            clash.has_value()) {
            throw ConfigEditError(std::string{noun} + " '" + std::string{name} +
                                  "' collides with existing '" + *clash + "' -- " +
                                  std::string{noun} +
                                  " names are compared case-insensitively, so the two would be "
                                  "the same " +
                                  std::string{noun} + "; choose a distinct name");
        }
    }

    SectionRange range = find_section(lines, section);

    if (range.found) {
        if (const std::optional<std::size_t> existing_line = find_entry_line(lines, range, name);
            existing_line.has_value()) {
            if (!force) {
                throw ConfigEditError(std::string{noun} + " '" + std::string{name} +
                                      "' already exists; pass --force to replace it");
            }
            // Replace in place, so the entry keeps its position and whatever
            // comment block sits above it.
            const auto [begin, end] = entry_extent(lines, *existing_line, range.end);
            lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(begin),
                        lines.begin() + static_cast<std::ptrdiff_t>(end));
            lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(begin), entry.begin(),
                         entry.end());
            return join_lines(lines);
        }

        // Insert after the section's last CONTENT line, skipping back over any
        // trailing blank and comment lines.
        //
        // Skipping comments matters more than it looks. A section commonly
        // ends with a block of commented-out examples (the shipped template's
        // `backends:` is nothing but those), and a section is only terminated
        // by the next TOP-LEVEL line -- which a comment is not. Appending at
        // range.end would drop the entry below every trailing comment in the
        // file, so the new entry would appear to sit under commentary about
        // something else entirely. It would still parse; it would just read as
        // though the file had been vandalised.
        std::size_t insert_at = range.begin;
        for (std::size_t i = range.begin; i < range.end; ++i) {
            const std::string_view line = body(lines[i]);
            if (!is_blank(line) && !is_comment(line)) {
                insert_at = i + 1;
            }
        }

        // A file whose final line has no terminator needs one before anything
        // can follow it -- that line is about to stop being last. This is the
        // single case where appending modifies a line other than its own.
        if (insert_at > 0 && insert_at == lines.size()) {
            std::string& previous = lines.back();
            if (!previous.empty() && previous.back() != '\n') {
                previous += terminator;
            }
        }

        // One blank separator, but only when there is real content above to
        // separate from -- an entry going in directly under the section header
        // needs none. The delete helper removes this line again, which is what
        // makes add-then-delete byte-identical.
        if (insert_at > range.begin) {
            entry.insert(entry.begin(), terminator);
        }
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insert_at), entry.begin(),
                     entry.end());
        return join_lines(lines);
    }

    // No such section yet -- create one at the end of the file.
    if (!lines.empty()) {
        std::string& last = lines.back();
        if (!last.empty() && last.back() != '\n') {
            last += terminator;
        }
        if (!is_blank(body(lines.back()))) {
            lines.push_back(terminator);
        }
    }
    lines.push_back(std::string{section} + ":" + terminator);
    lines.insert(lines.end(), entry.begin(), entry.end());
    return join_lines(lines);
}

/// Removes the entry `name` from `section:` -- its key line, its field lines,
/// and the single blank separator above it. The inverse of append_entry.
std::string delete_entry(std::string_view content, std::string_view section, std::string_view noun,
                         std::string_view name) {
    Lines lines = split_lines(content);
    const SectionRange range = find_section(lines, section);
    if (!range.found) {
        throw ConfigEditError("no '" + std::string{section} + ":' section in this config");
    }

    const std::optional<std::size_t> key_line = find_entry_line(lines, range, name);
    if (!key_line.has_value()) {
        throw ConfigEditError(std::string{noun} + " '" + std::string{name} +
                              "' not found in config");
    }

    auto [begin, end] = entry_extent(lines, *key_line, range.end);

    // Take the blank separator above the entry with it -- the exact inverse of
    // what append_entry inserted. A COMMENT above the entry is left alone: it
    // may belong to the section rather than this entry, and an orphaned
    // comment is recoverable where a deleted one is not.
    if (begin > range.begin && is_blank(body(lines[begin - 1]))) {
        --begin;
    }

    lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(begin),
                lines.begin() + static_cast<std::ptrdiff_t>(end));
    return join_lines(lines);
}

}  // namespace

std::string append_backend(std::string_view content, std::string_view name,
                           const BackendConfig& backend, bool force) {
    const Lines lines = split_lines(content);
    return append_entry(content, "backends", "backend", name,
                        format_backend_entry(name, backend, dominant_terminator(lines)), force);
}

std::string delete_backend(std::string_view content, std::string_view name) {
    return delete_entry(content, "backends", "backend", name);
}

namespace {

Lines format_mcp_server_entry(std::string_view name, const McpServerConfig& server,
                              std::string_view terminator) {
    Lines out;
    const std::string indent(kFieldIndent, ' ');
    auto field = [&](std::string_view key, const std::string& value) {
        out.push_back(indent + std::string{key} + ": " + value + std::string{terminator});
    };
    // Inside a flow list a comma or a bracket ends the item; yaml_scalar
    // already quotes both (and every other character YAML could misread),
    // so the items go through it unchanged. A mutation test found a second
    // check here to be dead code, and it was removed.
    auto list = [&](std::string_view key, const std::vector<std::string>& values) {
        std::string rendered = "[";
        for (const std::string& value : values) {
            rendered += rendered.size() > 1 ? ", " : "";
            rendered += yaml_scalar(value);
        }
        rendered += "]";
        out.push_back(indent + std::string{key} + ": " + rendered + std::string{terminator});
    };
    out.push_back(std::string(kEntryIndent, ' ') + std::string{name} + ":" +
                  std::string{terminator});
    // Alphabetical after the name, so two entries written by two surfaces
    // read alike.
    if (!server.args.empty()) {
        list("args", server.args);
    }
    field("command", yaml_scalar(server.command));
    field("enabled", server.enabled ? "true" : "false");
    if (!server.env.empty()) {
        list("env", server.env);
    }
    return out;
}

}  // namespace

std::string append_mcp_server(std::string_view content, std::string_view name,
                              const McpServerConfig& server, bool force) {
    const Lines lines = split_lines(content);
    return append_entry(content, "mcp_servers", "MCP server", name,
                        format_mcp_server_entry(name, server, dominant_terminator(lines)), force);
}

std::string delete_mcp_server(std::string_view content, std::string_view name) {
    return delete_entry(content, "mcp_servers", "MCP server", name);
}

Lines format_review_target_entry(std::string_view name, const ReviewTargetConfig& target,
                                 std::string_view terminator) {
    Lines out;
    const std::string indent(kFieldIndent, ' ');
    auto field = [&](std::string_view key, const std::string& value) {
        out.push_back(indent + std::string{key} + ": " + value + std::string{terminator});
    };
    out.push_back(std::string(kEntryIndent, ' ') + std::string{name} + ":" +
                  std::string{terminator});
    // Alphabetical after the name, only what is set: two surfaces' entries
    // read alike, and a default stays the loader's.
    if (!target.cli.empty()) {
        field("cli", yaml_scalar(target.cli));
    }
    if (target.drafts) {
        field("drafts", "true");
    }
    field("forge", yaml_scalar(target.forge));
    if (!target.host.empty()) {
        field("host", yaml_scalar(target.host));
    }
    if (!target.model.empty()) {
        field("model", yaml_scalar(target.model));
    }
    if (!target.owner.empty()) {
        field("owner", yaml_scalar(target.owner));
    }
    if (target.post) {
        field("post", "true");
    }
    if (!target.repo.empty()) {
        field("repo", yaml_scalar(target.repo));
    }
    if (!target.skills.empty()) {
        std::string rendered = "[";
        for (const std::string& skill : target.skills) {
            rendered += rendered.size() > 1 ? ", " : "";
            rendered += yaml_scalar(skill);
        }
        field("skills", rendered + "]");
    }
    return out;
}

std::string append_review_target(std::string_view content, std::string_view name,
                                 const ReviewTargetConfig& target, bool force) {
    const Lines lines = split_lines(content);
    return append_entry(content, "review", "review target", name,
                        format_review_target_entry(name, target, dominant_terminator(lines)),
                        force);
}

std::string delete_review_target(std::string_view content, std::string_view name) {
    return delete_entry(content, "review", "review target", name);
}

std::string set_mcp_server_enabled(std::string_view content, std::string_view name, bool enabled) {
    Lines lines = split_lines(content);
    const std::string terminator = dominant_terminator(lines);
    const SectionRange range = find_section(lines, "mcp_servers");
    if (!range.found) {
        throw ConfigEditError("no 'mcp_servers:' section in this config");
    }
    const std::optional<std::size_t> key_line = find_entry_line(lines, range, name);
    if (!key_line.has_value()) {
        throw ConfigEditError("MCP server '" + std::string{name} + "' not found in config");
    }
    const auto [begin, end] = entry_extent(lines, *key_line, range.end);
    const std::string value = enabled ? "true" : "false";
    for (std::size_t i = begin + 1; i < end; ++i) {
        const std::string_view line = body(lines[i]);
        if (is_blank(line) || is_comment(line) || indent_of(line) != kFieldIndent) {
            continue;
        }
        const std::string_view rest = line.substr(kFieldIndent);
        if (!rest.starts_with("enabled:")) {
            continue;
        }
        // Replace only the value, keeping any trailing comment.
        replace_field_value(lines, i, end, kFieldIndent, value, terminator);
        return join_lines(lines);
    }
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(*key_line + 1),
                 std::string(kFieldIndent, ' ') + "enabled: " + value + terminator);
    return join_lines(lines);
}

namespace {

Lines format_agent_entry(std::string_view name, const AgentConfig& agent,
                         std::string_view terminator) {
    Lines out;
    const std::string indent(kFieldIndent, ' ');
    auto field = [&](std::string_view key, const std::string& value) {
        out.push_back(indent + std::string{key} + ": " + value + std::string{terminator});
    };
    auto list = [&](std::string_view key, const std::vector<std::string>& values) {
        std::string rendered = "[";
        for (const std::string& value : values) {
            rendered += rendered.size() > 1 ? ", " : "";
            rendered += yaml_scalar(value);
        }
        rendered += "]";
        out.push_back(indent + std::string{key} + ": " + rendered + std::string{terminator});
    };
    out.push_back(std::string(kEntryIndent, ' ') + std::string{name} + ":" +
                  std::string{terminator});
    // Alphabetical after the name; a value is written only when it says
    // something, so a defaulted entry is a bare name plus its policy.
    if (!agent.collection.empty()) {
        field("collection", yaml_scalar(agent.collection));
    }
    if (!agent.description.empty()) {
        field("description", yaml_scalar(agent.description));
    }
    if (!agent.mcp.empty()) {
        list("mcp", agent.mcp);
    }
    if (!agent.model.empty()) {
        field("model", yaml_scalar(agent.model));
    }
    if (agent.output_format != AgentOutputFormat::Auto) {
        field("output_format", std::string{to_string(agent.output_format)});
    }
    if (!agent.prompts.empty()) {
        list("prompts", agent.prompts);
    }
    if (agent.questions) {
        field("questions", "true");
    }
    if (!agent.save_dir.empty()) {
        field("save_dir", yaml_scalar(agent.save_dir));
    }
    if (!agent.save_filename.empty()) {
        field("save_filename", yaml_scalar(agent.save_filename));
    }
    if (!agent.save_subdir.empty()) {
        field("save_subdir", yaml_scalar(agent.save_subdir));
    }
    if (!agent.schemas.empty()) {
        list("schemas", agent.schemas);
    }
    field("tools", std::string{to_string(agent.tools)});
    return out;
}

}  // namespace

std::string append_agent(std::string_view content, std::string_view name, const AgentConfig& agent,
                         bool force) {
    const Lines lines = split_lines(content);
    return append_entry(content, "agents", "agent", name,
                        format_agent_entry(name, agent, dominant_terminator(lines)), force);
}

std::string delete_agent(std::string_view content, std::string_view name) {
    return delete_entry(content, "agents", "agent", name);
}

namespace {

Lines format_graph_entry(std::string_view name, const NamedGraphConfig& graph,
                         std::string_view terminator) {
    Lines out;
    const std::string indent(kFieldIndent, ' ');
    auto field = [&](std::string_view key, const std::string& value) {
        out.push_back(indent + std::string{key} + ": " + value + std::string{terminator});
    };
    out.push_back(std::string(kEntryIndent, ' ') + std::string{name} + ":" +
                  std::string{terminator});
    // `collections:` is always written for a graph of collections; a code
    // graph (27k) with only `sources:` omits the empty list.
    if (!graph.collections.empty() || graph.sources.empty()) {
        std::string members = "[";
        for (const std::string& collection : graph.collections) {
            members += members.size() > 1 ? ", " : "";
            members += yaml_scalar(collection);
        }
        members += "]";
        field("collections", members);
    }
    const auto list = [&](std::string_view key, const std::vector<std::string>& values) {
        std::string rendered = "[";
        for (const std::string& value : values) {
            rendered += rendered.size() > 1 ? ", " : "";
            rendered += yaml_scalar(value);
        }
        field(key, rendered + "]");
    };
    if (!graph.sources.empty()) {
        list("sources", graph.sources);
    }
    if (!graph.languages.empty()) {
        list("languages", graph.languages);
    }
    if (!graph.extract_backend.empty()) {
        field("extract_backend", yaml_scalar(graph.extract_backend));
    }
    if (graph.hops != NamedGraphConfig{}.hops) {
        field("hops", std::to_string(graph.hops));
    }
    if (graph.max_entities != NamedGraphConfig{}.max_entities) {
        field("max_entities", std::to_string(graph.max_entities));
    }
    return out;
}

}  // namespace

std::string append_graph(std::string_view content, std::string_view name,
                         const NamedGraphConfig& graph, bool force) {
    const Lines lines = split_lines(content);
    return append_entry(content, "graphs", "graph", name,
                        format_graph_entry(name, graph, dominant_terminator(lines)), force);
}

std::string delete_graph(std::string_view content, std::string_view name) {
    return delete_entry(content, "graphs", "graph", name);
}

std::string append_embedding(std::string_view content, std::string_view name,
                             const EmbeddingConfig& collection, bool force) {
    const Lines lines = split_lines(content);
    return append_entry(content, "embeddings", "collection", name,
                        format_embedding_entry(name, collection, dominant_terminator(lines)),
                        force);
}

std::string delete_embedding(std::string_view content, std::string_view name) {
    return delete_entry(content, "embeddings", "collection", name);
}

namespace {

/// A suite member's role sits at this indent under `members:`, and a long-form
/// member's fields at the next.
constexpr std::size_t kMemberIndent = 6;
constexpr std::size_t kMemberFieldIndent = 8;

/// A flow list of YAML scalars: `[a, b]`.
std::string flow_list(const std::vector<std::string>& values) {
    std::string rendered = "[";
    for (const std::string& value : values) {
        rendered += rendered.size() > 1 ? ", " : "";
        rendered += yaml_scalar(value);
    }
    return rendered + "]";
}

/// One member's lines: `role: backend` alone, or the long form when it pins.
Lines format_suite_member(std::string_view role, const SuiteMember& member,
                          std::string_view terminator) {
    const std::string indent(kMemberIndent, ' ');
    const std::string field(kMemberFieldIndent, ' ');
    const std::string end{terminator};
    if (!member.pins()) {
        return {indent + std::string{role} + ": " + yaml_scalar(member.backend) + end};
    }
    Lines out{indent + std::string{role} + ":" + end,
              field + "backend: " + yaml_scalar(member.backend) + end};
    if (member.context_size.has_value()) {
        out.push_back(field + "context_size: " + std::to_string(*member.context_size) + end);
    }
    if (member.toolset.has_value()) {
        out.push_back(field + "toolset: " + flow_list(*member.toolset) + end);
    }
    return out;
}

/// `consultable: [utility, vision]` (27f) -- nothing for an empty list.
Lines format_consultable(const std::vector<std::string>& roles, std::string_view terminator) {
    if (roles.empty()) {
        return {};
    }
    return {std::string(kFieldIndent, ' ') + "consultable: " + flow_list(roles) +
            std::string{terminator}};
}

/// The `consult_caps:` block (27f), its caps in `consult_cap_names()` order --
/// nothing when none is set.
Lines format_consult_caps(const ConsultCaps& caps, std::string_view terminator) {
    if (!caps.any()) {
        return {};
    }
    const std::string end{terminator};
    const std::string field(kMemberIndent, ' ');
    Lines out{std::string(kFieldIndent, ' ') + "consult_caps:" + end};
    const auto add = [&](std::string_view name, const std::optional<std::int64_t>& value) {
        if (value.has_value()) {
            out.push_back(field + std::string{name} + ": " + std::to_string(*value) + end);
        }
    };
    add("per_turn", caps.per_turn);
    add("brief_tokens", caps.brief_tokens);
    add("answer_tokens", caps.answer_tokens);
    return out;
}

/// The `validate:` block (27g), its keys in writing order -- `verifier`,
/// then `validate_seam_names()` -- each only when set; nothing when none is.
Lines format_validate(const ValidateConfig& validate, std::string_view terminator) {
    if (!validate.any()) {
        return {};
    }
    const std::string end{terminator};
    const std::string field(kMemberIndent, ' ');
    Lines out{std::string(kFieldIndent, ' ') + "validate:" + end};
    if (validate.verifier.has_value()) {
        out.push_back(field + "verifier: " + yaml_scalar(*validate.verifier) + end);
    }
    const auto add = [&](std::string_view name, const std::optional<bool>& value) {
        if (value.has_value()) {
            out.push_back(field + std::string{name} + ": " + (*value ? "on" : "off") + end);
        }
    };
    add("tool_args", validate.tool_args);
    add("extraction", validate.extraction);
    if (validate.answers.has_value()) {
        out.push_back(field + "answers: " + yaml_scalar(*validate.answers) + end);
    }
    return out;
}

/// `orchestrate: true` (27t) when the suite orchestrates; nothing when it
/// does not -- off is absent.
Lines format_orchestrate(bool orchestrate, std::string_view terminator) {
    if (!orchestrate) {
        return {};
    }
    return Lines{std::string(kFieldIndent, ' ') + "orchestrate: true" + std::string{terminator}};
}

Lines format_suite_entry(std::string_view name, const SuiteConfig& suite,
                         std::string_view terminator) {
    const std::string end{terminator};
    Lines out{std::string(kEntryIndent, ' ') + std::string{name} + ":" + end};
    if (!suite.description.empty()) {
        out.push_back(std::string(kFieldIndent, ' ') +
                      "description: " + yaml_scalar(suite.description) + end);
    }
    if (!suite.members.empty()) {
        out.push_back(std::string(kFieldIndent, ' ') + "members:" + end);
        // In the order every listing shows the roles, not the map's.
        for (const std::string_view role : suite_role_names()) {
            if (const auto it = suite.members.find(role); it != suite.members.end()) {
                const Lines member = format_suite_member(role, it->second, terminator);
                out.insert(out.end(), member.begin(), member.end());
            }
        }
    }
    const Lines consultable = format_consultable(suite.consultable, terminator);
    out.insert(out.end(), consultable.begin(), consultable.end());
    const Lines caps = format_consult_caps(suite.consult_caps, terminator);
    out.insert(out.end(), caps.begin(), caps.end());
    const Lines validate = format_validate(suite.validate, terminator);
    out.insert(out.end(), validate.begin(), validate.end());
    const Lines orchestrate = format_orchestrate(suite.orchestrate, terminator);
    out.insert(out.end(), orchestrate.begin(), orchestrate.end());
    return out;
}

/// The lines a key at `key_index` owns: those after it indented at least
/// `child_indent`, up to `limit` -- blank and comment lines kept only when a
/// child follows them, as `entry_extent` keeps them.
std::pair<std::size_t, std::size_t> block_extent(const Lines& lines, std::size_t key_index,
                                                 std::size_t limit, std::size_t child_indent) {
    std::size_t last_content = key_index;
    for (std::size_t i = key_index + 1; i < limit; ++i) {
        const std::string_view line = body(lines[i]);
        if (is_blank(line) || is_comment(line)) {
            continue;
        }
        if (indent_of(line) >= child_indent) {
            last_content = i;
            continue;
        }
        break;
    }
    return {key_index, last_content + 1};
}

/// The key a content line at exactly `indent` names (`key:` or `key: value`),
/// or nullopt.
std::optional<std::string_view> key_at(std::string_view line_body, std::size_t indent) {
    if (is_blank(line_body) || is_comment(line_body) || indent_of(line_body) != indent) {
        return std::nullopt;
    }
    const std::string_view rest = line_body.substr(indent);
    const std::size_t colon = rest.find(':');
    if (colon == std::string_view::npos || colon == 0) {
        return std::nullopt;
    }
    return rest.substr(0, colon);
}

/// Inserts `added` at `at`, giving an unterminated last line its terminator
/// first when the insertion follows it.
void insert_lines(Lines& lines, std::size_t at, const Lines& added, const std::string& terminator) {
    if (at > 0 && at == lines.size()) {
        std::string& previous = lines.back();
        if (!previous.empty() && previous.back() != '\n') {
            previous += terminator;
        }
    }
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(at), added.begin(), added.end());
}

}  // namespace

std::string append_suite(std::string_view content, std::string_view name, const SuiteConfig& suite,
                         bool force) {
    if (equals_folded(name, kSuiteOff)) {
        throw ConfigEditError("suite name '" + std::string{name} + "' is reserved -- '/suite " +
                              std::string{kSuiteOff} + "' means no suite; choose another");
    }
    const Lines lines = split_lines(content);
    return append_entry(content, "suites", "suite", name,
                        format_suite_entry(name, suite, dominant_terminator(lines)), force);
}

std::string delete_suite(std::string_view content, std::string_view name) {
    return delete_entry(content, "suites", "suite", name);
}

// ---- Symphonies (27q) ---------------------------------------------------------

namespace {

/// The words YAML reads as something other than the strings they spell.
constexpr std::array<std::string_view, 13> kYamlKeywords{
    "true", "false", "null", "yes", "no", "on", "off", "y", "n", "~", ".nan", ".inf", "-.inf"};

/// The digits a `\xNN` escape is written with.
constexpr std::string_view kHexDigits = "0123456789abcdef";

/// A word YAML would read as something other than the string it spells.
bool yaml_keyword(std::string_view value) {
    return std::ranges::any_of(kYamlKeywords,
                               [&](std::string_view word) { return equals_folded(value, word); });
}

/// `parts` joined -- one allocation-friendly line of an entry.
std::string line_of(std::initializer_list<std::string_view> parts) {
    std::string out;
    for (const std::string_view part : parts) {
        out += part;
    }
    return out;
}

/// Whether `value` reads back as itself written plain: a letter or digit
/// first, a few punctuation marks inside, nothing at either end YAML would
/// strip, and no word YAML treats as a keyword.
bool plain_safe(std::string_view value) {
    if (value.empty() || yaml_keyword(value) || value.back() == ' ') {
        return false;
    }
    if (std::isalnum(static_cast<unsigned char>(value.front())) == 0) {
        return false;
    }
    return std::ranges::all_of(value, [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 ||
               std::string_view{" _-.,;()/+='?!"}.find(c) != std::string_view::npos;
    });
}

/// `value` double-quoted, every character YAML would not keep escaped.
std::string quoted_text(std::string_view value) {
    std::string out = "\"";
    for (const char c : value) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) {
                    out += "\\x";
                    out += kHexDigits[(static_cast<unsigned char>(c) >> 4U) & 0x0FU];
                    out += kHexDigits[static_cast<unsigned char>(c) & 0x0FU];
                } else {
                    out += c;
                }
        }
    }
    return out + "\"";
}

/// Whether a literal block can carry `value` exactly: printable lines (a tab
/// is), no carriage return, and at most one newline at the end -- a block
/// keeping trailing blank lines would swallow whatever an editor puts after
/// it.
bool block_safe(std::string_view value) {
    if (value.find('\n') == std::string_view::npos ||
        value.find_first_not_of(" \t\n") == std::string_view::npos) {
        return false;  // one line, or nothing to show: a scalar says it
    }
    if (value.size() >= 2 && value.substr(value.size() - 2) == "\n\n") {
        return false;
    }
    // A line of nothing but blanks reads as an empty one to a line scan.
    std::string_view rest = value;
    while (!rest.empty()) {
        const std::size_t newline = rest.find('\n');
        const std::string_view line = rest.substr(0, newline);
        if (!line.empty() && line.find_first_not_of(" \t") == std::string_view::npos) {
            return false;
        }
        if (newline == std::string_view::npos) {
            break;
        }
        rest.remove_prefix(newline + 1);
    }
    return std::ranges::none_of(value, [](char c) {
        return c == '\r' || c == 0x7F ||
               (static_cast<unsigned char>(c) < 0x20 && c != '\n' && c != '\t');
    });
}

/// `key: value` at `indent`, the value as its text reads back exactly through
/// `parse_symphony_spec`: plain when that is safe, a literal block for
/// several lines, double-quoted with escapes otherwise.
void text_field(Lines& out, std::size_t indent, std::string_view key, std::string_view value,
                std::string_view terminator) {
    const std::string pad(indent, ' ');
    const std::string end{terminator};
    if (!block_safe(value)) {
        out.push_back(pad + std::string{key} + ": " +
                      (plain_safe(value) ? std::string{value} : quoted_text(value)) + end);
        return;
    }
    // Clip keeps one final newline, strip none; an explicit indentation
    // indicator when the first line opens with a space, which detection
    // would take for the block's own indentation.
    const bool final_newline = value.back() == '\n';
    std::string_view lines = final_newline ? value.substr(0, value.size() - 1) : value;
    const std::size_t first = lines.find_first_not_of('\n');
    const bool leading_space = first != std::string_view::npos && lines[first] == ' ';
    out.push_back(
        line_of({pad, key, ": |", leading_space ? "2" : "", final_newline ? "" : "-", end}));
    const std::string content(indent + 2, ' ');
    while (true) {
        const std::size_t newline = lines.find('\n');
        const std::string_view line = lines.substr(0, newline);
        out.push_back(line.empty() ? end : line_of({content, line, end}));
        if (newline == std::string_view::npos) {
            break;
        }
        lines.remove_prefix(newline + 1);
    }
}

/// A definition's lines at `indent`, everything but the name: `description`
/// and `input` when they say something, then `stages`, each stage's short
/// fields before its prompt and schema -- a play stage's (27r) `play:`, and
/// its `input:` when it has one.
void symphony_body(Lines& out, const SymphonySpec& spec, std::size_t indent,
                   std::string_view terminator) {
    const std::string pad(indent, ' ');
    const std::string end{terminator};
    if (!spec.description.empty()) {
        text_field(out, indent, "description", spec.description, terminator);
    }
    if (!spec.input.description.empty() || spec.input.image) {
        out.push_back(line_of({pad, "input:", end}));
        if (!spec.input.description.empty()) {
            text_field(out, indent + 2, "description", spec.input.description, terminator);
        }
        if (spec.input.image) {
            out.push_back(line_of({pad, "  image: true", end}));
        }
    }
    out.push_back(line_of({pad, "stages:", end}));
    const std::string field(indent + 4, ' ');
    for (const SymphonyStage& stage : spec.stages) {
        out.push_back(line_of({pad, "  - name: ", stage.name, end}));
        // Every field the stage holds is written, so the parser -- not the
        // renderer -- judges a stage that mixes the two kinds (27r): a role
        // and a play both set read back refused, never as one of them.
        if (!stage.role.empty() || !stage.plays()) {
            out.push_back(line_of({field, "role: ", stage.role, end}));
        }
        if (stage.plays()) {
            out.push_back(line_of({field, "play: ", stage.play, end}));
        }
        if (stage.image) {
            out.push_back(line_of({field, "image: true", end}));
        }
        if (stage.brief_tokens.has_value()) {
            out.push_back(
                line_of({field, "brief_tokens: ", std::to_string(*stage.brief_tokens), end}));
        }
        if (stage.answer_tokens.has_value()) {
            out.push_back(
                line_of({field, "answer_tokens: ", std::to_string(*stage.answer_tokens), end}));
        }
        if (!stage.plays() || !stage.prompt.empty()) {
            text_field(out, indent + 4, "prompt", stage.prompt, terminator);
        }
        if (!stage.input.empty()) {
            text_field(out, indent + 4, "input", stage.input, terminator);
        }
        if (!stage.schema.empty()) {
            text_field(out, indent + 4, "schema", stage.schema, terminator);
        }
    }
}

/// The lines one `symphonies:` entry occupies. Unlike `entry_extent`, a
/// line indented under the entry is the entry's whatever it starts with: a
/// prompt's `# Heading` is block content, not a comment that might belong to
/// the next entry. Blank lines, and comments at an entry's own indent or
/// less, stay tentative.
std::pair<std::size_t, std::size_t> symphony_extent(const Lines& lines, std::size_t key_index,
                                                    std::size_t section_end) {
    std::size_t last_content = key_index;
    for (std::size_t i = key_index + 1; i < section_end; ++i) {
        const std::string_view line = body(lines[i]);
        if (is_blank(line)) {
            continue;
        }
        if (indent_of(line) >= kFieldIndent) {
            last_content = i;
            continue;
        }
        if (is_comment(line)) {
            continue;
        }
        break;
    }
    return {key_index, last_content + 1};
}

}  // namespace

std::string render_symphony_spec(const SymphonySpec& spec) {
    Lines out;
    out.push_back("name: " + spec.name + "\n");
    symphony_body(out, spec, 0, "\n");
    return join_lines(out);
}

std::string append_symphony(std::string_view content, std::string_view name,
                            const SymphonySpec& spec, bool force) {
    if (!is_symphony_name(name)) {
        throw ConfigEditError("symphony name '" + std::string{name} +
                              "' is not one (letters, digits, '_' and '-')");
    }
    Lines lines = split_lines(content);
    const std::string terminator = dominant_terminator(lines);
    Lines entry;
    entry.push_back(std::string(kEntryIndent, ' ') + std::string{name} + ":" + terminator);
    symphony_body(entry, spec, kFieldIndent, terminator);

    const SectionRange range = find_section(lines, "symphonies");
    if (!range.found) {
        // No section yet: the generic path creates it at the file's end.
        return append_entry(content, "symphonies", "symphony", name, std::move(entry), force);
    }
    if (const std::optional<std::string> clash =
            fold_collision(section_entry_names(content, "symphonies"), name);
        clash.has_value() && !force) {
        throw ConfigEditError("symphony '" + std::string{name} + "' collides with existing '" +
                              *clash +
                              "' -- symphony names are compared case-insensitively, so the two "
                              "would be the same symphony; choose a distinct name");
    }
    if (const std::optional<std::size_t> existing = find_entry_line(lines, range, name);
        existing.has_value()) {
        if (!force) {
            throw ConfigEditError("symphony '" + std::string{name} +
                                  "' already exists; pass --force to replace it");
        }
        // Replaced in place, its own extent: the entry keeps its position and
        // the comment above it.
        const auto [begin, end] = symphony_extent(lines, *existing, range.end);
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(begin),
                    lines.begin() + static_cast<std::ptrdiff_t>(end));
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(begin), entry.begin(),
                     entry.end());
        return join_lines(lines);
    }
    // After the last entry's own extent -- a prompt whose last line opens
    // with '#' is that entry's, not a trailing comment to insert above -- and
    // above any comment block that closes the section.
    std::size_t insert_at = range.begin;
    for (std::size_t i = range.begin; i < range.end; ++i) {
        if (entry_name(body(lines[i])).has_value()) {
            insert_at = symphony_extent(lines, i, range.end).second;
        }
    }
    if (insert_at > 0 && insert_at == lines.size()) {
        std::string& previous = lines.back();
        if (!previous.empty() && previous.back() != '\n') {
            previous += terminator;
        }
    }
    if (insert_at > range.begin) {
        entry.insert(entry.begin(), terminator);
    }
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insert_at), entry.begin(),
                 entry.end());
    return join_lines(lines);
}

std::string delete_symphony(std::string_view content, std::string_view name) {
    Lines lines = split_lines(content);
    const SectionRange range = find_section(lines, "symphonies");
    if (!range.found) {
        throw ConfigEditError("no 'symphonies:' section in this config");
    }
    const std::optional<std::size_t> key_line = find_entry_line(lines, range, name);
    if (!key_line.has_value()) {
        throw ConfigEditError("symphony '" + std::string{name} + "' not found in config");
    }
    auto [begin, end] = symphony_extent(lines, *key_line, range.end);
    // The blank separator above it goes too: the exact inverse of the append.
    if (begin > range.begin && is_blank(body(lines[begin - 1]))) {
        --begin;
    }
    lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(begin),
                lines.begin() + static_cast<std::ptrdiff_t>(end));
    return join_lines(lines);
}

std::string set_suite_member(std::string_view content, std::string_view suite,
                             std::string_view role, const std::optional<SuiteMember>& member) {
    const std::span<const std::string_view> roles = suite_role_names();
    const auto rank = std::ranges::find(roles, role);
    if (rank == roles.end()) {
        std::string accepted;
        for (const std::string_view name : roles) {
            accepted += (accepted.empty() ? "" : ", ") + std::string{name};
        }
        throw ConfigEditError("'" + std::string{role} + "' is not a role (accepted: " + accepted +
                              ")");
    }

    Lines lines = split_lines(content);
    const std::string terminator = dominant_terminator(lines);
    const SectionRange section = find_section(lines, "suites");
    if (!section.found) {
        throw ConfigEditError("no 'suites:' section in this config");
    }
    const std::optional<std::size_t> key = find_entry_line(lines, section, suite);
    if (!key.has_value()) {
        throw ConfigEditError("suite '" + std::string{suite} + "' not found in config");
    }
    const auto [entry_begin, entry_end] = entry_extent(lines, *key, section.end);
    const std::string missing =
        "suite '" + std::string{suite} + "' has no " + std::string{role} + " member";

    // The `members:` block, written as a block: a flow mapping on one line is
    // legal YAML, and rare enough that refusing it beats a splice that
    // misreads it.
    std::optional<std::size_t> members_line;
    for (std::size_t i = entry_begin + 1; i < entry_end; ++i) {
        const std::string_view line = body(lines[i]);
        if (key_at(line, kFieldIndent) != std::optional<std::string_view>{"members"}) {
            continue;
        }
        std::string_view value = line.substr(line.find(':') + 1);
        value.remove_prefix(std::min(value.find_first_not_of(" \t"), value.size()));
        if (!value.empty() && value.front() != '#') {
            throw ConfigEditError("suites." + std::string{suite} +
                                  ".members is written inline -- write it as a block, or "
                                  "replace the suite with add-suite --force");
        }
        members_line = i;
        break;
    }

    if (!members_line.has_value()) {
        if (!member.has_value()) {
            throw ConfigEditError(missing);
        }
        Lines added{std::string(kFieldIndent, ' ') + "members:" + terminator};
        const Lines rendered = format_suite_member(role, *member, terminator);
        added.insert(added.end(), rendered.begin(), rendered.end());
        insert_lines(lines, entry_end, added, terminator);
        return join_lines(lines);
    }

    const auto [block_begin, block_end] =
        block_extent(lines, *members_line, entry_end, kMemberIndent);
    std::optional<std::size_t> role_line;
    std::optional<std::size_t> later_role;
    for (std::size_t i = block_begin + 1; i < block_end; ++i) {
        const std::optional<std::string_view> name = key_at(body(lines[i]), kMemberIndent);
        if (!name.has_value()) {
            continue;
        }
        if (*name == role) {
            role_line = i;
            break;
        }
        // The first member that sorts after this role: a new one goes before
        // it, so the members read in the order every listing shows them.
        if (!later_role.has_value()) {
            if (const auto at = std::ranges::find(roles, *name); at != roles.end() && at > rank) {
                later_role = i;
            }
        }
    }

    if (role_line.has_value()) {
        const auto [begin, end] = block_extent(lines, *role_line, block_end, kMemberFieldIndent);
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(begin),
                    lines.begin() + static_cast<std::ptrdiff_t>(end));
        if (member.has_value()) {
            insert_lines(lines, begin, format_suite_member(role, *member, terminator), terminator);
        }
        return join_lines(lines);
    }
    if (!member.has_value()) {
        throw ConfigEditError(missing);
    }
    std::size_t at = block_end;
    if (later_role.has_value()) {
        // Above the comment that leads the member it goes before: that
        // comment is the later member's, and stays on it.
        at = *later_role;
        while (at > block_begin + 1 && is_comment(body(lines[at - 1]))) {
            --at;
        }
    }
    insert_lines(lines, at, format_suite_member(role, *member, terminator), terminator);
    return join_lines(lines);
}

namespace {

/// Replaces the suite field `key` of `suite` with `rendered` -- removing it
/// when `rendered` is empty -- in place, every other line of the entry as it
/// was. A field not yet written goes above `before` when the entry has that
/// field (above the comment leading it), else at the entry's end.
std::string set_suite_field(std::string_view content, std::string_view suite, std::string_view key,
                            std::string_view before,
                            const std::function<Lines(std::string_view)>& render) {
    Lines lines = split_lines(content);
    const std::string terminator = dominant_terminator(lines);
    const SectionRange section = find_section(lines, "suites");
    if (!section.found) {
        throw ConfigEditError("no 'suites:' section in this config");
    }
    const std::optional<std::size_t> entry = find_entry_line(lines, section, suite);
    if (!entry.has_value()) {
        throw ConfigEditError("suite '" + std::string{suite} + "' not found in config");
    }
    const auto [entry_begin, entry_end] = entry_extent(lines, *entry, section.end);
    std::optional<std::size_t> field_line;
    std::optional<std::size_t> before_line;
    for (std::size_t i = entry_begin + 1; i < entry_end; ++i) {
        const std::optional<std::string_view> name = key_at(body(lines[i]), kFieldIndent);
        if (name == std::optional<std::string_view>{key}) {
            field_line = i;
        } else if (!before.empty() && name == std::optional<std::string_view>{before}) {
            before_line = i;
        }
    }
    const Lines rendered = render(terminator);
    if (field_line.has_value()) {
        const auto [begin, end] = field_extent(lines, *field_line, entry_end, kFieldIndent);
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(begin),
                    lines.begin() + static_cast<std::ptrdiff_t>(end));
        if (!rendered.empty()) {
            insert_lines(lines, begin, rendered, terminator);
        }
        return join_lines(lines);
    }
    if (rendered.empty()) {
        return std::string{content};  // nothing written, nothing to remove
    }
    std::size_t at = entry_end;
    if (before_line.has_value()) {
        at = *before_line;
        while (at > entry_begin + 1 && is_comment(body(lines[at - 1]))) {
            --at;
        }
    }
    insert_lines(lines, at, rendered, terminator);
    return join_lines(lines);
}

}  // namespace

std::string set_suite_consultable(std::string_view content, std::string_view suite,
                                  const std::vector<std::string>& roles) {
    return set_suite_field(
        content, suite, "consultable", "consult_caps",
        [&roles](std::string_view terminator) { return format_consultable(roles, terminator); });
}

std::string set_suite_consult_caps(std::string_view content, std::string_view suite,
                                   const ConsultCaps& caps) {
    return set_suite_field(
        content, suite, "consult_caps", "",
        [&caps](std::string_view terminator) { return format_consult_caps(caps, terminator); });
}

std::string set_suite_validate(std::string_view content, std::string_view suite,
                               const ValidateConfig& validate) {
    return set_suite_field(
        content, suite, "validate", "",
        [&validate](std::string_view terminator) { return format_validate(validate, terminator); });
}

std::string set_suite_orchestrate(std::string_view content, std::string_view suite,
                                  bool orchestrate) {
    return set_suite_field(content, suite, "orchestrate", "",
                           [orchestrate](std::string_view terminator) {
                               return format_orchestrate(orchestrate, terminator);
                           });
}

std::vector<std::string_view> models_role_fields() {
    return {"default",        "default_embedding",     "default_extraction",
            "default_vision", "default_transcription", "default_utility"};
}

namespace {

/// Sets one field of a backend entry in place, keeping any trailing comment
/// -- the whole old value replaced, a block list's or a block scalar's lines
/// with it (55f) -- or, when the entry has none, after `after_field` (else
/// after `type:`, else first) -- where a reader looks for it.
std::string set_backend_path_field(std::string_view content, std::string_view name,
                                   std::string_view key, std::string_view after_field,
                                   std::string_view path, bool quote = true) {
    Lines lines = split_lines(content);
    const std::string terminator = dominant_terminator(lines);
    const SectionRange range = find_section(lines, "backends");
    if (!range.found) {
        throw ConfigEditError("no 'backends:' section in this config");
    }
    const std::optional<std::size_t> key_line = find_entry_line(lines, range, name);
    if (!key_line.has_value()) {
        throw ConfigEditError("backend '" + std::string{name} + "' not found in config");
    }
    const auto [begin, end] = entry_extent(lines, *key_line, range.end);
    const std::string value = quote ? yaml_scalar(std::string{path}) : std::string{path};

    const std::string prefix = std::string{key} + ":";
    const std::string after_prefix = std::string{after_field} + ":";
    std::optional<std::size_t> type_line;
    std::optional<std::size_t> after_line;
    for (std::size_t i = begin + 1; i < end; ++i) {
        const std::string_view line = body(lines[i]);
        if (is_blank(line) || is_comment(line) || indent_of(line) != kFieldIndent) {
            continue;
        }
        const std::string_view field = line.substr(kFieldIndent);
        if (field.starts_with("type:")) {
            type_line = i;
        }
        if (!after_field.empty() && field.starts_with(after_prefix)) {
            after_line = i;
        }
        if (!field.starts_with(prefix)) {
            continue;
        }
        replace_field_value(lines, i, end, kFieldIndent, value, terminator);
        return join_lines(lines);
    }
    // Below the field it follows, and below that field's own value lines.
    const std::size_t at =
        after_line.has_value()  ? field_extent(lines, *after_line, end, kFieldIndent).second
        : type_line.has_value() ? field_extent(lines, *type_line, end, kFieldIndent).second
                                : begin + 1;
    insert_lines(lines, at, {std::string(kFieldIndent, ' ') + prefix + " " + value + terminator},
                 terminator);
    return join_lines(lines);
}

/// Removes `backends.<name>.<key>` -- its line, a comment after it with it,
/// and every line of its value: a block list's items, a block scalar's text
/// (55f) -- leaving the entry otherwise as it was; a field it lacks leaves it
/// as is.
std::string remove_backend_field(std::string_view content, std::string_view name,
                                 std::string_view key) {
    Lines lines = split_lines(content);
    const SectionRange range = find_section(lines, "backends");
    if (!range.found) {
        throw ConfigEditError("no 'backends:' section in this config");
    }
    const std::optional<std::size_t> key_line = find_entry_line(lines, range, name);
    if (!key_line.has_value()) {
        throw ConfigEditError("backend '" + std::string{name} + "' not found in config");
    }
    const auto [begin, end] = entry_extent(lines, *key_line, range.end);
    const std::string prefix = std::string{key} + ":";
    for (std::size_t i = begin + 1; i < end; ++i) {
        const std::string_view line = body(lines[i]);
        if (!is_blank(line) && !is_comment(line) && indent_of(line) == kFieldIndent &&
            line.substr(kFieldIndent).starts_with(prefix)) {
            remove_field(lines, i, end, kFieldIndent);
            return join_lines(lines);
        }
    }
    return std::string{content};
}

/// `adapters` as the flow list an `adapters:` line holds (53a), each
/// `{path: ..., scale: ..., base_sha256: "..."}` -- the pin quoted, so a
/// digest of digits alone stays text in either format, and left out when an
/// adapter has none.
std::string adapters_flow_list(const std::vector<AdapterConfig>& adapters) {
    std::string list;
    for (const AdapterConfig& adapter : adapters) {
        list += list.empty() ? "" : ", ";
        list += "{path: " + yaml_scalar(adapter.path) + ", scale: " + number_scalar(adapter.scale);
        if (!adapter.base_sha256.empty()) {
            list += ", base_sha256: \"" + adapter.base_sha256 + "\"";
        }
        list += "}";
    }
    return "[" + list + "]";
}

/// One past the last line of the backend field whose key is at `key`: its
/// value's lines (`field_extent`), up to the entry's `end`.
std::size_t field_value_end(const Lines& lines, std::size_t key, std::size_t end) {
    return field_extent(lines, key, end, kFieldIndent).second;
}

}  // namespace

std::string set_backend_adapters(std::string_view content, std::string_view name,
                                 const std::vector<AdapterConfig>& adapters) {
    Lines lines = split_lines(content);
    const std::string terminator = dominant_terminator(lines);
    const SectionRange range = find_section(lines, "backends");
    if (!range.found) {
        throw ConfigEditError("no 'backends:' section in this config");
    }
    const std::optional<std::size_t> key_line = find_entry_line(lines, range, name);
    if (!key_line.has_value()) {
        throw ConfigEditError("backend '" + std::string{name} + "' not found in config");
    }
    const auto [begin, end] = entry_extent(lines, *key_line, range.end);
    std::optional<std::size_t> field_line;
    std::optional<std::size_t> path_line;
    std::optional<std::size_t> type_line;
    for (std::size_t i = begin + 1; i < end; ++i) {
        const std::string_view line = body(lines[i]);
        if (is_blank(line) || is_comment(line) || indent_of(line) != kFieldIndent) {
            continue;
        }
        const std::string_view field = line.substr(kFieldIndent);
        if (field.starts_with("adapters:")) {
            field_line = i;
        } else if (field.starts_with("model_path:")) {
            path_line = i;
        } else if (field.starts_with("type:")) {
            type_line = i;
        }
    }
    const std::string written =
        std::string(kFieldIndent, ' ') + "adapters: " + adapters_flow_list(adapters);
    if (field_line.has_value()) {
        // The whole value goes, a block list's lines with it; a comment after
        // the key stays, spacing and all, on the one line written instead.
        const std::size_t stop = field_value_end(lines, *field_line, end);
        const std::string& original = lines[*field_line];
        const std::string_view original_body = body(original);
        const std::size_t colon = original_body.find(':');
        std::string tail;
        if (const std::size_t hash = comment_start(original_body, colon + 1);
            hash != std::string_view::npos) {
            std::size_t keep = hash;
            while (keep > colon + 1 &&
                   (original_body[keep - 1] == ' ' || original_body[keep - 1] == '\t')) {
                --keep;
            }
            tail = std::string{original_body.substr(keep)};
        }
        tail += terminator_of(lines[stop - 1]);
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(*field_line) + 1,
                    lines.begin() + static_cast<std::ptrdiff_t>(stop));
        if (adapters.empty()) {
            lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(*field_line));
        } else {
            lines[*field_line] = written + tail;
        }
        return join_lines(lines);
    }
    if (adapters.empty()) {
        return std::string{content};
    }
    // Where a reader looks for it: under the base it applies over.
    const std::size_t at = path_line.has_value()   ? field_value_end(lines, *path_line, end)
                           : type_line.has_value() ? field_value_end(lines, *type_line, end)
                                                   : begin + 1;
    // After a file's unterminated last line, the line before it gains the
    // terminator and the new one ends the file as that one did.
    std::string ending = terminator;
    if (std::string& before = lines[at - 1]; terminator_of(before).empty()) {
        before += terminator;
        ending.clear();
    }
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(at), written + ending);
    return join_lines(lines);
}

std::vector<std::string_view> backend_editable_fields() {
    return {"type",
            "model",
            "model_path",
            "mmproj_path",
            "embedding_model",
            "system_prompt",
            "context_size",
            "cache_type",
            "max_tokens",
            "temperature",
            "top_p",
            "top_k",
            "min_p",
            "repeat_penalty",
            "presence_penalty",
            "seed",
            "thinking",
            "thinking_budget",
            "binary",
            "mode",
            "host",
            "idle_unload_seconds",
            "server_tools",
            "adapters",
            "draft",
            "draft_block_size"};
}

/// `value`, a comma-separated list of names (`web_search,code_execution`),
/// as the YAML flow list a `server_tools:` line holds. Each name is a bare
/// word, so the list needs no quoting; whether the vendor runs it is the
/// loader's question, asked on the edit's own re-parse.
std::string flow_list_of_names(std::string_view value) {
    std::string list;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t comma = value.find(',', start);
        std::string_view word = value.substr(
            start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
        while (!word.empty() && word.front() == ' ') {
            word.remove_prefix(1);
        }
        while (!word.empty() && word.back() == ' ') {
            word.remove_suffix(1);
        }
        if (word.empty() || !std::ranges::all_of(word, [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
            })) {
            throw ConfigEditError("server_tools: '" + std::string{value} +
                                  "' is not a comma-separated list of tool names "
                                  "(e.g. web_search,code_execution)");
        }
        list += (list.empty() ? "" : ", ") + std::string{word};
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return "[" + list + "]";
}

std::string set_backend_field(std::string_view content, std::string_view name,
                              std::string_view field, std::string_view value) {
    if (field == "api_key") {
        throw ConfigEditError(
            "a key is never taken on the command line -- store it with 'apogee auth add' "
            "(asked with echo off, or --stdin, or --from-env)");
    }
    const std::vector<std::string_view> fields = backend_editable_fields();
    if (std::ranges::find(fields, field) == fields.end()) {
        throw ConfigEditError("'" + std::string{field} + "' is no field of a backend");
    }
    if (value.empty()) {
        if (field == "type") {
            throw ConfigEditError("a backend's type cannot be cleared -- name the type it is");
        }
        if (field == "adapters") {
            return yaml_text::set_backend_adapters(content, name, {});  // its block lines too
        }
        return remove_backend_field(content, name, field);
    }
    if (field == "server_tools") {
        return set_backend_path_field(content, name, field, "", flow_list_of_names(value),
                                      /*quote=*/false);
    }
    if (field == "adapters") {
        // The list as given, pinned to nothing: `config set-backend` records
        // each pin through `set_backend_adapters` itself (53a).
        return yaml_text::set_backend_adapters(content, name, parse_adapter_list(value));
    }
    // A drafter beside the model it drafts for (53i), its block size beside
    // it, where a reader looks for them.
    if (field == "draft") {
        return set_backend_path_field(content, name, field, "model_path", value);
    }
    if (field == "draft_block_size") {
        return set_backend_path_field(content, name, field, "draft", value);
    }
    return set_backend_path_field(content, name, field, "", value);
}

std::string set_backend_model_path(std::string_view content, std::string_view name,
                                   std::string_view path) {
    return set_backend_path_field(content, name, "model_path", "", path);
}

std::string set_backend_mmproj_path(std::string_view content, std::string_view name,
                                    std::string_view path) {
    return set_backend_path_field(content, name, "mmproj_path", "model_path", path);
}

std::string set_embedding_graph_enabled(std::string_view content, std::string_view collection,
                                        bool enabled) {
    Lines lines = split_lines(content);
    const std::string terminator = dominant_terminator(lines);
    const SectionRange range = find_section(lines, "embeddings");
    if (!range.found) {
        throw ConfigEditError("no 'embeddings:' section in this config");
    }
    const std::optional<std::size_t> key_line = find_entry_line(lines, range, collection);
    if (!key_line.has_value()) {
        throw ConfigEditError("collection '" + std::string{collection} + "' not found in config");
    }
    const auto [begin, end] = entry_extent(lines, *key_line, range.end);
    const std::string value = enabled ? "true" : "false";
    constexpr std::size_t kGraphFieldIndent = kFieldIndent + 2;

    // The entry's `graph:` block, then its `enabled:` line inside it.
    std::optional<std::size_t> graph_line;
    std::size_t graph_end = end;
    for (std::size_t i = begin + 1; i < end; ++i) {
        const std::string_view line = body(lines[i]);
        if (is_blank(line) || is_comment(line)) {
            continue;
        }
        if (graph_line.has_value()) {
            if (indent_of(line) <= kFieldIndent) {
                graph_end = i;  // the next sibling field ends the block
                break;
            }
            continue;
        }
        if (indent_of(line) == kFieldIndent && line.substr(kFieldIndent).starts_with("graph:")) {
            graph_line = i;
        }
    }
    if (graph_line.has_value()) {
        for (std::size_t i = *graph_line + 1; i < graph_end; ++i) {
            const std::string_view line = body(lines[i]);
            if (is_blank(line) || is_comment(line) || indent_of(line) != kGraphFieldIndent ||
                !line.substr(kGraphFieldIndent).starts_with("enabled:")) {
                continue;
            }
            // Replace only the value, keeping any trailing comment.
            replace_field_value(lines, i, graph_end, kGraphFieldIndent, value, terminator);
            return join_lines(lines);
        }
        // A `graph:` block with no `enabled:` line: the field goes first in it.
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(*graph_line + 1),
                     std::string(kGraphFieldIndent, ' ') + "enabled: " + value + terminator);
        return join_lines(lines);
    }
    // No block: one is appended after the entry's last field.
    if (!lines[end - 1].empty() && lines[end - 1].back() != '\n') {
        lines[end - 1] += terminator;
    }
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(end),
                 std::string(kFieldIndent, ' ') + "graph:" + terminator);
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(end + 1),
                 std::string(kGraphFieldIndent, ' ') + "enabled: " + value + terminator);
    return join_lines(lines);
}

namespace {

/// Sets `<section>.<field>` to `rendered` -- a value already written as
/// YAML: a quoted string, or a bare `true` or `42` (52e) -- the shared body
/// of every "one scalar under a top-level section" edit: replace the line in
/// place keeping its trailing comment, else insert it at the end of the
/// section, else append the section.
std::string set_section_rendered(std::string_view content, std::string_view section_name,
                                 std::string_view field, std::string_view rendered) {
    Lines lines = split_lines(content);
    const std::string terminator = dominant_terminator(lines);
    const std::string new_line =
        std::string(kEntryIndent, ' ') + std::string{field} + ": " + std::string{rendered};

    SectionRange section = find_section(lines, section_name);

    if (section.found) {
        std::size_t insert_at = section.begin;
        for (std::size_t i = section.begin; i < section.end; ++i) {
            const std::string_view line = body(lines[i]);
            if (is_blank(line) || is_comment(line)) {
                continue;
            }
            const std::size_t indent = indent_of(line);
            if (indent != kEntryIndent) {
                continue;
            }
            const std::string_view rest = line.substr(kEntryIndent);
            const std::size_t colon = rest.find(':');
            if (colon == std::string_view::npos || rest.substr(0, colon) != field) {
                // After this key's whole value: a new line never splits a
                // neighbour's block (55f).
                insert_at = field_extent(lines, i, section.end, kEntryIndent).second;
                continue;
            }
            // Found it. Replace ONLY the value, splicing it between the
            // original prefix and the original suffix.
            //
            // Done this way rather than by rebuilding the line so that the
            // trailing comment AND the exact whitespace before it survive: a
            // config whose comments are column-aligned stays aligned, and the
            // diff for setting a role is one token wide.
            replace_field_value(lines, i, section.end, kEntryIndent, rendered, terminator);
            return join_lines(lines);
        }

        insert_lines(lines, insert_at, {new_line + terminator}, terminator);
        return join_lines(lines);
    }

    if (!lines.empty()) {
        std::string& last = lines.back();
        if (!last.empty() && last.back() != '\n') {
            last += terminator;
        }
        if (!is_blank(body(lines.back()))) {
            lines.push_back(terminator);
        }
    }
    lines.push_back(std::string{section_name} + ":" + terminator);
    lines.push_back(new_line + terminator);
    return join_lines(lines);
}

std::string set_section_scalar(std::string_view content, std::string_view section_name,
                               std::string_view field, std::string_view value) {
    return set_section_rendered(content, section_name, field, yaml_scalar(value));
}

/// Sets `<section>.<sub>.<field>` to `rendered` (52e: `tools.search.url`):
/// the line replaced in place keeping its comment, else inserted at the end
/// of `<sub>:`'s block, else the block added at the end of the section, else
/// the section appended. A `<sub>:` written inline (`search: {url: …}`) is
/// refused, never misread.
std::string set_subsection_rendered(std::string_view content, std::string_view section_name,
                                    std::string_view sub, std::string_view field,
                                    std::string_view rendered) {
    constexpr std::size_t kSubIndent = kEntryIndent;
    Lines lines = split_lines(content);
    const std::string terminator = dominant_terminator(lines);
    const std::string field_line =
        std::string(kFieldIndent, ' ') + std::string{field} + ": " + std::string{rendered};
    const std::string sub_line = std::string(kSubIndent, ' ') + std::string{sub} + ":";

    const SectionRange section = find_section(lines, section_name);
    if (!section.found) {
        if (!lines.empty()) {
            std::string& last = lines.back();
            if (!last.empty() && last.back() != '\n') {
                last += terminator;
            }
            if (!is_blank(body(lines.back()))) {
                lines.push_back(terminator);
            }
        }
        lines.push_back(std::string{section_name} + ":" + terminator);
        lines.push_back(sub_line + terminator);
        lines.push_back(field_line + terminator);
        return join_lines(lines);
    }

    std::optional<std::size_t> header;
    std::size_t last_in_section = section.header;
    for (std::size_t i = section.begin; i < section.end; ++i) {
        const std::string_view line = body(lines[i]);
        if (is_blank(line) || is_comment(line)) {
            continue;
        }
        last_in_section = i;
        if (key_at(line, kSubIndent) == sub) {
            std::string_view rest = line.substr(kSubIndent + sub.size() + 1);
            while (!rest.empty() && rest.front() == ' ') {
                rest.remove_prefix(1);
            }
            if (!rest.empty() && rest.front() != '#') {
                throw ConfigEditError("the " + std::string{section_name} + "." + std::string{sub} +
                                      " line holds more than its key -- the editor sets the block "
                                      "form, '" +
                                      std::string{sub} + ":' alone with its fields under it");
            }
            header = i;
        }
    }
    if (!header.has_value()) {
        insert_lines(lines, last_in_section + 1, {sub_line + terminator, field_line + terminator},
                     terminator);
        return join_lines(lines);
    }
    std::size_t insert_at = *header + 1;
    for (std::size_t i = *header + 1; i < section.end; ++i) {
        const std::string_view line = body(lines[i]);
        if (is_blank(line) || is_comment(line)) {
            continue;
        }
        if (indent_of(line) <= kSubIndent) {
            break;  // the block ended
        }
        if (key_at(line, kFieldIndent) == field) {
            replace_field_value(lines, i, section.end, kFieldIndent, rendered, terminator);
            return join_lines(lines);
        }
        insert_at = i + 1;
    }
    insert_lines(lines, insert_at, {field_line + terminator}, terminator);
    return join_lines(lines);
}

}  // namespace

std::string set_models_role(std::string_view content, std::string_view field,
                            std::string_view value) {
    const std::vector<std::string_view> allowed = models_role_fields();
    if (std::find(allowed.begin(), allowed.end(), field) == allowed.end()) {
        std::string accepted;
        for (const std::string_view name : allowed) {
            accepted += (accepted.empty() ? "" : ", ") + std::string{name};
        }
        throw ConfigEditError("unknown models field '" + std::string{field} +
                              "' (accepted: " + accepted + ")");
    }
    return set_section_scalar(content, "models", field, value);
}

std::string set_default_suite(std::string_view content, std::string_view name) {
    return set_section_scalar(content, "models", "default_suite", name);
}

std::string set_default_video(std::string_view content, std::string_view name) {
    return set_section_scalar(content, "models", "default_video", name);
}

std::string set_attachments_graph(std::string_view content, std::string_view method) {
    if (!attachment_graph_method_from_string(method).has_value()) {
        throw ConfigEditError(attachment_graph_values_message("attachments.graph", method));
    }
    // `attachments: { graph: off }` -- or a header carrying a comment -- is a
    // section the line editor does not see: a block appended beside it would
    // be a second key, and the loader reads the first. Refused, never misread.
    for (const std::string& line : split_lines(content)) {
        const std::string_view text = body(line);
        if (is_top_level(text) && text.starts_with("attachments:") &&
            !is_section_header(text, "attachments")) {
            throw ConfigEditError(
                "the attachments: line holds more than its key -- the editor sets the block "
                "form, 'attachments:' alone with '  graph: <code|off>' under it");
        }
    }
    return set_section_scalar(content, "attachments", "graph", method);
}

std::string set_scalar(std::string_view content, std::string_view key, std::string_view rendered) {
    // Each key's place: `section.field`, or `section.sub.field`.
    const std::size_t first = key.find('.');
    const std::size_t second = key.find('.', first + 1);
    if (key == "attachments.graph") {
        return set_attachments_graph(content, rendered);
    }
    if (second == std::string_view::npos) {
        return set_section_rendered(content, key.substr(0, first), key.substr(first + 1), rendered);
    }
    return set_subsection_rendered(content, key.substr(0, first),
                                   key.substr(first + 1, second - first - 1),
                                   key.substr(second + 1), rendered);
}

std::string set_permission(std::string_view content, std::string_view tool,
                           std::string_view level) {
    if (tool.empty()) {
        throw ConfigEditError("a permission needs a tool name");
    }
    for (const char c : tool) {
        const bool ok =
            (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) {
            throw ConfigEditError("'" + std::string{tool} +
                                  "' is not a tool name (letters, digits and underscores)");
        }
    }
    if (level != "ask" && level != "allow" && level != "deny") {
        throw ConfigEditError("'" + std::string{level} +
                              "' is not a permission level (accepted: ask, allow, deny)");
    }
    return set_section_scalar(content, "permissions", tool, level);
}

namespace {

constexpr std::string_view kAllowedHostsKey = "allowed_hosts";

std::string checked_host(std::string_view host) {
    const std::optional<std::string> canonical = canonical_host(host);
    if (!canonical.has_value()) {
        throw ConfigEditError("'" + std::string{host} +
                              "' is not a host name: write the host alone, e.g. docs.python.org");
    }
    return *canonical;
}

/// A YAML scalar as written -- plain, `"double"` or `'single'` quoted --
/// read back to its value. Only as much YAML as a host name can need.
std::string unquote(std::string_view token) {
    if (token.size() >= 2 && token.front() == '"' && token.back() == '"') {
        std::string out;
        for (std::size_t i = 1; i + 1 < token.size(); ++i) {
            if (token[i] == '\\' && i + 2 < token.size()) {
                ++i;
            }
            out.push_back(token[i]);
        }
        return out;
    }
    if (token.size() >= 2 && token.front() == '\'' && token.back() == '\'') {
        std::string out;
        for (std::size_t i = 1; i + 1 < token.size(); ++i) {
            out.push_back(token[i]);
            if (token[i] == '\'' && i + 2 < token.size() && token[i + 1] == '\'') {
                ++i;
            }
        }
        return out;
    }
    return std::string{token};
}

/// One item of a list, located in the file.
struct ListItem {
    std::string value;      ///< unquoted
    std::size_t line = 0;   ///< the item's line (the key's line for a flow list)
    std::size_t begin = 0;  ///< flow: the item's first byte in that line
    std::size_t end = 0;    ///< flow: one past its last byte
};

/// `tools.allowed_hosts`, as the file spells it.
struct HostList {
    bool key_found = false;
    std::size_t key_line = 0;
    /// `[a, b]` on the key's line. Otherwise a block list (`- a` lines), or
    /// nothing at all (`allowed_hosts:` alone, which YAML reads as null).
    bool flow = false;
    std::size_t open = 0;   ///< flow: the `[`
    std::size_t close = 0;  ///< flow: the `]`
    std::vector<ListItem> items;
    /// Where a new `  allowed_hosts:` line goes when the key is absent: after
    /// the section's last content line, nested lists included.
    std::size_t insert_at = 0;
};

/// The byte where `line`'s YAML value ends: before a ` #` comment outside its
/// quotes (`comment_start`) and any trailing whitespace or terminator.
std::size_t value_end(std::string_view line, std::size_t from) {
    std::size_t limit = from < line.size() && line[from] == '#' ? from : comment_start(line, from);
    if (limit == std::string_view::npos) {
        limit = line.size();
    }
    while (limit > from && (line[limit - 1] == ' ' || line[limit - 1] == '\t' ||
                            line[limit - 1] == '\r' || line[limit - 1] == '\n')) {
        --limit;
    }
    return limit;
}

/// Splits `[a, "b", 'c']` into items. Throws when the list does not close on
/// its own line: a flow list spread over lines is legal YAML, and rare enough
/// that refusing it beats a splice that misreads it.
void read_flow_items(HostList& list, std::string_view line) {
    std::size_t i = list.open + 1;
    while (true) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
            ++i;
        }
        if (i >= line.size() || line[i] == '\n' || line[i] == '\r' || line[i] == '#') {
            throw ConfigEditError(
                "tools.allowed_hosts is a list that does not close on its own line; "
                "edit it by hand");
        }
        if (line[i] == ']') {
            list.close = i;
            return;
        }
        const std::size_t begin = i;
        if (line[i] == '"' || line[i] == '\'') {
            const char quote = line[i];
            ++i;
            while (i < line.size() && line[i] != quote) {
                i += (quote == '"' && line[i] == '\\') ? 2U : 1U;
            }
            ++i;
        } else {
            while (i < line.size() && line[i] != ',' && line[i] != ']' && line[i] != '\n') {
                ++i;
            }
        }
        std::size_t end = std::min(i, line.size());
        while (end > begin && (line[end - 1] == ' ' || line[end - 1] == '\t')) {
            --end;
        }
        list.items.push_back(
            ListItem{unquote(line.substr(begin, end - begin)), list.key_line, begin, end});
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
            ++i;
        }
        if (i < line.size() && line[i] == ',') {
            ++i;
        }
    }
}

HostList find_host_list(const Lines& lines, const SectionRange& section) {
    HostList list;
    list.insert_at = section.begin;
    for (std::size_t i = section.begin; i < section.end; ++i) {
        const std::string_view line = body(lines[i]);
        if (is_blank(line) || is_comment(line)) {
            continue;
        }
        list.insert_at = i + 1;
        if (list.key_found || indent_of(line) != kEntryIndent) {
            continue;
        }
        const std::string_view rest = line.substr(kEntryIndent);
        if (!rest.starts_with(kAllowedHostsKey) || rest.size() <= kAllowedHostsKey.size() ||
            rest[kAllowedHostsKey.size()] != ':') {
            continue;
        }
        list.key_found = true;
        list.key_line = i;
        const std::string& original = lines[i];
        const std::size_t colon = original.find(':', kEntryIndent);
        std::size_t value = colon + 1;
        while (value < original.size() && (original[value] == ' ' || original[value] == '\t')) {
            ++value;
        }
        if (value < original.size() && original[value] == '[') {
            list.flow = true;
            list.open = value;
            read_flow_items(list, original);
            continue;
        }
        if (value_end(original, value) > value) {
            throw ConfigEditError("tools.allowed_hosts is not a list; edit it by hand");
        }
        // A block list: the `- item` lines nested under the key.
        for (std::size_t j = i + 1; j < section.end; ++j) {
            const std::string_view item_line = body(lines[j]);
            if (is_blank(item_line) || is_comment(item_line)) {
                continue;
            }
            const std::size_t indent = indent_of(item_line);
            if (indent <= kEntryIndent || item_line.substr(indent, 1) != "-") {
                break;
            }
            const std::size_t start = indent + 1;
            std::size_t token = start;
            while (token < lines[j].size() && (lines[j][token] == ' ' || lines[j][token] == '\t')) {
                ++token;
            }
            const std::size_t end = value_end(lines[j], token);
            list.items.push_back(
                ListItem{unquote(std::string_view{lines[j]}.substr(token, end - token)), j, 0, 0});
        }
    }
    return list;
}

}  // namespace

std::string add_allowed_host(std::string_view content, std::string_view host) {
    const std::string canonical = checked_host(host);
    const std::string written = yaml_scalar(canonical);
    Lines lines = split_lines(content);
    const std::string terminator = dominant_terminator(lines);
    const SectionRange section = find_section(lines, "tools");

    if (!section.found) {
        if (!lines.empty()) {
            std::string& last = lines.back();
            if (!last.empty() && last.back() != '\n') {
                last += terminator;
            }
            if (!is_blank(body(lines.back()))) {
                lines.push_back(terminator);
            }
        }
        lines.push_back("tools:" + terminator);
        lines.push_back("  allowed_hosts: [" + written + "]" + terminator);
        return join_lines(lines);
    }

    const HostList list = find_host_list(lines, section);
    for (const ListItem& item : list.items) {
        if (canonical_host(item.value) == canonical) {
            return std::string{content};  // already there: nothing to write
        }
    }
    if (!list.key_found) {
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(list.insert_at),
                     "  allowed_hosts: [" + written + "]" + terminator);
        return join_lines(lines);
    }
    std::string& line = lines[list.key_line];
    if (list.flow) {
        if (list.items.empty()) {
            line.replace(list.open + 1, list.close - list.open - 1, written);
        } else {
            line.insert(list.items.back().end, ", " + written);
        }
        return join_lines(lines);
    }
    if (list.items.empty()) {
        // `allowed_hosts:` alone reads as null; give it the one-line form.
        const std::size_t colon = line.find(':', kEntryIndent);
        line.replace(colon + 1, value_end(line, colon + 1) - colon - 1, " [" + written + "]");
        if (line.back() != '\n') {
            line += terminator;
        }
        return join_lines(lines);
    }
    const std::string& last = lines[list.items.back().line];
    const std::string indent(indent_of(last), ' ');
    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(list.items.back().line + 1),
                 indent + "- " + written + terminator);
    return join_lines(lines);
}

std::string remove_allowed_host(std::string_view content, std::string_view host) {
    const std::string canonical = checked_host(host);
    Lines lines = split_lines(content);
    const SectionRange section = find_section(lines, "tools");
    const HostList list = section.found ? find_host_list(lines, section) : HostList{};

    for (std::size_t index = 0; index < list.items.size(); ++index) {
        const ListItem& item = list.items[index];
        if (canonical_host(item.value) != canonical) {
            continue;
        }
        if (!list.flow) {
            lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(item.line));
            return join_lines(lines);
        }
        // The exact inverse of the add: the first item takes the separator
        // after it, any other the separator before it.
        std::string& line = lines[list.key_line];
        if (list.items.size() == 1) {
            line.erase(list.open + 1, list.close - list.open - 1);
        } else if (index == 0) {
            line.erase(item.begin, list.items[1].begin - item.begin);
        } else {
            line.erase(list.items[index - 1].end, item.end - list.items[index - 1].end);
        }
        return join_lines(lines);
    }
    throw ConfigEditError("'" + canonical + "' is not in tools.allowed_hosts");
}

std::string format_config(std::string_view content) {
    Lines lines = split_lines(content);
    const std::string terminator = dominant_terminator(lines);

    Lines out;
    out.reserve(lines.size());
    bool previous_blank = false;
    for (const std::string& line : lines) {
        const std::string_view trimmed = body(line);
        const bool blank = is_blank(trimmed);
        if (blank && previous_blank) {
            continue;  // fold runs of blank lines down to one
        }
        previous_blank = blank;
        out.push_back(blank ? terminator : std::string{trimmed} + terminator);
    }

    // Exactly one trailing newline: drop trailing blank lines, and the last
    // real line already carries its terminator.
    while (!out.empty() && is_blank(body(out.back()))) {
        out.pop_back();
    }
    return join_lines(out);
}

// ---- renaming a backend (46a) ----------------------------------------------

namespace {

/// A scalar token's text: a quoted one unquoted (a double-quoted one's `\"`
/// and `\\` read), a plain one as it stands.
std::string token_text_of(std::string_view token) {
    if (token.size() >= 2 && token.front() == '\'' && token.back() == '\'') {
        std::string out;
        for (std::size_t i = 1; i + 1 < token.size(); ++i) {
            out += token[i];
            if (token[i] == '\'' && i + 2 < token.size() && token[i + 1] == '\'') {
                ++i;  // '' is one quote
            }
        }
        return out;
    }
    if (token.size() >= 2 && token.front() == '"' && token.back() == '"') {
        std::string out;
        for (std::size_t i = 1; i + 1 < token.size(); ++i) {
            if (token[i] == '\\' && i + 2 < token.size()) {
                ++i;
            }
            out += token[i];
        }
        return out;
    }
    return std::string{token};
}

/// Where the key that starts at `at` ends: past its closing quote, or at the
/// `:` a plain key runs to (`: ` or `:` at the end); npos when there is none.
std::size_t key_token_end(std::string_view line, std::size_t at) {
    if (at < line.size() && (line[at] == '"' || line[at] == '\'')) {
        const char quote = line[at];
        for (std::size_t i = at + 1; i < line.size(); ++i) {
            if (quote == '"' && line[i] == '\\') {
                ++i;
                continue;
            }
            if (line[i] == quote) {
                return i + 1 < line.size() && line[i + 1] == ':' ? i + 1 : std::string_view::npos;
            }
        }
        return std::string_view::npos;
    }
    for (std::size_t i = at; i < line.size(); ++i) {
        if (line[i] == ':' && (i + 1 == line.size() || line[i + 1] == ' ' || line[i + 1] == '\t')) {
            return i;
        }
        if (line[i] == '#' && i > at && (line[i - 1] == ' ' || line[i - 1] == '\t')) {
            return std::string_view::npos;
        }
    }
    return std::string_view::npos;
}

/// Where a value starting at `from` ends: before a ` #` comment outside its
/// quotes and its trailing blanks.
std::size_t value_token_end(std::string_view line, std::size_t from) {
    char quote = 0;
    std::size_t end = line.size();
    for (std::size_t i = from; i < line.size(); ++i) {
        const char c = line[i];
        if (quote != 0) {
            if (quote == '"' && c == '\\') {
                ++i;
            } else if (c == quote) {
                quote = 0;
            }
            continue;
        }
        if ((c == '"' || c == '\'') && i == from) {
            quote = c;
        } else if (c == '#' && i > from && (line[i - 1] == ' ' || line[i - 1] == '\t')) {
            end = i;
            break;
        }
    }
    while (end > from && (line[end - 1] == ' ' || line[end - 1] == '\t')) {
        --end;
    }
    return end;
}

/// Whether `path` is one of `patterns`, `*` standing for any key or item.
bool reference_path(const std::vector<std::string>& path,
                    const std::vector<std::vector<std::string>>& patterns) {
    return std::ranges::any_of(patterns, [&path](const std::vector<std::string>& pattern) {
        if (pattern.size() != path.size()) {
            return false;
        }
        for (std::size_t i = 0; i < path.size(); ++i) {
            if (pattern[i] != "*" && pattern[i] != path[i]) {
                return false;
            }
        }
        return true;
    });
}

/// A flow mapping's values naming `old_name` replaced, each at a reference
/// path under `path` -- `{chat: root, utility: helper}` as a suite's members.
std::string rename_in_flow_map(std::string_view flow, std::vector<std::string> path,
                               const std::vector<std::vector<std::string>>& patterns,
                               std::string_view old_name, std::string_view new_name) {
    std::string out;
    std::size_t at = 1;  // past `{`
    out += '{';
    while (at < flow.size()) {
        // One `key: value` item, to the next top-level `,` or the `}`.
        std::size_t end = at;
        char quote = 0;
        while (end < flow.size()) {
            const char c = flow[end];
            if (quote != 0) {
                if (c == quote) {
                    quote = 0;
                }
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == ',' || c == '}') {
                break;
            }
            ++end;
        }
        std::string item{flow.substr(at, end - at)};
        if (const std::size_t colon = item.find(':'); colon != std::string::npos && colon > 0) {
            const std::size_t key_from = item.find_first_not_of(" \t");
            const std::string key = token_text_of(std::string_view{item}.substr(
                key_from, item.find_last_not_of(" \t", colon - 1) + 1 - key_from));
            const std::size_t from = item.find_first_not_of(" \t", colon + 1);
            if (from != std::string::npos) {
                const std::size_t last = item.find_last_not_of(" \t");
                path.push_back(key);
                if (reference_path(path, patterns) && token_text_of(std::string_view{item}.substr(
                                                          from, last + 1 - from)) == old_name) {
                    item = item.substr(0, from) + yaml_scalar(new_name) + item.substr(last + 1);
                }
                path.pop_back();
            }
        }
        out += item;
        if (end < flow.size()) {
            out += flow[end];
        }
        at = end + 1;
    }
    return out;
}

}  // namespace

std::string rename_backend(std::string_view content, std::string_view old_name,
                           std::string_view new_name,
                           const std::vector<std::vector<std::string>>& patterns) {
    Lines lines = split_lines(content);
    const SectionRange backends = find_section(lines, "backends");
    if (!backends.found) {
        throw ConfigEditError("no backend named '" + std::string{old_name} + "'");
    }
    // The entry's key line, a comment after it allowed; the key renamed
    // where it stands, its fields and comments untouched.
    std::optional<std::size_t> key_line;
    for (std::size_t i = backends.begin; i < backends.end; ++i) {
        const std::string_view line = body(lines[i]);
        if (is_blank(line) || is_comment(line) || indent_of(line) != kEntryIndent) {
            continue;
        }
        const std::size_t end = key_token_end(line, kEntryIndent);
        if (end == std::string_view::npos) {
            continue;
        }
        if (const std::size_t rest = line.find_first_not_of(" \t", end + 1);
            rest != std::string_view::npos && line[rest] != '#') {
            continue;  // a `key: value` line, no entry
        }
        if (token_text_of(line.substr(kEntryIndent, end - kEntryIndent)) == old_name) {
            key_line = i;
            break;
        }
    }
    if (!key_line.has_value()) {
        throw ConfigEditError("no backend named '" + std::string{old_name} + "'");
    }
    {
        // Plain, as add-backend writes a key: the new name is one YAML reads
        // as written (`yaml_plain_key`), and a quoted key is one the editor's
        // later edits would not find (55f).
        std::string& line = lines[*key_line];
        const std::size_t end = key_token_end(body(line), kEntryIndent);
        line = line.substr(0, kEntryIndent) + std::string{new_name} + line.substr(end);
    }

    // Every reference: a walk of the mappings by path, a value naming the old
    // key at a reference path replaced where it stands.
    struct Frame {
        std::size_t indent;
        std::string key;
    };

    std::vector<Frame> stack;
    std::optional<std::size_t> block_indent;  // inside a `|` or `>` scalar
    for (std::string& raw : lines) {
        const std::string_view line = body(raw);
        if (is_blank(line) || is_comment(line)) {
            continue;
        }
        std::size_t at = indent_of(line);
        if (block_indent.has_value()) {
            if (at > *block_indent) {
                continue;
            }
            block_indent.reset();
        }
        const auto pop_to = [&stack](std::size_t indent) {
            while (!stack.empty() && stack.back().indent >= indent) {
                stack.pop_back();
            }
        };
        // `- ` items, each a level of its own -- under the key above, even
        // when the list is written at that key's own indent.
        while (at + 1 < line.size() && line[at] == '-' && line[at + 1] == ' ') {
            while (!stack.empty() && (stack.back().indent > at ||
                                      (stack.back().indent == at && stack.back().key == "*"))) {
                stack.pop_back();
            }
            stack.push_back({at, "*"});
            at += 2;
            while (at < line.size() && line[at] == ' ') {
                ++at;
            }
        }
        pop_to(at);
        const std::size_t end = key_token_end(line, at);
        if (end == std::string_view::npos) {
            continue;  // a list's scalar, or text
        }
        const std::string key = token_text_of(line.substr(at, end - at));
        const std::size_t from = line.find_first_not_of(" \t", end + 1);
        const std::size_t stop =
            from == std::string_view::npos ? from : value_token_end(line, from);
        if (from == std::string_view::npos || stop == from || line[from] == '#') {
            stack.push_back({at, key});  // a mapping or a list follows
            continue;
        }
        const std::string_view value = line.substr(from, stop - from);
        std::vector<std::string> path;
        for (const Frame& frame : stack) {
            path.push_back(frame.key);
        }
        path.push_back(key);
        if (value.front() == '|' || value.front() == '>') {
            block_indent = at;
            continue;
        }
        if (value.front() == '{') {
            raw = raw.substr(0, from) +
                  rename_in_flow_map(value, path, patterns, old_name, new_name) + raw.substr(stop);
            continue;
        }
        if (reference_path(path, patterns) && token_text_of(value) == old_name) {
            raw = raw.substr(0, from) + yaml_scalar(new_name) + raw.substr(stop);
        }
    }
    return join_lines(lines);
}

}  // namespace yaml_text

namespace {

/// Applies a YAML transform to `content` in its own format: YAML directly; a
/// JSONC config through the bridge -- its value written as YAML, the
/// transform applied, the result spliced back into the JSONC text in place,
/// so every comment and every byte the edit does not touch stays (28i).
template <typename Transform>
std::string in_format(std::string_view content, Transform&& transform) {
    if (!jsonc::looks_like_jsonc(content)) {
        return std::forward<Transform>(transform)(content);
    }
    nlohmann::ordered_json current;
    try {
        current = jsonc::parse_json(content);
    } catch (const jsonc::JsoncError& e) {
        throw ConfigEditError(std::string{"the config is not valid JSON: "} + e.what());
    }
    if (current.is_null()) {
        current = nlohmann::ordered_json::object();
    }
    const std::string yaml = yaml_of_json(current);
    const std::string edited = std::forward<Transform>(transform)(std::string_view{yaml});
    if (edited == yaml) {
        return std::string{content};
    }
    try {
        return jsonc::patch(content, json_of_yaml(edited, current));
    } catch (const jsonc::JsoncError& e) {
        throw ConfigEditError(e.what());
    }
}

}  // namespace

std::vector<std::vector<std::string>> backend_reference_paths() {
    std::vector<std::vector<std::string>> paths;
    for (const std::string_view field : yaml_text::models_role_fields()) {
        paths.push_back({"models", std::string{field}});
    }
    paths.push_back({"suites", "*", "members", "*"});
    paths.push_back({"suites", "*", "members", "*", "backend"});
    paths.push_back({"embeddings", "*", "backend"});
    paths.push_back({"embeddings", "*", "rerank"});
    paths.push_back({"embeddings", "*", "graph", "extract_backend"});
    paths.push_back({"graphs", "*", "extract_backend"});
    paths.push_back({"agents", "*", "model"});
    paths.push_back({"training", "judge_backend"});
    paths.push_back({"training", "regimes", "*", "teacher"});
    paths.push_back({"training", "cycle", "backend"});
    paths.push_back({"training", "cycle", "judge_backend"});
    paths.push_back({"training", "cycle", "sources", "*", "backend"});
    return paths;
}

namespace {

/// The values naming `old_name` at `path`'s matches under `node`, replaced.
void rename_references(nlohmann::ordered_json& node, const std::vector<std::string>& path,
                       std::size_t at, std::string_view old_name, std::string_view new_name) {
    if (at == path.size()) {
        if (node.is_string() && node.get_ref<const std::string&>() == old_name) {
            node = std::string{new_name};
        }
        return;
    }
    if (path[at] == "*") {
        if (node.is_object() || node.is_array()) {
            for (auto& child : node) {
                rename_references(child, path, at + 1, old_name, new_name);
            }
        }
        return;
    }
    if (node.is_object() && node.contains(path[at])) {
        rename_references(node[path[at]], path, at + 1, old_name, new_name);
    }
}

/// The JSONC half: the key renamed where it stands -- its member, its
/// comments and its place kept -- then the references, as values spliced in
/// place (`jsonc::patch`).
std::string rename_backend_jsonc(std::string_view content, std::string_view old_name,
                                 std::string_view new_name) {
    jsonc::Value root;
    try {
        root = jsonc::parse(content);
    } catch (const jsonc::JsoncError& e) {
        throw ConfigEditError(std::string{"the config is not valid JSON: "} + e.what());
    }
    const auto backends_at = std::ranges::find(root.keys, "backends");
    if (root.kind != jsonc::Kind::Object || backends_at == root.keys.end()) {
        throw ConfigEditError("no backend named '" + std::string{old_name} + "'");
    }
    const jsonc::Value& backends =
        root.children.at(static_cast<std::size_t>(backends_at - root.keys.begin()));
    const auto entry = std::ranges::find(backends.keys, old_name);
    if (backends.kind != jsonc::Kind::Object || entry == backends.keys.end()) {
        throw ConfigEditError("no backend named '" + std::string{old_name} + "'");
    }
    const std::size_t begin =
        backends.key_begins.at(static_cast<std::size_t>(entry - backends.keys.begin()));
    std::size_t end = begin + 1;
    while (end < content.size() && content[end] != '"') {
        end += content[end] == '\\' ? 2U : 1U;
    }
    std::string text = std::string{content.substr(0, begin)} +
                       nlohmann::ordered_json(std::string{new_name}).dump() +
                       std::string{content.substr(end + 1)};
    nlohmann::ordered_json target = jsonc::parse_json(text);
    for (const std::vector<std::string>& path : backend_reference_paths()) {
        rename_references(target, path, 0, old_name, new_name);
    }
    try {
        return jsonc::patch(text, target);
    } catch (const jsonc::JsoncError& e) {
        throw ConfigEditError(e.what());
    }
}

}  // namespace

std::string rename_backend(std::string_view content, std::string_view old_name,
                           std::string_view new_name) {
    // The name add-backend would take (55f): a name both formats edit, so the
    // renamed entry is one every later edit finds. The old name may be any.
    if (!yaml_plain_key(new_name)) {
        throw ConfigEditError("'" + std::string{new_name} + "' cannot name a backend -- " +
                              std::string{kEntryNameRule});
    }
    if (old_name == new_name) {
        throw ConfigEditError("'" + std::string{old_name} + "' is already its name");
    }
    // A name taken -- the same folded, as add-backend refuses one -- unless it
    // is this backend's own, a change of case.
    for (const std::string& name : section_entry_names(content, "backends")) {
        if (name != old_name && yaml_text::fold_collision({name}, new_name).has_value()) {
            throw ConfigEditError("a backend named '" + name + "' already exists");
        }
        if (name == new_name) {
            throw ConfigEditError("a backend named '" + name + "' already exists");
        }
    }
    if (jsonc::looks_like_jsonc(content)) {
        return rename_backend_jsonc(content, old_name, new_name);
    }
    return yaml_text::rename_backend(content, old_name, new_name, backend_reference_paths());
}

std::string yaml_scalar(std::string_view value) {
    return yaml_text::yaml_scalar(value);
}

std::vector<std::string> section_entry_names(std::string_view content, std::string_view section) {
    if (!jsonc::looks_like_jsonc(content)) {
        return yaml_text::section_entry_names(content, section);
    }
    std::vector<std::string> names;
    try {
        const nlohmann::ordered_json root = jsonc::parse_json(content);
        if (root.is_object() && root.contains(section) && root.at(section).is_object()) {
            for (const auto& [name, entry] : root.at(section).items()) {
                names.push_back(name);
            }
        }
    } catch (const jsonc::JsoncError&) {
        // As the YAML reader does on a file it cannot follow: no entries.
    }
    return names;
}

std::optional<std::string> fold_collision(const std::vector<std::string>& existing,
                                          std::string_view candidate) {
    return yaml_text::fold_collision(existing, candidate);
}

std::string append_backend(std::string_view content, std::string_view name,
                           const BackendConfig& backend, bool force) {
    return in_format(content, [&](std::string_view text) {
        return yaml_text::append_backend(text, name, backend, force);
    });
}

std::string delete_backend(std::string_view content, std::string_view name) {
    return in_format(content,
                     [&](std::string_view text) { return yaml_text::delete_backend(text, name); });
}

std::string append_embedding(std::string_view content, std::string_view name,
                             const EmbeddingConfig& collection, bool force) {
    return in_format(content, [&](std::string_view text) {
        return yaml_text::append_embedding(text, name, collection, force);
    });
}

std::string delete_embedding(std::string_view content, std::string_view name) {
    return in_format(
        content, [&](std::string_view text) { return yaml_text::delete_embedding(text, name); });
}

std::string set_embedding_graph_enabled(std::string_view content, std::string_view collection,
                                        bool enabled) {
    return in_format(content, [&](std::string_view text) {
        return yaml_text::set_embedding_graph_enabled(text, collection, enabled);
    });
}

std::string set_backend_model_path(std::string_view content, std::string_view name,
                                   std::string_view path) {
    return in_format(content, [&](std::string_view text) {
        return yaml_text::set_backend_model_path(text, name, path);
    });
}

std::vector<std::string_view> backend_editable_fields() {
    return yaml_text::backend_editable_fields();
}

std::string set_backend_field(std::string_view content, std::string_view name,
                              std::string_view field, std::string_view value) {
    return in_format(content, [&](std::string_view text) {
        return yaml_text::set_backend_field(text, name, field, value);
    });
}

std::string set_backend_adapters(std::string_view content, std::string_view name,
                                 const std::vector<AdapterConfig>& adapters) {
    return in_format(content, [&](std::string_view text) {
        return yaml_text::set_backend_adapters(text, name, adapters);
    });
}

std::vector<AdapterConfig> parse_adapter_list(std::string_view text) {
    std::vector<AdapterConfig> adapters;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t comma = text.find(',', start);
        std::string_view item = text.substr(
            start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
        while (!item.empty() && item.front() == ' ') {
            item.remove_prefix(1);
        }
        while (!item.empty() && item.back() == ' ') {
            item.remove_suffix(1);
        }
        AdapterConfig adapter;
        std::string_view file = item;
        // Only a number after the last colon is a scale -- 49e's rule, so
        // `C:\lora.gguf` and a name holding a colon stay paths.
        if (const std::size_t colon = item.rfind(':'); colon != std::string_view::npos) {
            const std::string tail{item.substr(colon + 1)};
            char* parsed = nullptr;
            const double value = tail.empty() ? 0.0 : std::strtod(tail.c_str(), &parsed);
            if (!tail.empty() && parsed == tail.c_str() + tail.size()) {
                if (!std::isfinite(value) || value <= 0.0 || value > kMaxAdapterScale) {
                    throw ConfigEditError("adapters: " + std::string{item} + ": its scale " + tail +
                                          " is outside (0, 2]");
                }
                adapter.scale = value;
                file = item.substr(0, colon);
            }
        }
        if (file.empty()) {
            throw ConfigEditError("adapters: '" + std::string{text} +
                                  "' names an empty adapter -- each is <path>[:<scale>], "
                                  "comma-separated");
        }
        adapter.path = std::string{file};
        adapters.push_back(std::move(adapter));
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return adapters;
}

std::string set_backend_mmproj_path(std::string_view content, std::string_view name,
                                    std::string_view path) {
    return in_format(content, [&](std::string_view text) {
        return yaml_text::set_backend_mmproj_path(text, name, path);
    });
}

std::vector<std::string_view> models_role_fields() {
    return yaml_text::models_role_fields();
}

std::string set_models_role(std::string_view content, std::string_view field,
                            std::string_view value) {
    return in_format(content, [&](std::string_view text) {
        return yaml_text::set_models_role(text, field, value);
    });
}

std::string append_mcp_server(std::string_view content, std::string_view name,
                              const McpServerConfig& server, bool force) {
    return in_format(content, [&](std::string_view text) {
        return yaml_text::append_mcp_server(text, name, server, force);
    });
}

std::string delete_mcp_server(std::string_view content, std::string_view name) {
    return in_format(
        content, [&](std::string_view text) { return yaml_text::delete_mcp_server(text, name); });
}

std::string append_review_target(std::string_view content, std::string_view name,
                                 const ReviewTargetConfig& target, bool force) {
    return in_format(content, [&](std::string_view text) {
        return yaml_text::append_review_target(text, name, target, force);
    });
}

std::string delete_review_target(std::string_view content, std::string_view name) {
    return in_format(content, [&](std::string_view text) {
        return yaml_text::delete_review_target(text, name);
    });
}

std::string set_mcp_server_enabled(std::string_view content, std::string_view name, bool enabled) {
    return in_format(content, [&](std::string_view text) {
        return yaml_text::set_mcp_server_enabled(text, name, enabled);
    });
}

std::string append_agent(std::string_view content, std::string_view name, const AgentConfig& agent,
                         bool force) {
    return in_format(content, [&](std::string_view text) {
        return yaml_text::append_agent(text, name, agent, force);
    });
}

std::string delete_agent(std::string_view content, std::string_view name) {
    return in_format(content,
                     [&](std::string_view text) { return yaml_text::delete_agent(text, name); });
}

std::string append_symphony(std::string_view content, std::string_view name,
                            const SymphonySpec& spec, bool force) {
    return in_format(content, [&](std::string_view text) {
        return yaml_text::append_symphony(text, name, spec, force);
    });
}

std::string delete_symphony(std::string_view content, std::string_view name) {
    return in_format(content,
                     [&](std::string_view text) { return yaml_text::delete_symphony(text, name); });
}

std::string render_symphony_spec(const SymphonySpec& spec) {
    // A spec file, not the config: it stays YAML with the other spec files.
    return yaml_text::render_symphony_spec(spec);
}

std::string append_graph(std::string_view content, std::string_view name,
                         const NamedGraphConfig& graph, bool force) {
    return in_format(content, [&](std::string_view text) {
        return yaml_text::append_graph(text, name, graph, force);
    });
}

std::string delete_graph(std::string_view content, std::string_view name) {
    return in_format(content,
                     [&](std::string_view text) { return yaml_text::delete_graph(text, name); });
}

std::string append_suite(std::string_view content, std::string_view name, const SuiteConfig& suite,
                         bool force) {
    return in_format(content, [&](std::string_view text) {
        return yaml_text::append_suite(text, name, suite, force);
    });
}

std::string delete_suite(std::string_view content, std::string_view name) {
    return in_format(content,
                     [&](std::string_view text) { return yaml_text::delete_suite(text, name); });
}

std::string set_suite_member(std::string_view content, std::string_view suite,
                             std::string_view role, const std::optional<SuiteMember>& member) {
    return in_format(content, [&](std::string_view text) {
        return yaml_text::set_suite_member(text, suite, role, member);
    });
}

std::string set_suite_consultable(std::string_view content, std::string_view suite,
                                  const std::vector<std::string>& roles) {
    return in_format(content, [&](std::string_view text) {
        return yaml_text::set_suite_consultable(text, suite, roles);
    });
}

std::string set_suite_consult_caps(std::string_view content, std::string_view suite,
                                   const ConsultCaps& caps) {
    return in_format(content, [&](std::string_view text) {
        return yaml_text::set_suite_consult_caps(text, suite, caps);
    });
}

std::string set_suite_validate(std::string_view content, std::string_view suite,
                               const ValidateConfig& validate) {
    return in_format(content, [&](std::string_view text) {
        return yaml_text::set_suite_validate(text, suite, validate);
    });
}

std::string set_suite_orchestrate(std::string_view content, std::string_view suite,
                                  bool orchestrate) {
    return in_format(content, [&](std::string_view text) {
        return yaml_text::set_suite_orchestrate(text, suite, orchestrate);
    });
}

std::string set_default_suite(std::string_view content, std::string_view name) {
    return in_format(
        content, [&](std::string_view text) { return yaml_text::set_default_suite(text, name); });
}

std::string set_default_video(std::string_view content, std::string_view name) {
    return in_format(
        content, [&](std::string_view text) { return yaml_text::set_default_video(text, name); });
}

std::string set_attachments_graph(std::string_view content, std::string_view method) {
    return in_format(content, [&](std::string_view text) {
        return yaml_text::set_attachments_graph(text, method);
    });
}

std::vector<SettableScalar> settable_scalars() {
    return {
        {.key = "memory.recall",
         .kind = ScalarKind::Boolean,
         .about = "summarise finished chats and recall them in new ones"},
        {.key = "knowledge.auto_capture",
         .kind = ScalarKind::Boolean,
         .about = "capture a chat into the knowledge collection at a clean exit"},
        {.key = "ui.markdown",
         .kind = ScalarKind::Boolean,
         .about = "render answers as markdown on a terminal"},
        {.key = "ui.thinking_display",
         .kind = ScalarKind::Word,
         .words = "summary|raw",
         .about = "how a model's reasoning is shown while it streams"},
        {.key = "attachments.graph",
         .kind = ScalarKind::Word,
         .words = "code|off",
         .about = "whether an attached folder of code builds its graph"},
        {.key = "tools.search.provider",
         .kind = ScalarKind::Word,
         .words = "searxng",
         .about = "the web search web_search answers from"},
        {.key = "tools.search.url",
         .kind = ScalarKind::Text,
         .about = "the web search's address, such as http://127.0.0.1:8888"},
        {.key = "tools.search.results",
         .kind = ScalarKind::Integer,
         .min = 1,
         .max = 20,
         .about = "how many results a search returns"},
        {.key = "training.python",
         .kind = ScalarKind::Text,
         .about = "the Python a new training environment is made from (M19)"},
    };
}

namespace {

[[nodiscard]] std::string scalar_keys_list() {
    std::string out;
    for (const SettableScalar& scalar : settable_scalars()) {
        out += (out.empty() ? "" : ", ") + std::string{scalar.key};
    }
    return out;
}

[[nodiscard]] bool has_segment(std::string_view key, std::string_view part) {
    std::string lowered{key};
    std::ranges::transform(lowered, lowered.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lowered.find(part) != std::string::npos;
}

}  // namespace

std::string scalar_refusal(std::string_view key) {
    for (const SettableScalar& scalar : settable_scalars()) {
        if (scalar.key == key) {
            return {};
        }
    }
    const std::string named = "'" + std::string{key} + "'";
    // A secret never goes in the file, whatever key it is under.
    for (const std::string_view secret :
         {"api_key", "apikey", "token", "secret", "password", "credential"}) {
        if (has_segment(key, secret)) {
            return named +
                   " holds a secret, and a secret never goes in the config file -- store it with "
                   "'apogee auth add'";
        }
    }

    struct Door {
        std::string_view prefix;
        std::string_view door;
    };

    static constexpr std::array<Door, 9> kDoors{{
        {"backends",
         "a backend's fields have their verb: 'apogee config set-backend <name> "
         "--<field> <value>'"},
        {"models",
         "the role pointers have their verbs: 'apogee config set-default <backend>' "
         "and the set-default-* beside it"},
        {"suites",
         "a suite has its verbs: 'apogee config add-suite' and 'apogee config "
         "set-suite'"},
        {"permissions",
         "a tool's level has its verb: 'apogee config set-permission <tool> "
         "<ask|allow|deny>'"},
        {"tools.allowed_hosts",
         "the websites have their verbs: 'apogee config "
         "add-allowed-host <host>' and delete-allowed-host"},
        {"mcp_servers",
         "an MCP server has its verbs: 'apogee mcp create', 'apogee mcp "
         "enable'/'disable' and 'apogee config delete-mcp-server'"},
        {"embeddings", "a collection is made by 'apogee embed ingest' and kept by its verbs"},
        {"graphs", "a named graph has its verbs: 'apogee config add-graph' and delete-graph"},
        {"agents", "an agent has its verbs: 'apogee agents create', edit and delete"},
    }};
    for (const Door& door : kDoors) {
        if (key == door.prefix ||
            (key.starts_with(door.prefix) && key.size() > door.prefix.size() &&
             key[door.prefix.size()] == '.')) {
            return named + " is not a key 'config set' moves -- " + std::string{door.door};
        }
    }
    return named + " is not a key 'config set' moves -- it moves: " + scalar_keys_list();
}

std::string set_scalar(std::string_view content, std::string_view key, std::string_view value) {
    if (const std::string refused = scalar_refusal(key); !refused.empty()) {
        throw ConfigEditError(refused);
    }
    const std::vector<SettableScalar> table = settable_scalars();
    const SettableScalar& scalar = *std::ranges::find(table, key, &SettableScalar::key);
    std::string rendered;
    switch (scalar.kind) {
        case ScalarKind::Boolean:
            if (value != "true" && value != "false") {
                throw ConfigEditError(std::string{key} + " takes true or false, not '" +
                                      std::string{value} + "'");
            }
            rendered = std::string{value};
            break;
        case ScalarKind::Word: {
            bool known = false;
            std::string_view rest = scalar.words;
            while (!rest.empty()) {
                const std::size_t bar = rest.find('|');
                known = known || rest.substr(0, bar) == value;
                rest = bar == std::string_view::npos ? std::string_view{} : rest.substr(bar + 1);
            }
            if (!known) {
                std::string words{scalar.words};
                std::ranges::replace(words, '|', ' ');
                throw ConfigEditError(std::string{key} + " takes " + words + ", not '" +
                                      std::string{value} + "'");
            }
            rendered = key == "attachments.graph" ? std::string{value} : yaml_scalar(value);
            break;
        }
        case ScalarKind::Text:
            rendered = yaml_scalar(value);
            break;
        case ScalarKind::Integer: {
            std::int64_t number = 0;
            const auto [end, error] =
                std::from_chars(value.data(), value.data() + value.size(), number);
            if (error != std::errc{} || end != value.data() + value.size() || number < scalar.min ||
                number > scalar.max) {
                throw ConfigEditError(
                    std::string{key} + " takes a whole number from " + std::to_string(scalar.min) +
                    " to " + std::to_string(scalar.max) + ", not '" + std::string{value} + "'");
            }
            rendered = std::to_string(number);
            break;
        }
    }
    return in_format(
        content, [&](std::string_view text) { return yaml_text::set_scalar(text, key, rendered); });
}

std::string set_permission(std::string_view content, std::string_view tool,
                           std::string_view level) {
    return in_format(content, [&](std::string_view text) {
        return yaml_text::set_permission(text, tool, level);
    });
}

std::string add_allowed_host(std::string_view content, std::string_view host) {
    return in_format(
        content, [&](std::string_view text) { return yaml_text::add_allowed_host(text, host); });
}

std::string remove_allowed_host(std::string_view content, std::string_view host) {
    return in_format(
        content, [&](std::string_view text) { return yaml_text::remove_allowed_host(text, host); });
}

std::string format_config(std::string_view content) {
    // Whitespace only, never a key or a value: the same tidy in either format.
    return yaml_text::format_config(content);
}

std::string read_config_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw ConfigError(path.string() +
                          ": cannot open config file (run 'apogee config init' to create one)");
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) {
        throw ConfigError(path.string() + ": error reading config file");
    }
    return buffer.str();
}

platform::FileLock lock_config_file(const std::filesystem::path& path,
                                    std::chrono::milliseconds wait) {
    // A config that is not there is said as reading it says it, and leaves no
    // lock file behind.
    if (std::error_code code; !std::filesystem::exists(path, code)) {
        (void)read_config_file(path);
    }
    // A sibling, never the config itself: the edit renames a new file over
    // the config, and a lock on the old one would order nothing after it.
    std::filesystem::path lock_path = path;
    lock_path += ".lock";
    platform::FileLock::Refusal refusal;
    std::optional<platform::FileLock> lock = platform::FileLock::acquire(lock_path, wait, refusal);
    if (!lock.has_value()) {
        if (refusal.timed_out) {
            throw ConfigEditError("another edit of " + path.string() + " is still running -- " +
                                  refusal.error +
                                  "; nothing was changed: run this again once it ends");
        }
        throw ConfigEditError("cannot lock " + path.string() + " for the edit -- " + refusal.error +
                              "; nothing was changed");
    }
    return std::move(*lock);
}

void write_file_atomically(const std::filesystem::path& path, std::string_view content,
                           bool private_mode) {
    const std::filesystem::path directory =
        path.has_parent_path() ? path.parent_path() : std::filesystem::path{"."};

    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        throw ConfigEditError(directory.string() + ": cannot create directory: " + ec.message());
    }

    // A unique sibling name, so two concurrent writers cannot collide on it.
    std::random_device entropy;
    const std::filesystem::path temp_path =
        directory / (path.filename().string() + ".tmp." + std::to_string(entropy()));

    {
        std::ofstream out(temp_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw ConfigEditError(temp_path.string() + ": cannot create temporary file");
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        if (!out) {
            std::filesystem::remove(temp_path, ec);
            throw ConfigEditError(temp_path.string() + ": error writing temporary file");
        }
    }

    if (private_mode && supports_private_modes()) {
        // On the temporary file, BEFORE the rename: the secret is never on
        // disk under the umask's mode, not even between two syscalls.
        std::filesystem::permissions(
            temp_path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
            std::filesystem::perm_options::replace, ec);
        if (ec) {
            std::error_code cleanup;
            std::filesystem::remove(temp_path, cleanup);
            throw ConfigEditError(temp_path.string() +
                                  ": cannot set private mode: " + ec.message());
        }
    }

    std::filesystem::rename(temp_path, path, ec);
    if (ec) {
        std::error_code cleanup;
        std::filesystem::remove(temp_path, cleanup);
        throw ConfigEditError(path.string() + ": cannot replace file: " + ec.message());
    }
}

}  // namespace apogee::harness
