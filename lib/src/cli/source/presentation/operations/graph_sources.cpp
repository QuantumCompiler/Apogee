#include "operations/graph_sources.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>
#include <utility>

#include "agentloop/attachments.h"

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

GraphSources open_graph_sources(const harness::NamedGraphConfig& named) {
    GraphSources out;
    for (const std::string& source : named.sources) {
        const std::filesystem::path root = std::filesystem::path{source}.lexically_normal();
        std::error_code code;
        if (!std::filesystem::is_directory(root, code)) {
            out.missing.push_back(source);
            continue;
        }
        graph::SourceMember member;
        member.label = source_member_label(source);
        const agentloop::FoundFiles found = agentloop::find_attachment_files(".", root);
        for (const agentloop::FoundFile& file : found.files) {
            member.files.push_back(file.name);
        }
        if (member.files.empty()) {
            out.empty.push_back(source);
        }
        member.read = [root](std::string_view path,
                             std::string& error) -> std::optional<std::string> {
            std::ifstream in{root / std::filesystem::path{std::string{path}}, std::ios::binary};
            if (!in) {
                error = "cannot open";
                return std::nullopt;
            }
            std::string content{std::istreambuf_iterator<char>{in},
                                std::istreambuf_iterator<char>{}};
            if (in.bad()) {
                error = "read failed";
                return std::nullopt;
            }
            return content;
        };
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

}  // namespace apogee::commands
