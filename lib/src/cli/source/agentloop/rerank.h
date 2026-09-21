#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "embedstore/store.h"
#include "harness/cancellation.h"
#include "harness/harness.h"

/// A generation model reorders retrieved chunks -- the relevance judgement
/// BM25 lacks -- as one call per turn that can never cost context.
///
/// ## The (ranked, applied) contract
///
/// `rerank` never fails. Every failure path -- no judge, a provider error, a
/// reply that is not a ranking -- returns the raw retrieval order with
/// `applied = false`, and the reason. A caller therefore cannot label results
/// "reranked" when they are really raw order: the ordering and the claim about
/// the ordering come from the same place. A judge that legitimately drops
/// every candidate returns nothing with `applied = true`: that is a verdict --
/// the relevance floor -- not a failure.
///
/// ## Structured output
///
/// The item planned provider-native structured output so that "the reply is
/// not a ranking" largely vanishes as a failure mode. The IR carries no such
/// request yet; this asks for a bare JSON array in prose and parses tolerantly
/// (fences, surrounding text, duplicates and out-of-range ids dropped). The
/// never-fail contract is what makes deferring that safe: a judge that
/// misbehaves costs nothing but the improvement.
namespace apogee::agentloop {

/// `--rerank off`, or `rerank: off` on a collection: disabled for the run.
inline constexpr std::string_view kRerankOff = "off";

/// Multiplies the limit to size the candidate set the judge sees: 5 injected
/// chunks are chosen from ~50. Reranking can only improve on what retrieval
/// returned, so the wider net is the point.
inline constexpr int kRerankWidenFactor = 10;
/// Caps the widened set so one turn cannot build an unbounded prompt (and an
/// unbounded bill) out of a large collection.
inline constexpr int kRerankMaxCandidates = 50;
/// Codepoints of each candidate shown to the judge. Enough to judge
/// relevance; far less than the chunk that is actually injected.
inline constexpr std::size_t kRerankSnippetCodepoints = 400;
/// The judge's generation budget. The ANSWER is a JSON array of a few dozen
/// small integers; the budget is sized for a **reasoning** judge, which spends
/// most of it thinking before the array appears. Found live: gpt-oss at 256
/// tokens never reached its final channel, and every rerank degraded to raw
/// order with "not a ranking" -- correct under the contract, and useless.
inline constexpr std::int64_t kRerankMaxTokens = 1024;

/// Which backend judges this turn: the flag, else the collection's pin, else
/// none. A backend that is not configured disables reranking with a note
/// rather than guessing another.
struct RerankChoice {
    /// Empty means no reranking this turn.
    std::string backend;
    std::string note;
};

[[nodiscard]] RerankChoice resolve_turn_rerank(std::string_view flag, std::string_view pin,
                                               const harness::Config& config);

/// How many candidates to retrieve when a judge will run: the widened set,
/// capped. `limit` unchanged when it will not, so the plain path retrieves
/// exactly what it always did.
[[nodiscard]] int rerank_fetch_limit(int limit, bool reranking) noexcept;

/// The judging prompt. Candidates are numbered from 1 -- a model reproduces
/// small integers far more reliably than chunk text.
[[nodiscard]] std::string build_rerank_prompt(std::string_view question,
                                              const std::vector<embedstore::SearchHit>& candidates,
                                              int limit);

/// The 1-based ids in a reply, tolerating prose and fences around the array
/// and dropping duplicates and out-of-range ids. nullopt only when no array
/// could be read at all; an empty array is a valid "none of these".
[[nodiscard]] std::optional<std::vector<int>> parse_rerank_ids(std::string_view reply,
                                                               std::size_t candidates);

struct RerankOutcome {
    std::vector<embedstore::SearchHit> hits;
    bool applied = false;
    /// Why the judge's ranking was not used, when it was not.
    std::string note;
};

/// One judge call over `hits`, cut to `limit`. See the contract above.
[[nodiscard]] RerankOutcome rerank(const harness::Harness& harness, std::string_view backend,
                                   std::string_view question,
                                   const std::vector<embedstore::SearchHit>& hits, int limit,
                                   const harness::CancellationToken& cancellation);

}  // namespace apogee::agentloop
