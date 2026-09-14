#include "tools/notes.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

#include "tools/args.h"

namespace apogee::tools {
namespace {

constexpr std::string_view kKeyRule =
    "Keys must be 1-128 characters: letters, digits, hyphens, underscores only.";

std::optional<std::filesystem::path> note_path(const std::filesystem::path& dir,
                                               const std::string& key, std::string& problem) {
    if (!valid_note_key(key)) {
        problem = "Invalid note key '" + key + "'. " + std::string{kKeyRule};
        return std::nullopt;
    }
    return dir / (key + ".txt");
}

}  // namespace

bool valid_note_key(std::string_view key) noexcept {
    if (key.empty() || key.size() > 128) {
        return false;
    }
    for (const char c : key) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

void register_notes_tools(agent::ToolRegistry& registry, const std::filesystem::path& notes_dir) {
    const std::string key_schema =
        R"({"type":"object","properties":{"key":{"type":"string","description":"The note's name: letters, digits, hyphens, underscores"}},"required":["key"]})";
    const auto describe_key = [](std::string_view arguments) {
        agent::ToolOutcome unused;
        const std::optional<Arguments> args = parse_arguments(arguments, "", unused);
        return args.has_value() ? args->string("key") : std::string{};
    };

    agent::Tool write;
    write.name = "write_note";
    write.description =
        "Save a note under a key, replacing any note with that key. Notes persist across "
        "sessions in " +
        notes_dir.string() + ". This operation requires the user's permission.";
    write.parameters_schema =
        R"({"type":"object","properties":{"key":{"type":"string","description":"The note's name: letters, digits, hyphens, underscores"},"content":{"type":"string","description":"The note's text"}},"required":["key","content"]})";
    write.writes = true;
    write.describe_target = describe_key;
    write.run = [notes_dir](std::string_view arguments) -> agent::ToolOutcome {
        agent::ToolOutcome failure;
        const std::optional<Arguments> args =
            parse_arguments(arguments, R"({"key": "todo", "content": "..."})", failure);
        if (!args.has_value()) {
            return failure;
        }
        std::string problem;
        const std::optional<std::filesystem::path> path =
            note_path(notes_dir, args->string("key"), problem);
        if (!path.has_value()) {
            return error(problem);
        }
        if (!args->has("content") || !args->object["content"].is_string()) {
            return error("content is required, as a string");
        }
        const std::string content = args->object["content"].get<std::string>();
        std::error_code code;
        std::filesystem::create_directories(notes_dir, code);
        std::ofstream out{*path, std::ios::binary | std::ios::trunc};
        if (!out) {
            return error("could not write " + path->string());
        }
        out << content;
        return ok("Note '" + args->string("key") + "' saved (" + std::to_string(content.size()) +
                  " bytes)");
    };
    registry.add(write);

    agent::Tool read;
    read.name = "read_note";
    read.description = "Read a saved note by key.";
    read.parameters_schema = key_schema;
    read.run = [notes_dir](std::string_view arguments) -> agent::ToolOutcome {
        agent::ToolOutcome failure;
        const std::optional<Arguments> args =
            parse_arguments(arguments, R"({"key": "todo"})", failure);
        if (!args.has_value()) {
            return failure;
        }
        std::string problem;
        const std::optional<std::filesystem::path> path =
            note_path(notes_dir, args->string("key"), problem);
        if (!path.has_value()) {
            return error(problem);
        }
        std::ifstream in{*path, std::ios::binary};
        if (!in) {
            return error("No note named '" + args->string("key") +
                         "'. Use list_notes to see available notes.");
        }
        std::ostringstream text;
        text << in.rdbuf();
        return ok(text.str());
    };
    registry.add(read);

    agent::Tool list;
    list.name = "list_notes";
    list.description = "List every saved note with its size.";
    list.parameters_schema = R"({"type":"object","properties":{}})";
    list.run = [notes_dir](std::string_view) -> agent::ToolOutcome {
        std::error_code code;
        std::vector<std::string> lines;
        if (std::filesystem::is_directory(notes_dir, code)) {
            for (const auto& entry : std::filesystem::directory_iterator{notes_dir, code}) {
                const std::filesystem::path& path = entry.path();
                if (path.extension() != ".txt" || !valid_note_key(path.stem().string())) {
                    continue;
                }
                lines.push_back("  " + path.stem().string() + "  (" +
                                std::to_string(entry.file_size(code)) + " B)");
            }
        }
        if (lines.empty()) {
            return ok("No notes yet. Use write_note to create one. Notes are stored in " +
                      notes_dir.string());
        }
        std::sort(lines.begin(), lines.end());
        std::string out;
        for (const std::string& line : lines) {
            out += line + "\n";
        }
        out.pop_back();
        return ok(std::move(out));
    };
    registry.add(list);

    agent::Tool remove;
    remove.name = "delete_note";
    remove.description =
        "Delete a saved note by key. This operation requires the user's permission.";
    remove.parameters_schema = key_schema;
    remove.writes = true;
    remove.describe_target = describe_key;
    remove.run = [notes_dir](std::string_view arguments) -> agent::ToolOutcome {
        agent::ToolOutcome failure;
        const std::optional<Arguments> args =
            parse_arguments(arguments, R"({"key": "todo"})", failure);
        if (!args.has_value()) {
            return failure;
        }
        std::string problem;
        const std::optional<std::filesystem::path> path =
            note_path(notes_dir, args->string("key"), problem);
        if (!path.has_value()) {
            return error(problem);
        }
        std::error_code code;
        if (!std::filesystem::remove(*path, code)) {
            return error("No note named '" + args->string("key") + "'");
        }
        return ok("Note '" + args->string("key") + "' deleted");
    };
    registry.add(remove);
}

}  // namespace apogee::tools
