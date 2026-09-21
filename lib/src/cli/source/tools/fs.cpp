#include "tools/fs.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

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
                canonical_root.string() + ". Set tools.fs_root in the config to expand access.";
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
    read.description = "Read a file and return its contents as text. Reads are capped at " +
                       std::to_string(read_limit / 1024) + " KB." + restricted;
    read.parameters_schema =
        R"({"type":"object","properties":{"path":{"type":"string","description":"Absolute path, ~ path, or a path relative to the root"}},"required":["path"]})";
    read.run = [root, read_limit](std::string_view arguments) -> agent::ToolOutcome {
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
        if (!std::filesystem::is_regular_file(*path, code)) {
            return error(std::filesystem::exists(*path, code)
                             ? path->string() + " is not a regular file"
                             : "No such file: " + path->string());
        }
        const std::uintmax_t size = std::filesystem::file_size(*path, code);
        std::string data = read_bytes(*path, read_limit);
        if (size > read_limit) {
            data += "\n\n... (truncated -- " + std::to_string(size - read_limit) +
                    " bytes omitted; read is capped at " + std::to_string(read_limit / 1024) +
                    " KB)";
        }
        return ok(std::move(data));
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
}

}  // namespace apogee::tools
