#pragma once

#include <string>
#include <vector>

#include "harness/cancellation.h"
#include "harness/harness.h"
#include "harness/types.h"

/// Turning a follow-up question into a search query that stands on its own
/// (26b).
///
/// Retrieval searches with the user's words, and a follow-up's words point
/// back into the conversation -- "and what about the second one?" finds
/// nothing, because the thing it asks about was named two turns ago. One
/// short side request to the utility model restates it with those names
/// filled in, before the collection is searched. The chat model's own
/// question, and what it is shown, are unchanged; only the search is.
///
/// Never fails a turn: any problem -- the model erring, answering nothing,
/// answering at length -- searches with the question as asked.
namespace apogee::agentloop {

/// What a rewrite produced.
struct QueryRewrite {
    /// The query to search with: the rewrite, or the question as asked.
    std::string query;
    /// Whether `query` is a rewrite.
    bool rewritten = false;
    /// Why the question was kept as asked, when a rewrite was tried.
    std::string note;
};

/// Whether `history` -- the conversation with the question last -- has an
/// earlier user turn: the only case a rewrite can help, since a first
/// question already stands alone (26b, default taken).
[[nodiscard]] bool has_earlier_turn(const std::vector<harness::ChatMessage>& history);

/// The request that asks `backend` for the standalone query: the latest
/// turns, clipped, and the question. A side request, with reasoning skipped
/// and a small budget. Exposed so its shape is testable.
[[nodiscard]] harness::ChatRequest rewrite_request(const std::string& backend,
                                                   const std::vector<harness::ChatMessage>& history,
                                                   const std::string& question);

/// The query to search with for `question`, asked of `backend` over
/// `history`. When there is no earlier turn, nothing is asked.
[[nodiscard]] QueryRewrite rewrite_query(const harness::Harness& harness,
                                         const std::string& backend,
                                         const std::vector<harness::ChatMessage>& history,
                                         const std::string& question,
                                         const harness::CancellationToken& cancellation);

}  // namespace apogee::agentloop
