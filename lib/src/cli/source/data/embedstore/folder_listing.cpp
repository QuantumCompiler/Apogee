#include "embedstore/folder_listing.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <optional>
#include <set>
#include <system_error>
#include <utility>

#include "platform/child_process.h"
#include "platform/platform.h"

namespace apogee::embedstore {
namespace {

/// Folder names whose contents are vendored or built, never the tree's own
/// code. A conservative list: each is a near-universal convention.
constexpr std::array<std::string_view, 12> kVendoredDirectories{
    "third_party", "third-party",   "thirdparty", "vendor",     "node_modules", "bower_components",
    "__pycache__", "site-packages", "_deps",      "CMakeFiles", "venv",         "target"};

/// `path` in the generic form, as UTF-8 on every platform.
[[nodiscard]] std::string utf8_of(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return std::string{text.begin(), text.end()};
}

[[nodiscard]] bool hidden_name(std::string_view name) {
    return name.size() > 1 && name.front() == '.' && name != "..";
}

/// One run of `git -C <folder> <arguments>`, captured: stdin closed, stderr a
/// tail written nowhere, bounded. Nullopt when git is not there or cannot
/// start here.
[[nodiscard]] std::optional<platform::CapturedRun> run_git(const std::filesystem::path& folder,
                                                           std::vector<std::string> arguments) {
    if (!platform::supports_child_processes() || platform::find_on_path("git").empty()) {
        return std::nullopt;
    }
    platform::ChildCommand command;
    command.program = "git";
    command.arguments = {"-C", folder.string()};
    command.arguments.insert(command.arguments.end(), std::make_move_iterator(arguments.begin()),
                             std::make_move_iterator(arguments.end()));
    return platform::run_captured(command, {});
}

/// What git printed when it succeeded, else nullopt.
[[nodiscard]] std::optional<std::string> git_output(const std::filesystem::path& folder,
                                                    std::vector<std::string> arguments) {
    std::optional<platform::CapturedRun> run = run_git(folder, std::move(arguments));
    if (!run.has_value() || run->status != 0) {
        return std::nullopt;
    }
    return std::move(run->out);
}

/// The NUL-separated entries of `listing`.
[[nodiscard]] std::vector<std::string> nul_separated(std::string_view listing) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start < listing.size()) {
        const std::size_t end = std::min(listing.find('\0', start), listing.size());
        if (end > start) {
            out.emplace_back(listing.substr(start, end - start));
        }
        start = end + 1;
    }
    return out;
}

[[nodiscard]] bool inside_work_tree(const std::filesystem::path& folder) {
    const std::optional<std::string> inside =
        git_output(folder, {"rev-parse", "--is-inside-work-tree"});
    return inside.has_value() && inside->starts_with("true");
}

/// Whether git ignores `folder` itself -- and so everything under it.
[[nodiscard]] bool ignored_whole(const std::filesystem::path& folder) {
    const std::optional<platform::CapturedRun> run = run_git(folder, {"check-ignore", "-q", "."});
    return run.has_value() && run->status == 0;
}

/// The part of `relative` a listing leaves out for: through its first hidden
/// component, or (under `vendored`) through its first vendored folder -- a
/// folder ending in '/' -- or empty when it is taken.
[[nodiscard]] std::string left_out_at(std::string_view relative, bool vendored) {
    std::size_t start = 0;
    while (start < relative.size()) {
        const std::size_t slash = relative.find('/', start);
        const bool last = slash == std::string_view::npos;
        const std::size_t end = last ? relative.size() : slash;
        const std::string_view name = relative.substr(start, end - start);
        if (hidden_name(name) || (vendored && !last && vendored_name(name))) {
            return std::string{relative.substr(0, last ? end : end + 1)};
        }
        if (last) {
            break;
        }
        start = slash + 1;
    }
    return {};
}

/// Git's list of `folder`: tracked, and untracked but not ignored, each
/// relative to it -- or nullopt when git could not answer.
[[nodiscard]] std::optional<std::vector<std::string>> git_listing(
    const std::filesystem::path& folder) {
    const std::optional<std::string> listed =
        git_output(folder, {"ls-files", "-z", "--cached", "--others", "--exclude-standard"});
    if (!listed.has_value()) {
        return std::nullopt;
    }
    return nul_separated(*listed);
}

void walk(const std::filesystem::path& folder, const ListingRules& rules, FolderListing& out,
          std::set<std::string>& left) {
    std::error_code code;
    std::filesystem::recursive_directory_iterator entries{
        folder, std::filesystem::directory_options::skip_permission_denied, code};
    for (; !code && entries != std::filesystem::recursive_directory_iterator{};
         entries.increment(code)) {
        const std::filesystem::path relative = entries->path().lexically_relative(folder);
        std::error_code entry_code;
        const bool directory = entries->is_directory(entry_code);
        const std::string name = utf8_of(entries->path().filename());
        if (hidden_name(name) || (rules.leave_out_vendored && directory && vendored_name(name))) {
            if (rules.name_left_out) {
                left.insert(utf8_of(relative) + (directory ? "/" : ""));
            }
            if (directory) {
                entries.disable_recursion_pending();
            }
            continue;
        }
        if (entries->is_regular_file(entry_code)) {
            out.files.push_back(relative);
        }
    }
}

}  // namespace

bool hidden_path(const std::filesystem::path& relative) {
    return std::ranges::any_of(
        relative, [](const std::filesystem::path& part) { return hidden_name(utf8_of(part)); });
}

bool vendored_name(std::string_view name) {
    return std::ranges::find(kVendoredDirectories, name) != kVendoredDirectories.end() ||
           name.starts_with("cmake-build-");
}

std::string vendored_directory(std::string_view path) {
    std::size_t start = 0;
    while (start < path.size()) {
        const std::size_t slash = path.find('/', start);
        if (slash == std::string_view::npos) {
            break;  // the last component is the file itself
        }
        if (vendored_name(path.substr(start, slash - start))) {
            return std::string{path.substr(0, slash)};
        }
        start = slash + 1;
    }
    return {};
}

FolderListing list_folder(const std::filesystem::path& folder, const ListingRules& rules) {
    FolderListing out;
    std::set<std::string> left;

    // Not `value_or` on the listing: that would walk the whole folder -- a
    // repository's build output and all -- even when git has answered.
    std::optional<std::vector<std::string>> listed;
    if (inside_work_tree(folder) && !(rules.walk_ignored_folder && ignored_whole(folder))) {
        listed = git_listing(folder);
    }

    if (listed.has_value()) {
        for (const std::string& relative : *listed) {
            if (std::string at = left_out_at(relative, rules.leave_out_vendored); !at.empty()) {
                if (rules.name_left_out) {
                    left.insert(std::move(at));
                }
                continue;
            }
            out.files.push_back(platform::path_from_utf8(relative));
        }
        if (rules.name_left_out) {
            // What git ignores, a wholly ignored folder named once. A failed
            // ask names nothing more; the files taken are git's either way.
            if (const std::optional<std::string> ignored =
                    git_output(folder, {"ls-files", "-z", "--others", "--ignored",
                                        "--exclude-standard", "--directory"});
                ignored.has_value()) {
                for (const std::string& entry : nul_separated(*ignored)) {
                    std::string at = left_out_at(entry, rules.leave_out_vendored);
                    left.insert(at.empty() ? entry : std::move(at));
                }
            }
            // Git never lists its own folder, and it is the one most worth
            // naming: a remote's token lives in .git/config.
            std::error_code code;
            const std::filesystem::path own = folder / ".git";
            if (std::filesystem::exists(own, code)) {
                left.insert(std::filesystem::is_directory(own, code) ? ".git/" : ".git");
            }
        }
    } else {
        walk(folder, rules, out, left);
    }

    std::ranges::sort(out.files);
    const auto [first, last] = std::ranges::unique(out.files);
    out.files.erase(first, last);
    out.left_out.assign(left.begin(), left.end());
    return out;
}

std::vector<std::filesystem::path> files_under(const std::filesystem::path& folder) {
    return list_folder(folder).files;
}

}  // namespace apogee::embedstore
