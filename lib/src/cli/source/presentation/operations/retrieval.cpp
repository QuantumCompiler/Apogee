#include "operations/retrieval.h"

#include "agentloop/embed_func.h"
#include "agentloop/graph_context.h"
#include "operations/collections.h"

namespace apogee::commands {

agentloop::RagResult retrieve_for_collection(
    const harness::Harness& harness, const harness::Config& config, std::string_view collection,
    const std::string& question, int limit, std::string_view retriever_flag,
    std::string_view rerank_flag, const harness::CancellationToken& cancellation,
    std::string_view conversation, const agentloop::TurnBudget& budget, std::int64_t share_used,
    const agentloop::SideCallSink& on_side_call) {
    agentloop::RagTurn turn;
    // Its embedder and judge said where the surface says them (26n).
    turn.on_side_call = on_side_call;
    turn.conversation = std::string{conversation};
    turn.budget = budget;
    turn.share_used = share_used;
    turn.store_path = collection_path(collection);
    turn.question = question;
    turn.limit = limit;
    turn.retriever_flag = std::string{retriever_flag};
    turn.rerank_flag = std::string{rerank_flag};
    turn.collection = std::string{collection};
    std::string collection_backend;
    if (const harness::EmbeddingConfig* pin = config.find_embedding(collection); pin != nullptr) {
        turn.retriever_pin = pin->retriever;
        turn.rerank_pin = pin->rerank;
        collection_backend = pin->backend;
    }
    // Which graph the turn walks -- a built named graph covering the
    // collection first, else its own enabled block -- decided in ONE place
    // for every surface, so the precedence rule cannot drift between them.
    const agentloop::TurnGraph graph = agentloop::resolve_turn_graph(config, collection);
    turn.graph_enabled = graph.enabled;
    turn.graph_hops = graph.hops;
    turn.graph_max_entities = graph.max_entities;
    turn.graph_store_path = graph.store_path;
    turn.graph_name = graph.name;
    turn.graph_seed_collection = graph.seed_collection;
    turn.embedder =
        agentloop::resolve_embedder(harness, config, collection_backend, turn.embedder_reason);
    turn.harness = &harness;
    turn.config = &config;
    turn.cancellation = cancellation;
    return agentloop::retrieve_for_turn(turn);
}

RagChoice choose_rag_collection(bool flag_given, std::string_view flag_value,
                                std::string_view auto_rag) {
    RagChoice choice;
    if (flag_given) {
        // Present wins, even when empty: `--rag ""` is the off switch.
        choice.collection = std::string{flag_value};
        choice.source = flag_value.empty() ? RagSource::None : RagSource::Flag;
        return choice;
    }
    if (!auto_rag.empty()) {
        choice.collection = std::string{auto_rag};
        choice.source = RagSource::Config;
    }
    return choice;
}

namespace {

/// `0.032 [hybrid, reranked]`, then what the band was read by: the raw score
/// and its retriever always ride beside the word (26s).
std::string raw_of(const agentloop::RagResult& result, double score) {
    std::string out = std::to_string(score).substr(0, 5) + " [" + result.retriever +
                      (result.reranked ? ", reranked" : "") + "]";
    if (!result.strength.measure.empty()) {
        out += ", " + result.strength.measure;
    }
    return out;
}

/// `, strong match (0.032 [hybrid], 98% of RRF's ceiling)`, or the raw facts
/// alone when there was nothing to read a band by.
std::string strength_of(const agentloop::RagResult& result) {
    const std::string raw = raw_of(result, result.best_score);
    return result.strength.band.empty() ? " (" + raw + ")"
                                        : ", " + result.strength.band + " match (" + raw + ")";
}

}  // namespace

std::string describe_attachment_retrieval(const agentloop::RagResult& result) {
    std::string notes;
    for (const std::string& note : result.notes) {
        notes += " -- " + note;
    }
    if (!result.error.empty()) {
        return "attachments not searched -- " + result.error + notes;
    }
    if (result.strength.floored) {
        // Said, never a quiet absence (26s).
        return "nothing relevant in the attachments -- the best match is under the floor (" +
               raw_of(result, result.best_score) + ")" + notes;
    }
    if (result.chunks == 0) {
        return "nothing in the attachments matched [" + result.retriever + "]" + notes;
    }
    return std::to_string(result.chunks) + (result.chunks == 1 ? " excerpt" : " excerpts") +
           " from the attachments" + strength_of(result) + notes;
}

std::string describe_retrieval(const RagChoice& choice, const agentloop::RagResult& result) {
    const std::string origin = choice.source == RagSource::Config ? " (auto_rag)" : "";
    std::string notes;
    for (const std::string& note : result.notes) {
        notes += " -- " + note;
    }
    if (!result.error.empty()) {
        return "retrieval unavailable -- " + result.error + origin + notes;
    }
    // The graph's contribution, always named: entities injected beside the
    // chunks are context the user did not see retrieved.
    const std::string graph = result.graph_entities > 0
                                  ? " +" + std::to_string(result.graph_entities) + " graph entities"
                                  : "";
    if (result.strength.floored) {
        return "nothing relevant in '" + choice.collection +
               "' -- the best match is under the floor (" + raw_of(result, result.best_score) +
               ")" + origin + notes;
    }
    if (result.chunks == 0) {
        return "no matching context in '" + choice.collection + "' [" + result.retriever + "]" +
               graph + origin + notes;
    }
    // Chunks, how strong a match, and beside it the raw score and the
    // retriever that produced it -- lexical, vector and RRF scales are
    // incomparable (26s) -- and whether a judge's ranking was actually
    // applied, from the same place as the ranking.
    std::string line = std::to_string(result.chunks) + " chunk(s) from '" + choice.collection +
                       "'" + strength_of(result) + graph + origin;
    for (const std::string& note : result.notes) {
        line += " -- " + note;
    }
    return line;
}

}  // namespace apogee::commands
