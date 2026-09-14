#include "tools/toolsets.h"

#include <array>
#include <system_error>

#include "harness/layout.h"
#include "platform/platform.h"
#include "tools/fs.h"
#include "tools/notes.h"
#include "tools/rag_query.h"
#include "tools/shell.h"

namespace apogee::tools {
namespace {

constexpr std::array<std::string_view, 5> kToolsetNames{"fs", "shell", "git", "notes", "rag"};
constexpr std::array<std::string_view, 5> kDestructiveTools{
    "write_file", "delete_file", "run_command", "write_note", "delete_note"};

bool disabled(const ToolsetOptions& options, std::string_view toolset) {
    for (const std::string& name : options.disabled) {
        if (name == toolset) {
            return true;
        }
    }
    return false;
}

std::filesystem::path effective_root(const ToolsetOptions& options) {
    if (!options.fs_root.empty()) {
        return options.fs_root;
    }
    if (const std::optional<std::string> home = platform::home_directory(); home.has_value()) {
        return std::filesystem::path{*home};
    }
    std::error_code code;
    return std::filesystem::current_path(code);
}

std::filesystem::path effective_cwd(const ToolsetOptions& options) {
    if (!options.working_directory.empty()) {
        return options.working_directory;
    }
    std::error_code code;
    return std::filesystem::current_path(code);
}

}  // namespace

std::span<const std::string_view> toolset_names() noexcept {
    return kToolsetNames;
}

std::span<const std::string_view> destructive_tool_names() noexcept {
    return kDestructiveTools;
}

void register_native_toolsets(agent::ToolRegistry& registry, const ToolsetOptions& options) {
    if (!disabled(options, "fs")) {
        register_fs_tools(registry, effective_root(options), options.read_limit);
    }
    if (!disabled(options, "shell")) {
        register_shell_tool(registry, effective_cwd(options), options.shell_timeout);
    }
    if (!disabled(options, "git")) {
        GitOptions git;
        git.timeout = options.git_timeout;
        git.working_directory = effective_cwd(options);
        git.review = options.review;
        register_git_tools(registry, git);
    }
    if (!disabled(options, "notes")) {
        register_notes_tools(registry,
                             options.notes_dir.empty() ? harness::notes_dir() : options.notes_dir);
    }
    if (!disabled(options, "rag")) {
        register_rag_tools(registry, options.harness, options.config);
    }
}

std::vector<std::string> native_tool_names(const ToolsetOptions& options) {
    agent::ToolRegistry registry;
    register_native_toolsets(registry, options);
    return registry.names();
}

}  // namespace apogee::tools
