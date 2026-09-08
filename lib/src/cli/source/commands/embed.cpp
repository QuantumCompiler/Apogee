#include "commands/embed.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <system_error>

#include "embedstore/ingest.h"
#include "embedstore/store.h"
#include "harness/layout.h"

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
    (void)context;  // collections live under APOGEE_HOME, not beside the config

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
    ingest->add_option("--chunk-size", *in_size, "Codepoints per chunk (default 512)");
    ingest->add_option("--chunk-overlap", *in_overlap, "Codepoints of overlap (default 64)");

    ingest->callback([in_collection, in_path, in_size, in_overlap]() {
        require_plain_name(*in_collection);

        const std::filesystem::path target{*in_path};
        std::error_code code;
        if (!std::filesystem::exists(target, code)) {
            fail("no such file or directory: " + target.string());
        }

        embedstore::IngestReport report;
        try {
            report = embedstore::ingest_path(collection_path(*in_collection), target,
                                             {.size = *in_size, .overlap = *in_overlap});
        } catch (const std::exception& e) {
            fail(e.what());
        }

        std::cout << report.files_read << " file(s), " << report.chunks_written << " chunk(s)\n";
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
    });

    // ---- query --------------------------------------------------------------
    auto q_collection = std::make_shared<std::string>();
    auto q_text = std::make_shared<std::string>();
    auto q_limit = std::make_shared<int>(5);

    CLI::App* query = cmd->add_subcommand("query", "Search a collection");
    query->add_option("collection", *q_collection, "Collection name")->required();
    query->add_option("text", *q_text, "What to search for")->required();
    query->add_option("-n,--limit", *q_limit, "How many results (default 5)");

    query->callback([q_collection, q_text, q_limit]() {
        require_plain_name(*q_collection);
        const std::filesystem::path path = collection_path(*q_collection);
        std::error_code code;
        if (!std::filesystem::exists(path, code)) {
            fail("no collection named '" + *q_collection + "'. Create one with 'apogee embed " +
                 "ingest " + *q_collection + " <path>'");
        }

        try {
            const embedstore::Store store{path};
            const std::vector<embedstore::SearchHit> hits = store.search(*q_text, *q_limit);
            if (hits.empty()) {
                std::cout << "no matches\n";
                return;
            }
            for (const embedstore::SearchHit& hit : hits) {
                // The retriever is printed with every score. Lexical and vector
                // scales are incomparable, and a bare number invites exactly
                // the comparison that cannot be made.
                std::cout << two_places(hit.score) << "  [" << hit.retriever << "]  "
                          << hit.chunk.source << "#" << hit.chunk.ordinal << "\n";
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
            std::cout << "chunks:      " << store.chunk_count() << "\n";
            std::cout << "retriever:   lexical (BM25) -- works with no model and no network\n";

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
