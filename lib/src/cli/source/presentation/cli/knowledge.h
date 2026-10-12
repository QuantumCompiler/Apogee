#pragma once

#include <nlohmann/json_fwd.hpp>

#include <iosfwd>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "cli/command.h"
#include "knowledge/query.h"
#include "knowledge/record.h"

/// `apogee knowledge` (alias `kn`) -- the organizational knowledge layer.
///
/// `capture` runs the normalization clerk over a raw conversation -- an
/// argument, a file, a saved chat, or piped stdin -- and stores ONE canonical
/// record: the why behind a decision, what was chosen, whether it shipped,
/// who said it (separately, so it can be stripped), and the artifact it
/// produced. The record lands in an ordinary collection as one chunk whose
/// text is the immutable reasoning and whose metadata is the whole record,
/// with the raw conversation archived privately beside it; `--dry-run` runs
/// everything but the write and says what a real run would do.
///
/// Every path -- this command, chat's `/capture`, the exit-time auto-capture,
/// the HTTP twin -- goes through `cli/knowledge_core.h`, so the record
/// each produces is the same record.
namespace apogee::commands {

/// A knowledge read or act refused, in the command's own words (37c):
/// `backend` when a model call failed rather than the request -- the
/// command's two exit codes.
class KnowledgeRefusal : public std::runtime_error {
public:
    KnowledgeRefusal(const std::string& message, bool backend)
        : std::runtime_error{message}, backend_{backend} {}

    [[nodiscard]] bool backend() const noexcept {
        return backend_;
    }

private:
    bool backend_ = false;
};

/// The records `knowledge list` reads, and the collection they are in.
struct KnowledgeRecords {
    std::string db;
    std::vector<knowledge::Record> records;
};

/// `knowledge list`'s read (37c): the collection `db` names (else the
/// config's, else `knowledge`), newest first, kept to `status` and
/// `discipline` when given -- what the command, its JSON document and the
/// shell's Knowledge view draw. Throws KnowledgeRefusal for a collection that
/// does not exist.
[[nodiscard]] KnowledgeRecords read_knowledge_records(const RootContext& context,
                                                      const std::string& db = {},
                                                      const std::string& status = {},
                                                      const std::string& discipline = {});

/// `knowledge list` as a person reads it.
[[nodiscard]] std::string knowledge_list_text(const KnowledgeRecords& read);

/// `knowledge list --output-format json`: `{"object": "list", "db", "data":
/// [records]}`, each record as `--json` prints it.
[[nodiscard]] nlohmann::json knowledge_list_document(const KnowledgeRecords& read);

/// `knowledge info` as a person reads it: the id, every field, when captured.
[[nodiscard]] std::string knowledge_record_text(const knowledge::Record& record);

/// `knowledge query`'s arguments.
struct KnowledgeQuery {
    std::string question;
    std::string status{knowledge::kStatusShipped};
    std::string discipline;
    int top_k = 5;
    std::string retriever;
    std::string rerank;
    std::string db;
};

/// What `knowledge query` found, and in which collection.
struct KnowledgeAnswer {
    std::string db;
    knowledge::QueryResult result;
};

/// `knowledge query`'s search (37c): the one retriever resolved from the flag,
/// the collection's pin and the facts, a rerank judge when one applies --
/// providers built only when an embedder or a judge might be needed. Throws
/// KnowledgeRefusal in the command's words.
[[nodiscard]] KnowledgeAnswer run_knowledge_query(const RootContext& context,
                                                  const KnowledgeQuery& query);

/// The answer as the command prints it: its notes, the retriever that ran,
/// each record's score on that retriever's scale.
void print_knowledge_answer(std::ostream& out, const KnowledgeQuery& query,
                            const KnowledgeAnswer& answer);

/// Both, as text -- the shell's query row.
[[nodiscard]] std::string knowledge_query_text(const RootContext& context,
                                               const KnowledgeQuery& query);

/// `knowledge delete`: the record and its raw archive, said as the command
/// says it. Throws KnowledgeRefusal in its words.
[[nodiscard]] std::string delete_knowledge_record(const RootContext& context, const std::string& db,
                                                  const std::string& id);

class KnowledgeCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
