#include "commands/embed.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <system_error>

#include "agentloop/embed_func.h"
#include "agentloop/rerank.h"
#include "agentloop/retriever.h"
#include "backends/factory.h"
#include "embedstore/ingest.h"
#include "embedstore/store.h"
#include "harness/config.h"
#include "harness/config_edit.h"
#include "harness/harness.h"
#include "harness/layout.h"
#include "harness/paths.h"

namespace apogee::commands {
namespace {

[[noreturn]] void fail(const std::string& message) {
    std::cerr << "apogee embed: " << message << "\n";
    throw CLI::RuntimeError(1);
}

/// Refuses a name that would escape the embeddings directory.
///
/// The same rule `models delete` follows: a collection is NAMED, not pathed. A
/// `..` or an absolute path here would turn "search my notes" into a way to
/// address any file on the machine.
void require_plain_name(std::string_view name) {
    if (name.empty()) {
        fail("a collection needs a name");
    }
    if (name.find("..") != std::string_view::npos || name.find('/') != std::string_view::npos ||
        name.find('\\') != std::string_view::npos) {
        fail("'" + std::string{name} + "' is not a plain collection name");
    }
}

[[nodiscard]] std::string two_places(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(3) << value;
    return out.str();
}

/// A one-line preview of a chunk, for a listing.
[[nodiscard]] std::string preview(std::string_view text, std::size_t width) {
    std::string out;
    out.reserve(width);
    for (const char c : text) {
        if (out.size() >= width) {
            out += "…";
            break;
        }
        out += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
    }
    return out;
}

/// The facts the resolver reads from a store.
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

[[nodiscard]] agentloop::EmbedderFacts facts_for(const std::optional<agentloop::Embedder>& e) {
    agentloop::EmbedderFacts facts;
    if (e.has_value()) {
        facts.available = true;
        facts.model = e->model;
        facts.metered = e->metered;
    }
    return facts;
}

/// Builds every configured provider so the embedding capability can be asked
/// of the real objects. Only done when a decision might need an embedder.
struct Providers {
    harness::Harness harness;

    explicit Providers(const harness::Config& config) : harness{config} {
        (void)backends::build_providers(harness, {});
    }
};

}  // namespace

std::filesystem::path collection_path(std::string_view name) {
    return harness::embeddings_dir() / (std::string{name} + ".db");
}

std::vector<std::string> collection_names() {
    std::vector<std::string> names;
    std::error_code code;
    for (const auto& entry : std::filesystem::directory_iterator(harness::embeddings_dir(), code)) {
        if (code) {
            break;
        }
        if (entry.is_regular_file(code) && entry.path().extension() == ".db") {
            names.push_back(entry.path().stem().string());
        }
    }
    std::ranges::sort(names);
    return names;
}

std::string_view EmbedCommand::name() const noexcept {
    return "embed";
}

std::string_view EmbedCommand::summary() const noexcept {
    return "Ingest documents into a searchable collection, and query it";
}

void EmbedCommand::bind(CLI::App& root, const RootContext& context) {
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->require_subcommand(1);

    // ---- ingest -------------------------------------------------------------
    auto in_collection = std::make_shared<std::string>();
    auto in_path = std::make_shared<std::string>();
    auto in_size = std::make_shared<std::size_t>(512);
    auto in_overlap = std::make_shared<std::size_t>(64);

    CLI::App* ingest = cmd->add_subcommand("ingest", "Add a file or directory to a collection");
    ingest->add_option("collection", *in_collection, "Collection name")->required();
    ingest->add_option("path", *in_path, "File or directory to read")->required();
    CLI::Option* size_option = ingest->add_option(
        "--chunk-size", *in_size, "Codepoints per chunk (default: the collection's, else 512)");
    CLI::Option* overlap_option =
        ingest->add_option("--chunk-overlap", *in_overlap,
                           "Codepoints of overlap (default: the collection's, else 64)");
    auto in_retriever = std::make_shared<std::string>();
    ingest
        ->add_option("--retriever", *in_retriever,
                     "lexical (text index only), vector (also embed every chunk), or auto")
        ->check([](const std::string& value) {
            return agentloop::valid_retriever(value)
                       ? std::string{}
                       : agentloop::retriever_values_message("", value);
        });

    ingest->callback([&context, in_collection, in_path, in_size, in_overlap, in_retriever,
                      size_option, overlap_option]() {
        require_plain_name(*in_collection);

        const std::filesystem::path target{*in_path};
        std::error_code code;
        if (!std::filesystem::exists(target, code)) {
            fail("no such file or directory: " + target.string());
        }

        // The config is consulted, never required. A collection works the
        // moment its file exists; the entry under `embeddings:` is where its
        // settings live, and it is written the first time a name is seen. A
        // config that will not load costs the user its defaults and its
        // registration -- both said out loud below -- and never the ingest.
        std::filesystem::path config_path;
        std::optional<harness::Config> config;
        std::string config_trouble;
        try {
            config_path = harness::resolve_config_path(context.config_path);
            config = harness::load_config(config_path);
        } catch (const std::exception& e) {
            config_trouble = e.what();
        }
        const harness::EmbeddingConfig* registered =
            config.has_value() ? config->find_embedding(*in_collection) : nullptr;

        // Chunking: the flag, else the collection's own entry, else the
        // default. A corpus of ADRs wants 768 where prose wants 512, and
        // re-typing that on every ingest is how corpora end up chunked
        // inconsistently -- so the entry remembers, and the flag overrides for
        // one run without rewriting what the user put in the file.
        embedstore::ChunkOptions chunking{.size = *in_size, .overlap = *in_overlap};
        if (size_option->count() == 0 && registered != nullptr &&
            registered->chunk_size.value_or(0) > 0) {
            chunking.size = static_cast<std::size_t>(*registered->chunk_size);
        }
        if (overlap_option->count() == 0 && registered != nullptr &&
            registered->chunk_overlap.value_or(-1) >= 0) {
            chunking.overlap = static_cast<std::size_t>(*registered->chunk_overlap);
        }

        // Lexical or vector, decided ONCE by the shared resolver -- and under
        // the user's spend rule: a whole collection is never pushed through a
        // paid embedder unless asked. The pin is the collection's own.
        std::optional<Providers> providers;
        std::optional<agentloop::Embedder> embedder;
        std::string embedder_reason;
        const std::string pin = registered != nullptr ? registered->retriever : std::string{};
        const std::string collection_backend =
            registered != nullptr ? registered->backend : std::string{};
        if (config.has_value() && *in_retriever != "lexical" && pin != "lexical") {
            providers.emplace(*config);
            embedder = agentloop::resolve_embedder(providers->harness, *config, collection_backend,
                                                   embedder_reason);
        }
        const agentloop::IngestRetrieval decision =
            agentloop::resolve_ingest_retriever(*in_retriever, pin, facts_for(embedder));
        if (!decision.error.empty()) {
            fail(decision.error + (embedder_reason.empty() ? "" : " (" + embedder_reason + ")"));
        }

        embedstore::EmbedChunks embed;
        if (decision.retriever == agentloop::Retriever::Vector) {
            embed = [&embedder](const std::vector<std::string>& texts) {
                return embedder->embed(texts, {});
            };
        }

        embedstore::IngestReport report;
        try {
            report =
                embedstore::ingest_path(collection_path(*in_collection), target, chunking, embed);
            embedstore::Store store{collection_path(*in_collection)};
            const embedstore::Store::Stats stats = store.stats();
            if (decision.retriever == agentloop::Retriever::Vector && stats.dimension > 0) {
                // Record the space the vectors live in, so a later query under
                // another model falls to lexical instead of scoring across
                // spaces.
                store.set_embedding_model(embedder->model, stats.dimension);
            } else if (stats.dimension == 0) {
                // No vectors left at all: a stale record must not make auto
                // pick vector search over a store that holds none.
                store.clear_embedding_model();
            }
        } catch (const std::exception& e) {
            fail(e.what());
        }

        std::cout << report.files_read << " file(s), " << report.chunks_written << " chunk(s)";
        if (decision.retriever == agentloop::Retriever::Vector) {
            std::cout << ", " << report.vectors_written << " vector(s) [" << embedder->model << "]";
        }
        std::cout << "\n";
        if (!decision.note.empty()) {
            std::cout << decision.note << "\n";
        }
        if (!report.unvectorised.empty()) {
            // Named, like every skip: these chunks are searchable by text but
            // invisible to vector search, and the resolver will say so until
            // they are re-ingested.
            std::cout << "\nstored without vectors " << report.unvectorised.size() << ":\n";
            for (const std::string& entry : report.unvectorised) {
                std::cout << "  " << entry << "\n";
            }
        }
        if (report.files_skipped > 0) {
            // Every skip is NAMED. A corpus that quietly omitted half a
            // directory answers wrongly and gives no clue why.
            std::cout << "\nskipped " << report.files_skipped << ":\n";
            for (const std::string& skip : report.skips) {
                std::cout << "  " << skip << "\n";
            }
            if (!embedstore::pdftotext_available()) {
                std::cout << "\n(install poppler for PDF support -- it provides pdftotext)\n";
            }
        }

        // Register a collection the config has not met, through the one path
        // that writes a config file -- so this edit is byte-for-byte the edit
        // `apogee config` would have made, and every comment survives it.
        //
        // Reported and never fatal: the corpus is already on disk, and telling
        // the user their ingest failed because their config could not be
        // edited would be a lie about what happened.
        if (!config_trouble.empty()) {
            std::cerr << "apogee embed: not registered in config -- " << config_trouble << "\n";
        } else if (registered == nullptr) {
            const harness::EmbeddingConfig entry{
                .chunk_size = static_cast<std::int64_t>(chunking.size),
                .chunk_overlap = static_cast<std::int64_t>(chunking.overlap),
                .description = {}};
            try {
                harness::edit_config_file(config_path, [&](std::string_view content) {
                    return harness::append_embedding(content, *in_collection, entry, false);
                });
                std::cout << "registered '" << *in_collection << "' in " << config_path.string()
                          << "\n";
            } catch (const std::exception& e) {
                std::cerr << "apogee embed: could not register '" << *in_collection
                          << "' in config -- " << e.what() << "\n";
            }
        }
    });

    // ---- query --------------------------------------------------------------
    auto q_collection = std::make_shared<std::string>();
    auto q_text = std::make_shared<std::string>();
    auto q_limit = std::make_shared<int>(5);

    auto q_retriever = std::make_shared<std::string>();
    auto q_rerank = std::make_shared<std::string>();
    CLI::App* query = cmd->add_subcommand("query", "Search a collection");
    query->add_option("collection", *q_collection, "Collection name")->required();
    query->add_option("text", *q_text, "What to search for")->required();
    query->add_option("-n,--limit", *q_limit, "How many results (default 5)");
    query->add_option("--retriever", *q_retriever, "lexical, vector, hybrid, or auto")
        ->check([](const std::string& value) {
            return agentloop::valid_retriever(value)
                       ? std::string{}
                       : agentloop::retriever_values_message("", value);
        });
    query->add_option("--rerank", *q_rerank,
                      "Backend that reorders the hits with one generation call, or off");

    query->callback([&context, q_collection, q_text, q_limit, q_retriever, q_rerank]() {
        require_plain_name(*q_collection);
        const std::filesystem::path path = collection_path(*q_collection);
        std::error_code code;
        if (!std::filesystem::exists(path, code)) {
            fail("no collection named '" + *q_collection + "'. Create one with 'apogee embed " +
                 "ingest " + *q_collection + " <path>'");
        }

        // The same decision `complete` and `chat` make, from the same facts.
        std::optional<harness::Config> config;
        try {
            config = harness::load_config(harness::resolve_config_path(context.config_path));
        } catch (const std::exception&) {
            // No config is the lexical floor's home ground; the resolver sees
            // no embedder and no pins.
        }
        std::string pin;
        std::string rerank_pin;
        std::string collection_backend;
        if (config.has_value()) {
            if (const harness::EmbeddingConfig* entry = config->find_embedding(*q_collection);
                entry != nullptr) {
                pin = entry->retriever;
                rerank_pin = entry->rerank;
                collection_backend = entry->backend;
            }
        }
        std::optional<Providers> providers;
        std::optional<agentloop::Embedder> embedder;
        std::string embedder_reason;
        if (config.has_value() && *q_retriever != "lexical" && pin != "lexical") {
            providers.emplace(*config);
            embedder = agentloop::resolve_embedder(providers->harness, *config, collection_backend,
                                                   embedder_reason);
        }

        try {
            const embedstore::Store store{path};
            const agentloop::TurnRetrieval decision = agentloop::resolve_turn_retriever(
                *q_retriever, pin, facts_for(embedder), facts_for(store));
            if (!decision.error.empty()) {
                fail(decision.error);
            }
            if (decision.excluded) {
                std::cout << decision.note << "\n";
                return;
            }
            if (!decision.note.empty()) {
                std::cout << decision.note << "\n";
            }

            agentloop::RerankChoice judge;
            if (config.has_value()) {
                judge = agentloop::resolve_turn_rerank(*q_rerank, rerank_pin, *config);
                if (!judge.note.empty()) {
                    std::cout << judge.note << "\n";
                }
                if (!judge.backend.empty() && !providers.has_value()) {
                    providers.emplace(*config);
                }
            }
            const int fetch = agentloop::rerank_fetch_limit(*q_limit, !judge.backend.empty());

            std::vector<embedstore::SearchHit> hits;
            if (decision.retriever == agentloop::Retriever::Lexical) {
                hits = store.search(*q_text, fetch);
            } else {
                const std::vector<std::vector<float>> vectors = embedder->embed({*q_text}, {});
                hits = decision.retriever == agentloop::Retriever::Vector
                           ? store.search_vector(vectors.front(), fetch)
                           : store.search_hybrid(vectors.front(), *q_text, fetch);
            }
            bool reranked = false;
            if (!judge.backend.empty()) {
                const agentloop::RerankOutcome judged = agentloop::rerank(
                    providers->harness, judge.backend, *q_text, hits, *q_limit, {});
                hits = judged.hits;
                reranked = judged.applied;
                if (!judged.note.empty()) {
                    std::cout << judged.note << "\n";
                }
            } else if (*q_limit > 0 && static_cast<std::size_t>(*q_limit) < hits.size()) {
                hits.resize(static_cast<std::size_t>(*q_limit));
            }

            if (hits.empty()) {
                std::cout << "no matches [" << agentloop::to_string(decision.retriever) << "]\n";
                return;
            }
            for (const embedstore::SearchHit& hit : hits) {
                // The retriever is printed with every score. Lexical, vector
                // and RRF scales are incomparable, and a bare number invites
                // exactly the comparison that cannot be made.
                std::cout << two_places(hit.score) << "  [" << hit.retriever
                          << (reranked ? ", reranked" : "") << "]  " << hit.chunk.source << "#"
                          << hit.chunk.ordinal << "\n";
                std::cout << "    " << preview(hit.chunk.text, 100) << "\n";
            }
        } catch (const std::exception& e) {
            fail(e.what());
        }
    });

    // ---- list ---------------------------------------------------------------
    CLI::App* list = cmd->add_subcommand("list", "List collections");
    list->callback([]() {
        const std::vector<std::string> names = collection_names();
        if (names.empty()) {
            std::cout << "no collections yet -- create one with 'apogee embed ingest <name> "
                         "<path>'\n";
            return;
        }
        for (const std::string& name : names) {
            try {
                const embedstore::Store store{collection_path(name)};
                std::cout << name << "  " << store.chunk_count() << " chunk(s), "
                          << store.sources().size() << " source(s)\n";
            } catch (const std::exception& e) {
                std::cout << name << "  (unreadable: " << e.what() << ")\n";
            }
        }
    });

    // ---- info ---------------------------------------------------------------
    auto info_collection = std::make_shared<std::string>();
    CLI::App* info = cmd->add_subcommand("info", "Show a collection's sources and health");
    info->add_option("collection", *info_collection, "Collection name")->required();
    info->callback([info_collection]() {
        require_plain_name(*info_collection);
        const std::filesystem::path path = collection_path(*info_collection);
        std::error_code code;
        if (!std::filesystem::exists(path, code)) {
            fail("no collection named '" + *info_collection + "'");
        }

        try {
            const embedstore::Store store{path};
            std::cout << "collection:  " << *info_collection << "\n";
            std::cout << "path:        " << path.string() << "\n";
            std::cout << "schema:      v" << store.schema_version() << "\n";
            const embedstore::Store::Stats stats = store.stats();
            const embedstore::Store::EmbeddingBinding binding = store.embedding_model();
            std::cout << "chunks:      " << stats.chunk_count << "\n";
            std::cout
                << "lexical:     BM25 over every chunk -- works with no model and no network\n";
            if (stats.dimension == 0) {
                std::cout << "vectors:     none (ingest with --retriever vector to build them)\n";
            } else {
                std::cout << "vectors:     " << (stats.chunk_count - stats.lexical_only) << " of "
                          << stats.chunk_count << " chunk(s), " << stats.dimension << "-d";
                if (binding.recorded()) {
                    std::cout << " [" << binding.model << "]";
                } else {
                    std::cout << " [model not recorded -- re-ingest to bind]";
                }
                if (stats.vector_dims > 1) {
                    std::cout << " -- MIXED: " << stats.vector_dims << " widths";
                }
                std::cout << "\n";
            }

            const std::string trouble = store.verify_index();
            std::cout << "index:       " << (trouble.empty() ? "ok" : "FAILED -- " + trouble)
                      << "\n";

            std::cout << "\nsources:\n";
            for (const std::string& source : store.sources()) {
                std::cout << "  " << source << "\n";
            }
        } catch (const std::exception& e) {
            fail(e.what());
        }
    });

    // ---- delete -------------------------------------------------------------
    auto del_collection = std::make_shared<std::string>();
    auto del_source = std::make_shared<std::string>();
    auto del_yes = std::make_shared<bool>(false);

    CLI::App* remove = cmd->add_subcommand("delete", "Delete a collection, or one source in it");
    remove->add_option("collection", *del_collection, "Collection name")->required();
    remove->add_option("--source", *del_source, "Delete only this source, keeping the rest");
    remove->add_flag("-y,--yes", *del_yes, "Do not ask for confirmation");

    remove->callback([del_collection, del_source, del_yes]() {
        require_plain_name(*del_collection);
        const std::filesystem::path path = collection_path(*del_collection);
        std::error_code code;
        if (!std::filesystem::exists(path, code)) {
            fail("no collection named '" + *del_collection + "'");
        }

        if (!del_source->empty()) {
            try {
                embedstore::Store store{path};
                const std::int64_t gone = store.delete_source(*del_source);
                std::cout << "removed " << gone << " chunk(s) from '" << *del_source << "'\n";
            } catch (const std::exception& e) {
                fail(e.what());
            }
            return;
        }

        std::cout << "will delete the whole collection:\n  " << path.string() << "\n";
        if (!*del_yes) {
            std::cout << "\nre-run with --yes to delete.\n";
            return;
        }
        std::filesystem::remove(path, code);
        if (code) {
            fail("could not delete: " + code.message());
        }
        // WAL and shared-memory siblings, which SQLite leaves beside the file.
        for (const char* suffix : {"-wal", "-shm"}) {
            std::filesystem::path sibling = path;
            sibling += suffix;
            std::filesystem::remove(sibling, code);
        }
        std::cout << "\ndeleted.\n";
    });
}

}  // namespace apogee::commands
