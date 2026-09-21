#include "agentloop/rag.h"

#include <catch2/catch_test_macros.hpp>

#include <sqlite3.h>

#include <filesystem>
#include <random>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "agentloop/embed_func.h"
#include "agentloop/loop.h"
#include "agentloop/retriever.h"
#include "backends/mock.h"
#include "commands/helpers.h"
#include "embedstore/store.h"
#include "harness/config.h"
#include "harness/harness.h"

/// RAG injection, and the one property that makes it safe to use every turn:
/// **retrieved context reaches the outgoing request and nothing else.**
///
/// If it reached persisted history, two things would go wrong and both get
/// worse over time. The transcript would fill with injected documents and grow
/// without bound; and turn one's chunks would still be sitting there on turn
/// five, competing for the model's attention with the chunks that actually
/// answer the new question.
namespace {

using apogee::agentloop::build_rag_prefix;
using apogee::agentloop::RagResult;
using apogee::agentloop::render_rag_context;
using apogee::embedstore::Store;

struct Scratch {
    // Named by a random draw, not a per-process counter: ctest runs these
    // cases as parallel PROCESSES, and a counter produced the same path in
    // several of them at once -- each deleting the others' directories.
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() /
        ("apogee-rag-" + std::to_string(std::random_device{}()) + "-" + std::to_string(counter()));

    Scratch() {
        std::error_code code;
        std::filesystem::create_directories(dir, code);
    }

    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    Scratch(Scratch&&) = delete;
    Scratch& operator=(Scratch&&) = delete;

    ~Scratch() {
        std::error_code code;
        std::filesystem::remove_all(dir, code);
    }

    [[nodiscard]] std::filesystem::path db() const {
        return dir / "collection.db";
    }

    static int counter() {
        static int next = 0;
        return ++next;
    }
};

/// A collection holding one very distinctive sentence, so a grep for it can
/// only match injected context.
constexpr std::string_view kSecret = "the zarquon protocol requires seventeen widgets";

/// Seeds `scratch` in place. `Scratch` is deliberately neither copyable nor
/// movable -- it owns a directory it deletes -- so this fills one rather than
/// returning a new one.
void seed(const Scratch& scratch) {
    Store store{scratch.db()};
    store.replace_source("notes.md", {std::string{kSecret}, "an unrelated second chunk"});
}

}  // namespace

TEST_CASE("retrieval finds a chunk and reports its retriever", "[agentloop][rag]") {
    const Scratch scratch;
    seed(scratch);
    const RagResult result = build_rag_prefix(scratch.db(), "zarquon protocol widgets", 4);

    CHECK(result.error.empty());
    CHECK(result.chunks > 0);
    CHECK(result.top_score > 0.0);
    // Always named. Lexical and vector scores are incomparable, and a number
    // without its retriever invites exactly that comparison.
    CHECK(result.retriever == "lexical");
    REQUIRE(result.prefix.size() == 1);
    CHECK(result.prefix.front().content.plain_text().find(kSecret) != std::string::npos);
}

TEST_CASE("injected context never reaches persisted history", "[agentloop][rag][transient]") {
    // THE acceptance criterion, asserted as a grep: the distinctive sentence
    // must appear in what the model was sent and NOT in the history the
    // session would save.
    const Scratch scratch;
    seed(scratch);
    const RagResult rag = build_rag_prefix(scratch.db(), "zarquon protocol widgets", 4);
    REQUIRE(rag.chunks > 0);

    apogee::backends::MockProvider::Options options;
    options.backend_name = "mock";
    options.turns = {apogee::backends::MockTurn{.text = "answered"}};

    apogee::harness::Harness harness{apogee::harness::Config{}};
    harness.register_provider("mock",
                              std::make_shared<apogee::backends::MockProvider>(std::move(options)));
    harness.use_default_router();

    std::vector<apogee::harness::ChatMessage> history{
        apogee::harness::ChatMessage::user("what does the protocol require?")};

    apogee::agentloop::Options loop;
    loop.model = "mock";
    loop.transient_prefix = rag.prefix;
    loop.stream_answer = false;

    apogee::agentloop::NullReporter reporter;
    const apogee::agentloop::RunResult result =
        apogee::agentloop::run(harness, history, loop, reporter);

    CHECK(result.answer == "answered");

    // The grep. Every message the session would persist, concatenated.
    std::string persisted;
    for (const apogee::harness::ChatMessage& message : history) {
        persisted += message.content.plain_text();
        persisted += "\n";
    }
    INFO("persisted history:\n" << persisted);
    CHECK(persisted.find(kSecret) == std::string::npos);
    // ...while the question and the answer are both there, so this is not
    // passing because history is simply empty.
    CHECK(persisted.find("what does the protocol require?") != std::string::npos);
    CHECK(persisted.find("answered") != std::string::npos);
}

TEST_CASE("a missing collection is reported without failing the turn", "[agentloop][rag]") {
    // Answering without retrieved context is much better than refusing to
    // answer because a collection was not there.
    const RagResult result = build_rag_prefix("/no/such/collection.db", "anything", 4);

    CHECK_FALSE(result.error.empty());
    CHECK(result.prefix.empty());
    CHECK(result.chunks == 0);
}

TEST_CASE("a corpus with nothing relevant is a normal empty result", "[agentloop][rag]") {
    // Not an error. A collection that has nothing to say about this question is
    // not a fault, and must not be reported as one.
    const Scratch scratch;
    seed(scratch);
    const RagResult result = build_rag_prefix(scratch.db(), "xylophone bicycle", 4);

    CHECK(result.error.empty());
    CHECK(result.chunks == 0);
    CHECK(result.prefix.empty());
}

TEST_CASE("the limit caps how much is injected", "[agentloop][rag]") {
    Scratch scratch;
    {
        Store store{scratch.db()};
        store.replace_source("doc",
                             {"alpha one", "alpha two", "alpha three", "alpha four", "alpha five"});
    }
    const RagResult result = build_rag_prefix(scratch.db(), "alpha", 2);
    CHECK(result.chunks == 2);
}

TEST_CASE("injected context is framed as retrieved excerpts", "[agentloop][rag]") {
    // Without the framing a model reads an injected document as something the
    // user said, and answers about the document rather than the question --
    // and the more relevant the retrieval, the more confidently it does so.
    const std::string rendered = render_rag_context({"first chunk", "second chunk"});

    CHECK(rendered.find("retrieved") != std::string::npos);
    CHECK(rendered.find("first chunk") != std::string::npos);
    CHECK(rendered.find("second chunk") != std::string::npos);
    // Each excerpt is delimited, so the model can tell where one ends.
    CHECK(rendered.find("excerpt 1") != std::string::npos);
    CHECK(rendered.find("excerpt 2") != std::string::npos);
}

TEST_CASE("rendering nothing yields nothing", "[agentloop][rag]") {
    CHECK(render_rag_context({}).empty());
}

TEST_CASE("context injected by auto_rag reaches the request and never persisted history",
          "[agentloop][rag][transient][config]") {
    // The transient-history rule, extended to the CONFIG path. Injection that
    // nobody typed a flag for is held to exactly the rule the flag is: it rides
    // the outgoing request, and the saved transcript never learns it happened.
    //
    // Two assertions, and the first is what keeps the second honest: the
    // secret MUST appear in what the provider was sent, so this cannot pass by
    // simply not injecting anything.
    const Scratch scratch;
    seed(scratch);

    apogee::harness::Config config;
    config.auto_rag = "notes";  // the key, with no --rag flag anywhere
    const apogee::commands::RagChoice choice =
        apogee::commands::choose_rag_collection(false, {}, config.auto_rag);
    REQUIRE(choice.source == apogee::commands::RagSource::Config);
    REQUIRE(choice.collection == "notes");

    const RagResult rag = build_rag_prefix(scratch.db(), "zarquon protocol widgets", 4);
    REQUIRE(rag.chunks > 0);

    bool secret_reached_the_model = false;
    apogee::backends::MockProvider::Options options;
    options.backend_name = "mock";
    options.turns = {apogee::backends::MockTurn{.text = "answered"}};
    options.on_request = [&secret_reached_the_model](const apogee::harness::ChatRequest& request) {
        for (const apogee::harness::ChatMessage& message : request.messages) {
            if (message.content.plain_text().find(kSecret) != std::string::npos) {
                secret_reached_the_model = true;
            }
        }
    };

    apogee::harness::Harness harness{apogee::harness::Config{}};
    harness.register_provider("mock",
                              std::make_shared<apogee::backends::MockProvider>(std::move(options)));
    harness.use_default_router();

    std::vector<apogee::harness::ChatMessage> history{
        apogee::harness::ChatMessage::user("what does the protocol require?")};

    apogee::agentloop::Options loop;
    loop.model = "mock";
    loop.transient_prefix = rag.prefix;  // the same seam the flag path uses
    loop.stream_answer = false;

    apogee::agentloop::NullReporter reporter;
    (void)apogee::agentloop::run(harness, history, loop, reporter);

    CHECK(secret_reached_the_model);

    std::string persisted;
    for (const apogee::harness::ChatMessage& message : history) {
        persisted += message.content.plain_text();
        persisted += "\n";
    }
    INFO("persisted history:\n" << persisted);
    CHECK(persisted.find(kSecret) == std::string::npos);
    CHECK(persisted.find("what does the protocol require?") != std::string::npos);
}

// --- the full matrix through one turn --------------------------------------------

namespace {

/// A deterministic embedder over a two-dimensional space: "zarquon" texts point
/// one way, everything else the other. Enough for vector search to have an
/// opinion, and for a model-name mismatch to be staged.
apogee::agentloop::Embedder toy_embedder(std::string model, bool metered = false) {
    apogee::agentloop::Embedder embedder;
    embedder.backend = "toy";
    embedder.model = std::move(model);
    embedder.dimensions = 2;
    embedder.metered = metered;
    embedder.embed = [](const std::vector<std::string>& texts,
                        const apogee::harness::CancellationToken&) {
        std::vector<std::vector<float>> out;
        for (const std::string& text : texts) {
            const bool zarquon = text.find("zarquon") != std::string::npos;
            out.push_back(zarquon ? std::vector<float>{1.0F, 0.0F}
                                  : std::vector<float>{0.0F, 1.0F});
        }
        return out;
    };
    return embedder;
}

/// Seeds with vectors from `embedder` and records its model on the store.
void seed_vectors(const Scratch& scratch, const apogee::agentloop::Embedder& embedder) {
    Store store{scratch.db()};
    const std::vector<std::string> chunks{std::string{kSecret}, "an unrelated second chunk"};
    store.replace_source("notes.md", chunks, embedder.embed(chunks, {}));
    store.set_embedding_model(embedder.model, 2);
}

apogee::agentloop::RagTurn turn_for(const Scratch& scratch, std::string question) {
    apogee::agentloop::RagTurn turn;
    turn.store_path = scratch.db();
    turn.question = std::move(question);
    turn.limit = 4;
    return turn;
}

}  // namespace

TEST_CASE("a vector turn retrieves by cosine and reports vector", "[agentloop][rag][vector]") {
    const Scratch scratch;
    const auto embedder = toy_embedder("toy-v1");
    seed_vectors(scratch, embedder);

    apogee::agentloop::RagTurn turn = turn_for(scratch, "tell me about zarquon");
    turn.embedder = embedder;
    const RagResult result = apogee::agentloop::retrieve_for_turn(turn);

    CHECK(result.error.empty());
    CHECK(result.retriever == "vector");
    REQUIRE(result.chunks > 0);
    CHECK(result.prefix.front().content.plain_text().find(kSecret) != std::string::npos);
    CHECK_FALSE(result.reranked);
}

TEST_CASE("a model mismatch falls to lexical with the re-ingest hint, never a cross-space query",
          "[agentloop][rag][vector]") {
    // Ingested under toy-v1, queried under toy-v2: the first acceptance
    // criterion of the item.
    const Scratch scratch;
    seed_vectors(scratch, toy_embedder("toy-v1"));

    apogee::agentloop::RagTurn turn = turn_for(scratch, "zarquon protocol widgets");
    turn.embedder = toy_embedder("toy-v2");
    const RagResult result = apogee::agentloop::retrieve_for_turn(turn);

    CHECK(result.error.empty());
    CHECK(result.retriever == "lexical");
    REQUIRE_FALSE(result.notes.empty());
    CHECK(result.notes.front().find("re-run") != std::string::npos);
    CHECK(result.notes.front().find("toy-v1") != std::string::npos);
    CHECK(result.chunks > 0);  // the lexical half still answers
}

TEST_CASE("an explicit vector flag that cannot run is an error, not a substitution",
          "[agentloop][rag][vector]") {
    const Scratch scratch;
    seed(scratch);  // lexical-only
    apogee::agentloop::RagTurn turn = turn_for(scratch, "zarquon");
    turn.embedder = toy_embedder("toy-v1");
    turn.retriever_flag = "vector";
    const RagResult result = apogee::agentloop::retrieve_for_turn(turn);
    CHECK_FALSE(result.error.empty());
    CHECK(result.chunks == 0);
    CHECK(result.prefix.empty());
}

TEST_CASE("a hybrid turn fuses and reports hybrid; without a vector half it reports lexical",
          "[agentloop][rag][hybrid]") {
    const Scratch scratch;
    const auto embedder = toy_embedder("toy-v1");
    seed_vectors(scratch, embedder);

    apogee::agentloop::RagTurn both = turn_for(scratch, "zarquon protocol");
    both.embedder = embedder;
    both.retriever_flag = "hybrid";
    const RagResult fused = apogee::agentloop::retrieve_for_turn(both);
    CHECK(fused.retriever == "hybrid");
    CHECK(fused.chunks > 0);
    // RRF scores are small numbers on their own scale; the label says so.
    CHECK(fused.top_score < 0.1);

    apogee::agentloop::RagTurn degraded = turn_for(scratch, "zarquon protocol");
    degraded.retriever_flag = "hybrid";  // no embedder at all
    const RagResult lexical = apogee::agentloop::retrieve_for_turn(degraded);
    CHECK(lexical.error.empty());
    CHECK(lexical.retriever == "lexical");
    REQUIRE_FALSE(lexical.notes.empty());
    CHECK(lexical.notes.front().find("hybrid requested") != std::string::npos);
    CHECK(lexical.chunks > 0);
}

TEST_CASE("a transient embedding failure degrades the turn to lexical and says so",
          "[agentloop][rag][vector]") {
    const Scratch scratch;
    seed_vectors(scratch, toy_embedder("toy-v1"));
    apogee::agentloop::RagTurn turn = turn_for(scratch, "zarquon protocol");
    turn.embedder = toy_embedder("toy-v1");
    turn.embedder->embed =
        [](const std::vector<std::string>&,
           const apogee::harness::CancellationToken&) -> std::vector<std::vector<float>> {
        throw std::runtime_error("endpoint down");
    };
    const RagResult result = apogee::agentloop::retrieve_for_turn(turn);
    CHECK(result.error.empty());
    CHECK(result.retriever == "lexical");
    REQUIRE_FALSE(result.notes.empty());
    CHECK(result.notes.front().find("endpoint down") != std::string::npos);
    CHECK(result.chunks > 0);
}

TEST_CASE("context from a vector turn never reaches persisted history either",
          "[agentloop][rag][vector][transient]") {
    // The grep, on the new path: the secret reaches the provider and not the
    // history, so this cannot pass by not injecting.
    const Scratch scratch;
    const auto embedder = toy_embedder("toy-v1");
    seed_vectors(scratch, embedder);
    apogee::agentloop::RagTurn turn = turn_for(scratch, "what does zarquon require?");
    turn.embedder = embedder;
    const RagResult rag = apogee::agentloop::retrieve_for_turn(turn);
    REQUIRE(rag.retriever == "vector");
    REQUIRE(rag.chunks > 0);

    bool seen = false;
    apogee::backends::MockProvider::Options options;
    options.backend_name = "mock";
    options.turns = {apogee::backends::MockTurn{.text = "answered"}};
    options.on_request = [&seen](const apogee::harness::ChatRequest& request) {
        for (const apogee::harness::ChatMessage& message : request.messages) {
            if (message.content.plain_text().find(kSecret) != std::string::npos) {
                seen = true;
            }
        }
    };
    apogee::harness::Harness harness{apogee::harness::Config{}};
    harness.register_provider("mock",
                              std::make_shared<apogee::backends::MockProvider>(std::move(options)));
    harness.use_default_router();

    std::vector<apogee::harness::ChatMessage> history{
        apogee::harness::ChatMessage::user("what does zarquon require?")};
    apogee::agentloop::Options loop;
    loop.model = "mock";
    loop.transient_prefix = rag.prefix;
    loop.stream_answer = false;
    apogee::agentloop::NullReporter reporter;
    (void)apogee::agentloop::run(harness, history, loop, reporter);

    CHECK(seen);
    std::string persisted;
    for (const apogee::harness::ChatMessage& message : history) {
        persisted += message.content.plain_text() + "\n";
    }
    CHECK(persisted.find(kSecret) == std::string::npos);
    CHECK(persisted.find("answered") != std::string::npos);
}

namespace {

/// A graph over the seeded collection: the secret chunk mentions Zarquon,
/// the unrelated chunk mentions the Widget Factory, and one edge links them.
/// Neither entity's text appears in the secret chunk.
void seed_graph(const Scratch& scratch) {
    Store store{scratch.db()};
    const std::vector<apogee::embedstore::Chunk> chunks = store.chunks_by_source("notes.md");
    REQUIRE(chunks.size() == 2);
    const std::int64_t zarquon = store.upsert_node("Zarquon", "concept", "a protocol").id;
    const std::int64_t factory =
        store.upsert_node("Widget Factory", "organization", "makes the widgets").id;
    (void)store.add_mention(zarquon, chunks.front().id);
    (void)store.add_mention(factory, chunks.back().id);
    store.upsert_edge(zarquon, factory, "is supplied by", "");
}

}  // namespace

TEST_CASE(
    "an enabled graph expands the retrieved chunks, rides the transient prefix, and is "
    "counted",
    "[agentloop][rag][graph]") {
    const Scratch scratch;
    seed(scratch);
    seed_graph(scratch);
    apogee::agentloop::RagTurn turn = turn_for(scratch, "zarquon protocol");
    // Off by default: the graph exists but the collection's block does not
    // enable it, so nothing is expanded and nothing is claimed.
    const RagResult plain = apogee::agentloop::retrieve_for_turn(turn);
    CHECK(plain.chunks == 1);
    CHECK(plain.graph_entities == 0);
    CHECK(plain.prefix.front().content.plain_text().find("Knowledge graph") == std::string::npos);

    turn.collection = "notes";
    turn.graph_enabled = true;
    const RagResult expanded = apogee::agentloop::retrieve_for_turn(turn);
    CHECK(expanded.chunks == 1);
    // Two: the Widget Factory reached by the edge, and Zarquon itself -- a
    // lexical turn seeds by the query's own terms, and "zarquon" names it.
    CHECK(expanded.graph_entities == 2);
    REQUIRE(expanded.prefix.size() == 1);
    const std::string sent = expanded.prefix.front().content.plain_text();
    CHECK(sent.find(kSecret) != std::string::npos);
    CHECK(sent.find("[Knowledge graph: notes]") != std::string::npos);
    CHECK(sent.find("Zarquon (concept): a protocol") != std::string::npos);
    CHECK(sent.find("Widget Factory (organization): makes the widgets") != std::string::npos);
    CHECK(sent.find("Zarquon —[is supplied by]→ Widget Factory") != std::string::npos);
    // The section follows the excerpts, never precedes them.
    CHECK(sent.find("--- excerpt 1 ---") < sent.find("[Knowledge graph: notes]"));
    CHECK(expanded.notes.empty());

    // Transient: the section reaches the request and never persisted history.
    apogee::backends::MockProvider::Options options;
    options.backend_name = "mock";
    options.turns = {apogee::backends::MockTurn{.text = "answered"}};
    std::vector<apogee::harness::ChatRequest> seen;
    options.on_request = [&seen](const apogee::harness::ChatRequest& request) {
        seen.push_back(request);
    };
    apogee::harness::Harness harness{apogee::harness::Config{}};
    harness.register_provider("mock",
                              std::make_shared<apogee::backends::MockProvider>(std::move(options)));
    harness.use_default_router();
    std::vector<apogee::harness::ChatMessage> history{
        apogee::harness::ChatMessage::user("who supplies it?")};
    apogee::agentloop::Options loop;
    loop.model = "mock";
    loop.transient_prefix = expanded.prefix;
    loop.stream_answer = false;
    apogee::agentloop::NullReporter reporter;
    (void)apogee::agentloop::run(harness, history, loop, reporter);
    REQUIRE(seen.size() == 1);
    std::string wire;
    for (const apogee::harness::ChatMessage& message : seen.front().messages) {
        wire += message.content.plain_text();
    }
    CHECK(wire.find("Widget Factory") != std::string::npos);
    std::string persisted;
    for (const apogee::harness::ChatMessage& message : history) {
        persisted += message.content.plain_text();
    }
    CHECK(persisted.find("Widget Factory") == std::string::npos);
    CHECK(persisted.find("Knowledge graph") == std::string::npos);
    CHECK(persisted.find("who supplies it?") != std::string::npos);
}

TEST_CASE(
    "the graph is seeded BEFORE the judge, so a judge that drops every chunk keeps the section",
    "[agentloop][rag][graph][rerank]") {
    const Scratch scratch;
    seed(scratch);
    seed_graph(scratch);
    apogee::harness::Config config;
    apogee::harness::BackendConfig mock;
    mock.type = apogee::harness::BackendType::Mock;
    config.backends.emplace("judge", mock);
    // A verdict that keeps nothing: every retrieved chunk is dropped.
    apogee::backends::MockProvider::Options options;
    options.backend_name = "judge";
    options.turns = {apogee::backends::MockTurn{.text = "[]"}};
    apogee::harness::Harness harness{config};
    harness.register_provider("judge",
                              std::make_shared<apogee::backends::MockProvider>(std::move(options)));
    harness.use_default_router();

    apogee::agentloop::RagTurn turn = turn_for(scratch, "zarquon widgets chunk");
    turn.rerank_flag = "judge";
    turn.harness = &harness;
    turn.config = &config;
    turn.collection = "notes";
    turn.graph_enabled = true;
    const RagResult judged = apogee::agentloop::retrieve_for_turn(turn);
    CHECK(judged.reranked);
    CHECK(judged.chunks == 0);
    CHECK(judged.top_score == 0.0);
    // Both chunks seeded the walk before the judge ran; each entity is a
    // seed, so what remains to inject is the relation between them.
    REQUIRE(judged.prefix.size() == 1);
    const std::string sent = judged.prefix.front().content.plain_text();
    CHECK(sent.find(kSecret) == std::string::npos);
    CHECK(sent.find("knowledge-graph context") != std::string::npos);
    CHECK(sent.find("Zarquon —[is supplied by]→ Widget Factory") != std::string::npos);
}

TEST_CASE("a vector turn seeds the graph from its chunks alone, never from the query's terms",
          "[agentloop][rag][graph][vector]") {
    const Scratch scratch;
    const auto embedder = toy_embedder("toy-v1");
    seed_vectors(scratch, embedder);
    seed_graph(scratch);
    apogee::agentloop::RagTurn turn = turn_for(scratch, "zarquon");
    turn.embedder = embedder;
    turn.limit = 1;  // the secret chunk alone
    turn.collection = "notes";
    turn.graph_enabled = true;
    const RagResult result = apogee::agentloop::retrieve_for_turn(turn);
    CHECK(result.retriever == "vector");
    CHECK(result.chunks == 1);
    // Zarquon is a seed (mentioned by the retrieved chunk) and is not
    // re-listed; the query naming it does not make it a hop-0 entity on a
    // vector turn. The Widget Factory arrives by the edge.
    CHECK(result.graph_entities == 1);
    const std::string sent = result.prefix.front().content.plain_text();
    CHECK(sent.find("Widget Factory (organization)") != std::string::npos);
    CHECK(sent.find("Zarquon (concept)") == std::string::npos);
}

TEST_CASE("a graph the store cannot read is a note, never the reason a turn loses its chunks",
          "[agentloop][rag][graph][failure]") {
    const Scratch scratch;
    seed(scratch);
    apogee::agentloop::RagTurn turn = turn_for(scratch, "zarquon protocol");
    turn.collection = "notes";
    turn.graph_enabled = true;
    // An enabled block over a collection with no graph: nothing related,
    // nothing claimed, no note -- the ordinary quiet case.
    const RagResult empty = apogee::agentloop::retrieve_for_turn(turn);
    CHECK(empty.chunks == 1);
    CHECK(empty.graph_entities == 0);
    CHECK(empty.notes.empty());

    // The entity index replaced out of band by a plain table: the walk
    // throws on its first query. The turn keeps its chunks and says so.
    seed_graph(scratch);
    {
        sqlite3* raw = nullptr;
        REQUIRE(sqlite3_open(scratch.db().string().c_str(), &raw) == SQLITE_OK);
        REQUIRE(sqlite3_exec(raw, "DROP TABLE kg_nodes_fts; CREATE TABLE kg_nodes_fts(x)", nullptr,
                             nullptr, nullptr) == SQLITE_OK);
        sqlite3_close(raw);
    }
    const RagResult broken = apogee::agentloop::retrieve_for_turn(turn);
    CHECK(broken.error.empty());
    CHECK(broken.chunks == 1);
    CHECK(broken.graph_entities == 0);
    CHECK(broken.prefix.front().content.plain_text().find(kSecret) != std::string::npos);
    REQUIRE_FALSE(broken.notes.empty());
    CHECK(broken.notes.back().find("graph expansion failed") != std::string::npos);
}

TEST_CASE("a judge on the turn is reported as applied only when its ranking was used",
          "[agentloop][rag][rerank]") {
    const Scratch scratch;
    seed(scratch);
    apogee::harness::Config config;
    apogee::harness::BackendConfig mock;
    mock.type = apogee::harness::BackendType::Mock;
    config.backends.emplace("judge", mock);

    apogee::backends::MockProvider::Options options;
    options.backend_name = "judge";
    options.turns = {apogee::backends::MockTurn{.text = "[2]"}};
    apogee::harness::Harness harness{config};
    harness.register_provider("judge",
                              std::make_shared<apogee::backends::MockProvider>(std::move(options)));
    harness.use_default_router();

    apogee::agentloop::RagTurn turn = turn_for(scratch, "zarquon widgets chunk");
    turn.rerank_flag = "judge";
    turn.harness = &harness;
    turn.config = &config;
    const RagResult judged = apogee::agentloop::retrieve_for_turn(turn);
    CHECK(judged.reranked);
    CHECK(judged.chunks == 1);

    // A garbage judge: raw order, and the flag says so.
    apogee::backends::MockProvider::Options bad;
    bad.backend_name = "judge";
    bad.turns = {apogee::backends::MockTurn{.text = "no idea"}};
    apogee::harness::Harness harness2{config};
    harness2.register_provider("judge",
                               std::make_shared<apogee::backends::MockProvider>(std::move(bad)));
    harness2.use_default_router();
    turn.harness = &harness2;
    const RagResult raw = apogee::agentloop::retrieve_for_turn(turn);
    CHECK_FALSE(raw.reranked);
    CHECK(raw.chunks > 0);
    REQUIRE_FALSE(raw.notes.empty());
    CHECK(raw.notes.back().find("not a ranking") != std::string::npos);
}
