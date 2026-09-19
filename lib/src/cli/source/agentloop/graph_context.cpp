#include "agentloop/graph_context.h"

#include <system_error>
#include <utility>

#include "harness/layout.h"

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

}  // namespace

std::string entity_line(const embedstore::GraphNode& node) {
    std::string label = node.type;
    if (node.type == embedstore::kNodeTypeDecision) {
        const embedstore::DecisionNodeMetadata meta =
            embedstore::parse_decision_node_metadata(node.metadata);
        if (!meta.status.empty()) {
            label += ", " + meta.status;
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
    GraphSection section;
    std::vector<std::int64_t> seed_nodes;
    if (!lexical_query.empty()) {
        // The model-free seed: an entity the question names by name.
        for (const embedstore::NodeResult& hit : store.search_nodes(lexical_query, max_entities)) {
            seed_nodes.push_back(hit.node.id);
        }
    }
    if (seed_chunks.empty() && seed_nodes.empty()) {
        return section;
    }
    const embedstore::Expansion expansion =
        store.graph_expand_labelled(seed_chunks, seed_nodes, hops, max_entities);
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
            std::string line = edge.source_name + " —[" + edge.relation + "]→ " + edge.target_name;
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
