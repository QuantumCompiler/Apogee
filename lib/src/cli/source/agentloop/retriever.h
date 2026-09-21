#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

/// Which retriever a turn runs, decided ONCE, by one function, for every
/// surface.
///
/// ## One retriever per turn, reported honestly
///
/// Normalised BM25, cosine and RRF are three scales. A turn runs exactly one
/// of lexical / vector / hybrid and every surface reports which -- including
/// when it degraded: nothing ever claims hybrid or vector when what ran was
/// lexical. That reporting contract is why this is a pure function over facts
/// rather than a branch inside each command: Ommi's subtlest retrieval bugs
/// lived in resolution rules that had drifted between surfaces, and a rule
/// that exists once cannot drift.
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
    std::string recorded_model;
    /// Vector width, 0 when the store holds no vectors.
    std::int64_t dimension = 0;
    /// Chunks with no vector.
    std::int64_t lexical_only = 0;
    /// Distinct vector widths; more than one means mixed spaces.
    std::int64_t vector_dims = 0;
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

}  // namespace apogee::agentloop
