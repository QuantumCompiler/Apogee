#include "harness/config_edit.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <random>
#include <sstream>
#include <system_error>
#include <utility>

namespace apogee::harness {
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

/// Quotes `value` when YAML would otherwise misread it.
///
/// Erring toward quoting is deliberate -- an api_key of `${VAR}` or a
/// Windows model_path with a colon in it must survive a round trip, and a
/// needlessly quoted string reads the same to the loader.
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
    if (!backend.embedding_model.empty()) {
        field("embedding_model", yaml_scalar(backend.embedding_model));
    }
    if (backend.context_size.has_value()) {
        field("context_size", std::to_string(*backend.context_size));
    }
    if (backend.max_tokens.has_value()) {
        field("max_tokens", std::to_string(*backend.max_tokens));
    }
    if (backend.temperature.has_value()) {
        field("temperature", number_scalar(*backend.temperature));
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
    if (name.find_first_of(" \t:#") != std::string_view::npos) {
        throw ConfigEditError(std::string{noun} + " name '" + std::string{name} +
                              "' cannot contain spaces, tabs, colons, or '#'");
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

std::vector<std::string_view> models_role_fields() {
    return {"default", "default_embedding", "default_extraction"};
}

std::string set_models_role(std::string_view content, std::string_view field,
                            std::string_view value) {
    const std::vector<std::string_view> allowed = models_role_fields();
    if (std::find(allowed.begin(), allowed.end(), field) == allowed.end()) {
        throw ConfigEditError("unknown models field '" + std::string{field} +
                              "' (accepted: default, default_embedding, default_extraction)");
    }

    Lines lines = split_lines(content);
    const std::string terminator = dominant_terminator(lines);
    const std::string new_line =
        std::string(kEntryIndent, ' ') + std::string{field} + ": " + yaml_scalar(value);

    SectionRange section = find_section(lines, "models");

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
                insert_at = i + 1;
                continue;
            }
            // Found it. Replace ONLY the value token, splicing it between the
            // original prefix and the original suffix.
            //
            // Done this way rather than by rebuilding the line so that the
            // trailing comment AND the exact whitespace before it survive: a
            // config whose comments are column-aligned stays aligned, and the
            // diff for setting a role is one token wide.
            const std::string& original = lines[i];
            const std::size_t key_colon = original.find(':', kEntryIndent);
            const std::size_t hash = original.find('#', key_colon);
            const std::size_t limit = (hash == std::string::npos) ? original.size() : hash;

            std::size_t value_start = key_colon + 1;
            while (value_start < limit &&
                   (original[value_start] == ' ' || original[value_start] == '\t')) {
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
            replacement += yaml_scalar(value);
            replacement += original.substr(value_end);
            if (replacement.empty() || replacement.back() != '\n') {
                replacement += terminator;
            }
            lines[i] = replacement;
            return join_lines(lines);
        }

        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insert_at), new_line + terminator);
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
    lines.push_back("models:" + terminator);
    lines.push_back(new_line + terminator);
    return join_lines(lines);
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

void write_file_atomically(const std::filesystem::path& path, std::string_view content) {
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

    std::filesystem::rename(temp_path, path, ec);
    if (ec) {
        std::error_code cleanup;
        std::filesystem::remove(temp_path, cleanup);
        throw ConfigEditError(path.string() + ": cannot replace config file: " + ec.message());
    }
}

}  // namespace apogee::harness
