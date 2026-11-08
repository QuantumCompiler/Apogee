#include "operations/graph_sources.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>
#include <utility>

#include "agentloop/attachments.h"
#include "graph/code_languages.h"
#include "platform/file_lock.h"

namespace apogee::commands {

std::string absolute_source_path(std::string_view path) {
    std::error_code code;
    const std::filesystem::path given{std::string{path}};
    std::filesystem::path absolute = std::filesystem::absolute(given, code);
    if (code) {
        absolute = given;
    }
    std::string out = absolute.lexically_normal().generic_string();
    while (out.size() > 1 && out.back() == '/') {
        out.pop_back();
    }
    return out;
}

std::string source_member_label(std::string_view source) {
    std::filesystem::path path{std::string{source}};
    path = path.lexically_normal();
    // `/a/b/` names `b`, as `/a/b` does.
    if (!path.has_filename() && path.has_parent_path()) {
        path = path.parent_path();
    }
    std::string label = path.filename().generic_string();
    return label.empty() || label == "." ? path.generic_string() : label;
}

std::vector<std::string> source_files_under(const std::vector<agentloop::FoundFile>& found,
                                            const std::filesystem::path& root) {
    // `/a/b/` is the folder `/a/b` is: a trailing separator's empty element
    // counts for nothing in `lexically_relative` (LWG 3070).
    const std::filesystem::path base = root.lexically_normal();
    std::vector<std::string> out;
    out.reserve(found.size());
    for (const agentloop::FoundFile& file : found) {
        const std::filesystem::path relative =
            file.path.lexically_normal().lexically_relative(base);
        if (relative.empty() || *relative.begin() == "..") {
            continue;
        }
        out.push_back(relative.generic_string());
    }
    return out;
}

graph::SourceMember source_member(std::string label, const std::filesystem::path& root,
                                  std::vector<std::string> files) {
    graph::SourceMember member;
    member.label = std::move(label);
    member.files = std::move(files);
    member.read = [root](std::string_view path, std::string& error) -> std::optional<std::string> {
        std::ifstream in{root / std::filesystem::path{std::string{path}}, std::ios::binary};
        if (!in) {
            error = "cannot open";
            return std::nullopt;
        }
        std::string content{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
        if (in.bad()) {
            error = "read failed";
            return std::nullopt;
        }
        return content;
    };
    return member;
}

bool offers_code(const std::vector<std::string>& files) {
    std::vector<std::string> kept;
    for (const std::string& file : files) {
        if (graph::vendored_directory(file).empty()) {
            kept.push_back(file);
        }
    }
    const bool headers_are_cpp = graph::tree_has_cpp(kept);
    return std::ranges::any_of(kept, [&](const std::string& file) {
        return graph::code_language_for_path(file, headers_are_cpp) != nullptr;
    });
}

GraphSources open_graph_sources(const harness::NamedGraphConfig& named) {
    GraphSources out;
    for (const std::string& source : named.sources) {
        const std::filesystem::path root = std::filesystem::path{source}.lexically_normal();
        std::error_code code;
        if (!std::filesystem::is_directory(root, code)) {
            out.missing.push_back(source);
            continue;
        }
        const agentloop::FoundFiles found = agentloop::find_attachment_files(".", root);
        graph::SourceMember member =
            source_member(source_member_label(source), root, source_files_under(found.files, root));
        if (member.files.empty()) {
            out.empty.push_back(source);
        }
        out.members.push_back(std::move(member));
    }
    return out;
}

GraphSourceBuild build_graph_sources(embedstore::Store& store,
                                     const harness::NamedGraphConfig& named,
                                     graph::SourceBuildOptions options) {
    GraphSources sources = open_graph_sources(named);
    options.languages = named.languages;
    // The graph's membership is its config's: a tree removed from
    // `sources:` -- or gone from the disk -- is forgotten.
    options.prune_other_members = true;
    GraphSourceBuild out;
    out.missing = std::move(sources.missing);
    out.empty = std::move(sources.empty);
    out.result = graph::build_source(store, sources.members, options);
    return out;
}

std::filesystem::path graph_build_lock_path(const std::filesystem::path& db_path) {
    return std::filesystem::path{db_path.string() + ".build.lock"};
}

std::optional<platform::FileLock> lock_graph_build(const std::filesystem::path& db_path,
                                                   std::string_view name, std::string& refusal,
                                                   std::chrono::milliseconds wait) {
    std::error_code code;
    std::filesystem::create_directories(db_path.parent_path(), code);
    platform::FileLock::Refusal why;
    std::optional<platform::FileLock> lock =
        platform::FileLock::acquire(graph_build_lock_path(db_path), wait, why);
    if (!lock.has_value()) {
        refusal = why.timed_out ? "another build or a delete of \"" + std::string{name} +
                                      "\" is running -- wait for it to finish, then build again"
                                : "graph \"" + std::string{name} +
                                      "\" cannot take its build lock: " + why.error;
    }
    return lock;
}

NamedGraphDeletion delete_named_graph(const std::filesystem::path& db_path, std::string_view name) {
    NamedGraphDeletion out;
    std::error_code code;
    if (!std::filesystem::exists(db_path, code)) {
        return out;  // Absent: nothing to guard
    }
    platform::FileLock::Refusal why;
    std::optional<platform::FileLock> lock = platform::FileLock::acquire(
        graph_build_lock_path(db_path), std::chrono::milliseconds{0}, why);
    if (!lock.has_value()) {
        if (why.timed_out) {
            out.outcome = NamedGraphDeletion::Outcome::Building;
            out.message = "a build of \"" + std::string{name} +
                          "\" is running -- stop it, or wait for it, then delete";
        } else {
            out.outcome = NamedGraphDeletion::Outcome::Failed;
            out.message = why.error;
        }
        return out;
    }
    // Under the lock: another delete may have gone first.
    if (!std::filesystem::exists(db_path, code)) {
        return out;
    }
    {
        const embedstore::Store store{db_path};
        out.stats = store.graph_stats();
    }
    if (!std::filesystem::remove(db_path, code) || code) {
        out.outcome = NamedGraphDeletion::Outcome::Failed;
        out.message =
            "could not delete the graph database " + db_path.string() + ": " + code.message();
        return out;
    }
    // Absent unless SQLite left one behind.
    for (const char* sidecar : {"-wal", "-shm"}) {
        std::filesystem::remove(db_path.string() + sidecar, code);
    }
    out.outcome = NamedGraphDeletion::Outcome::Deleted;
    return out;
}

}  // namespace apogee::commands
