#include "graph/build.h"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

#include "agentloop/embed_func.h"
#include "agentloop/retriever.h"

namespace apogee::graph {
namespace {

[[nodiscard]] std::string trim(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\n' ||
                                   text[begin] == '\r' || text[begin] == '\t')) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\n' || text[end - 1] == '\r' ||
                           text[end - 1] == '\t')) {
        --end;
    }
    return std::string{text.substr(begin, end - begin)};
}

/// The first `limit` codepoints of `text`, with an ellipsis when it was cut.
[[nodiscard]] std::string clip_codepoints(const std::string& text, std::size_t limit) {
    std::size_t codepoints = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        std::size_t length = 1;
        if ((lead & 0xE0U) == 0xC0U) {
            length = 2;
        } else if ((lead & 0xF0U) == 0xE0U) {
            length = 3;
        } else if ((lead & 0xF8U) == 0xF0U) {
            length = 4;
        }
        if (codepoints + 1 >= limit && i + length < text.size()) {
            return trim(text.substr(0, i)) + "…";
        }
        i += length;
        ++codepoints;
    }
    return text;
}

/// A record chunk's decision node, keyed by chunk id, and its lineage.
struct RecordRef {
    std::int64_t node_id = 0;
    std::string supersedes;
};

/// The build's deterministic record pass. Returns the decision node id per
/// record chunk so extraction can hang `concerns` edges off it. In a dry run
/// it only counts.
std::map<std::int64_t, std::int64_t> materialize_records(embedstore::Store& store,
                                                         std::set<std::int64_t>& mutated,
                                                         BuildResult& out, bool dry_run) {
    std::map<std::string, RecordRef> by_record;
    std::map<std::int64_t, std::int64_t> by_chunk;
    for (const embedstore::Chunk& chunk : store.chunks_with_metadata()) {
        std::string error;
        const std::optional<knowledge::Record> record =
            knowledge::record_from_metadata(chunk.source, chunk.metadata, error);
        if (!record.has_value()) {
            continue;  // metadata that is not a knowledge record -- not ours
        }
        ++out.record_nodes;
        if (dry_run) {
            continue;
        }
        const embedstore::UpsertResult upsert = store.upsert_decision_node(
            record->id, decision_node_description(*record),
            embedstore::decision_node_metadata_json(record->status, record->discipline));
        if (upsert.mutated) {
            mutated.insert(upsert.id);
            ++out.nodes_upserted;
        }
        (void)store.add_mention(upsert.id, chunk.id);
        by_record[record->id] = RecordRef{.node_id = upsert.id, .supersedes = record->supersedes};
        by_chunk[chunk.id] = upsert.id;
    }
    if (dry_run) {
        return by_chunk;
    }
    // `supersedes` edges once every record node exists, in id order so the
    // insertion order is deterministic.
    for (const auto& [id, ref] : by_record) {
        if (ref.supersedes.empty()) {
            continue;
        }
        const auto target = by_record.find(ref.supersedes);
        if (target == by_record.end()) {
            ++out.supersedes_skipped;
            continue;
        }
        if (store.ensure_edge(ref.node_id, target->second.node_id, kRelationSupersedes, "")) {
            ++out.edges_upserted;
        }
        ++out.supersedes_edges;
    }
    return by_chunk;
}

/// Commits one chunk's normalised extraction: entities upserted, mentions
/// linked, relations resolved by normalised name against this chunk's own
/// entities (when two same-named entities of different types survive, the
/// first listed wins the name), and -- for a record chunk -- a `concerns`
/// edge from its decision node to every surviving entity.
void store_extraction(embedstore::Store& store, std::int64_t chunk_id, std::int64_t decision_id,
                      const ExtractResult& result, std::set<std::int64_t>& mutated,
                      BuildResult& out) {
    std::map<std::string, std::int64_t> by_name;
    for (const Entity& entity : result.entities) {
        const embedstore::UpsertResult upsert =
            store.upsert_node(entity.name, entity.type, entity.description);
        if (upsert.mutated) {
            mutated.insert(upsert.id);
            ++out.nodes_upserted;
        }
        by_name.try_emplace(embedstore::normalize_entity_name(entity.name), upsert.id);
        if (store.add_mention(upsert.id, chunk_id)) {
            ++out.mentions_added;
        }
        if (decision_id != 0 && upsert.id != decision_id &&
            store.ensure_edge(decision_id, upsert.id, kRelationConcerns, "")) {
            ++out.edges_upserted;
        }
    }
    for (const Relation& relation : result.relations) {
        const auto source = by_name.find(embedstore::normalize_entity_name(relation.source));
        const auto target = by_name.find(embedstore::normalize_entity_name(relation.target));
        if (source == by_name.end() || target == by_name.end()) {
            continue;  // unreachable after normalize; guarded anyway
        }
        store.upsert_edge(source->second, target->second, relation.relation, relation.description);
        ++out.edges_upserted;
    }
}

}  // namespace

std::string decision_node_description(const knowledge::Record& record) {
    std::string text = trim(record.decision);
    const std::string intent = trim(record.intent);
    if (!intent.empty()) {
        if (!text.empty()) {
            text += " — ";
        }
        text += intent;
    }
    return clip_codepoints(text, kMaxDecisionNodeDescriptionLen);
}

EntityEmbedder resolve_entity_embedder(const harness::Harness& harness,
                                       const harness::Config& config,
                                       std::string_view collection_backend,
                                       std::string_view retriever_pin) {
    EntityEmbedder out;
    std::string reason;
    const std::optional<agentloop::Embedder> embedder =
        agentloop::resolve_embedder(harness, config, collection_backend, reason);
    agentloop::EmbedderFacts facts;
    if (embedder.has_value()) {
        facts.available = true;
        facts.model = embedder->model;
        facts.metered = embedder->metered;
    }
    // The ingest decision, because that is what this is: a whole graph's
    // entities pushed through an embedder on Apogee's initiative.
    const agentloop::IngestRetrieval decision =
        agentloop::resolve_ingest_retriever("", retriever_pin, facts);
    if (decision.retriever == agentloop::Retriever::Vector && embedder.has_value()) {
        out.model = embedder->model;
        out.embed = [embedder](std::string_view text,
                               const harness::CancellationToken& cancellation) {
            const std::vector<std::vector<float>> vectors =
                embedder->embed({std::string{text}}, cancellation);
            return vectors.empty() ? std::vector<float>{} : vectors.front();
        };
        return out;
    }
    out.note = !decision.note.empty() ? decision.note
               : !reason.empty()      ? reason
                                      : std::string{"no embedding backend resolves"};
    return out;
}

bool source_stale(const embedstore::SourceState& state, const embedstore::ChunkSpan& span,
                  std::string_view model) {
    if (state.chunk_count != span.count || state.model != model) {
        return true;
    }
    return state.max_chunk_id != 0 && state.max_chunk_id != span.max_id;
}

BuildResult build(embedstore::Store& store, const ExtractFn& extract, const EmbedFn& embed,
                  const BuildOptions& options) {
    if (!extract) {
        throw std::invalid_argument("graph build needs an extraction function");
    }
    BuildResult out;
    out.dry_run = options.dry_run;

    // 1. Reconcile: prune rows orphaned by chunk churn so this build never
    // links against dead provenance. Skipped in a dry run (it writes).
    if (!options.dry_run) {
        out.reconcile = store.reconcile_graph();
    }

    // Node ids to (re)embed -- decision nodes from the record pass and
    // entities from extraction alike.
    std::set<std::int64_t> mutated;

    // 1b. Records as decision nodes, every build: after reconcile (a
    // re-captured record's old node was just pruned with its dead mention)
    // and before extraction (which links `concerns` edges from these nodes).
    const std::map<std::int64_t, std::int64_t> decision_by_chunk =
        materialize_records(store, mutated, out, options.dry_run);

    // 2. Plan: the stale sources, sorted, by the (count, max id, model)
    // fingerprint.
    const std::map<std::string, embedstore::SourceState> states = store.source_states();

    struct Planned {
        std::string source;
        std::int64_t chunks = 0;
    };

    std::vector<Planned> planned;
    int chunks_total = 0;
    for (const auto& [source, span] : store.source_chunk_spans()) {
        const auto state = states.find(source);
        if (options.force || state == states.end() ||
            source_stale(state->second, span, options.model)) {
            planned.push_back(Planned{.source = source, .chunks = span.count});
            chunks_total += static_cast<int>(span.count);
        }
    }
    out.files_planned = static_cast<int>(planned.size());
    if (options.limit > 0 && options.limit < chunks_total) {
        chunks_total = options.limit;
    }

    // 3. Extract, file by file, chunk by chunk.
    bool stopped = false;
    for (std::size_t index = 0; index < planned.size() && !stopped; ++index) {
        const Planned& file = planned[index];
        const std::vector<embedstore::Chunk> chunks = store.chunks_by_source(file.source);
        bool complete = true;
        for (const embedstore::Chunk& chunk : chunks) {
            if (options.cancellation.stop_requested()) {
                out.cancelled = true;
                return out;
            }
            if (options.limit > 0 && out.chunks_done >= options.limit) {
                out.limit_hit = true;
                stopped = true;
                complete = false;
                break;
            }
            if (options.on_progress) {
                Progress progress;
                progress.stage = Progress::Stage::Extract;
                progress.file = file.source;
                progress.file_index = static_cast<int>(index) + 1;
                progress.file_count = out.files_planned;
                progress.chunks_done = out.chunks_done;
                progress.chunks_total = chunks_total;
                progress.failed = out.chunks_failed;
                options.on_progress(progress);
            }
            ExtractOutcome outcome = extract(chunk.text, options.cancellation);
            if (!outcome.ok()) {
                if (options.cancellation.stop_requested()) {
                    out.cancelled = true;
                    return out;
                }
                outcome = extract(chunk.text, options.cancellation);  // one retry
            }
            ++out.chunks_done;
            if (!outcome.ok()) {
                if (options.cancellation.stop_requested()) {
                    out.cancelled = true;
                    return out;
                }
                ++out.chunks_failed;
                complete = false;
                if (options.on_chunk_failed) {
                    options.on_chunk_failed(chunk, outcome.error);
                }
                continue;
            }
            ExtractResult extracted = std::move(*outcome.result);
            normalize(extracted);
            if (options.on_extract) {
                options.on_extract(chunk, extracted);
            }
            if (options.dry_run) {
                continue;
            }
            const auto decision = decision_by_chunk.find(chunk.id);
            store_extraction(store, chunk.id,
                             decision == decision_by_chunk.end() ? 0 : decision->second, extracted,
                             mutated, out);
        }
        // A file with a failed chunk gets no state row, so the next build
        // retries it instead of considering it done.
        if (complete && !options.dry_run) {
            std::int64_t max_id = 0;
            for (const embedstore::Chunk& chunk : chunks) {
                max_id = std::max(max_id, chunk.id);
            }
            store.set_source_state(file.source, static_cast<std::int64_t>(chunks.size()), max_id,
                                   options.model);
        }
        if (complete) {
            ++out.files_extracted;
        }
    }
    if (!options.dry_run) {
        store.set_graph_meta(embedstore::kGraphMetaExtractModel, options.model);
        store.set_graph_meta(embedstore::kGraphMetaFailedChunks, std::to_string(out.chunks_failed));
    }

    // 4. Embed new or changed entities, batched at the end so a failed embed
    // never loses extraction work.
    if (embed && !options.dry_run && !mutated.empty()) {
        const std::vector<std::int64_t> ids(mutated.begin(), mutated.end());
        const std::vector<embedstore::GraphNode> nodes = store.nodes_by_ids(ids);
        if (options.on_progress) {
            Progress progress;
            progress.stage = Progress::Stage::Embed;
            progress.entities = static_cast<int>(nodes.size());
            options.on_progress(progress);
        }
        for (const embedstore::GraphNode& node : nodes) {
            if (options.cancellation.stop_requested()) {
                out.cancelled = true;
                return out;
            }
            // A decision node's name is a record id -- no semantic content --
            // so its vector is the description alone.
            std::string text = node.name;
            if (node.type == embedstore::kNodeTypeDecision && !node.description.empty()) {
                text = node.description;
            } else if (!node.description.empty()) {
                text += ": " + node.description;
            }
            std::vector<float> vector;
            try {
                vector = embed(text, options.cancellation);
            } catch (const std::exception& e) {
                // Endpoint trouble affects every remaining call -- stop the
                // phase, keep the build.
                out.embed_error = e.what();
                break;
            }
            if (vector.empty()) {
                out.embed_error = "the embedder returned no vector";
                break;
            }
            store.update_node_embedding(node.id, vector);
            ++out.nodes_embedded;
        }
        // The graph's entity-vector model is recorded only when the phase ran
        // to completion -- a partial embed must not claim the model.
        if (out.embed_error.empty() && !options.embed_model.empty()) {
            store.set_graph_meta(embedstore::kGraphMetaEmbedModel, options.embed_model);
        }
    }
    return out;
}

}  // namespace apogee::graph
