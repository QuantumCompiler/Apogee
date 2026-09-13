#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

#include "agentloop/embed_func.h"
#include "agentloop/retriever.h"
#include "backends/mock.h"
#include "backends/openai.h"
#include "harness/config.h"
#include "harness/harness.h"
#include "support/fake_transport.h"

/// The resolution rules, as the exhaustive table Ommi's were encoded in. Every
/// row is one turn: what the user said (flag, pin), what exists (embedder,
/// store), and exactly what must run, be reported, or be refused.
namespace {

using apogee::agentloop::EmbedderFacts;
using apogee::agentloop::IngestRetrieval;
using apogee::agentloop::resolve_ingest_retriever;
using apogee::agentloop::resolve_turn_retriever;
using apogee::agentloop::Retriever;
using apogee::agentloop::StoreFacts;
using apogee::agentloop::TurnRetrieval;
using apogee::agentloop::valid_retriever;

EmbedderFacts none() {
    return {};
}

EmbedderFacts local() {
    return {.available = true, .model = "local-embed", .metered = false};
}

EmbedderFacts cloud() {
    return {.available = true, .model = "text-embedding-3-small", .metered = true};
}

StoreFacts empty_store() {
    return {.exists = true};
}

StoreFacts lexical_store() {
    return {.exists = true, .chunk_count = 10};
}

/// Fully vectorised under `model`, one space.
StoreFacts vector_store(std::string model) {
    return {.exists = true,
            .chunk_count = 10,
            .recorded_model = std::move(model),
            .dimension = 4,
            .lexical_only = 0,
            .vector_dims = 1};
}

StoreFacts partial_store(std::string model) {
    StoreFacts facts = vector_store(std::move(model));
    facts.lexical_only = 3;
    return facts;
}

StoreFacts mixed_store(std::string model) {
    StoreFacts facts = vector_store(std::move(model));
    facts.vector_dims = 2;
    return facts;
}

StoreFacts unrecorded_store() {
    StoreFacts facts = vector_store("");
    return facts;
}

}  // namespace

TEST_CASE("the validator accepts exactly the four spellings and empty", "[agentloop][retrieval]") {
    for (const std::string_view ok : {"", "auto", "lexical", "vector", "hybrid"}) {
        INFO(ok);
        CHECK(valid_retriever(ok));
    }
    for (const std::string_view bad : {"vectors", "Lexical", "bm25", "hybird", " "}) {
        INFO(bad);
        CHECK_FALSE(valid_retriever(bad));
    }
    CHECK(apogee::agentloop::retriever_values_message("retriever", "hybird").find("hybird") !=
          std::string::npos);
}

TEST_CASE("auto: vector only when everything qualifies, else lexical with the reason",
          "[agentloop][retrieval][auto]") {
    struct Row {
        const char* name;
        EmbedderFacts embedder;
        StoreFacts store;
        Retriever expect;
        bool note_mentions_reingest;
    };

    const Row rows[] = {
        {"no embedder, lexical store", none(), lexical_store(), Retriever::Lexical, false},
        {"no embedder, vector store", none(), vector_store("m"), Retriever::Lexical, false},
        {"local, lexical store", local(), lexical_store(), Retriever::Lexical, false},
        {"local, matching vectors", local(), vector_store("local-embed"), Retriever::Vector, false},
        {"local, other model's vectors", local(), vector_store("someone-else"), Retriever::Lexical,
         true},
        {"local, unrecorded vectors", local(), unrecorded_store(), Retriever::Lexical, true},
        {"local, partial coverage", local(), partial_store("local-embed"), Retriever::Lexical,
         true},
        {"local, mixed widths", local(), mixed_store("local-embed"), Retriever::Lexical, true},
        // The spend rule allows the per-question call once the store was built
        // with that paid model: questions do not need a yes.
        {"cloud, matching vectors", cloud(), vector_store("text-embedding-3-small"),
         Retriever::Vector, false},
        {"cloud, empty store", cloud(), empty_store(), Retriever::Lexical, false},
    };
    for (const Row& row : rows) {
        INFO(row.name);
        const TurnRetrieval got = resolve_turn_retriever("", "", row.embedder, row.store);
        CHECK(got.error.empty());
        CHECK_FALSE(got.excluded);
        CHECK(got.retriever == row.expect);
        CHECK((got.note.find("re-run") != std::string::npos) == row.note_mentions_reingest);
    }
}

TEST_CASE("auto never resolves to hybrid", "[agentloop][retrieval][auto]") {
    for (const StoreFacts& store :
         {lexical_store(), vector_store("local-embed"), partial_store("local-embed")}) {
        CHECK(resolve_turn_retriever("", "", local(), store).retriever != Retriever::Hybrid);
        CHECK(resolve_turn_retriever("", "auto", local(), store).retriever != Retriever::Hybrid);
    }
}

TEST_CASE("a partially vectorised store is demoted wholesale, never searched as a fraction",
          "[agentloop][retrieval][auto]") {
    // The dim=0 rule: three chunks without vectors would be invisible to a
    // vector search, so the whole turn runs lexical and says why.
    const TurnRetrieval got = resolve_turn_retriever("", "", local(), partial_store("local-embed"));
    CHECK(got.retriever == Retriever::Lexical);
    CHECK(got.note.find("3 of 10") != std::string::npos);
}

TEST_CASE("an explicit vector flag with nothing to run it against is a hard error naming lexical",
          "[agentloop][retrieval][explicit]") {
    for (const auto& [embedder, store] : std::vector<std::pair<EmbedderFacts, StoreFacts>>{
             {none(), vector_store("m")},
             {local(), lexical_store()},
             {local(), vector_store("someone-else")},
             {local(), partial_store("local-embed")},
         }) {
        const TurnRetrieval got = resolve_turn_retriever("vector", "", embedder, store);
        CHECK_FALSE(got.error.empty());
        CHECK(got.error.find("--retriever lexical") != std::string::npos);
    }
    // And when it CAN run, it runs, with no note.
    const TurnRetrieval ok =
        resolve_turn_retriever("vector", "", local(), vector_store("local-embed"));
    CHECK(ok.error.empty());
    CHECK(ok.retriever == Retriever::Vector);
    CHECK(ok.note.empty());
}

TEST_CASE("a vector PIN that does not qualify excludes the collection with a note, never lexical",
          "[agentloop][retrieval][pin]") {
    // The same facts as the flag case, but from the collection's own pin: not
    // an error (the user typed nothing), not a silent lexical search (the pin
    // said vector), but excluded and explained.
    const TurnRetrieval got = resolve_turn_retriever("", "vector", local(), lexical_store());
    CHECK(got.error.empty());
    CHECK(got.excluded);
    CHECK(got.note.find("pinned to vector") != std::string::npos);
    CHECK(got.note.find("not searched") != std::string::npos);

    const TurnRetrieval qualified =
        resolve_turn_retriever("", "vector", local(), vector_store("local-embed"));
    CHECK_FALSE(qualified.excluded);
    CHECK(qualified.retriever == Retriever::Vector);
}

TEST_CASE("the flag beats the pin, both ways", "[agentloop][retrieval][precedence]") {
    CHECK(resolve_turn_retriever("lexical", "vector", local(), vector_store("local-embed"))
              .retriever == Retriever::Lexical);
    CHECK(resolve_turn_retriever("vector", "lexical", local(), vector_store("local-embed"))
              .retriever == Retriever::Vector);
    // `auto` in the flag defers to the pin.
    CHECK(
        resolve_turn_retriever("auto", "lexical", local(), vector_store("local-embed")).retriever ==
        Retriever::Lexical);
}

TEST_CASE("hybrid runs when the vector half qualifies and is REPORTED lexical when it does not",
          "[agentloop][retrieval][hybrid]") {
    const TurnRetrieval both =
        resolve_turn_retriever("hybrid", "", local(), vector_store("local-embed"));
    CHECK(both.retriever == Retriever::Hybrid);
    CHECK(both.note.empty());

    // Honest degradation: never partial, never an error.
    for (const auto& [embedder, store] : std::vector<std::pair<EmbedderFacts, StoreFacts>>{
             {none(), vector_store("m")},
             {local(), lexical_store()},
             {local(), vector_store("someone-else")},
             {local(), partial_store("local-embed")},
         }) {
        const TurnRetrieval got = resolve_turn_retriever("hybrid", "", embedder, store);
        CHECK(got.error.empty());
        CHECK_FALSE(got.excluded);
        CHECK(got.retriever == Retriever::Lexical);
        CHECK(got.note.find("hybrid requested") != std::string::npos);
        CHECK(got.note.find("lexical only") != std::string::npos);
    }
    // A hybrid pin behaves like the flag.
    CHECK(resolve_turn_retriever("", "hybrid", local(), vector_store("local-embed")).retriever ==
          Retriever::Hybrid);
}

TEST_CASE("ingest: a metered embedder needs the explicit ask, a local one does not",
          "[agentloop][retrieval][ingest][spend]") {
    // The user's rule, decided 2026-09-13: a whole corpus is never pushed
    // through a paid embedder on Apogee's own initiative.
    const IngestRetrieval paid_auto = resolve_ingest_retriever("", "", cloud());
    CHECK(paid_auto.retriever == Retriever::Lexical);
    CHECK(paid_auto.note.find("paid") != std::string::npos);
    CHECK(paid_auto.note.find("--retriever vector") != std::string::npos);

    const IngestRetrieval paid_asked = resolve_ingest_retriever("vector", "", cloud());
    CHECK(paid_asked.retriever == Retriever::Vector);
    CHECK(paid_asked.note.empty());

    const IngestRetrieval paid_pinned = resolve_ingest_retriever("", "vector", cloud());
    CHECK(paid_pinned.retriever == Retriever::Vector);

    const IngestRetrieval free_auto = resolve_ingest_retriever("", "", local());
    CHECK(free_auto.retriever == Retriever::Vector);
    CHECK(free_auto.note.empty());

    CHECK(resolve_ingest_retriever("", "", none()).retriever == Retriever::Lexical);
    CHECK(resolve_ingest_retriever("lexical", "", local()).retriever == Retriever::Lexical);
}

TEST_CASE(
    "ingest: an explicit vector ask with no embedder is a hard error; hybrid resolves like auto",
    "[agentloop][retrieval][ingest]") {
    const IngestRetrieval got = resolve_ingest_retriever("vector", "", none());
    CHECK_FALSE(got.error.empty());
    CHECK(got.error.find("--retriever lexical") != std::string::npos);
    // Fusion is a query concern; at write time hybrid means "whatever auto says".
    CHECK(resolve_ingest_retriever("hybrid", "", local()).retriever == Retriever::Vector);
    CHECK(resolve_ingest_retriever("hybrid", "", cloud()).retriever == Retriever::Lexical);
    CHECK(resolve_ingest_retriever("hybrid", "", none()).retriever == Retriever::Lexical);
}

TEST_CASE("the embedder is resolved through the role chain and reports model and metering",
          "[agentloop][retrieval][embedder]") {
    // The three facts the policy needs come from the provider the harness
    // routes to -- never from a type. A mock embedder says it is free; an
    // OpenAI entry says it is metered; a chat-only backend resolves to nothing.
    apogee::harness::Config config;
    apogee::harness::BackendConfig mock;
    mock.type = apogee::harness::BackendType::Mock;
    config.backends.emplace("free", mock);
    config.backends.emplace("paid", mock);
    config.backends.emplace("chat", mock);
    config.models.default_embedding = "free";

    apogee::harness::Harness harness{config};
    auto free_embedder = std::make_shared<apogee::backends::MockEmbeddingProvider>("free");
    free_embedder->set_model_name("mock-embed-a");
    harness.register_provider("free", free_embedder);
    apogee::backends::OpenAIProvider::Options options;
    options.backend_name = "paid";
    options.api_key = "sk-test";
    auto client =
        std::make_unique<apogee::backends::HttpClient>(apogee::testing::FakeTransport::ok("{}"));
    harness.register_provider("paid", std::make_shared<apogee::backends::OpenAIProvider>(
                                          std::move(options), std::move(client)));
    apogee::backends::MockProvider::Options chat_options;
    chat_options.backend_name = "chat";
    harness.register_provider(
        "chat", std::make_shared<apogee::backends::MockProvider>(std::move(chat_options)));
    harness.use_default_router();

    std::string reason;
    // The role pointer.
    const auto by_role = apogee::agentloop::resolve_embedder(harness, config, "", reason);
    REQUIRE(by_role.has_value());
    CHECK(by_role->backend == "free");
    CHECK(by_role->model == "mock-embed-a");
    CHECK_FALSE(by_role->metered);
    CHECK(by_role->dimensions == 8);
    CHECK(by_role->embed({"x"}, {}).size() == 1);

    // The collection's own pin beats the role pointer, and a cloud embedder
    // says it costs money.
    const auto pinned = apogee::agentloop::resolve_embedder(harness, config, "paid", reason);
    REQUIRE(pinned.has_value());
    CHECK(pinned->backend == "paid");
    CHECK(pinned->model == "text-embedding-3-small");
    CHECK(pinned->metered);

    // A backend that cannot embed resolves to nothing, with the reason.
    CHECK_FALSE(apogee::agentloop::resolve_embedder(harness, config, "chat", reason).has_value());
    CHECK(reason.find("cannot embed") != std::string::npos);
}
