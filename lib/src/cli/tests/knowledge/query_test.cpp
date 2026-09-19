#include "knowledge/query.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <random>
#include <string>
#include <vector>

#include "backends/mock.h"
#include "embedstore/graph.h"
#include "embedstore/store.h"
#include "harness/config.h"
#include "harness/harness.h"
#include "knowledge/record.h"
#include "knowledge/store.h"
#include "support/env_guard.h"

/// Records back out: the one resolver applied to a knowledge collection, the
/// status filter defaulting to shipped and applied BEFORE the cut, and the
/// judge under its never-fail contract.
namespace {

using apogee::backends::MockEmbeddingProvider;
using apogee::backends::MockProvider;
using apogee::backends::MockTurn;
using apogee::knowledge::QueryOptions;
using apogee::knowledge::QueryResult;
using apogee::knowledge::Record;
using apogee::knowledge::Store;

constexpr std::string_view kConfig = R"(
models:
  default_embedding: embed
backends:
  embed:
    type: mock
    embedding_model: mock-space
  judge:
    type: mock
embeddings:
  pinned:
    retriever: vector
)";

struct Scratch {
    apogee::testing::TempDir dir{"knowledge-query-" + std::to_string(std::random_device{}())};
    Store store{dir.path() / "knowledge.db", dir.path() / "raw"};
    int next = 0;

    Record put(std::string intent, std::string status, std::string discipline = "eng",
               const std::vector<float>& vector = {}) {
        Record record;
        record.id = "kr-20260913T12000" + std::to_string(next / 10) + std::to_string(next % 10) +
                    "Z-00000" + std::to_string(next % 10);
        ++next;
        record.intent = std::move(intent);
        record.decision = "the choice";
        record.status = std::move(status);
        record.discipline = std::move(discipline);
        record.timestamp = "2026-09-13T12:00:00.000000Z";
        store.put(record, vector, "");
        return record;
    }
};

/// A harness with nothing in it: the lexical floor's home ground.
struct Bare {
    apogee::harness::Config config;
    apogee::harness::Harness harness{config};
};

/// A harness holding a free mock embedder and a scripted judge.
struct Equipped {
    apogee::harness::Config config = apogee::harness::parse_config(kConfig, "<test>");
    apogee::harness::Harness harness{config};
    std::shared_ptr<MockEmbeddingProvider> embedder =
        std::make_shared<MockEmbeddingProvider>("embed");
    std::shared_ptr<MockProvider> judge;

    explicit Equipped(std::vector<MockTurn> verdicts = {MockTurn{"[2]"}}) {
        embedder->set_model_name("mock-space");
        MockProvider::Options options;
        options.backend_name = "judge";
        options.turns = std::move(verdicts);
        judge = std::make_shared<MockProvider>(std::move(options));
        harness.register_provider("embed", embedder);
        harness.register_provider("judge", judge);
        harness.use_default_router();
    }

    [[nodiscard]] std::vector<float> vector_of(std::string_view text) const {
        return embedder->embed({std::string{text}}, {}).front();
    }
};

QueryOptions ask(std::string question) {
    QueryOptions options;
    options.question = std::move(question);
    return options;
}

}  // namespace

TEST_CASE("a lexical query needs no model: shipped records that match, scored and labelled",
          "[knowledge][query][lexical]") {
    Scratch scratch;
    const Record hit =
        scratch.put("we dropped the cancel button because testers were confused", "shipped", "ux");
    scratch.put("we chose SQLite because one file cannot half-succeed", "shipped");
    const Bare bare;
    // No stop-words: every record's index text ends "Decision: the choice",
    // and FTS5's OR would match "the" against all of them.
    const QueryResult result =
        apogee::knowledge::query(scratch.store, bare.harness, bare.config, "knowledge",
                                 ask("cancel button confused testers"));
    REQUIRE(result.ok());
    CHECK(result.retriever == apogee::agentloop::Retriever::Lexical);
    CHECK_FALSE(result.reranked);
    CHECK_FALSE(result.excluded);
    REQUIRE(result.records.size() == 1);
    CHECK(result.records.front().record.id == hit.id);
    CHECK(result.records.front().score > 0.0);
    CHECK(result.records.front().score <= 1.0);
    CHECK(result.records.front().chunk_id == *scratch.store.chunk_id(hit.id));
    // A plain document sharing the collection is never a record.
    scratch.store.chunks().replace_source("notes.md", {"cancel button notes cancel button"});
    CHECK(apogee::knowledge::query(scratch.store, bare.harness, bare.config, "knowledge",
                                   ask("cancel button"))
              .records.size() == 1);
}

TEST_CASE(
    "the status filter defaults to shipped and runs BEFORE the cut, so an off-branch top "
    "hit never starves the result",
    "[knowledge][query][status]") {
    Scratch scratch;
    // Six rejected records that match the question hardest, and one shipped
    // record that matches weakly: a filter after a top-k cut would return
    // nothing on the default branch.
    for (int i = 0; i < 6; ++i) {
        scratch.put("cancel button cancel button cancel button rejected idea", "rejected", "ux");
    }
    const Record shipped =
        scratch.put("the cancel control was removed after testing", "shipped", "eng");
    const Bare bare;
    QueryOptions options = ask("cancel button");
    options.top_k = 1;
    QueryResult result =
        apogee::knowledge::query(scratch.store, bare.harness, bare.config, "knowledge", options);
    REQUIRE(result.ok());
    REQUIRE(result.records.size() == 1);
    CHECK(result.records.front().record.id == shipped.id);
    CHECK(result.records.front().record.status == "shipped");

    // Every branch: the strongest match wins, and it is rejected.
    options.status.clear();
    result =
        apogee::knowledge::query(scratch.store, bare.harness, bare.config, "knowledge", options);
    REQUIRE(result.records.size() == 1);
    CHECK(result.records.front().record.status == "rejected");
    options.top_k = 10;
    CHECK(apogee::knowledge::query(scratch.store, bare.harness, bare.config, "knowledge", options)
              .records.size() == 7);

    // What was considered and dropped.
    options.status = "rejected";
    CHECK(apogee::knowledge::query(scratch.store, bare.harness, bare.config, "knowledge", options)
              .records.size() == 6);
    // And by discipline, on top of the branch.
    options.status.clear();
    options.discipline = "eng";
    result =
        apogee::knowledge::query(scratch.store, bare.harness, bare.config, "knowledge", options);
    REQUIRE(result.records.size() == 1);
    CHECK(result.records.front().record.id == shipped.id);
}

TEST_CASE("filter_hits decodes, filters on both fields, skips non-records, and cuts last",
          "[knowledge][query][filter]") {
    Scratch scratch;
    scratch.put("alpha reasoning", "rejected", "ux");
    scratch.put("alpha reasoning", "shipped", "ux");
    scratch.put("alpha reasoning", "shipped", "eng");
    scratch.store.chunks().replace_source("notes.md", {"alpha reasoning"});
    const std::vector<apogee::embedstore::SearchHit> hits =
        scratch.store.chunks().search("alpha", 0);
    REQUIRE(hits.size() == 4);
    CHECK(apogee::knowledge::filter_hits(hits, "", "", 0).size() == 3);
    CHECK(apogee::knowledge::filter_hits(hits, "shipped", "", 0).size() == 2);
    CHECK(apogee::knowledge::filter_hits(hits, "shipped", "eng", 0).size() == 1);
    CHECK(apogee::knowledge::filter_hits(hits, "", "ux", 0).size() == 2);
    CHECK(apogee::knowledge::filter_hits(hits, "", "", 2).size() == 2);
    CHECK(apogee::knowledge::filter_hits(hits, "shipped", "", 1).size() == 1);
}

TEST_CASE(
    "the resolver's rules apply to records: an explicit vector ask with nothing to run it "
    "is refused, hybrid runs and is reported lexical, a vector pin excludes",
    "[knowledge][query][resolver]") {
    Scratch scratch;
    scratch.put("cancel button reasoning", "shipped");
    const Bare bare;
    QueryOptions options = ask("cancel button");
    options.retriever_flag = "vector";
    const QueryResult refused =
        apogee::knowledge::query(scratch.store, bare.harness, bare.config, "knowledge", options);
    CHECK_FALSE(refused.ok());
    CHECK(refused.error.find("lexical") != std::string::npos);
    CHECK(refused.records.empty());

    options.retriever_flag = "hybrid";
    const QueryResult degraded =
        apogee::knowledge::query(scratch.store, bare.harness, bare.config, "knowledge", options);
    REQUIRE(degraded.ok());
    CHECK(degraded.retriever == apogee::agentloop::Retriever::Lexical);
    CHECK(degraded.records.size() == 1);
    REQUIRE_FALSE(degraded.notes.empty());

    // A `retriever: vector` pin on the collection, with no embedder: excluded
    // with a note, never silently searched the other way.
    options.retriever_flag.clear();
    const Equipped equipped;
    apogee::harness::Config pinned = equipped.config;
    const Bare empty;
    const QueryResult excluded =
        apogee::knowledge::query(scratch.store, empty.harness, pinned, "pinned", options);
    REQUIRE(excluded.ok());
    CHECK(excluded.excluded);
    CHECK(excluded.records.empty());
    CHECK_FALSE(excluded.notes.empty());
}

TEST_CASE(
    "with a free embedder and a matching store, auto is vector; a model mismatch demotes "
    "to lexical with the re-ingest hint",
    "[knowledge][query][vector]") {
    Scratch scratch;
    const Equipped equipped;
    const std::string wanted = "we dropped the cancel button because testers were confused";
    scratch.put(wanted, "shipped", "ux", equipped.vector_of(wanted + "\n\nDecision: the choice"));
    scratch.put("we chose SQLite because one file cannot half-succeed", "shipped", "eng",
                equipped.vector_of("we chose SQLite because one file cannot half-succeed"
                                   "\n\nDecision: the choice"));
    scratch.store.chunks().set_embedding_model("mock-space", 8);

    // The mock embedder is deterministic, not semantic: asking with the exact
    // index text is what makes one record's cosine 1.0 and puts it first.
    QueryResult result =
        apogee::knowledge::query(scratch.store, equipped.harness, equipped.config, "knowledge",
                                 ask(wanted + "\n\nDecision: the choice"));
    REQUIRE(result.ok());
    CHECK(result.retriever == apogee::agentloop::Retriever::Vector);
    REQUIRE(result.records.size() == 2);
    CHECK(result.records.front().record.intent == wanted);
    CHECK(result.records.front().score > result.records.back().score);

    QueryOptions hybrid = ask(wanted);
    hybrid.retriever_flag = "hybrid";
    CHECK(apogee::knowledge::query(scratch.store, equipped.harness, equipped.config, "knowledge",
                                   hybrid)
              .retriever == apogee::agentloop::Retriever::Hybrid);

    scratch.store.chunks().set_embedding_model("other-space", 8);
    result = apogee::knowledge::query(scratch.store, equipped.harness, equipped.config, "knowledge",
                                      ask(wanted));
    REQUIRE(result.ok());
    CHECK(result.retriever == apogee::agentloop::Retriever::Lexical);
    REQUIRE_FALSE(result.notes.empty());
    // The note names both models and the way back to vector search.
    CHECK(result.notes.front().find("other-space") != std::string::npos);
    CHECK(result.notes.front().find("mock-space") != std::string::npos);
    CHECK(result.notes.front().find("embed ingest") != std::string::npos);
}

TEST_CASE(
    "a judge reorders the records by their index text and reports it; a judge that "
    "fails leaves the retrieval order and says so",
    "[knowledge][query][rerank]") {
    Scratch scratch;
    const Record first = scratch.put("cancel button cancel button strongest match", "shipped");
    const Record second = scratch.put("cancel button weaker match", "shipped");
    {
        const Equipped equipped{{MockTurn{"[2]"}}};
        QueryOptions options = ask("cancel button");
        options.retriever_flag = "lexical";
        options.rerank_flag = "judge";
        const QueryResult result = apogee::knowledge::query(scratch.store, equipped.harness,
                                                            equipped.config, "knowledge", options);
        REQUIRE(result.ok());
        CHECK(result.reranked);
        REQUIRE(result.records.size() == 1);
        CHECK(result.records.front().record.id == second.id);
        // The judge saw the immutable index text, never a name or a link.
        REQUIRE(equipped.judge->requests().size() == 1);
        const std::string prompt =
            equipped.judge->requests().front().messages.front().content.plain_text();
        CHECK(prompt.find("strongest match") != std::string::npos);
        CHECK(prompt.find("Decision: the choice") != std::string::npos);
    }
    {
        const Equipped equipped{{MockTurn{"I decline to rank anything."}}};
        QueryOptions options = ask("cancel button");
        options.retriever_flag = "lexical";
        options.rerank_flag = "judge";
        const QueryResult result = apogee::knowledge::query(scratch.store, equipped.harness,
                                                            equipped.config, "knowledge", options);
        REQUIRE(result.ok());
        CHECK_FALSE(result.reranked);
        REQUIRE(result.records.size() == 2);
        CHECK(result.records.front().record.id == first.id);
        bool said = false;
        for (const std::string& note : result.notes) {
            said = said || note.find("rank") != std::string::npos;
        }
        CHECK(said);
    }
    // `off` switches a judge off, and an unconfigured one is a note.
    const Equipped equipped;
    QueryOptions off = ask("cancel button");
    off.retriever_flag = "lexical";
    off.rerank_flag = "off";
    const QueryResult plain = apogee::knowledge::query(scratch.store, equipped.harness,
                                                       equipped.config, "knowledge", off);
    CHECK_FALSE(plain.reranked);
    CHECK(plain.records.size() == 2);
    CHECK(equipped.judge->requests().empty());
}

TEST_CASE(
    "graph_for_records walks from the matched records, seeds by the question only on a "
    "lexical result, and notes an uncovered collection",
    "[knowledge][query][graph]") {
    Scratch scratch;
    apogee::knowledge::Store& records = scratch.store;
    apogee::knowledge::Record record;
    record.id = "kr-1";
    record.intent = "we route readings through Atlas";
    record.status = "shipped";
    record.timestamp = "2026-09-19T12:00:00.000000Z";
    records.put(record, {}, "");
    apogee::embedstore::Store& store = records.chunks();
    store.replace_source("docs.md", {"a ghost of a document"});
    const std::int64_t record_chunk = *records.chunk_id("kr-1");
    const std::int64_t docs_chunk = store.chunks_by_source("docs.md").front().id;
    const std::int64_t atlas = store.upsert_node("Atlas", "system", "collects readings").id;
    const std::int64_t ghost = store.upsert_node("Ghost", "concept", "an isolated entity").id;
    (void)store.add_mention(atlas, record_chunk);
    (void)store.add_mention(ghost, docs_chunk);

    apogee::harness::Config config;
    apogee::knowledge::QueryResult result;
    apogee::knowledge::ScoredRecord scored;
    scored.record = record;
    scored.chunk_id = record_chunk;
    result.records.push_back(scored);

    // No graph covers the collection: the note, and nothing else.
    apogee::knowledge::RecordGraph none =
        apogee::knowledge::graph_for_records(records, config, "knowledge", result, "ghost");
    CHECK(none.entities == 0);
    CHECK(none.note.find("no knowledge graph covers collection \"knowledge\"") !=
          std::string::npos);

    apogee::harness::EmbeddingConfig entry;
    entry.graph.enabled = true;
    config.embeddings.emplace("knowledge", entry);
    // A lexical result also seeds by the question's own terms: "ghost" names
    // an entity no record chunk reaches.
    result.retriever = apogee::agentloop::Retriever::Lexical;
    const apogee::knowledge::RecordGraph lexical =
        apogee::knowledge::graph_for_records(records, config, "knowledge", result, "ghost");
    CHECK(lexical.note.empty());
    CHECK(lexical.entities == 1);
    CHECK(lexical.context.find("Ghost (concept)") != std::string::npos);
    // A vector result seeds from the matched records alone.
    result.retriever = apogee::agentloop::Retriever::Vector;
    const apogee::knowledge::RecordGraph vector =
        apogee::knowledge::graph_for_records(records, config, "knowledge", result, "ghost");
    CHECK(vector.note.empty());
    CHECK(vector.entities == 0);
    CHECK(vector.context.empty());
}
