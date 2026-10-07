#include "agentloop/graph_context.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "embedstore/store.h"
#include "support/env_guard.h"

/// The rendered section: entity lines, triples, the decision marker, the
/// budget with whole-line truncation, the lexical seed -- and the acceptance
/// fixture: two chunks sharing no vocabulary, linked only by an edge, where a
/// query matching one injects the other's entity.
namespace {

using apogee::agentloop::GraphSection;
using apogee::embedstore::GraphNode;
using apogee::embedstore::Store;

struct Scratch {
    apogee::testing::TempDir dir{"graph-context-" + std::to_string(std::random_device{}())};
    Store store{dir.path() / "c.db"};
};

/// Chunk A is about Atlas; chunk B about the Vault; they share no words.
struct Fixture : Scratch {
    std::int64_t chunk_a = 0;
    std::int64_t chunk_b = 0;
    std::int64_t atlas = 0;
    std::int64_t vault = 0;

    Fixture() {
        store.replace_source("a.md", {"Atlas collects readings from the field probes"});
        store.replace_source("b.md", {"The warehouse keeps every record for seven years"});
        chunk_a = store.chunks_by_source("a.md").front().id;
        chunk_b = store.chunks_by_source("b.md").front().id;
        atlas = store.upsert_node("Atlas", "system", "collects readings").id;
        vault = store.upsert_node("Vault", "system", "the warehouse Atlas stores readings in").id;
        (void)store.add_mention(atlas, chunk_a);
        (void)store.add_mention(vault, chunk_b);
        store.upsert_edge(atlas, vault, "stores readings in", "Atlas writes to Vault");
    }
};

}  // namespace

TEST_CASE("entity_line renders a plain entity and folds a decision's branch marker in",
          "[agentloop][graph][render]") {
    GraphNode plain;
    plain.name = "Vault";
    plain.type = "system";
    plain.description = "the warehouse";
    CHECK(apogee::agentloop::entity_line(plain) == "Vault (system): the warehouse");
    plain.description.clear();
    CHECK(apogee::agentloop::entity_line(plain) == "Vault (system)");

    GraphNode decision;
    decision.name = "kr-1";
    decision.type = "decision";
    decision.description = "drop it — testers were lost";
    decision.metadata = apogee::embedstore::decision_node_metadata_json("superseded", "ux");
    CHECK(apogee::agentloop::entity_line(decision) ==
          "kr-1 (decision, superseded): drop it — testers were lost");
}

TEST_CASE("a lexical query matching chunk A injects B's entity through the edge alone",
          "[agentloop][graph][expand]") {
    const Fixture f;
    // The seeds are the retrieved chunks -- A -- and the query's terms, which
    // name nothing in the entity index. The Vault shares no vocabulary with
    // the query and is reached only by the edge.
    const GraphSection section =
        apogee::agentloop::build_graph_section(f.store, "notes", {f.chunk_a}, "field probes", 1, 8);
    CHECK(section.entities == 1);
    CHECK(section.text ==
          "[Knowledge graph: notes]\n"
          "Vault (system): the warehouse Atlas stores readings in\n"
          "Atlas —[stores readings in]→ Vault: Atlas writes to Vault");
    // Nothing retrieved and nothing named: nothing rendered.
    CHECK(apogee::agentloop::build_graph_section(f.store, "notes", {}, "unrelated words", 1, 8)
              .empty());
    CHECK(apogee::agentloop::build_graph_section(f.store, "notes", {}, "", 1, 8).empty());
}

TEST_CASE(
    "on a lexical turn the query's own terms seed through the entity index, with no chunks "
    "at all",
    "[agentloop][graph][expand][lexical]") {
    const Fixture f;
    // "Atlas" names an entity: it is listed itself (hop 0) and its neighbour.
    const GraphSection section =
        apogee::agentloop::build_graph_section(f.store, "notes", {}, "what does Atlas do", 1, 8);
    CHECK(section.entities == 2);
    CHECK(section.text.find("Atlas (system): collects readings") != std::string::npos);
    CHECK(section.text.find("Vault (system)") != std::string::npos);
    // A vector turn passes no query: only chunk seeds count.
    CHECK(apogee::agentloop::build_graph_section(f.store, "notes", {}, "", 1, 8).empty());
}

TEST_CASE("the budget cuts whole lines and never leaves a triple without its entity",
          "[agentloop][graph][budget]") {
    Scratch s;
    // The seed chunk mentions Seed; six other chunks mention one neighbour
    // each, so the neighbours are reached by edge, not re-listed as seeds.
    s.store.replace_source("a.md", {"seed", "n0", "n1", "n2", "n3", "n4", "n5"});
    const std::vector<apogee::embedstore::Chunk> chunks = s.store.chunks_by_source("a.md");
    const std::int64_t chunk = chunks.front().id;
    const std::int64_t seed = s.store.upsert_node("Seed", "concept", "").id;
    (void)s.store.add_mention(seed, chunk);
    // Six neighbours with 300-codepoint descriptions: the header (24) plus
    // four lines (~314 each) fit under 1500; the fifth does not.
    for (int i = 0; i < 6; ++i) {
        const std::int64_t id = s.store
                                    .upsert_node("N" + std::to_string(i), "concept",
                                                 std::string(300, static_cast<char>('a' + i)))
                                    .id;
        (void)s.store.add_mention(id, chunks[static_cast<std::size_t>(i) + 1].id);
        s.store.upsert_edge(seed, id, "links", "");
    }
    const GraphSection section =
        apogee::agentloop::build_graph_section(s.store, "c", {chunk}, "", 1, 8);
    CHECK(section.entities == 4);
    CHECK(section.text.size() <= apogee::agentloop::kGraphSectionBudget);
    CHECK(section.text.find("N4") == std::string::npos);
    // The triples were skipped entirely once an entity line was cut.
    CHECK(section.text.find("—[links]→") == std::string::npos);
    CHECK(section.text.back() != '\n');

    // With room, the triples follow the entities and the section stays whole.
    const GraphSection small =
        apogee::agentloop::build_graph_section(s.store, "c", {chunk}, "", 1, 2);
    CHECK(small.entities == 2);
    CHECK(small.text.find("Seed —[links]→ N0") != std::string::npos);
}

TEST_CASE("a code entity carries its file:line, and a parsed relation says it was parsed",
          "[agentloop][graph][render][code]") {
    GraphNode function;
    function.name = "apogee::graph::build_source";
    function.type = "function";
    function.description = "SourceBuildResult build_source(...)";
    function.metadata = apogee::embedstore::code_node_metadata_json(
        apogee::embedstore::CodeNodeMetadata{.code = true,
                                             .unresolved = false,
                                             .language = "cpp",
                                             .member = "cli",
                                             .file = "source/business/graph/code_build.cpp",
                                             .line = 198,
                                             .end_line = 335});
    CHECK(apogee::agentloop::entity_line(function) ==
          "apogee::graph::build_source (function, source/business/graph/code_build.cpp:198): "
          "SourceBuildResult build_source(...)");

    // A code graph's section under the existing budget: entities with their
    // lines, relations marked parsed; a model's relation keeps its bare form.
    Scratch scratch;
    apogee::embedstore::CodeNodeRow caller;
    caller.type = "function";
    caller.name = "run";
    caller.description = "void run()";
    caller.metadata = apogee::embedstore::code_node_metadata_json({.code = true,
                                                                   .language = "cpp",
                                                                   .member = "src",
                                                                   .file = "a.cc",
                                                                   .line = 3,
                                                                   .end_line = 5});
    caller.mentions.push_back(
        {.collection = "src", .file = "a.cc", .line = 3, .end_line = 5, .role = "definition"});
    apogee::embedstore::CodeNodeRow callee = caller;
    callee.name = "helper";
    callee.description = "int helper()";
    callee.metadata = apogee::embedstore::code_node_metadata_json({.code = true,
                                                                   .language = "cpp",
                                                                   .member = "src",
                                                                   .file = "b.cc",
                                                                   .line = 9,
                                                                   .end_line = 9});
    callee.mentions.front().file = "b.cc";
    callee.mentions.front().line = 9;
    apogee::embedstore::CodeEdgeRow call;
    call.source_type = "function";
    call.source_name = "run";
    call.target_type = "function";
    call.target_name = "helper";
    call.relation = "calls";
    call.sites.push_back({.collection = "src", .file = "a.cc", .line = 4});
    (void)scratch.store.sync_code_graph({caller, callee}, {call});
    const GraphSection section =
        apogee::agentloop::build_graph_section_labelled(scratch.store, "code", {}, "helper", 1, 8);
    REQUIRE(section.entities >= 1);
    CHECK(section.text.find("[Knowledge graph: code]") == 0);
    CHECK(section.text.find("run (function, a.cc:3): void run()") != std::string::npos);
    CHECK(section.text.find("run —[calls·extracted]→ helper") != std::string::npos);
}

TEST_CASE("a prose relation keeps its bare form beside a parsed one",
          "[agentloop][graph][render]") {
    Fixture f;
    const GraphSection section =
        apogee::agentloop::build_graph_section(f.store, "notes", {f.chunk_a}, "", 1, 8);
    CHECK(section.text.find("Atlas —[stores readings in]→ Vault") != std::string::npos);
    CHECK(section.text.find("extracted") == std::string::npos);
}
