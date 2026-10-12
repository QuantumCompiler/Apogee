#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

#include "cli/command.h"
#include "operations/collections.h"

/// `apogee embed` — ingest documents and search them.
///
/// **The whole surface works with no model, no API key, and no network.** That
/// is the point of building the lexical floor first: retrieval is available to
/// someone who has installed Apogee and nothing else, and it keeps working
/// when no embedding model can be had — the lexical path never needs one.
namespace apogee::commands {

/// One collection as `embed list` reports it (37c): the row function the
/// listing, its JSON document and the shell's Collections view all draw.
struct CollectionRow {
    std::string name;
    std::int64_t chunks = 0;
    std::size_t sources = 0;
    /// Why it could not be read, or empty.
    std::string error;
};

[[nodiscard]] std::vector<CollectionRow> collection_rows();

/// `embed list` as a person reads it.
[[nodiscard]] std::string render_collection_rows(const std::vector<CollectionRow>& rows);

/// `embed list --output-format json`: `{"object": "list", "data": [{"name",
/// "chunks", "sources", "error"?}]}`.
[[nodiscard]] nlohmann::json collection_list_document(const std::vector<CollectionRow>& rows);

/// `embed info <name>` as a person reads it, and as one document (37c):
/// `{"collection", "path", "schema", "chunks", "vectors": {"chunks",
/// "dimension", "model"?, "widths"} | null, "index", "sources": [...]}`.
/// Each throws std::runtime_error in the command's words for a name that is
/// not plain or names no collection.
[[nodiscard]] std::string collection_info_text(const std::string& name);
[[nodiscard]] nlohmann::json collection_info_document(const std::string& name);

/// `embed query`'s arguments.
struct CollectionQuery {
    std::string collection;
    std::string text;
    int limit = 5;
    std::string retriever;
    std::string rerank;
};

/// `embed query` as the command prints it (37c): the retriever decided by the
/// one resolver from the same facts, its notes, each hit's score beside the
/// retriever that scored it. Throws std::runtime_error in the command's words.
[[nodiscard]] std::string collection_query_text(const RootContext& context,
                                                const CollectionQuery& query);

/// The same, written to `out` as it goes -- what the command runs, so a
/// failure past the first notes leaves them said.
void print_collection_query(std::ostream& out, const RootContext& context,
                            const CollectionQuery& query);

/// `embed delete <name> --yes`: the whole collection and SQLite's siblings,
/// said as the command says it. Throws std::runtime_error in its words.
[[nodiscard]] std::string delete_collection(const std::string& name);

class EmbedCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
