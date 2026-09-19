#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "agentloop/retriever.h"
#include "embedstore/store.h"
#include "harness/config.h"
#include "harness/harness.h"
#include "knowledge/record.h"
#include "knowledge/store.h"

/// Records back out: the question, the one retriever resolver, the filters
/// applied BEFORE the cut, and the judge under its never-fail contract.
///
/// **Defaults to shipped.** A brainstorm is mostly roads not taken, and a
/// query that returned an abandoned idea's rationale as the reason something
/// exists would be worse than no answer. `status = ""` crosses every branch;
/// `rejected` asks what was considered and dropped.
///
/// **Rank everything, then filter, then cut.** A status filter applied after
/// a top-k cut starves the result the moment the best lexical matches sit on
/// another branch; here the whole collection is ranked, the off-branch
/// records fall out, and only then is the list cut.
namespace apogee::knowledge {

struct QueryOptions {
    std::string question;
    /// The branch marker to keep; empty keeps every branch.
    std::string status{kStatusShipped};
    /// Empty keeps every discipline.
    std::string discipline;
    int top_k = 5;
    /// The raw `--retriever` spelling; empty means auto.
    std::string retriever_flag;
    /// The raw `--rerank` spelling; empty means the collection's pin.
    std::string rerank_flag;
};

/// A record and where it ranked. `chunk_id` is a storage handle, not part of
/// the record -- the seed a graph walk expands from later -- and stays off
/// the wire.
struct ScoredRecord {
    Record record;
    /// On the retriever's scale: normalised BM25, cosine, or RRF.
    double score = 0.0;
    std::int64_t chunk_id = 0;
};

struct QueryResult {
    std::vector<ScoredRecord> records;
    /// The retriever that ran -- set from the same decision that chose it.
    agentloop::Retriever retriever = agentloop::Retriever::Lexical;
    /// Whether the judge's ranking was applied; false on every degradation.
    bool reranked = false;
    /// A vector-pinned collection whose vectors do not qualify: nothing was
    /// searched, and the note says why.
    bool excluded = false;
    std::vector<std::string> notes;
    /// The user asked for something that cannot run (an explicit vector ask
    /// with nothing to run it), or the embedder failed. The surface fails
    /// rather than substitutes.
    std::string error;
    bool backend_error = false;

    [[nodiscard]] bool ok() const noexcept {
        return error.empty();
    }
};

/// The hits that are records on the wanted branch and discipline, in rank
/// order, cut to `limit` (0 for no cut). Chunks that are not records -- plain
/// documents in a shared collection -- fall out. Exposed so the
/// filter-before-cut rule is testable on its own.
[[nodiscard]] std::vector<embedstore::SearchHit> filter_hits(
    const std::vector<embedstore::SearchHit>& hits, std::string_view status,
    std::string_view discipline, int limit);

/// One query against `collection`'s store: the collection's pins from
/// `config`, the embedder the harness resolves, the ONE resolver, the search,
/// the filters, the judge, the cut.
[[nodiscard]] QueryResult query(const Store& store, const harness::Harness& harness,
                                const harness::Config& config, std::string_view collection,
                                const QueryOptions& options);

}  // namespace apogee::knowledge
