#include "tools/fs.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <regex>
#include <sstream>
#include <system_error>
#include <vector>

#include "contracts/config_edit.h"
#include "platform/platform.h"
#include "tools/args.h"

namespace apogee::tools {
namespace {

std::filesystem::path expand_home(std::string_view path) {
    if (path == "~" || path.starts_with("~/")) {
        const std::optional<std::string> home = platform::home_directory();
        if (home.has_value()) {
            return std::filesystem::path{*home} / std::string{path.substr(path == "~" ? 1 : 2)};
        }
    }
    return std::filesystem::path{std::string{path}};
}

/// Whether `ancestor` is `path` itself or one of its ancestors, judged by
/// components -- the comparison that `/home/user` vs `/home/userX` needs.
bool is_within(const std::filesystem::path& ancestor, const std::filesystem::path& path) {
    auto a = ancestor.begin();
    auto p = path.begin();
    for (; a != ancestor.end(); ++a, ++p) {
        if (p == path.end() || *a != *p) {
            return false;
        }
    }
    return true;
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string read_bytes(const std::filesystem::path& path, std::size_t limit) {
    std::ifstream in{path, std::ios::binary};
    std::string data;
    data.resize(limit);
    in.read(data.data(), static_cast<std::streamsize>(limit));
    data.resize(static_cast<std::size_t>(in.gcount()));
    return data;
}

/// `number`, right-aligned in six columns and followed by a tab -- `cat -n`'s
/// shape, which models read as "a line number, not the file's text".
std::string numbered(std::int64_t number, std::string_view line) {
    std::string prefix = std::to_string(number);
    if (prefix.size() < 6) {
        prefix.insert(0, 6 - prefix.size(), ' ');
    }
    return prefix + "\t" + std::string{line};
}

/// A line range of a file, numbered, capped at `limit_bytes` of output.
///
/// The whole file is streamed, never held: the lines before the range are
/// skipped, and the ones after it are only counted, so a model is told how
/// long the file is and where to read on.
agent::ToolOutcome read_range(const std::filesystem::path& path, std::int64_t offset,
                              std::optional<std::int64_t> limit, std::size_t limit_bytes) {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        return error("could not open " + path.string());
    }
    std::string out;
    std::string line;
    std::int64_t number = 0;
    std::int64_t last_shown = 0;
    bool stopped_by_size = false;
    while (std::getline(in, line)) {
        ++number;
        if (number < offset || stopped_by_size ||
            (limit.has_value() && number >= offset + *limit)) {
            continue;
        }
        std::string row = numbered(number, line);
        if (out.size() + row.size() + 1 > limit_bytes) {
            if (last_shown == 0) {
                // One line longer than the whole cap: its start, said so,
                // cut before a character rather than through one.
                std::size_t cut = limit_bytes;
                while (cut > 0 && (static_cast<unsigned char>(row[cut]) & 0xC0U) == 0x80U) {
                    --cut;
                }
                row.resize(cut);
                out = row + "\n[line " + std::to_string(number) + " is longer than " +
                      std::to_string(limit_bytes / 1024) + " KB; only its start is shown]\n";
                last_shown = number;
            }
            stopped_by_size = true;
            continue;
        }
        out += row;
        out += '\n';
        last_shown = number;
    }
    const std::int64_t total = number;
    if (total == 0) {
        return ok("[" + path.string() + " is empty]");
    }
    if (offset > total) {
        return error("offset " + std::to_string(offset) + " is past the end of " + path.string() +
                     ", which has " + std::to_string(total) + " lines");
    }
    out += "[lines " + std::to_string(offset) + "-" + std::to_string(last_shown) + " of " +
           std::to_string(total);
    if (stopped_by_size) {
        out += "; a read returns at most " + std::to_string(limit_bytes / 1024) + " KB";
    }
    if (last_shown < total) {
        out += ". Read on with offset " + std::to_string(last_shown + 1);
    }
    out += "]";
    return ok(std::move(out));
}

/// How many lines `path` holds, a last one without a newline included.
std::int64_t count_lines(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    std::int64_t lines = 0;
    std::array<char, 64 * 1024> buffer{};
    char last = '\n';
    while (in.read(buffer.data(), buffer.size()) || in.gcount() > 0) {
        const auto read = static_cast<std::size_t>(in.gcount());
        lines +=
            std::count(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(read), '\n');
        last = buffer[read - 1];
    }
    return lines + (last == '\n' ? 0 : 1);
}

/// Where each occurrence of `needle` starts in `haystack`, not overlapping.
std::vector<std::size_t> occurrences(std::string_view haystack, std::string_view needle) {
    std::vector<std::size_t> found;
    for (std::size_t at = haystack.find(needle); at != std::string_view::npos;
         at = haystack.find(needle, at + needle.size())) {
        found.push_back(at);
    }
    return found;
}

/// `text` with every "\n" not already preceded by "\r" made "\r\n".
std::string with_crlf(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n' && (i == 0 || text[i - 1] != '\r')) {
            out += '\r';
        }
        out += text[i];
    }
    return out;
}

/// The 1-based line `offset` falls on in `text`.
std::int64_t line_of(std::string_view text, std::size_t offset) {
    return 1 + static_cast<std::int64_t>(std::count(
                   text.begin(), text.begin() + static_cast<std::ptrdiff_t>(offset), '\n'));
}

/// A file that holds a NUL in its first 8 KiB is data, not text -- grep's
/// own rule, and what keeps a search out of object files and images.
bool looks_binary(const std::filesystem::path& path) {
    return read_bytes(path, 8 * 1024).find('\0') != std::string::npos;
}

}  // namespace

std::optional<std::filesystem::path> resolve_in_root(const std::filesystem::path& root,
                                                     std::string_view path, std::string& error) {
    if (trim(path).empty()) {
        error = "path is required";
        return std::nullopt;
    }
    std::error_code code;
    const std::filesystem::path canonical_root = std::filesystem::weakly_canonical(root, code);
    if (code) {
        error = "the sandbox root " + root.string() + " cannot be resolved: " + code.message();
        return std::nullopt;
    }
    std::filesystem::path candidate = expand_home(trim(path));
    if (candidate.is_relative()) {
        candidate = canonical_root / candidate;
    }
    // weakly_canonical follows symlinks on the part that exists and
    // normalises the rest, so a symlink pointing out of the root is judged by
    // where it points, and a file about to be created by where it will land.
    const std::filesystem::path resolved = std::filesystem::weakly_canonical(candidate, code);
    if (code) {
        error = "'" + std::string{path} + "' cannot be resolved: " + code.message();
        return std::nullopt;
    }
    if (!is_within(canonical_root, resolved)) {
        error = "'" + std::string{path} + "' is outside the allowed root " +
                canonical_root.string() +
                ". The file tools work in the folder Apogee was started in unless "
                "tools.fs_root in the config says otherwise: start Apogee in a folder that "
                "contains it, or set tools.fs_root to expand access.";
        return std::nullopt;
    }
    return resolved;
}

bool wildcard_match(std::string_view pattern, std::string_view name) {
    // Iterative glob with backtracking on the last `*` -- the classic
    // algorithm, enough for the file patterns a model writes.
    std::size_t p = 0;
    std::size_t n = 0;
    std::size_t star = std::string_view::npos;
    std::size_t match = 0;
    while (n < name.size()) {
        if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            match = n;
            continue;
        }
        if (p < pattern.size() && pattern[p] == '[') {
            const std::size_t close = pattern.find(']', p + 1);
            if (close != std::string_view::npos) {
                const std::string_view set = pattern.substr(p + 1, close - p - 1);
                bool negate = !set.empty() && (set[0] == '!' || set[0] == '^');
                bool hit = false;
                for (std::size_t i = negate ? 1 : 0; i < set.size(); ++i) {
                    if (i + 2 < set.size() && set[i + 1] == '-') {
                        if (name[n] >= set[i] && name[n] <= set[i + 2]) {
                            hit = true;
                        }
                        i += 2;
                    } else if (set[i] == name[n]) {
                        hit = true;
                    }
                }
                if (hit != negate) {
                    p = close + 1;
                    ++n;
                    continue;
                }
            }
        } else if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == name[n])) {
            ++p;
            ++n;
            continue;
        }
        if (star != std::string_view::npos) {
            p = star + 1;
            n = ++match;
            continue;
        }
        return false;
    }
    while (p < pattern.size() && pattern[p] == '*') {
        ++p;
    }
    return p == pattern.size();
}

void register_fs_tools(agent::ToolRegistry& registry, const std::filesystem::path& root,
                       std::size_t read_limit) {
    const std::string root_text = root.string();
    const std::string restricted =
        " Access is restricted to " + root_text + " and its subdirectories.";

    agent::Tool read;
    read.name = "read_file";
    read.description =
        "Read a file and return its contents as text. A file larger than " +
        std::to_string(read_limit / 1024) +
        " KB is not returned whole: read it in ranges by passing offset (the line to start at, "
        "counting from 1) and limit (how many lines), or search it first with grep_files. A "
        "ranged read prefixes each line with its number and a tab, which are not part of the "
        "file, and returns at most " +
        std::to_string(read_limit / 1024) + " KB." + restricted;
    read.parameters_schema =
        R"({"type":"object","properties":{"path":{"type":"string","description":"Absolute path, ~ path, or a path relative to the root"},"offset":{"type":"integer","description":"The line to start reading at, counting from 1"},"limit":{"type":"integer","description":"How many lines to read"}},"required":["path"]})";
    read.run = [root, read_limit](std::string_view arguments) -> agent::ToolOutcome {
        agent::ToolOutcome failure;
        const std::optional<Arguments> args =
            parse_arguments(arguments, R"({"path": "...", "offset": 1, "limit": 200})", failure);
        if (!args.has_value()) {
            return failure;
        }
        std::string problem;
        const std::optional<std::filesystem::path> path =
            resolve_in_root(root, args->string("path"), problem);
        if (!path.has_value()) {
            return error(problem);
        }
        std::error_code code;
        if (!std::filesystem::is_regular_file(*path, code)) {
            return error(std::filesystem::exists(*path, code)
                             ? path->string() + " is not a regular file"
                             : "No such file: " + path->string());
        }
        if (args->has("offset") || args->has("limit")) {
            // Numbered only when a range is asked for: a plain read stays
            // byte for byte what is on disk, for a model about to quote it.
            const std::optional<std::int64_t> offset = args->integer("offset");
            const std::optional<std::int64_t> limit = args->integer("limit");
            if (args->has("offset") && (!offset.has_value() || *offset < 1)) {
                return error("offset is a line number, counting from 1");
            }
            if (args->has("limit") && (!limit.has_value() || *limit < 1)) {
                return error("limit is a number of lines, at least 1");
            }
            return read_range(*path, offset.value_or(1), limit, read_limit);
        }
        const std::uintmax_t size = std::filesystem::file_size(*path, code);
        if (size > read_limit) {
            // Nothing rather than the first 64 KB (2026-09-28): on a local
            // model's window that much text is the whole context, and the
            // turn ends there (found on real weights). The size and length
            // are what a model needs to read it in ranges instead.
            return error(path->string() + " is " + std::to_string(size) + " bytes, " +
                         std::to_string(count_lines(*path)) +
                         " lines: more than one read returns (" +
                         std::to_string(read_limit / 1024) +
                         " KB), so none of it was read. Read it in ranges with offset and limit "
                         "(in lines), or find what you need first with grep_files.");
        }
        return ok(read_bytes(*path, read_limit));
    };
    read.describe_target = [](std::string_view arguments) {
        agent::ToolOutcome unused;
        const std::optional<Arguments> args = parse_arguments(arguments, "", unused);
        return args.has_value() ? args->string("path") : std::string{};
    };
    registry.add(read);

    agent::Tool write;
    write.name = "write_file";
    write.description =
        "Write text to a file, creating it and any missing parent directories. Overwrites "
        "existing content." +
        restricted + " This operation requires the user's permission.";
    write.parameters_schema =
        R"({"type":"object","properties":{"path":{"type":"string","description":"Absolute path, ~ path, or a path relative to the root"},"content":{"type":"string","description":"The text to write"}},"required":["path","content"]})";
    write.writes = true;
    write.describe_target = read.describe_target;
    write.run = [root](std::string_view arguments) -> agent::ToolOutcome {
        agent::ToolOutcome failure;
        const std::optional<Arguments> args =
            parse_arguments(arguments, R"({"path": "...", "content": "..."})", failure);
        if (!args.has_value()) {
            return failure;
        }
        std::string problem;
        const std::optional<std::filesystem::path> path =
            resolve_in_root(root, args->string("path"), problem);
        if (!path.has_value()) {
            return error(problem);
        }
        if (!args->has("content") || !args->object["content"].is_string()) {
            return error("content is required, as a string");
        }
        const std::string content = args->object["content"].get<std::string>();
        std::error_code code;
        std::filesystem::create_directories(path->parent_path(), code);
        std::ofstream out{*path, std::ios::binary | std::ios::trunc};
        if (!out) {
            return error("could not open " + path->string() + " for writing");
        }
        out << content;
        if (!out) {
            return error("could not write " + path->string());
        }
        return ok("Wrote " + std::to_string(content.size()) + " bytes to " + path->string());
    };
    registry.add(write);

    agent::Tool edit;
    edit.name = "edit_file";
    edit.description =
        "Replace exact text in an existing file, leaving the rest of it byte for byte as it "
        "was. old_string must match the file exactly, whitespace and indentation included, and "
        "only once: include enough surrounding lines to make it unique, or set replace_all to "
        "replace every occurrence. Nothing is changed when it is missing or ambiguous. To create "
        "a file, use write_file." +
        restricted + " This operation requires the user's permission.";
    edit.parameters_schema =
        R"({"type":"object","properties":{"path":{"type":"string","description":"Absolute path, ~ path, or a path relative to the root"},"old_string":{"type":"string","description":"The exact text to replace"},"new_string":{"type":"string","description":"The text to put in its place"},"replace_all":{"type":"boolean","description":"Replace every occurrence rather than exactly one"}},"required":["path","old_string","new_string"]})";
    edit.writes = true;
    edit.describe_target = read.describe_target;
    edit.run = [root](std::string_view arguments) -> agent::ToolOutcome {
        agent::ToolOutcome failure;
        const std::optional<Arguments> args = parse_arguments(
            arguments, R"({"path": "...", "old_string": "...", "new_string": "..."})", failure);
        if (!args.has_value()) {
            return failure;
        }
        std::string problem;
        const std::optional<std::filesystem::path> path =
            resolve_in_root(root, args->string("path"), problem);
        if (!path.has_value()) {
            return error(problem);
        }
        // Read raw, never trimmed: the whitespace is part of what must match.
        const auto raw = [&](std::string_view key) -> std::optional<std::string> {
            const auto it = args->object.find(key);
            if (it == args->object.end() || !it->is_string()) {
                return std::nullopt;
            }
            return it->get<std::string>();
        };
        std::optional<std::string> old_text = raw("old_string");
        std::optional<std::string> new_text = raw("new_string");
        if (!old_text.has_value() || old_text->empty()) {
            return error(
                "old_string is required: the exact text to replace. To create a file, "
                "use write_file");
        }
        if (!new_text.has_value()) {
            return error("new_string is required, as a string (empty deletes old_string)");
        }
        if (*old_text == *new_text) {
            return error("old_string and new_string are the same; nothing to change");
        }
        const auto flag = args->object.find("replace_all");
        const bool replace_all =
            flag != args->object.end() &&
            (flag->is_boolean() ? flag->get<bool>()
                                : flag->is_string() && flag->get<std::string>() == "true");

        std::error_code code;
        if (!std::filesystem::is_regular_file(*path, code)) {
            return error(std::filesystem::exists(*path, code)
                             ? path->string() + " is not a regular file"
                             : "No such file: " + path->string() +
                                   ". edit_file changes an existing file; write_file creates one");
        }
        std::string content;
        {
            // Closed before the edit is renamed over it: Windows refuses to
            // replace a file that is still open, so an edit there failed
            // every time (found on the first Windows run, 2026-10-04).
            std::ifstream in{*path, std::ios::binary};
            content.assign(std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{});
            if (!in.good() && !in.eof()) {
                return error("could not read " + path->string());
            }
        }

        std::vector<std::size_t> found = occurrences(content, *old_text);
        if (found.empty() && content.find("\r\n") != std::string::npos &&
            old_text->find('\n') != std::string::npos) {
            // A file with Windows line endings, and text copied without
            // them: matched as the file writes its lines, and replaced so.
            std::string crlf_old = with_crlf(*old_text);
            found = occurrences(content, crlf_old);
            if (!found.empty()) {
                old_text = std::move(crlf_old);
                new_text = with_crlf(*new_text);
            }
        }
        if (found.empty()) {
            return error("old_string was not found in " + path->string() +
                         "; nothing was changed. Read the file and copy the text exactly, "
                         "whitespace and indentation included");
        }
        if (found.size() > 1 && !replace_all) {
            return error("old_string occurs " + std::to_string(found.size()) + " times in " +
                         path->string() +
                         "; nothing was changed. Include more of the surrounding text to make "
                         "it unique, or set replace_all to replace every occurrence");
        }

        std::string edited;
        edited.reserve(content.size());
        std::string lines;
        std::size_t from = 0;
        for (const std::size_t at : found) {
            edited.append(content, from, at - from);
            lines += (lines.empty() ? "" : ", ") + std::to_string(line_of(content, at));
            edited += *new_text;
            from = at + old_text->size();
        }
        edited.append(content, from, std::string::npos);

        // Written beside the file and renamed over it, so a failed write
        // leaves the file as it was; the mode carried over, so an edited
        // script stays executable.
        const std::filesystem::perms mode = std::filesystem::status(*path, code).permissions();
        try {
            harness::write_file_atomically(*path, edited);
        } catch (const std::exception& e) {
            return error("could not write " + path->string() + ": " + e.what());
        }
        std::filesystem::permissions(*path, mode, code);
        return ok("Replaced " + std::to_string(found.size()) +
                  (found.size() == 1 ? " occurrence" : " occurrences") + " in " + path->string() +
                  (found.size() == 1 ? " at line " : " at lines ") + lines);
    };
    registry.add(edit);

    agent::Tool remove;
    remove.name = "delete_file";
    remove.description =
        "Delete a single file permanently. Directories are not accepted -- use the shell tool "
        "for recursive removal." +
        restricted + " This operation requires the user's permission.";
    remove.parameters_schema = read.parameters_schema;
    remove.writes = true;
    remove.describe_target = read.describe_target;
    remove.run = [root](std::string_view arguments) -> agent::ToolOutcome {
        agent::ToolOutcome failure;
        const std::optional<Arguments> args =
            parse_arguments(arguments, R"({"path": "..."})", failure);
        if (!args.has_value()) {
            return failure;
        }
        std::string problem;
        const std::optional<std::filesystem::path> path =
            resolve_in_root(root, args->string("path"), problem);
        if (!path.has_value()) {
            return error(problem);
        }
        std::error_code code;
        if (!std::filesystem::exists(*path, code)) {
            return error("No such file: " + path->string());
        }
        if (std::filesystem::is_directory(*path, code)) {
            return error(path->string() +
                         " is a directory -- use the shell tool to remove directories");
        }
        if (!std::filesystem::remove(*path, code) || code) {
            return error("could not delete " + path->string() + ": " + code.message());
        }
        return ok("Deleted " + path->string());
    };
    registry.add(remove);

    agent::Tool list;
    list.name = "list_directory";
    list.description =
        "List a directory: subdirectories first, then files with their sizes." + restricted;
    list.parameters_schema = read.parameters_schema;
    list.run = [root](std::string_view arguments) -> agent::ToolOutcome {
        agent::ToolOutcome failure;
        const std::optional<Arguments> args =
            parse_arguments(arguments, R"({"path": "..."})", failure);
        if (!args.has_value()) {
            return failure;
        }
        std::string problem;
        const std::optional<std::filesystem::path> path = resolve_in_root(
            root, args->string("path").empty() ? "." : args->string("path"), problem);
        if (!path.has_value()) {
            return error(problem);
        }
        std::error_code code;
        if (!std::filesystem::is_directory(*path, code)) {
            return error(path->string() + " is not a directory");
        }
        struct Entry {
            std::string name;
            bool directory;
            std::uintmax_t size;
        };
        std::vector<Entry> entries;
        for (const auto& item : std::filesystem::directory_iterator{*path, code}) {
            const bool directory = item.is_directory(code);
            entries.push_back(Entry{item.path().filename().string(), directory,
                                    directory ? 0 : item.file_size(code)});
        }
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            if (a.directory != b.directory) {
                return a.directory;
            }
            return lower(a.name) < lower(b.name);
        });
        if (entries.empty()) {
            return ok("(empty directory: " + path->string() + ")");
        }
        std::string out = path->string() + "/\n";
        for (const Entry& entry : entries) {
            out += entry.directory ? "DIR   " : "FILE  ";
            out += entry.name;
            if (!entry.directory) {
                out += "  " + std::to_string(entry.size) + " B";
            }
            out += "\n";
        }
        out.pop_back();
        return ok(std::move(out));
    };
    registry.add(list);

    agent::Tool search;
    search.name = "search_files";
    search.description =
        "Find files whose name matches a shell-style pattern (*, ?, [abc]) under a directory, "
        "recursively. Hidden directories (.git and the like) are skipped." +
        restricted;
    search.parameters_schema =
        R"({"type":"object","properties":{"pattern":{"type":"string","description":"A filename pattern such as *.md"},"root":{"type":"string","description":"Directory to search; defaults to the sandbox root"}},"required":["pattern"]})";
    search.run = [root](std::string_view arguments) -> agent::ToolOutcome {
        agent::ToolOutcome failure;
        const std::optional<Arguments> args =
            parse_arguments(arguments, R"({"pattern": "*.md"})", failure);
        if (!args.has_value()) {
            return failure;
        }
        const std::string pattern = args->string("pattern");
        if (pattern.empty()) {
            return error("pattern is required");
        }
        std::string problem;
        const std::string start = args->string("root");
        const std::optional<std::filesystem::path> base =
            resolve_in_root(root, start.empty() ? "." : start, problem);
        if (!base.has_value()) {
            return error(problem);
        }
        std::error_code code;
        if (!std::filesystem::is_directory(*base, code)) {
            return error(base->string() + " is not a directory");
        }
        std::vector<std::string> matches;
        std::filesystem::recursive_directory_iterator it{
            *base, std::filesystem::directory_options::skip_permission_denied, code};
        for (; it != std::filesystem::recursive_directory_iterator{}; it.increment(code)) {
            if (code) {
                break;
            }
            const std::string name = it->path().filename().string();
            if (it->is_directory(code)) {
                if (name.starts_with(".")) {
                    it.disable_recursion_pending();
                }
                continue;
            }
            if (wildcard_match(pattern, name)) {
                // Generic form: the listing is a model-facing contract, and a
                // path with '/' is valid on every platform the model may name
                // it back to.
                matches.push_back(
                    std::filesystem::relative(it->path(), *base, code).generic_string());
            }
        }
        if (matches.empty()) {
            return ok("No files matching '" + pattern + "' found under " + base->string());
        }
        std::sort(matches.begin(), matches.end());
        std::string out;
        for (const std::string& match : matches) {
            out += match + "\n";
        }
        out.pop_back();
        return ok(std::move(out));
    };
    registry.add(search);

    agent::Tool grep;
    grep.name = "grep_files";
    grep.description =
        "Search the contents of files for a regular expression (ECMAScript syntax), "
        "recursively, and return each matching line as path:line: text, the path relative to " +
        root_text + ". Returns at most " + std::to_string(kGrepMaxMatches) + " matches and " +
        std::to_string(kGrepMaxBytes / 1024) +
        " KB, saying how many more there were. Hidden directories (.git and the like) and binary "
        "files are skipped." +
        restricted;
    grep.parameters_schema =
        R"({"type":"object","properties":{"pattern":{"type":"string","description":"A regular expression, such as TODO or ^int main"},"path":{"type":"string","description":"A directory to search, or one file; defaults to the root"},"glob":{"type":"string","description":"Search only files whose name matches this pattern, such as *.cpp"},"ignore_case":{"type":"boolean","description":"Match regardless of case"}},"required":["pattern"]})";
    grep.run = [root](std::string_view arguments) -> agent::ToolOutcome {
        agent::ToolOutcome failure;
        const std::optional<Arguments> args =
            parse_arguments(arguments, R"({"pattern": "TODO", "glob": "*.cpp"})", failure);
        if (!args.has_value()) {
            return failure;
        }
        const auto pattern_it = args->object.find("pattern");
        if (pattern_it == args->object.end() || !pattern_it->is_string() ||
            pattern_it->get<std::string>().empty()) {
            return error("pattern is required: a regular expression");
        }
        const std::string pattern = pattern_it->get<std::string>();
        const auto flag = args->object.find("ignore_case");
        const bool ignore_case =
            flag != args->object.end() &&
            (flag->is_boolean() ? flag->get<bool>()
                                : flag->is_string() && flag->get<std::string>() == "true");
        std::regex expression;
        try {
            expression =
                std::regex{pattern, ignore_case ? std::regex::ECMAScript | std::regex::icase
                                                : std::regex::ECMAScript};
        } catch (const std::regex_error& e) {
            return error("'" + pattern +
                         "' is not a regular expression this search reads: " + e.what());
        }
        const std::string glob = args->string("glob");

        std::string problem;
        std::error_code code;
        const std::filesystem::path canonical_root = std::filesystem::weakly_canonical(root, code);
        const std::string start = args->string("path");
        const std::optional<std::filesystem::path> base =
            resolve_in_root(root, start.empty() ? "." : start, problem);
        if (!base.has_value()) {
            return error(problem);
        }

        // Every file to read, found first and sorted, so the same search
        // answers the same way whatever order the directory lists in.
        std::vector<std::filesystem::path> files;
        if (std::filesystem::is_regular_file(*base, code)) {
            files.push_back(*base);
        } else if (std::filesystem::is_directory(*base, code)) {
            std::filesystem::recursive_directory_iterator it{
                *base, std::filesystem::directory_options::skip_permission_denied, code};
            for (; it != std::filesystem::recursive_directory_iterator{}; it.increment(code)) {
                if (code) {
                    break;
                }
                const std::string name = it->path().filename().string();
                if (it->is_directory(code)) {
                    if (name.starts_with(".")) {
                        it.disable_recursion_pending();
                    }
                    continue;
                }
                if (!it->is_regular_file(code) || (!glob.empty() && !wildcard_match(glob, name))) {
                    continue;
                }
                if (it->is_symlink(code)) {
                    // A link is read where it points, and it may point out
                    // of the root: judged there, like any other path.
                    const std::filesystem::path target =
                        std::filesystem::weakly_canonical(it->path(), code);
                    if (code || !is_within(canonical_root, target)) {
                        continue;
                    }
                }
                files.push_back(it->path());
            }
        } else {
            return error("No such file or directory: " + base->string());
        }
        std::sort(files.begin(), files.end());

        std::string out;
        std::size_t matched = 0;
        std::size_t shown = 0;
        std::size_t binary = 0;
        // Once a row does not fit, none after it is shown: what a model
        // reads is the first matches in order, never a sample with holes.
        bool full = false;
        for (const std::filesystem::path& file : files) {
            if (looks_binary(file)) {
                ++binary;
                continue;
            }
            std::ifstream in{file, std::ios::binary};
            const std::string shown_path =
                std::filesystem::relative(file, canonical_root, code).generic_string();
            std::string line;
            std::int64_t number = 0;
            while (std::getline(in, line)) {
                ++number;
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                // A minified file's one enormous line is searched in its
                // first part: std::regex recurses per character, and a line
                // of megabytes is a stack overflow rather than a match.
                const std::string_view searched =
                    std::string_view{line}.substr(0, kGrepLineSearchLimit);
                if (!std::regex_search(searched.begin(), searched.end(), expression)) {
                    continue;
                }
                ++matched;
                std::string text =
                    line.size() > kGrepLineShown ? line.substr(0, kGrepLineShown) + " ..." : line;
                std::string row = shown_path + ":" + std::to_string(number) + ": " + text + "\n";
                if (!full && shown < kGrepMaxMatches && out.size() + row.size() <= kGrepMaxBytes) {
                    out += row;
                    ++shown;
                } else {
                    full = true;
                }
            }
        }
        const std::string skipped = binary == 0 ? std::string{}
                                                : std::to_string(binary) + " binary file" +
                                                      (binary == 1 ? "" : "s") + " skipped";
        if (matched == 0) {
            return ok("No matches for '" + pattern + "' under " + base->string() +
                      (skipped.empty() ? "" : " (" + skipped + ")"));
        }
        if (shown < matched) {
            out += "[... " + std::to_string(matched - shown) + " more of " +
                   std::to_string(matched) +
                   " matches not shown: narrow the pattern, or search a smaller folder or a glob" +
                   (skipped.empty() ? "" : "; " + skipped) + "]";
        } else if (!skipped.empty()) {
            out += "[" + skipped + "]";
        } else {
            out.pop_back();
        }
        return ok(std::move(out));
    };
    registry.add(grep);
}

}  // namespace apogee::tools
