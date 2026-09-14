#include "tools/rag_query.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <vector>

#include "agentloop/embed_func.h"
#include "agentloop/retriever.h"
#include "embedstore/store.h"
#include "harness/layout.h"
#include "tools/args.h"

namespace apogee::tools {
namespace {

std::filesystem::path collection_path(std::string_view name) {
    return harness::embeddings_dir() / (std::string{name} + ".db");
}

std::string two_decimals(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << value;
    return out.str();
}

std::vector<std::string> collection_names() {
    std::vector<std::string> names;
    std::error_code code;
    const std::filesystem::path dir = harness::embeddings_dir();
    if (!std::filesystem::is_directory(dir, code)) {
        return names;
    }
    for (const auto& entry : std::filesystem::directory_iterator{dir, code}) {
        if (entry.path().extension() == ".db") {
            names.push_back(entry.path().stem().string());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

}  // namespace

bool valid_collection_name(std::string_view name) noexcept {
    if (name.empty() || name.size() > 128) {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return std::isalnum(c) != 0 || c == '.' || c == '_' || c == '-';
    });
}

void register_rag_tools(agent::ToolRegistry& registry, const harness::Harness* harness,
                        const harness::Config* config) {
    agent::Tool search;
    search.name = "search_documents";
    search.description =
        "Search an ingested collection for passages relevant to a query. `retriever` is "
        "auto (default), lexical (BM25; needs no model), vector, or hybrid; the answer says "
        "which one actually ran, because their scores are not comparable.";
    search.parameters_schema =
        R"({"type":"object","properties":{"query":{"type":"string"},"collection":{"type":"string","description":"The collection name, as `apogee embed list` shows it"},"top_k":{"type":"integer","description":"How many passages (1-20); default 5"},"retriever":{"type":"string","enum":["auto","lexical","vector","hybrid"]}},"required":["query","collection"]})";
    search.run = [harness, config](std::string_view arguments) -> agent::ToolOutcome {
        agent::ToolOutcome failure;
        const std::optional<Arguments> args =
            parse_arguments(arguments, R"({"query": "...", "collection": "notes"})", failure);
        if (!args.has_value()) {
            return failure;
        }
        const std::string query = args->string("query");
        const std::string collection = args->string("collection");
        if (query.empty()) {
            return error("query is required");
        }
        if (!valid_collection_name(collection)) {
            return error("collection is required: a name like the ones list_collections shows");
        }
        const std::int64_t top_k = args->integer("top_k").value_or(5);
        if (top_k < 1 || top_k > 20) {
            return error("top_k must be between 1 and 20");
        }
        std::string flag = args->string("retriever");
        if (flag == "auto") {
            flag.clear();
        }
        if (!flag.empty() && !agentloop::valid_retriever(flag)) {
            return error(agentloop::retriever_values_message("retriever", flag));
        }

        const std::filesystem::path path = collection_path(collection);
        std::error_code code;
        if (!std::filesystem::exists(path, code)) {
            return error("no collection named '" + collection +
                         "'. Use list_collections to see what exists.");
        }

        // The same facts the turn-level retrieval gathers, resolved by the
        // same function -- so this tool and `--rag` cannot disagree.
        std::string pin;
        std::string collection_backend;
        if (config != nullptr) {
            if (const harness::EmbeddingConfig* entry = config->find_embedding(collection);
                entry != nullptr) {
                pin = entry->retriever;
                collection_backend = entry->backend;
            }
        }
        std::optional<agentloop::Embedder> embedder;
        std::string embedder_reason;
        if (harness != nullptr && config != nullptr) {
            embedder =
                agentloop::resolve_embedder(*harness, *config, collection_backend, embedder_reason);
        }
        agentloop::EmbedderFacts embedder_facts;
        if (embedder.has_value()) {
            embedder_facts.available = true;
            embedder_facts.model = embedder->model;
            embedder_facts.metered = embedder->metered;
        }
        std::optional<embedstore::Store> store;
        agentloop::StoreFacts facts;
        facts.exists = true;
        try {
            store.emplace(path);
            const embedstore::Store::Stats stats = store->stats();
            facts.chunk_count = stats.chunk_count;
            facts.dimension = stats.dimension;
            facts.lexical_only = stats.lexical_only;
            facts.vector_dims = stats.vector_dims;
            facts.recorded_model = store->embedding_model().model;
        } catch (const std::exception& e) {
            return error(std::string{"could not open the collection: "} + e.what());
        }
        const agentloop::TurnRetrieval decision =
            agentloop::resolve_turn_retriever(flag, pin, embedder_facts, facts);
        if (!decision.error.empty()) {
            return error(decision.error);
        }
        if (decision.excluded) {
            return error("collection '" + collection +
                         "' cannot be searched as pinned: " + decision.note);
        }

        std::vector<embedstore::SearchHit> hits;
        try {
            if (decision.retriever == agentloop::Retriever::Lexical) {
                hits = store->search(query, static_cast<int>(top_k));
            } else {
                harness::CancellationToken cancellation;
                const std::vector<std::vector<float>> vectors =
                    embedder->embed({query}, cancellation);
                if (vectors.size() != 1) {
                    return error("the embedder returned no vector for the query");
                }
                hits = decision.retriever == agentloop::Retriever::Vector
                           ? store->search_vector(vectors.front(), static_cast<int>(top_k))
                           : store->search_hybrid(vectors.front(), query, static_cast<int>(top_k));
            }
        } catch (const std::exception& e) {
            return error(std::string{"search failed: "} + e.what());
        }

        std::string out = "collection: " + collection +
                          "  retriever: " + std::string{agentloop::to_string(decision.retriever)};
        if (!decision.note.empty()) {
            out += "  (" + decision.note + ")";
        }
        out += "\n";
        if (hits.empty()) {
            out += "No passages matched.";
            return ok(std::move(out));
        }
        for (std::size_t i = 0; i < hits.size(); ++i) {
            const embedstore::SearchHit& hit = hits[i];
            out += "\n" + std::to_string(i + 1) + ". [" + hit.chunk.source + " #" +
                   std::to_string(hit.chunk.ordinal) + "]  score=" + two_decimals(hit.score) +
                   "  retriever=" + hit.retriever + "\n" + hit.chunk.text + "\n";
        }
        out.pop_back();
        return ok(std::move(out));
    };
    registry.add(search);

    agent::Tool list;
    list.name = "list_collections";
    list.description = "List the ingested collections that can be searched.";
    list.parameters_schema = R"({"type":"object","properties":{}})";
    list.run = [](std::string_view) -> agent::ToolOutcome {
        const std::vector<std::string> names = collection_names();
        if (names.empty()) {
            return ok("No collections yet. Create one with 'apogee embed ingest <name> <path>'.");
        }
        std::string out;
        for (const std::string& name : names) {
            std::string line = "  " + name;
            try {
                const embedstore::Store store{collection_path(name)};
                line += "  " + std::to_string(store.chunk_count()) + " chunks";
            } catch (const std::exception&) {
                line += "  (unreadable)";
            }
            out += line + "\n";
        }
        out.pop_back();
        return ok(std::move(out));
    };
    registry.add(list);

    agent::Tool info;
    info.name = "collection_info";
    info.description = "Describe a collection: chunk count, embedding model, and its sources.";
    info.parameters_schema =
        R"({"type":"object","properties":{"collection":{"type":"string"}},"required":["collection"]})";
    info.run = [](std::string_view arguments) -> agent::ToolOutcome {
        agent::ToolOutcome failure;
        const std::optional<Arguments> args =
            parse_arguments(arguments, R"({"collection": "notes"})", failure);
        if (!args.has_value()) {
            return failure;
        }
        const std::string collection = args->string("collection");
        if (!valid_collection_name(collection)) {
            return error("collection is required");
        }
        const std::filesystem::path path = collection_path(collection);
        std::error_code code;
        if (!std::filesystem::exists(path, code)) {
            return error("no collection named '" + collection + "'");
        }
        try {
            const embedstore::Store store{path};
            const embedstore::Store::Stats stats = store.stats();
            const embedstore::Store::EmbeddingBinding binding = store.embedding_model();
            std::string out = "collection: " + collection +
                              "\nchunks: " + std::to_string(stats.chunk_count) +
                              "\nembedding model: " +
                              (binding.recorded()
                                   ? binding.model + " (" + std::to_string(binding.dimension) + "d)"
                                   : "none (lexical only)") +
                              "\n";
            if (stats.lexical_only > 0 && stats.chunk_count > 0) {
                out += "chunks without vectors: " + std::to_string(stats.lexical_only) + "\n";
            }
            out += "sources:\n";
            for (const std::string& source : store.sources()) {
                out += "  " + source + "\n";
            }
            out.pop_back();
            return ok(std::move(out));
        } catch (const std::exception& e) {
            return error(std::string{"could not open the collection: "} + e.what());
        }
    };
    registry.add(info);
}

}  // namespace apogee::tools
