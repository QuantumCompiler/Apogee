#include "agentloop/rag.h"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <system_error>

#include "agentloop/attachments.h"
#include "agentloop/graph_context.h"
#include "agentloop/rerank.h"
#include "embedstore/store.h"

namespace apogee::agentloop {
namespace {

/// What `prefix` costs by `budget`'s count.
[[nodiscard]] std::int64_t prefix_tokens(const TurnBudget& budget,
                                         const std::vector<harness::ChatMessage>& prefix) {
    if (prefix.empty()) {
        return 0;
    }
    harness::ChatRequest request;
    request.messages = prefix;
    return budget.tokens(request).tokens;
}

/// A chat's attachment hits as labelled excerpts, fitted to `share` (26d).
[[nodiscard]] RagResult package_attachments(const RagTurn& turn,
                                            const std::vector<embedstore::SearchHit>& hits,
                                            std::int64_t share, RagResult result) {
    std::vector<AttachmentExcerpt> excerpts = attachment_excerpts(hits);
    if (turn.budget.budget.known()) {
        const auto render = [&](std::size_t count) {
            return render_attachment_excerpts(
                {excerpts.begin(), excerpts.begin() + static_cast<std::ptrdiff_t>(count)});
        };
        const std::size_t fit = fitting_prefix(turn.budget, share, excerpts.size(), render);
        if (fit < excerpts.size()) {
            result.notes.push_back(std::to_string(fit) + " of " + std::to_string(excerpts.size()) +
                                   " excerpts fit the context budget");
            excerpts.resize(fit);
        }
    }
    if (excerpts.empty()) {
        return result;
    }
    result.chunks = static_cast<std::int64_t>(excerpts.size());
    result.top_score = excerpts.front().score;
    result.prefix.push_back(harness::ChatMessage::system(render_attachment_excerpts(excerpts)));
    result.tokens = prefix_tokens(turn.budget, result.prefix);
    return result;
}

}  // namespace

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

std::string render_rag_context(const std::vector<std::string>& chunks,
                               std::string_view graph_section) {
    if (graph_section.empty()) {
        return render_rag_context(chunks);
    }
    if (chunks.empty()) {
        return "The following knowledge-graph context was retrieved from the user's documents "
               "and may be relevant. Use it if it helps; ignore it if it does not, and do not "
               "mention it unless it informed your answer.\n\n" +
               std::string{graph_section} + "\n";
    }
    return render_rag_context(chunks) + "\n" + std::string{graph_section} + "\n";
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
        judge =
            resolve_turn_rerank(turn.rerank_flag, turn.rerank_pin, *turn.config, turn.conversation);
        if (!judge.note.empty()) {
            result.notes.push_back(judge.note);
        }
    }
    // Inlined attachments are left out after the search, so it reaches past
    // them: they would otherwise take the places of excerpts that are not
    // already in front of the model.
    const int fetch =
        rerank_fetch_limit(turn.limit, !judge.backend.empty()) +
        (turn.exclude_sources.empty() ? 0 : static_cast<int>(turn.exclude_sources.size()) * 8);

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

    std::erase_if(hits, [&](const embedstore::SearchHit& hit) {
        return turn.exclude_sources.contains(hit.chunk.source);
    });

    // --- the graph, seeded BEFORE the judge -----------------------------------
    //
    // The seeds are the retrieval-ordered top-k, captured here so graph
    // context survives a judge that drops every chunk. Best-effort: a
    // failure is a note, never the reason the turn loses its chunks.
    std::string graph_section;
    if (turn.graph_enabled) {
        std::vector<embedstore::ChunkRef> seeds;
        for (const embedstore::SearchHit& hit : hits) {
            if (turn.limit > 0 && seeds.size() >= static_cast<std::size_t>(turn.limit)) {
                break;
            }
            seeds.push_back(embedstore::ChunkRef{.collection = turn.graph_seed_collection,
                                                 .chunk_id = hit.chunk.id});
        }
        // A lexical (or degraded, or hybrid) turn also seeds by the query's
        // own terms through the entity index; a vector turn does not.
        // Both arms as views: `"" : turn.question` would pick std::string as
        // the common type, copy the question into a temporary, and leave
        // this view dangling on freed memory -- which is exactly what
        // happened on Linux, where the allocator reuses it at once.
        const std::string_view lexical_query =
            result.retriever == "vector" ? std::string_view{} : std::string_view{turn.question};
        const std::string header = turn.graph_name.empty() ? turn.collection : turn.graph_name;
        try {
            // The collection's own graph lives beside its chunks; a named
            // graph's in its own database, opened for this walk alone.
            const std::optional<embedstore::Store> named =
                turn.graph_store_path.empty()
                    ? std::nullopt
                    : std::optional<embedstore::Store>{std::in_place, turn.graph_store_path};
            const GraphSection section = build_graph_section_labelled(
                named.has_value() ? *named : *store, header, seeds, lexical_query, turn.graph_hops,
                turn.graph_max_entities);
            graph_section = section.text;
            result.graph_entities = section.entities;
        } catch (const std::exception& e) {
            result.notes.push_back(std::string{"graph expansion failed ("} + e.what() +
                                   ") -- the chunks stand alone");
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

    if (turn.attachments && !turn.moments.empty()) {
        // A moment the question names is looked up by its time, not searched
        // for by its digits, and goes first: just above the best the search
        // found, so the score shown stays the search's.
        double top = 0.0;
        for (const embedstore::SearchHit& hit : hits) {
            top = std::max(top, hit.score);
        }
        const std::vector<embedstore::SearchHit> at =
            hits_at(*store, turn.moments, turn.exclude_sources, top + 1e-6);
        std::erase_if(hits, [&](const embedstore::SearchHit& hit) {
            return std::ranges::any_of(at, [&](const embedstore::SearchHit& pinned) {
                return pinned.chunk.id == hit.chunk.id;
            });
        });
        hits.insert(hits.begin(), at.begin(), at.end());
    }

    if (hits.empty() && graph_section.empty()) {
        return result;
    }
    const std::int64_t share = std::max<std::int64_t>(
        turn.budget.budget.share(BudgetSource::Retrieval) - turn.share_used, 0);
    if (turn.attachments) {
        return package_attachments(turn, hits, share, std::move(result));
    }
    std::vector<std::string> texts;
    texts.reserve(hits.size());
    for (const embedstore::SearchHit& hit : hits) {
        texts.push_back(hit.chunk.text);
    }

    // --- the budget: the leading chunks that fit the retrieval share ----------
    if (turn.budget.budget.known()) {
        const auto render = [&](std::size_t count) {
            const std::vector<std::string> leading{
                texts.begin(), texts.begin() + static_cast<std::ptrdiff_t>(count)};
            return render_rag_context(leading, graph_section);
        };
        const std::size_t fit = fitting_prefix(turn.budget, share, texts.size(), render);
        if (fit < texts.size()) {
            result.notes.push_back(std::to_string(fit) + " of " + std::to_string(texts.size()) +
                                   " chunks fit the context budget");
            texts.resize(fit);
            hits.resize(fit);
        }
        if (fit == 0 && !graph_section.empty() &&
            !fitting_prefix(turn.budget, share, 1,
                            [&](std::size_t) { return render_rag_context({}, graph_section); })) {
            result.notes.push_back("the graph context did not fit the context budget either");
            graph_section.clear();
            result.graph_entities = 0;
        }
        if (texts.empty() && graph_section.empty()) {
            return result;
        }
    }
    result.chunks = static_cast<std::int64_t>(hits.size());
    result.top_score = hits.empty() ? 0.0 : hits.front().score;
    // The retriever that RAN, which on a hybrid turn is hybrid and its scores
    // RRF -- small numbers are normal there, and the label says which scale.
    // The graph section rides after the chunk list, in the same transient
    // message, so it augments them and never crowds them out.
    result.prefix.push_back(harness::ChatMessage::system(render_rag_context(texts, graph_section)));
    result.tokens = prefix_tokens(turn.budget, result.prefix);
    return result;
}

}  // namespace apogee::agentloop
