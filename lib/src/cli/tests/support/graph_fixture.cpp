#include "support/graph_fixture.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "embedstore/graph.h"
#include "embedstore/graph_code.h"

namespace apogee::testing {
namespace {

using embedstore::CodeEdgeRow;
using embedstore::CodeMentionRow;
using embedstore::CodeNodeRow;
using embedstore::CodeSiteRow;

constexpr std::string_view kCodeMember = "app";

struct Code {
    std::vector<CodeNodeRow> nodes;
    std::vector<CodeEdgeRow> edges;

    void defined(std::string type, std::string name, std::string file, std::int64_t line,
                 std::int64_t end_line, std::string description) {
        embedstore::CodeNodeMetadata meta;
        meta.code = true;
        meta.language = "python";
        meta.member = std::string{kCodeMember};
        meta.file = file;
        meta.line = line;
        meta.end_line = end_line;
        CodeNodeRow row;
        row.type = std::move(type);
        row.name = std::move(name);
        row.description = std::move(description);
        row.metadata = embedstore::code_node_metadata_json(meta);
        row.mentions.push_back(CodeMentionRow{.collection = std::string{kCodeMember},
                                              .file = std::move(file),
                                              .line = line,
                                              .end_line = end_line,
                                              .role = "definition"});
        nodes.push_back(std::move(row));
    }

    void unresolved(std::string name, const std::vector<std::pair<std::string, int>>& at) {
        embedstore::CodeNodeMetadata meta;
        meta.code = true;
        meta.unresolved = true;
        CodeNodeRow row;
        row.type = std::string{embedstore::kCodeKindName};
        row.name = std::move(name);
        row.metadata = embedstore::code_node_metadata_json(meta);
        for (const auto& [file, line] : at) {
            row.mentions.push_back(CodeMentionRow{.collection = std::string{kCodeMember},
                                                  .file = file,
                                                  .line = line,
                                                  .end_line = line,
                                                  .role = "reference"});
        }
        nodes.push_back(std::move(row));
    }

    void edge(std::string source_type, std::string source, std::string target_type,
              std::string target, std::string relation,
              const std::vector<std::pair<std::string, int>>& sites) {
        CodeEdgeRow row;
        row.source_type = std::move(source_type);
        row.source_name = std::move(source);
        row.target_type = std::move(target_type);
        row.target_name = std::move(target);
        row.relation = std::move(relation);
        for (const auto& [file, line] : sites) {
            row.sites.push_back(
                CodeSiteRow{.collection = std::string{kCodeMember}, .file = file, .line = line});
        }
        edges.push_back(std::move(row));
    }

    void function(const std::string& name, const std::string& file, int line, int end_line,
                  std::string description) {
        defined("function", name, file, line, end_line, std::move(description));
        edge("function", name, "file", file, "defined_in", {{file, line}});
    }
};

[[nodiscard]] std::string two_digits(int value) {
    return (value < 10 ? "0" : "") + std::to_string(value);
}

[[nodiscard]] std::int64_t id_of(const embedstore::Store& graph, const std::string& name,
                                 std::string_view type) {
    for (const embedstore::GraphNode& node : graph.find_nodes(name)) {
        if (node.type == type && node.name == name) {
            return node.id;
        }
    }
    throw std::runtime_error("the fixture has no " + std::string{type} + " " + name);
}

}  // namespace

void build_navigation_graph(embedstore::Store& graph, embedstore::Store& chunks,
                            std::string_view member) {
    Code code;
    for (const auto& [file, end] :
         std::vector<std::pair<std::string, int>>{{"pkg/app.py", 20},
                                                  {"pkg/lib.py", 20},
                                                  {"pkg/util.py", 5},
                                                  {"pkg/island.py", 3},
                                                  {"pkg/chain.py", 30},
                                                  {"pkg/callers.py", 40}}) {
        code.defined("file", file, file, 1, end, "Python file");
    }
    code.edge("file", "pkg/app.py", "file", "pkg/lib.py", "imports", {{"pkg/app.py", 1}});

    code.function("pkg.app.run", "pkg/app.py", 5, 9, "def run(path: str) -> str");
    code.function("pkg.app.main", "pkg/app.py", 12, 14, "def main()");
    code.function("pkg.lib.helper", "pkg/lib.py", 1, 3, "def helper(x: int) -> int");
    code.function("pkg.lib.other", "pkg/lib.py", 6, 8, "def other() -> int");
    code.function("pkg.util.helper", "pkg/util.py", 1, 2, "def helper()");
    code.function("pkg.island.alone", "pkg/island.py", 1, 3, "def alone()");
    code.defined("class", "pkg.lib.Store", "pkg/lib.py", 10, 18, "class Store");
    code.edge("class", "pkg.lib.Store", "file", "pkg/lib.py", "defined_in", {{"pkg/lib.py", 10}});
    for (const auto& [name, line] : std::vector<std::pair<std::string, int>>{
             {"pkg.lib.Store.add", 11}, {"pkg.lib.Store.Add", 14}}) {
        code.defined("function", name, "pkg/lib.py", line, line + 1, "def add(self, item)");
        code.edge("function", name, "class", "pkg.lib.Store", "defined_in", {{"pkg/lib.py", line}});
    }
    code.unresolved("json.dumps", {{"pkg/app.py", 8}, {"pkg/lib.py", 8}, {"pkg/island.py", 2}});
    // Fourteen references: past a card's twelve lines of provenance.
    std::vector<std::pair<std::string, int>> push_back_sites{{"pkg/app.py", 7}};
    for (int line = 1; line <= 13; ++line) {
        push_back_sites.emplace_back("pkg/callers.py", line);
    }
    code.unresolved(".push_back", push_back_sites);

    code.edge("function", "pkg.app.main", "function", "pkg.app.run", "calls", {{"pkg/app.py", 13}});
    code.edge("function", "pkg.app.run", "function", "pkg.lib.helper", "calls",
              {{"pkg/app.py", 6}, {"pkg/app.py", 7}});
    code.edge("function", "pkg.app.run", "function", "pkg.lib.other", "calls", {{"pkg/app.py", 7}});
    code.edge("function", "pkg.app.run", "name", ".push_back", "calls", {{"pkg/app.py", 7}});
    code.edge("function", "pkg.app.run", "name", "json.dumps", "calls", {{"pkg/app.py", 8}});
    code.edge("function", "pkg.lib.other", "function", "pkg.lib.helper", "calls",
              {{"pkg/lib.py", 7}});
    code.edge("function", "pkg.lib.other", "name", "json.dumps", "calls", {{"pkg/lib.py", 8}});
    code.edge("function", "pkg.island.alone", "name", "json.dumps", "calls",
              {{"pkg/island.py", 2}});

    for (int i = 0; i <= 10; ++i) {
        const std::string name = "pkg.chain.n" + std::to_string(i);
        code.function(name, "pkg/chain.py", 1 + (2 * i), 2 + (2 * i),
                      "def n" + std::to_string(i) + "()");
        if (i < 10) {
            code.edge("function", name, "function", "pkg.chain.n" + std::to_string(i + 1), "calls",
                      {{"pkg/chain.py", 2 + (2 * i)}});
        }
    }
    for (int i = 1; i <= 15; ++i) {
        const std::string name = "pkg.callers.c" + two_digits(i);
        code.function(name, "pkg/callers.py", (2 * i) - 1, 2 * i, "def c" + two_digits(i) + "()");
        code.edge("function", name, "function", "pkg.lib.helper", "calls",
                  {{"pkg/callers.py", 2 * i}});
    }
    (void)graph.sync_code_graph(code.nodes, code.edges);

    // The prose layer, over chunks in `chunks` under `member`.
    chunks.replace_source("docs/atlas.md", {"Atlas stores its readings in the Vault."});
    chunks.replace_source("docs/vault.md", {"The Vault is implemented by the Store class."});
    chunks.replace_source("docs/kr-0001.md", {"Decision kr-0001: keep every reading in Vault."});
    const std::int64_t atlas_chunk = chunks.chunks_by_source("docs/atlas.md").front().id;
    const std::int64_t vault_chunk = chunks.chunks_by_source("docs/vault.md").front().id;
    const std::int64_t record_chunk = chunks.chunks_by_source("docs/kr-0001.md").front().id;

    const std::int64_t atlas = graph.upsert_node("Atlas", "system", "the field probe network").id;
    const std::int64_t team =
        graph.upsert_node("Atlas", "organization", "the team that runs the probes").id;
    const std::int64_t vault =
        graph.upsert_node("Vault", "system", "the warehouse Atlas stores readings in").id;
    const std::int64_t decision =
        graph
            .upsert_decision_node("kr-0001", "Keep every reading in Vault — one store, one backup",
                                  embedstore::decision_node_metadata_json("shipped", "engineering"))
            .id;
    (void)graph.add_mention(atlas, member, atlas_chunk);
    (void)graph.add_mention(team, member, atlas_chunk);
    (void)graph.add_mention(vault, member, atlas_chunk);
    (void)graph.add_mention(vault, member, vault_chunk);
    (void)graph.add_mention(decision, member, record_chunk);
    graph.upsert_edge(atlas, vault, "stores readings in", "Atlas writes its readings to Vault");
    graph.upsert_edge(atlas, vault, "stores readings in", "");
    graph.upsert_edge(team, atlas, "runs", "");
    (void)graph.ensure_edge(decision, vault, "concerns", "");
    graph.upsert_edge(vault, id_of(graph, "pkg.lib.Store", "class"), "implemented by", "");

    // Twelve more chunks mention Vault: past a card's twelve chunks.
    std::vector<std::string> entries;
    entries.reserve(12);
    for (int i = 0; i < 12; ++i) {
        entries.push_back("Vault entry " + std::to_string(i));
    }
    chunks.replace_source("docs/log.md", entries);
    for (const embedstore::Chunk& chunk : chunks.chunks_by_source("docs/log.md")) {
        (void)graph.add_mention(vault, member, chunk.id);
    }

    // Ledger: a description past a card's clip, and thirteen records that
    // concern it -- past a card's twelve decisions.
    std::string long_description;
    while (long_description.size() < 500) {
        long_description += "The ledger keeps every entry. ";
    }
    const std::int64_t ledger = graph.upsert_node("Ledger", "artifact", long_description).id;
    (void)graph.add_mention(ledger, member, vault_chunk);
    for (int i = 1; i <= 13; ++i) {
        const std::int64_t rule =
            graph
                .upsert_decision_node(
                    "kr-01" + two_digits(i), "Ledger rule " + std::to_string(i),
                    embedstore::decision_node_metadata_json("proposed", "finance"))
                .id;
        (void)graph.ensure_edge(rule, ledger, "concerns", "");
    }
    (void)graph.replace_community(
        std::to_string(atlas) + "," + std::to_string(vault) + "," + std::to_string(decision),
        {atlas, vault, decision}, "Atlas and the Vault it writes its readings to.", "fixture");
}

void add_report_orphans(embedstore::Store& graph, embedstore::Store& chunks,
                        std::string_view member) {
    const std::vector<embedstore::Chunk> log = chunks.chunks_by_source("docs/log.md");
    for (int i = 1; i <= 12; ++i) {
        const std::int64_t orphan =
            graph
                .upsert_node("Orphan " + two_digits(i), "concept",
                             "An orphan, mentioned " + std::to_string(i) + " time(s).")
                .id;
        for (int c = 0; c < i && c < static_cast<int>(log.size()); ++c) {
            (void)graph.add_mention(orphan, member, log[static_cast<std::size_t>(c)].id);
        }
    }
}

}  // namespace apogee::testing
