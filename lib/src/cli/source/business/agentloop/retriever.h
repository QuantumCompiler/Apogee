#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// Which retriever a turn runs, decided ONCE, by one function, for every
/// surface.
///
/// ## One retriever per turn, reported honestly
///
/// Normalised BM25, cosine and RRF are three scales. A turn runs exactly one
/// of lexical / vector / hybrid and every surface reports which -- including
/// when it degraded: nothing ever claims hybrid or vector when what ran was
/// lexical. That reporting contract is why this is a pure function over facts
/// rather than a branch inside each command: the subtlest retrieval bugs live
/// in resolution rules that drift between surfaces, and a rule that exists
/// once cannot drift.
///
/// ## The rules
///
/// Precedence: the `--retriever` flag, then the collection's `retriever:`
/// pin, then auto.
///
/// - **Explicit `lexical`** always runs lexical.
/// - **Explicit `vector`** needs an embedder AND a store whose vectors were
///   recorded under that embedder's model, with every chunk vectorised in one
///   space. From the flag, anything short of that is a **hard error** naming
///   lexical as the way out -- the user asked for something that cannot be
///   done and must not get something else. From a pin, the collection is
///   **excluded with a note**: a vector-pinned collection is never silently
///   searched the other way.
/// - **Explicit `hybrid`** needs the same vector half; without it the turn
///   runs and is *reported* as lexical, with the reason. Never partial, never
///   an error.
/// - **Auto** is vector only when everything above holds, else lexical -- with
///   a reason when the fallback is worth surfacing (a model mismatch, partial
///   coverage, mixed spaces), each carrying the re-ingest hint. **Auto never
///   resolves to hybrid.**
///
/// ## Spend
///
/// Decided by the user, 2026-09-13: a whole collection is never vectorised
/// through a metered embedder unless asked (`resolve_ingest_retriever` turns
/// auto into lexical with a note), but a question against a collection already
/// built with that model is one small call and is allowed. Whether an embedder
/// is metered is a fact it states about itself.
namespace apogee::agentloop {

enum class Retriever : std::uint8_t { Lexical, Vector, Hybrid };

[[nodiscard]] std::string_view to_string(Retriever retriever) noexcept;

/// Accepted spellings: `lexical`, `vector`, `hybrid`, and `auto` or empty for
/// automatic. **The one validator every surface uses** -- the flag, the config
/// pin checked by `apogee check`, the chat command -- so a value cannot be
/// accepted on one surface and refused on another.
[[nodiscard]] bool valid_retriever(std::string_view value) noexcept;

/// The words `valid_retriever` accepts, `auto` last -- for completion to offer.
[[nodiscard]] std::span<const std::string_view> retriever_names() noexcept;

/// The ones an ingest takes: what gets built, `lexical` (the text index) or
/// `vector` (every chunk embedded too), or `auto`. Hybrid is a way of
/// searching, not of building.
[[nodiscard]] std::span<const std::string_view> ingest_retriever_names() noexcept;

/// The shared "not a retriever" message. `label` is how the surface spells the
/// setting: `--retriever`, or `retriever` for a config field.
[[nodiscard]] std::string retriever_values_message(std::string_view label, std::string_view got);

/// What the resolver needs to know about the embedder that would answer.
struct EmbedderFacts {
    bool available = false;
    std::string model;
    bool metered = true;
};

/// What the resolver needs to know about the store.
struct StoreFacts {
    bool exists = false;
    std::int64_t chunk_count = 0;
    /// The model recorded at vector ingest; empty when none.
    std::string recorded_model{};
    /// Vector width, 0 when the store holds no vectors.
    std::int64_t dimension = 0;
    /// Chunks with no vector.
    std::int64_t lexical_only = 0;
    /// Distinct vector widths; more than one means mixed spaces.
    std::int64_t vector_dims = 0;
    /// The models a store marked mixed holds vectors from (57s), an empty
    /// name for an unrecorded one; empty when it is not mixed.
    std::vector<std::string> mixed_models{};
};

/// The decision for one turn.
struct TurnRetrieval {
    Retriever retriever = Retriever::Lexical;
    /// A vector-pinned collection whose vectors do not qualify: searched by
    /// nothing this turn, and the note says why.
    bool excluded = false;
    /// Set when the user asked explicitly for something that cannot run. The
    /// surface must fail the request rather than substitute.
    std::string error;
    /// Worth telling the user once: why auto fell back, why hybrid ran
    /// lexical, why a collection was excluded.
    std::string note;
};

/// Resolves the retriever for a query. `flag` and `pin` are the raw spellings
/// (validated first with `valid_retriever`); empty or `auto` defers.
[[nodiscard]] TurnRetrieval resolve_turn_retriever(std::string_view flag, std::string_view pin,
                                                   const EmbedderFacts& embedder,
                                                   const StoreFacts& store);

/// The ingest decision: lexical or vector (never hybrid -- fusion is a query
/// concern; a hybrid flag or pin resolves like auto here). Auto is vector when
/// an embedder resolves that is not metered; a metered one needs the explicit
/// ask, and `note` says so.
struct IngestRetrieval {
    Retriever retriever = Retriever::Lexical;
    std::string error;
    std::string note;
};

[[nodiscard]] IngestRetrieval resolve_ingest_retriever(std::string_view flag, std::string_view pin,
                                                       const EmbedderFacts& embedder);

/// What a store holds after an ingest wrote to it, and what the ingest did:
/// the facts its binding afterwards is decided from (57s).
struct IngestBindingFacts {
    /// The model each source holding vectors after the ingest has them from,
    /// one entry per source: the model its own ingest recorded (since 57s),
    /// else the model the store was bound to before the ingest -- every
    /// vector of a bound store is that model's -- else empty, unknown.
    std::vector<std::string> source_models{};
    /// The model this run embedded with; empty for a lexical run.
    std::string run_model{};
    /// The vectors this run wrote.
    std::int64_t vectors_written = 0;
};

/// What an ingest leaves the store's binding saying.
enum class BindingAction : std::uint8_t {
    /// As it was: no source's model is known, so nothing can be said of it.
    Keep,
    /// Bound to `model`: every vector the store holds is from it.
    Bind,
    /// Unrecorded: the store holds no vectors at all.
    Clear,
    /// Marked mixed over `models`: its sources' vectors are from more than
    /// one model, so every query demotes the store wholesale.
    Mixed,
};

struct IngestBinding {
    BindingAction action = BindingAction::Keep;
    /// `Bind`'s model.
    std::string model{};
    /// `Mixed`'s models in the order its sources first name them, this
    /// run's last; an empty name stands for an unrecorded model.
    std::vector<std::string> models{};
    /// Said when a run that wrote vectors leaves the store mixed: how many
    /// sources keep which other model's vectors, and what makes the
    /// collection one space again.
    std::string warning{};
};

/// **The one decision of what a store is bound to after an ingest** (57s),
/// beside the one that decides how it is built. A store is bound to a model
/// only when every source holding vectors has them from it -- it was bound
/// to this run's model already, or no vector from another survives the run;
/// otherwise it is marked mixed, and a run that made it so warns, so no query
/// ranks one model's vectors against another's question: the wholesale
/// demotion, never a fraction searched under a `[vector]` label. A store left
/// with no vectors is unbound; one whose every source's model is unknown is
/// left as it was.
[[nodiscard]] IngestBinding resolve_ingest_binding(const IngestBindingFacts& facts);

/// `models` as a person reads them: each quoted, an unrecorded one said so,
/// joined with "and".
[[nodiscard]] std::string spoken_models(const std::vector<std::string>& models);

/// How strongly a turn's best hit matched, as a person reads it (26s).
///
/// Per retriever, because the three scales are incomparable: a hybrid
/// score is Reciprocal Rank Fusion, whose best possible value -- first in both
/// lists -- is 2/61, so `0.032` is a top match that reads as a poor one;
/// cosine is read as it is; normalised BM25 saturates (an unrelated question
/// scored 0.950 on the probe), so a lexical match is read by how many of the
/// question's own words the hit holds instead.
struct MatchStrength {
    /// `strong`, `fair` or `weak`; empty when there was nothing to read it by.
    std::string band;
    /// Under the retriever's floor: the turn injects nothing, and says so.
    bool floored = false;
    /// What it was read by, for the line: `98% of RRF's ceiling`, `2 of 3
    /// question words`; empty for cosine, which the score itself is.
    std::string measure{};
};

/// The floors, named (26s, defaults confirmed, tuned by tests): a hybrid
/// match under a quarter of RRF's ceiling, a cosine under 0.25, a lexical
/// match holding none of the question's words.
inline constexpr double kRrfFloor = 0.25;
inline constexpr double kCosineFloor = 0.25;
inline constexpr std::size_t kLexicalFloorWords = 1;

/// The band edges above each floor.
inline constexpr double kRrfStrong = 0.75;
inline constexpr double kRrfFair = 0.5;
inline constexpr double kCosineStrong = 0.6;
inline constexpr double kCosineFair = 0.4;
inline constexpr double kWordsStrong = 0.75;
inline constexpr double kWordsFair = 0.4;

/// RRF's best possible score: first in both of a hybrid search's lists.
[[nodiscard]] double rrf_ceiling() noexcept;

/// How many of `question`'s content words -- stop words left out -- `text`
/// holds, word for word, as the lexical index itself matches: measured on
/// the docs, sharing a stem let "painted" count for "painter".
struct WordCoverage {
    std::size_t found = 0;
    std::size_t asked = 0;
};

[[nodiscard]] WordCoverage word_coverage(std::string_view question, std::string_view text);

/// What a turn's best hit gives to read its strength by.
struct MatchEvidence {
    /// The retriever that RAN: `lexical`, `vector` or `hybrid`.
    std::string retriever;
    /// The best hit's score, on that retriever's scale.
    double score = 0.0;
    /// The question's words the best hit holds.
    WordCoverage words{};
    /// A hybrid hit's cosine in the vector half, when it was in it.
    std::optional<double> cosine{};
};

/// The band and the floor for `evidence`. A hybrid match is floored under a
/// quarter of RRF's ceiling -- which at the fetch depth a hybrid search uses
/// a hit never falls to -- and, where ranks cannot tell, when neither half
/// has evidence for it: a cosine under 0.25 and none of the question's
/// words.
[[nodiscard]] MatchStrength match_strength(const MatchEvidence& evidence);

}  // namespace apogee::agentloop
