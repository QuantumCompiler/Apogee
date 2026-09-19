#include "graph/communities.h"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

#include "agentloop/loop.h"
#include "agentloop/reporter.h"
#include "harness/errors.h"
#include "harness/harness.h"
#include "harness/types.h"

// The summariser's prompt is GENERATED from lib/src/cli/assets/clerks/
// community_prompt.txt; tests/graph/communities_test.cpp fails the build the
// moment the two drift. Edit the FILE, then regenerate; never the literal.

namespace apogee::graph {
namespace {

constexpr std::string_view kCommunityPrompt =
    R"PROMPT(You are a corpus analyst summarizing one thematic cluster of a knowledge
graph. You are given the cluster's entities (name, type, and what the corpus
says about each) and the relations stated between them. Your single job is to
write one concise summary of what this cluster is about, so that questions
like "what are the main themes here?" can be answered from summaries alone.

Rules:

1. Write 3 to 6 sentences of plain prose. No headings, no lists, no markdown.
2. Name the principal entities — a reader should learn WHO/WHAT this cluster
   centers on and HOW those things relate.
3. Stay inside the given material. Summarize only what the entity
   descriptions and relations support; do not speculate, generalize beyond
   them, or import outside knowledge.
4. Lead with the theme. Open with what binds the cluster together, then the
   load-bearing specifics.

Output ONLY the summary prose — no preamble, no commentary, no code fences.
)PROMPT";

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

}  // namespace

std::string_view community_prompt() noexcept {
    return kCommunityPrompt;
}

std::string community_system_prompt() {
    return trim(kCommunityPrompt);
}

SummarizeFn make_summarizer(const harness::Harness& harness, std::string model) {
    return [&harness, model = std::move(model)](std::string_view community_text,
                                                const harness::CancellationToken& cancellation) {
        std::vector<harness::ChatMessage> history{
            harness::ChatMessage::system(community_system_prompt()),
            harness::ChatMessage::user(std::string{community_text})};
        agentloop::Options options;
        options.model = model;
        options.temperature = kExtractTemperature;
        options.max_tokens = kExtractMaxTokens;
        options.stream_answer = false;
        // Not a turn of anyone's conversation: a local backend runs it on
        // its own context, and a session's cache is never touched.
        options.side_request = true;
        options.cancellation = cancellation;
        agentloop::NullReporter reporter;
        try {
            return agentloop::run(harness, history, options, reporter).answer;
        } catch (const harness::HarnessError& e) {
            throw std::runtime_error(std::string{"the summariser could not run: "} + e.what());
        }
    };
}

std::vector<std::vector<std::int64_t>> detect_communities(
    const std::vector<embedstore::GraphEdge>& edges, int min_size) {
    if (min_size <= 0) {
        min_size = kDefaultMinCommunitySize;
    }

    // Adjacency over the undirected view of the edge set.
    struct Neighbor {
        std::int64_t id = 0;
        std::int64_t weight = 1;
    };

    std::map<std::int64_t, std::vector<Neighbor>> adjacency;
    for (const embedstore::GraphEdge& edge : edges) {
        adjacency[edge.source_id].push_back(Neighbor{.id = edge.target_id, .weight = edge.weight});
        adjacency[edge.target_id].push_back(Neighbor{.id = edge.source_id, .weight = edge.weight});
    }
    if (adjacency.empty()) {
        return {};
    }
    // Ascending-id order: a std::map iterates that way already.
    std::map<std::int64_t, std::int64_t> labels;
    for (const auto& [id, unused] : adjacency) {
        labels[id] = id;
    }
    for (int round = 0; round < kMaxLabelPropagationRounds; ++round) {
        bool changed = false;
        for (const auto& [id, neighbors] : adjacency) {
            std::map<std::int64_t, std::int64_t> support;
            for (const Neighbor& neighbor : neighbors) {
                support[labels[neighbor.id]] += neighbor.weight;
            }
            std::int64_t best = labels[id];
            std::int64_t best_weight = 0;
            for (const auto& [label, weight] : support) {
                if (weight > best_weight || (weight == best_weight && label < best)) {
                    best = label;
                    best_weight = weight;
                }
            }
            if (best != labels[id]) {
                labels[id] = best;
                changed = true;
            }
        }
        if (!changed) {
            break;
        }
    }
    std::map<std::int64_t, std::vector<std::int64_t>> groups;
    for (const auto& [id, label] : labels) {
        groups[label].push_back(id);
    }
    std::vector<std::vector<std::int64_t>> communities;
    for (auto& [label, members] : groups) {
        if (static_cast<int>(members.size()) < min_size) {
            continue;
        }
        std::ranges::sort(members);
        communities.push_back(std::move(members));
    }
    std::ranges::sort(communities, [](const auto& a, const auto& b) {
        if (a.size() != b.size()) {
            return a.size() > b.size();
        }
        return a.front() < b.front();
    });
    return communities;
}

std::string community_key(const std::vector<std::int64_t>& members) {
    std::vector<std::int64_t> sorted = members;
    std::ranges::sort(sorted);
    std::string out;
    for (const std::int64_t id : sorted) {
        if (!out.empty()) {
            out += ',';
        }
        out += std::to_string(id);
    }
    return out;
}

std::string community_text(const std::vector<embedstore::GraphNode>& nodes,
                           const std::vector<embedstore::GraphEdge>& edges) {
    std::map<std::int64_t, const embedstore::GraphNode*> by_id;
    for (const embedstore::GraphNode& node : nodes) {
        by_id[node.id] = &node;
    }
    std::vector<const embedstore::GraphNode*> sorted;
    sorted.reserve(nodes.size());
    for (const embedstore::GraphNode& node : nodes) {
        sorted.push_back(&node);
    }
    std::ranges::sort(sorted, [](const embedstore::GraphNode* a, const embedstore::GraphNode* b) {
        if (a->mention_count != b->mention_count) {
            return a->mention_count > b->mention_count;
        }
        return a->name < b->name;
    });
    std::string out = "Entities:\n";
    for (const embedstore::GraphNode* node : sorted) {
        out += "- " + node->name + " (" + node->type + ")";
        if (!node->description.empty()) {
            out += ": " + node->description;
        }
        out += '\n';
    }
    std::vector<const embedstore::GraphEdge*> intra;
    for (const embedstore::GraphEdge& edge : edges) {
        if (by_id.contains(edge.source_id) && by_id.contains(edge.target_id)) {
            intra.push_back(&edge);
        }
    }
    if (intra.empty()) {
        return out;
    }
    std::ranges::sort(intra, [](const embedstore::GraphEdge* a, const embedstore::GraphEdge* b) {
        if (a->weight != b->weight) {
            return a->weight > b->weight;
        }
        return a->id < b->id;
    });
    if (intra.size() > kMaxCommunityRelationLines) {
        intra.resize(kMaxCommunityRelationLines);
    }
    out += "Relations:\n";
    for (const embedstore::GraphEdge* edge : intra) {
        out += "- " + by_id[edge->source_id]->name + " —[" + edge->relation + "]→ " +
               by_id[edge->target_id]->name;
        if (!edge->description.empty()) {
            out += ": " + edge->description;
        }
        out += '\n';
    }
    return out;
}

CommunitiesResult build_communities(embedstore::Store& store, const SummarizeFn& summarize,
                                    const EmbedFn& embed, const CommunitiesOptions& options) {
    if (!summarize) {
        throw std::invalid_argument("communities need a summarise function");
    }
    CommunitiesResult out;
    const std::vector<embedstore::GraphEdge> edges = store.all_edges();
    const std::vector<std::vector<std::int64_t>> communities =
        detect_communities(edges, options.min_size);
    out.detected = static_cast<int>(communities.size());

    std::map<std::string, embedstore::GraphCommunity> existing;
    for (embedstore::GraphCommunity& community : store.graph_communities()) {
        existing[community.member_key] = std::move(community);
    }

    std::set<std::string> keep;
    for (std::size_t index = 0; index < communities.size(); ++index) {
        const std::vector<std::int64_t>& members = communities[index];
        if (options.on_progress) {
            options.on_progress(static_cast<int>(index), static_cast<int>(communities.size()));
        }
        // Checked after the heartbeat: a cancel raised from the progress seam
        // stops before the next call, not after it.
        if (options.cancellation.stop_requested()) {
            out.cancelled = true;
            return out;
        }
        const std::string key = community_key(members);
        keep.insert(key);
        if (const auto it = existing.find(key);
            it != existing.end() && !it->second.summary.empty() && !options.force) {
            ++out.unchanged;
            continue;
        }
        const std::vector<embedstore::GraphNode> nodes = store.nodes_by_ids(members);
        std::string summary;
        try {
            summary = trim(summarize(community_text(nodes, edges), options.cancellation));
        } catch (const std::exception&) {
            if (options.cancellation.stop_requested()) {
                out.cancelled = true;
                return out;
            }
            ++out.failed;
            continue;
        }
        if (summary.empty()) {
            ++out.failed;
            continue;
        }
        (void)store.replace_community(key, members, summary, options.model);
        ++out.summarized;
    }
    out.pruned = static_cast<int>(store.prune_communities(keep));

    // Vectorise last, batched, and every summary still without a vector --
    // not only this run's -- so an earlier embed failure heals here instead
    // of needing a forced regeneration.
    if (embed) {
        for (const std::int64_t id : store.communities_without_vectors()) {
            if (options.cancellation.stop_requested()) {
                out.cancelled = true;
                return out;
            }
            std::string summary;
            for (const embedstore::GraphCommunity& community : store.graph_communities()) {
                if (community.id == id) {
                    summary = community.summary;
                    break;
                }
            }
            std::vector<float> vector;
            try {
                vector = embed(summary, options.cancellation);
            } catch (const std::exception& e) {
                out.embed_error = e.what();
                break;
            }
            if (vector.empty()) {
                out.embed_error = "the embedder returned no vector";
                break;
            }
            store.update_community_embedding(id, vector);
            ++out.embedded;
        }
    }
    return out;
}

}  // namespace apogee::graph
