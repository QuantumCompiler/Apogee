#include "graph/code_build.h"

#include <algorithm>
#include <array>
#include <memory>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

#include "contracts/sha256.h"
#include "graph/code_extract.h"
#include "graph/code_languages.h"
#include "graph/code_parser.h"

namespace apogee::graph {
namespace {

/// Directory names whose contents are vendored or built, never the tree's
/// own code. A conservative list: each is a near-universal convention.
constexpr std::array<std::string_view, 12> kVendoredDirectories{
    "third_party", "third-party",   "thirdparty", "vendor",     "node_modules", "bower_components",
    "__pycache__", "site-packages", "_deps",      "CMakeFiles", "venv",         "target"};

[[nodiscard]] std::string extension_label(std::string_view path) {
    const std::size_t slash = path.find_last_of('/');
    const std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
    const std::size_t dot = name.find_last_of('.');
    if (dot == std::string_view::npos || dot == 0) {
        return "no extension";
    }
    return std::string{name.substr(dot)};
}

/// A file the parser should not be fed: NUL bytes in its first 8 KiB.
[[nodiscard]] bool looks_binary(std::string_view content) {
    return content.substr(0, 8192).find('\0') != std::string_view::npos;
}

/// One member's files, sorted and unique, the vendored ones counted out.
struct Selection {
    struct File {
        std::string path;
        const CodeLanguage* language = nullptr;
    };

    std::vector<File> files;
};

[[nodiscard]] Selection select_files(const SourceMember& member,
                                     const std::vector<std::string>& languages,
                                     SourceBuildResult& out) {
    std::vector<std::string> paths = member.files;
    std::ranges::sort(paths);
    paths.erase(std::ranges::unique(paths).begin(), paths.end());
    out.files_offered += static_cast<int>(paths.size());
    std::vector<std::string> kept;
    for (const std::string& path : paths) {
        if (const std::string vendored = vendored_directory(path); !vendored.empty()) {
            ++out.excluded[vendored + "/"];
            continue;
        }
        kept.push_back(path);
    }
    const bool headers_are_cpp = tree_has_cpp(kept);
    Selection selection;
    for (const std::string& path : kept) {
        const CodeLanguage* language = code_language_for_path(path, headers_are_cpp);
        if (language == nullptr) {
            out.skipped.push_back(
                SkippedFile{.member = member.label,
                            .file = path,
                            .reason = "unsupported language (" + extension_label(path) + ")"});
            continue;
        }
        // A language named by alias (`py`, `c++`) is the language: the
        // comparison is by the roster's own entry.
        if (!languages.empty() && std::ranges::none_of(languages, [&](const std::string& wanted) {
                const CodeLanguage* named = code_language_by_name(wanted);
                return named != nullptr && named->id == language->id;
            })) {
            out.skipped.push_back(
                SkippedFile{.member = member.label,
                            .file = path,
                            .reason = std::string{language->name} + " left out by --lang"});
            continue;
        }
        selection.files.push_back(Selection::File{.path = path, .language = language});
    }
    return selection;
}

/// One parser per language, made on first use: its query compiles once.
class Parsers {
public:
    [[nodiscard]] CodeParser& of(const CodeLanguage& language) {
        auto& slot = parsers_[static_cast<std::size_t>(language.id)];
        if (!slot) {
            slot = std::make_unique<CodeParser>(language.id);
        }
        return *slot;
    }

private:
    std::array<std::unique_ptr<CodeParser>, 12> parsers_;
};

[[nodiscard]] std::string hash_of(std::string_view content) {
    models::Sha256 sha;
    sha.update(content);
    return sha.hex_digest();
}

/// Reads, checks and parses one file into its facts -- or names why not.
[[nodiscard]] std::optional<FileFacts> parse_file(Parsers& parsers, const CodeLanguage& language,
                                                  const std::string& path, std::string_view content,
                                                  std::string& reason, bool& partial) {
    const CodeParseResult parse = parsers.of(language).parse(content);
    if (!parse.parsed) {
        reason = "did not parse (" + parse.error + ")";
        return std::nullopt;
    }
    partial = parse.has_errors;
    return extract_code_facts(language, path, content, parse);
}

[[nodiscard]] std::vector<embedstore::CodeNodeRow> node_rows(const CodeGraph& graph) {
    std::vector<embedstore::CodeNodeRow> rows;
    rows.reserve(graph.nodes.size());
    for (const CodeNode& node : graph.nodes) {
        embedstore::CodeNodeRow row;
        row.type = node.type;
        row.name = node.name;
        row.description = node.description;
        row.metadata = node.metadata;
        for (const CodeLocation& at : node.mentions) {
            row.mentions.push_back(embedstore::CodeMentionRow{.collection = at.member,
                                                              .file = at.file,
                                                              .line = at.line,
                                                              .end_line = at.end_line,
                                                              .role = at.role});
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

[[nodiscard]] std::vector<embedstore::CodeEdgeRow> edge_rows(const CodeGraph& graph) {
    std::vector<embedstore::CodeEdgeRow> rows;
    rows.reserve(graph.edges.size());
    for (const CodeEdge& edge : graph.edges) {
        embedstore::CodeEdgeRow row;
        row.source_type = edge.source_type;
        row.source_name = edge.source_name;
        row.target_type = edge.target_type;
        row.target_name = edge.target_name;
        row.relation = edge.relation;
        for (const CodeLocation& at : edge.sites) {
            row.sites.push_back(
                embedstore::CodeSiteRow{.collection = at.member, .file = at.file, .line = at.line});
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

void check_members(const std::vector<SourceMember>& members) {
    std::set<std::string> labels;
    for (const SourceMember& member : members) {
        if (member.label.empty()) {
            throw std::invalid_argument("a source member needs a label");
        }
        if (!labels.insert(member.label).second) {
            throw std::invalid_argument("two source members are labelled '" + member.label + "'");
        }
        if (!member.read) {
            throw std::invalid_argument("source member '" + member.label + "' has no reader");
        }
    }
}

}  // namespace

std::string vendored_directory(std::string_view path) {
    std::size_t start = 0;
    while (start < path.size()) {
        const std::size_t slash = path.find('/', start);
        if (slash == std::string_view::npos) {
            break;  // the last component is the file itself
        }
        const std::string_view part = path.substr(start, slash - start);
        if (std::ranges::find(kVendoredDirectories, part) != kVendoredDirectories.end() ||
            part.starts_with("cmake-build-")) {
            return std::string{path.substr(0, slash)};
        }
        start = slash + 1;
    }
    return {};
}

SourceBuildResult build_source(embedstore::Store& store, const std::vector<SourceMember>& members,
                               const SourceBuildOptions& options) {
    check_members(members);
    SourceBuildResult out;
    out.dry_run = options.dry_run;
    Parsers parsers;
    std::vector<SourceFacts> all;
    std::set<std::string> labels;

    for (const SourceMember& member : members) {
        labels.insert(member.label);
        const Selection selection = select_files(member, options.languages, out);
        const std::map<std::string, embedstore::CodeFileState> states =
            store.code_file_states(member.label);
        std::set<std::string> used;
        int index = 0;
        for (const Selection::File& file : selection.files) {
            ++index;
            if (options.cancellation.stop_requested()) {
                out.cancelled = true;
                return out;
            }
            std::string error;
            const std::optional<std::string> content = member.read(file.path, error);
            if (!content.has_value()) {
                out.skipped.push_back(SkippedFile{
                    .member = member.label, .file = file.path, .reason = "unreadable: " + error});
                continue;
            }
            if (content->size() > kMaxSourceFileBytes) {
                out.skipped.push_back(SkippedFile{
                    .member = member.label, .file = file.path, .reason = "too large to parse"});
                continue;
            }
            if (looks_binary(*content)) {
                out.skipped.push_back(
                    SkippedFile{.member = member.label, .file = file.path, .reason = "binary"});
                continue;
            }
            const std::string hash = hash_of(*content);
            const std::string extractor = code_extractor_id(*file.language);
            std::optional<FileFacts> facts;
            const auto state = states.find(file.path);
            if (!options.force && state != states.end() && state->second.content_hash == hash &&
                state->second.extractor == extractor) {
                try {
                    facts = facts_from_json(state->second.facts);
                } catch (const std::exception&) {
                    facts.reset();  // a damaged cache is a re-parse, never a failure
                }
            }
            const bool parsing = !facts.has_value();
            if (options.on_progress) {
                options.on_progress(
                    SourceProgress{.member = member.label,
                                   .file = file.path,
                                   .index = index,
                                   .count = static_cast<int>(selection.files.size()),
                                   .parsing = parsing});
            }
            if (parsing) {
                std::string reason;
                bool partial = false;
                facts = parse_file(parsers, *file.language, file.path, *content, reason, partial);
                if (!facts.has_value()) {
                    out.skipped.push_back(
                        SkippedFile{.member = member.label, .file = file.path, .reason = reason});
                    continue;
                }
                ++out.files_parsed;
                out.parsed.push_back(member.label + "/" + file.path);
                if (!options.dry_run) {
                    store.set_code_file_state(
                        embedstore::CodeFileState{.collection = member.label,
                                                  .file = file.path,
                                                  .content_hash = hash,
                                                  .extractor = extractor,
                                                  .facts = facts_to_json(*facts),
                                                  .parsed_at = {}});
                }
            } else {
                ++out.files_unchanged;
            }
            if (facts->has_errors) {
                out.partial.push_back(member.label + "/" + file.path);
            }
            used.insert(file.path);
            ++out.files_used;
            ++out.files_by_language[std::string{file.language->name}];
            all.push_back(SourceFacts{.member = member.label, .facts = std::move(*facts)});
        }
        // What the tree no longer offers (or now skips) is forgotten.
        std::vector<std::string> gone;
        for (const auto& [path, unused] : states) {
            if (!used.contains(path)) {
                gone.push_back(path);
            }
        }
        out.files_removed += static_cast<int>(gone.size());
        if (!options.dry_run && !gone.empty()) {
            (void)store.remove_code_files(member.label, gone);
        }
    }

    // The graph's other source members: forgotten when the build owns the
    // whole membership, else linked from their cache as they stand.
    for (const std::string& label : store.code_members()) {
        if (labels.contains(label)) {
            continue;
        }
        if (options.prune_other_members) {
            out.files_removed += static_cast<int>(store.code_file_states(label).size());
            if (!options.dry_run) {
                (void)store.remove_code_member(label);
            }
            continue;
        }
        for (const auto& [path, state] : store.code_file_states(label)) {
            try {
                all.push_back(SourceFacts{.member = label, .facts = facts_from_json(state.facts)});
            } catch (const std::exception&) {
                // A damaged cache of a member this build was not handed: its
                // own next build re-parses it; it contributes nothing here.
                continue;
            }
        }
    }

    const CodeGraph graph = resolve_code(std::move(all));
    out.counts = graph.counts;
    out.nodes = static_cast<int>(graph.nodes.size());
    out.edges = static_cast<int>(graph.edges.size());
    if (!options.dry_run) {
        out.sync = store.sync_code_graph(node_rows(graph), edge_rows(graph));
    }
    std::ranges::sort(out.skipped, [](const SkippedFile& a, const SkippedFile& b) {
        return std::tie(a.member, a.file) < std::tie(b.member, b.file);
    });
    return out;
}

CodeGraph parse_and_resolve(const std::vector<SourceMember>& members,
                            const std::vector<std::string>& languages) {
    check_members(members);
    Parsers parsers;
    SourceBuildResult ignored;
    std::vector<SourceFacts> all;
    for (const SourceMember& member : members) {
        for (const Selection::File& file : select_files(member, languages, ignored).files) {
            std::string error;
            const std::optional<std::string> content = member.read(file.path, error);
            if (!content.has_value() || content->size() > kMaxSourceFileBytes ||
                looks_binary(*content)) {
                continue;
            }
            std::string reason;
            bool partial = false;
            std::optional<FileFacts> facts =
                parse_file(parsers, *file.language, file.path, *content, reason, partial);
            if (facts.has_value()) {
                all.push_back(SourceFacts{.member = member.label, .facts = std::move(*facts)});
            }
        }
    }
    return resolve_code(std::move(all));
}

}  // namespace apogee::graph
