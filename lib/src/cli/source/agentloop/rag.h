#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "agentloop/embed_func.h"
#include "agentloop/retriever.h"
#include "harness/cancellation.h"
#include "harness/config.h"
#include "harness/harness.h"
#include "harness/types.h"

/// Retrieval-augmented generation: finding relevant chunks and splicing them
/// into one outgoing request.
///
/// ## The injected context is transient, and that is the load-bearing property
///
/// Retrieved chunks go into the request that is about to be sent and **nowhere
/// else**. They are never appended to the conversation the session persists.
/// Two reasons, and the second is the one that bites:
///
/// - A transcript full of injected documents is unreadable, and grows without
///   bound as every turn adds more.
/// - Retrieval is *per turn*. Turn two's question needs turn two's chunks; if
///   turn one's chunks were in history they would still be there, competing for
///   attention with the ones that actually answer the new question.
///
/// The seam already exists — `agentloop::Options::transient_prefix` and
/// `transient_at`, spliced by `splice_transient` and test-locked as never
/// reaching persisted history. This file only decides *what* to put there.
namespace apogee::agentloop {

/// What a retrieval produced, for the caller to report.
struct RagResult {
    /// The messages to hand to `Options::transient_prefix`. Empty when nothing
    /// was found, which is a normal outcome and not an error.
    std::vector<harness::ChatMessage> prefix;

    std::int64_t chunks = 0;
    /// The best normalised score among the injected chunks, 0 when none.
    double top_score = 0.0;
    /// Which retriever produced those scores.
    ///
    /// **Always reported**, because lexical and vector scores are on
    /// incomparable scales and a number shown without its retriever invites
    /// exactly the comparison that cannot be made.
    std::string retriever;
    /// Set when retrieval could not run at all — a missing collection, say.
    /// A reason to tell the user, not a reason to fail the turn.
    std::string error;

    /// Whether a judge's ranking was actually applied. False on every rerank
    /// degradation, so the claim and the ordering come from one place.
    bool reranked = false;

    /// Things worth saying once: why auto fell back, why hybrid ran lexical,
    /// why a collection was excluded, why the judge was not used.
    std::vector<std::string> notes;
};

/// Everything one turn's retrieval needs, gathered by the surface.
struct RagTurn {
    std::filesystem::path store_path;
    std::string question;
    int limit = 4;

    /// Raw spellings, already validated with `valid_retriever`.
    std::string retriever_flag;
    std::string retriever_pin;
    std::string rerank_flag;
    std::string rerank_pin;

    /// The embedder that would answer, or nullopt when none resolves.
    std::optional<Embedder> embedder;
    /// Why it did not resolve, for the note.
    std::string embedder_reason;

    /// For the judge. May be null, in which case reranking is off.
    const harness::Harness* harness = nullptr;
    const harness::Config* config = nullptr;
    harness::CancellationToken cancellation;
};

/// The full retrieval matrix for one turn: resolve the retriever ONCE, run it,
/// optionally rerank, and package the transient prefix. Never throws for an
/// ordinary problem; `error` is set when the user asked for something that
/// cannot run (an explicit vector search with no vectors), which the surface
/// must fail rather than substitute.
[[nodiscard]] RagResult retrieve_for_turn(const RagTurn& turn);

/// Retrieves for `question` from the collection at `store_path`.
///
/// Never throws for an ordinary problem: a collection that does not exist, or
/// one with nothing in it, comes back as an empty result with `error` set. A
/// turn should still happen — answering without retrieved context is much
/// better than refusing to answer.
[[nodiscard]] RagResult build_rag_prefix(const std::filesystem::path& store_path,
                                         const std::string& question, int limit);

/// Renders retrieved chunks as the message text that gets injected.
///
/// Exposed so the exact wording is testable, and so a caller can see what the
/// model will see. The framing tells the model these are *retrieved excerpts*
/// rather than something the user said — without that, a model treats an
/// injected document as the user's own words and answers about it instead of
/// about the question.
[[nodiscard]] std::string render_rag_context(const std::vector<std::string>& chunks);

}  // namespace apogee::agentloop
