#include "knowledge/query.h"

#include <optional>
#include <utility>

#include "agentloop/embed_func.h"
#include "agentloop/graph_context.h"
#include "agentloop/rerank.h"
#include "harness/errors.h"

namespace apogee::knowledge {
namespace {

[[nodiscard]] agentloop::EmbedderFacts facts_for(const std::optional<agentloop::Embedder>& e) {
    agentloop::EmbedderFacts facts;
    if (e.has_value()) {
        facts.available = true;
        facts.model = e->model;
        facts.metered = e->metered;
    }
    return facts;
}

[[nodiscard]] agentloop::StoreFacts facts_for(const embedstore::Store& store) {
    agentloop::StoreFacts facts;
    facts.exists = true;
    const embedstore::Store::Stats stats = store.stats();
    facts.chunk_count = stats.chunk_count;
    facts.dimension = stats.dimension;
    facts.lexical_only = stats.lexical_only;
    facts.vector_dims = stats.vector_dims;
    facts.recorded_model = store.embedding_model().model;
    return facts;
}

}  // namespace

std::vector<embedstore::SearchHit> filter_hits(const std::vector<embedstore::SearchHit>& hits,
                                               std::string_view status, std::string_view discipline,
                                               int limit) {
    std::vector<embedstore::SearchHit> kept;
    for (const embedstore::SearchHit& hit : hits) {
        std::string error;
        const std::optional<Record> record =
            record_from_metadata(hit.chunk.source, hit.chunk.metadata, error);
        if (!record.has_value()) {
            continue;
        }
        if (!status.empty() && record->status != status) {
            continue;
        }
        if (!discipline.empty() && record->discipline != discipline) {
            continue;
        }
        kept.push_back(hit);
        if (limit > 0 && kept.size() >= static_cast<std::size_t>(limit)) {
            break;
        }
    }
    return kept;
}

QueryResult query(const Store& store, const harness::Harness& harness,
                  const harness::Config& config, std::string_view collection,
                  const QueryOptions& options) {
    QueryResult out;
    const harness::EmbeddingConfig* entry = config.find_embedding(collection);
    const std::string pin = entry != nullptr ? entry->retriever : std::string{};
    const std::string rerank_pin = entry != nullptr ? entry->rerank : std::string{};
    const std::string collection_backend = entry != nullptr ? entry->backend : std::string{};

    // The embedder that would answer, through the capability probe -- only
    // when the decision could need one.
    std::optional<agentloop::Embedder> embedder;
    std::string reason;
    if (options.retriever_flag != "lexical" && pin != "lexical") {
        embedder = agentloop::resolve_embedder(harness, config, collection_backend, reason);
    }

    // The ONE decision, from the same facts every other surface reads.
    const agentloop::TurnRetrieval decision = agentloop::resolve_turn_retriever(
        options.retriever_flag, pin, facts_for(embedder), facts_for(store.chunks()));
    if (!decision.error.empty()) {
        out.error = decision.error + (reason.empty() ? "" : " (" + reason + ")");
        return out;
    }
    out.retriever = decision.retriever;
    if (!decision.note.empty()) {
        out.notes.push_back(decision.note);
    }
    if (decision.excluded) {
        out.excluded = true;
        return out;
    }

    const agentloop::RerankChoice judge =
        agentloop::resolve_turn_rerank(options.rerank_flag, rerank_pin, config);
    if (!judge.note.empty()) {
        out.notes.push_back(judge.note);
    }
    const int fetch = agentloop::rerank_fetch_limit(options.top_k, !judge.backend.empty());

    // Rank EVERYTHING (limit 0), then filter, then cut.
    std::vector<embedstore::SearchHit> hits;
    try {
        if (decision.retriever == agentloop::Retriever::Lexical) {
            hits = store.chunks().search(options.question, 0);
        } else {
            const std::vector<std::vector<float>> vectors = embedder->embed({options.question}, {});
            if (vectors.empty() || vectors.front().empty()) {
                out.error = "the embedder returned no vector for the question";
                out.backend_error = true;
                return out;
            }
            hits = decision.retriever == agentloop::Retriever::Vector
                       ? store.chunks().search_vector(vectors.front(), 0)
                       : store.chunks().search_hybrid(vectors.front(), options.question, 0);
        }
    } catch (const harness::HarnessError& e) {
        out.error = std::string{"embedding the question failed: "} + e.what();
        out.backend_error = true;
        return out;
    }

    std::vector<embedstore::SearchHit> kept =
        filter_hits(hits, options.status, options.discipline, fetch);

    if (!judge.backend.empty() && !kept.empty()) {
        // The judge sees the same immutable index text the retrievers
        // matched, and `reranked` comes from the same place as the order.
        const agentloop::RerankOutcome judged =
            agentloop::rerank(harness, judge.backend, options.question, kept, options.top_k, {});
        kept = judged.hits;  // cut to top_k on every path, judged or not
        out.reranked = judged.applied;
        if (!judged.note.empty()) {
            out.notes.push_back(judged.note);
        }
    }
    // Without a judge `fetch` IS `top_k`, so the filter already made the cut.

    for (const embedstore::SearchHit& hit : kept) {
        std::string error;
        std::optional<Record> record =
            record_from_metadata(hit.chunk.source, hit.chunk.metadata, error);
        if (!record.has_value()) {
            continue;
        }
        out.records.push_back(ScoredRecord{std::move(*record), hit.score, hit.chunk.id});
    }
    return out;
}

RecordGraph graph_for_records(const Store& store, const harness::Config& config,
                              std::string_view collection, const QueryResult& result,
                              std::string_view question) {
    RecordGraph out;
    // The same decision a `--rag` turn makes: a built named graph covering
    // the collection first, else its own enabled block.
    const agentloop::TurnGraph graph = agentloop::resolve_turn_graph(config, collection);
    if (!graph.enabled) {
        out.note = "no knowledge graph covers collection \"" + std::string{collection} +
                   "\" -- build one with `apogee graph build " + std::string{collection} + "`";
        return out;
    }
    std::vector<embedstore::ChunkRef> seeds;
    for (const ScoredRecord& scored : result.records) {
        if (scored.chunk_id != 0) {
            seeds.push_back(embedstore::ChunkRef{.collection = graph.seed_collection,
                                                 .chunk_id = scored.chunk_id});
        }
    }
    const std::string_view lexical_query =
        result.retriever == agentloop::Retriever::Vector ? std::string_view{} : question;
    try {
        const std::optional<embedstore::Store> named =
            graph.store_path.empty()
                ? std::nullopt
                : std::optional<embedstore::Store>{std::in_place, graph.store_path};
        const agentloop::GraphSection section = agentloop::build_graph_section_labelled(
            named.has_value() ? *named : store.chunks(), graph.name, seeds, lexical_query,
            graph.hops, graph.max_entities);
        out.context = section.text;
        out.entities = section.entities;
    } catch (const std::exception& e) {
        out.note = std::string{"graph expansion failed: "} + e.what();
    }
    return out;
}

}  // namespace apogee::knowledge
