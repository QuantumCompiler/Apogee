#include "agentloop/rag.h"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <system_error>

#include "agentloop/rerank.h"
#include "embedstore/store.h"

namespace apogee::agentloop {

std::string render_rag_context(const std::vector<std::string>& chunks) {
    if (chunks.empty()) {
        return {};
    }

    // Framed as retrieved excerpts, explicitly. Without that framing a model
    // reads an injected document as something the user said and answers about
    // the document rather than about the question -- and the more relevant the
    // retrieval, the more confidently it does so.
    std::string out =
        "The following excerpts were retrieved from the user's documents and may be relevant. "
        "Use them if they help; ignore them if they do not, and do not mention them unless they "
        "informed your answer.\n";

    for (std::size_t index = 0; index < chunks.size(); ++index) {
        out += "\n--- excerpt " + std::to_string(index + 1) + " ---\n";
        out += chunks[index];
        out += "\n";
    }
    return out;
}

RagResult build_rag_prefix(const std::filesystem::path& store_path, const std::string& question,
                           int limit) {
    RagResult result;

    std::error_code code;
    if (!std::filesystem::exists(store_path, code)) {
        // Not an error that should stop the turn: answering without retrieved
        // context is far better than refusing to answer.
        result.error = "no collection at " + store_path.string();
        return result;
    }

    try {
        const embedstore::Store store{store_path};
        const std::vector<embedstore::SearchHit> hits = store.search(question, limit);
        if (hits.empty()) {
            // A normal outcome. A corpus that has nothing to say about this
            // question is not a fault.
            result.retriever = "lexical";
            return result;
        }

        std::vector<std::string> texts;
        texts.reserve(hits.size());
        for (const embedstore::SearchHit& hit : hits) {
            texts.push_back(hit.chunk.text);
        }

        result.chunks = static_cast<std::int64_t>(hits.size());
        result.top_score = hits.front().score;
        result.retriever = hits.front().retriever;
        result.prefix.push_back(harness::ChatMessage::system(render_rag_context(texts)));
    } catch (const std::exception& e) {
        result.prefix.clear();
        result.chunks = 0;
        result.error = e.what();
    }
    return result;
}

RagResult retrieve_for_turn(const RagTurn& turn) {
    RagResult result;

    std::error_code code;
    const bool exists = std::filesystem::exists(turn.store_path, code);

    // --- facts, then ONE decision ----------------------------------------------
    EmbedderFacts embedder;
    if (turn.embedder.has_value()) {
        embedder.available = true;
        embedder.model = turn.embedder->model;
        embedder.metered = turn.embedder->metered;
    }
    StoreFacts facts;
    facts.exists = exists;
    std::optional<embedstore::Store> store;
    if (exists) {
        try {
            store.emplace(turn.store_path);
            const embedstore::Store::Stats stats = store->stats();
            facts.chunk_count = stats.chunk_count;
            facts.dimension = stats.dimension;
            facts.lexical_only = stats.lexical_only;
            facts.vector_dims = stats.vector_dims;
            facts.recorded_model = store->embedding_model().model;
        } catch (const std::exception& e) {
            result.error = e.what();
            return result;
        }
    }

    const TurnRetrieval decision =
        resolve_turn_retriever(turn.retriever_flag, turn.retriever_pin, embedder, facts);
    if (!decision.error.empty()) {
        result.error = decision.error;
        return result;
    }
    result.retriever = std::string{to_string(decision.retriever)};
    if (!decision.note.empty()) {
        result.notes.push_back(decision.note);
    }
    if (decision.excluded) {
        // Pinned to vector, unqualified: searched by nothing. The note says so.
        return result;
    }
    if (!exists) {
        result.error = "no collection at " + turn.store_path.string();
        return result;
    }

    // --- the judge, resolved once too ---------------------------------------------
    RerankChoice judge;
    if (turn.config != nullptr && turn.harness != nullptr) {
        judge = resolve_turn_rerank(turn.rerank_flag, turn.rerank_pin, *turn.config);
        if (!judge.note.empty()) {
            result.notes.push_back(judge.note);
        }
    }
    const int fetch = rerank_fetch_limit(turn.limit, !judge.backend.empty());

    // --- run exactly what was decided ---------------------------------------------
    std::vector<embedstore::SearchHit> hits;
    try {
        if (decision.retriever == Retriever::Lexical) {
            hits = store->search(turn.question, fetch);
        } else {
            const std::vector<std::vector<float>> vectors =
                turn.embedder->embed({turn.question}, turn.cancellation);
            if (vectors.size() != 1) {
                throw std::runtime_error("the embedder returned no vector for the question");
            }
            hits = decision.retriever == Retriever::Vector
                       ? store->search_vector(vectors.front(), fetch)
                       : store->search_hybrid(vectors.front(), turn.question, fetch);
        }
    } catch (const std::exception& e) {
        if (decision.retriever == Retriever::Lexical) {
            result.error = e.what();
            return result;
        }
        // A transient embedding failure degrades to the lexical half and is
        // REPORTED as lexical: honest about the scale the scores are on.
        result.retriever = "lexical";
        result.notes.push_back(std::string{"embedding the question failed ("} + e.what() +
                               ") -- searched lexical only");
        try {
            hits = store->search(turn.question, fetch);
        } catch (const std::exception& inner) {
            result.error = inner.what();
            return result;
        }
    }

    if (!judge.backend.empty()) {
        const RerankOutcome judged = rerank(*turn.harness, judge.backend, turn.question, hits,
                                            turn.limit, turn.cancellation);
        hits = judged.hits;
        result.reranked = judged.applied;
        if (!judged.note.empty()) {
            result.notes.push_back(judged.note);
        }
    } else if (turn.limit > 0 && static_cast<std::size_t>(turn.limit) < hits.size()) {
        hits.resize(static_cast<std::size_t>(turn.limit));
    }

    if (hits.empty()) {
        return result;
    }
    std::vector<std::string> texts;
    texts.reserve(hits.size());
    for (const embedstore::SearchHit& hit : hits) {
        texts.push_back(hit.chunk.text);
    }
    result.chunks = static_cast<std::int64_t>(hits.size());
    result.top_score = hits.front().score;
    // The retriever that RAN, which on a hybrid turn is hybrid and its scores
    // RRF -- small numbers are normal there, and the label says which scale.
    result.prefix.push_back(harness::ChatMessage::system(render_rag_context(texts)));
    return result;
}

}  // namespace apogee::agentloop
