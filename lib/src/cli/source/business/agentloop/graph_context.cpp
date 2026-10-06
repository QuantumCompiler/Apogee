#include "agentloop/graph_context.h"

#include <system_error>
#include <utility>

#include "contracts/layout.h"

namespace apogee::agentloop {
namespace {

[[nodiscard]] std::size_t codepoints(std::string_view text) {
    std::size_t count = 0;
    for (const char c : text) {
        // Every byte that is not a UTF-8 continuation byte starts a codepoint.
        if ((static_cast<unsigned char>(c) & 0xC0U) != 0x80U) {
            ++count;
        }
    }
    return count;
}

/// The model-free seed: the entities the question names by name.
[[nodiscard]] std::vector<std::int64_t> lexical_seeds(const embedstore::Store& store,
                                                      std::string_view lexical_query,
                                                      int max_entities) {
    std::vector<std::int64_t> out;
    if (lexical_query.empty()) {
        return out;
    }
    for (const embedstore::NodeResult& hit : store.search_nodes(lexical_query, max_entities)) {
        out.push_back(hit.node.id);
    }
    return out;
}

/// `expansion` as the section: the header, the entity lines, the triples.
[[nodiscard]] GraphSection render_section(const embedstore::Expansion& expansion,
                                          std::string_view header) {
    GraphSection section;
    if (expansion.empty()) {
        return section;
    }

    // Whole lines under the budget: a line that does not fit ends the
    // section, and nothing after it is tried -- so the section never carries
    // a triple whose entity line was cut.
    std::size_t budget = kGraphSectionBudget;
    std::string out;
    const auto append = [&](const std::string& line) {
        const std::size_t cost = codepoints(line) + 1;
        if (cost > budget) {
            return false;
        }
        budget -= cost;
        out += line;
        out += '\n';
        return true;
    };
    if (!append("[Knowledge graph: " + std::string{header} + "]")) {
        return section;
    }
    bool truncated = false;
    for (const embedstore::ExpandEntity& entity : expansion.entities) {
        if (!append(entity_line(entity.node))) {
            truncated = true;
            break;
        }
        ++section.entities;
    }
    if (!truncated) {
        for (const embedstore::ExpandEdge& edge : expansion.edges) {
            // A parsed edge says so (27k): what the source states, beside
            // what a model asserted -- which keeps the bare form it always had.
            const std::string relation = edge.origin == embedstore::kOriginExtracted
                                             ? edge.relation + "·" + edge.origin
                                             : edge.relation;
            std::string line = edge.source_name + " —[" + relation + "]→ " + edge.target_name;
            if (!edge.description.empty()) {
                line += ": " + edge.description;
            }
            if (!append(line)) {
                break;
            }
        }
    }
    if (section.entities == 0) {
        return GraphSection{};
    }
    if (!out.empty() && out.back() == '\n') {
        out.pop_back();
    }
    section.text = std::move(out);
    return section;
}

}  // namespace

std::string entity_line(const embedstore::GraphNode& node) {
    std::string label = node.type;
    if (node.type == embedstore::kNodeTypeDecision) {
        const embedstore::DecisionNodeMetadata meta =
            embedstore::parse_decision_node_metadata(node.metadata);
        if (!meta.status.empty()) {
            label += ", " + meta.status;
        }
    } else if (embedstore::is_code_node_type(node.type)) {
        // A code entity carries where it is defined (27k): the model can
        // cite, and a reader open, the line itself.
        const embedstore::CodeNodeMetadata meta =
            embedstore::parse_code_node_metadata(node.metadata);
        if (meta.code && !meta.unresolved && !meta.file.empty()) {
            label += ", " + meta.file + ":" + std::to_string(meta.line);
        }
    }
    std::string line = node.name + " (" + label + ")";
    if (!node.description.empty()) {
        line += ": " + node.description;
    }
    return line;
}

GraphSection build_graph_section(const embedstore::Store& store, std::string_view collection,
                                 const std::vector<std::int64_t>& seed_chunks,
                                 std::string_view lexical_query, int hops, int max_entities) {
    std::vector<embedstore::ChunkRef> refs;
    refs.reserve(seed_chunks.size());
    for (const std::int64_t id : seed_chunks) {
        refs.push_back(embedstore::ChunkRef{.collection = "", .chunk_id = id});
    }
    return build_graph_section_labelled(store, collection, refs, lexical_query, hops, max_entities);
}

GraphSection build_graph_section_labelled(const embedstore::Store& store, std::string_view header,
                                          const std::vector<embedstore::ChunkRef>& seed_chunks,
                                          std::string_view lexical_query, int hops,
                                          int max_entities) {
    const std::vector<std::int64_t> seed_nodes = lexical_seeds(store, lexical_query, max_entities);
    if (seed_chunks.empty() && seed_nodes.empty()) {
        return GraphSection{};
    }
    return render_section(store.graph_expand_labelled(seed_chunks, seed_nodes, hops, max_entities),
                          header);
}

GraphSection build_graph_section_excerpts(const embedstore::Store& store, std::string_view header,
                                          const std::vector<embedstore::CodeExcerptRef>& excerpts,
                                          std::string_view lexical_query, int hops,
                                          int max_entities) {
    const std::vector<std::int64_t> seed_nodes = lexical_seeds(store, lexical_query, max_entities);
    if (excerpts.empty() && seed_nodes.empty()) {
        return GraphSection{};
    }
    return render_section(store.graph_expand_excerpts(excerpts, seed_nodes, hops, max_entities),
                          header);
}

std::filesystem::path graph_db_path(std::string_view name) {
    return harness::embeddings_dir() / "graphs" / (std::string{name} + ".db");
}

std::optional<CoveringGraph> named_graph_covering(const harness::Config& config,
                                                  std::string_view collection, bool built_only) {
    for (const auto& [name, graph] : config.graphs) {
        const harness::CaseInsensitiveLess less;
        bool member = false;
        for (const std::string& candidate : graph.collections) {
            if (!less(candidate, collection) && !less(collection, candidate)) {
                member = true;
                break;
            }
        }
        if (!member) {
            continue;
        }
        if (built_only) {
            std::error_code code;
            if (!std::filesystem::exists(graph_db_path(name), code)) {
                continue;  // never built: the member keeps its own graph
            }
        }
        return CoveringGraph{.name = name, .config = &graph};
    }
    return std::nullopt;
}

TurnGraph resolve_turn_graph(const harness::Config& config, std::string_view collection) {
    TurnGraph out;
    if (const std::optional<CoveringGraph> named =
            named_graph_covering(config, collection, /*built_only=*/true);
        named.has_value()) {
        out.enabled = true;
        out.store_path = graph_db_path(named->name);
        out.name = named->name;
        out.seed_collection = std::string{collection};
        out.hops = named->config->hops;
        out.max_entities = named->config->max_entities;
        return out;
    }
    const harness::EmbeddingConfig* entry = config.find_embedding(collection);
    if (entry == nullptr || !entry->graph.enabled) {
        return out;
    }
    out.enabled = true;
    out.name = std::string{collection};
    out.hops = entry->graph.hops;
    out.max_entities = entry->graph.max_entities;
    return out;
}

}  // namespace apogee::agentloop
