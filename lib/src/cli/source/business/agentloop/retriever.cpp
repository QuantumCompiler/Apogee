#include "agentloop/retriever.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <set>

#include "embedstore/vector.h"

namespace apogee::agentloop {
namespace {

constexpr std::string_view kLexical = "lexical";
constexpr std::string_view kVector = "vector";
constexpr std::string_view kHybrid = "hybrid";
constexpr std::string_view kAuto = "auto";

constexpr std::array<std::string_view, 4> kRetrieverNames{kLexical, kVector, kHybrid, kAuto};
constexpr std::array<std::string_view, 3> kIngestRetrieverNames{kLexical, kVector, kAuto};

/// The explicit choice from the flag, else the pin, else nullopt for auto.
[[nodiscard]] std::optional<Retriever> explicit_choice(std::string_view flag,
                                                       std::string_view pin) noexcept {
    for (const std::string_view value : {flag, pin}) {
        if (value == kLexical) {
            return Retriever::Lexical;
        }
        if (value == kVector) {
            return Retriever::Vector;
        }
        if (value == kHybrid) {
            return Retriever::Hybrid;
        }
        // empty or auto: fall through to the next rung
    }
    return std::nullopt;
}

/// Whether the store's vectors qualify for a vector search under this
/// embedder, and if not, why -- with the re-ingest hint.
[[nodiscard]] std::string why_not_vector(const EmbedderFacts& embedder, const StoreFacts& store) {
    if (!embedder.available) {
        return "no embedding backend is configured";
    }
    if (!store.exists || store.chunk_count == 0) {
        return "the collection is empty";
    }
    if (store.dimension == 0) {
        return "the collection holds no vectors -- re-run `apogee embed ingest` with "
               "--retriever vector to build them";
    }
    if (store.recorded_model.empty() || store.recorded_model != embedder.model) {
        const std::string from = store.recorded_model.empty()
                                     ? "an unrecorded model (or an interrupted vector ingest)"
                                     : "model '" + store.recorded_model + "'";
        return "the collection's vectors are from " + from +
               " but the embedding backend would use '" + embedder.model +
               "' -- re-run `apogee embed ingest` with the current model to vector-query";
    }
    if (store.vector_dims > 1) {
        return "the collection holds vectors of " + std::to_string(store.vector_dims) +
               " different widths (mixed vector spaces) -- re-run `apogee embed ingest` over "
               "every source with one model";
    }
    if (store.lexical_only > 0) {
        return std::to_string(store.lexical_only) + " of " + std::to_string(store.chunk_count) +
               " chunks have no vectors and would be invisible to vector search -- re-run "
               "`apogee embed ingest` over every source to vector-query";
    }
    return {};
}

}  // namespace

std::span<const std::string_view> retriever_names() noexcept {
    return kRetrieverNames;
}

std::span<const std::string_view> ingest_retriever_names() noexcept {
    return kIngestRetrieverNames;
}

std::string_view to_string(Retriever retriever) noexcept {
    switch (retriever) {
        case Retriever::Lexical:
            return kLexical;
        case Retriever::Vector:
            return kVector;
        case Retriever::Hybrid:
            return kHybrid;
    }
    return kLexical;
}

bool valid_retriever(std::string_view value) noexcept {
    return value.empty() || value == kAuto || value == kLexical || value == kVector ||
           value == kHybrid;
}

std::string retriever_values_message(std::string_view label, std::string_view got) {
    // An empty label omits the prefix: CLI11 prints the option's own name in
    // front of a validator's message, and "--retriever: --retriever:" is the
    // kind of doubled label that reads as a bug.
    return (label.empty() ? std::string{} : std::string{label} + ": ") + "unknown value '" +
           std::string{got} + "' (accepted: lexical, vector, hybrid, auto)";
}

TurnRetrieval resolve_turn_retriever(std::string_view flag, std::string_view pin,
                                     const EmbedderFacts& embedder, const StoreFacts& store) {
    TurnRetrieval out;
    const std::optional<Retriever> chosen = explicit_choice(flag, pin);
    const std::string blocker = why_not_vector(embedder, store);
    const bool from_flag = flag == kLexical || flag == kVector || flag == kHybrid;

    if (chosen == Retriever::Lexical) {
        out.retriever = Retriever::Lexical;
        return out;
    }

    if (chosen == Retriever::Vector) {
        if (blocker.empty()) {
            out.retriever = Retriever::Vector;
            return out;
        }
        if (from_flag) {
            // Asked for by name and impossible: say so, name the way out, and
            // do NOT run something else under that name.
            out.error = "vector search cannot run: " + blocker +
                        ". Pass --retriever lexical to search without vectors";
            return out;
        }
        // A pin. Never searched the other way; excluded, with the reason.
        out.retriever = Retriever::Lexical;
        out.excluded = true;
        out.note =
            "collection is pinned to vector search but " + blocker + " -- not searched this turn";
        return out;
    }

    if (chosen == Retriever::Hybrid) {
        if (blocker.empty()) {
            out.retriever = Retriever::Hybrid;
            return out;
        }
        // Runs, and is REPORTED as what ran. Never partial, never an error.
        out.retriever = Retriever::Lexical;
        out.note = "hybrid requested, but " + blocker + " -- searched lexical only";
        return out;
    }

    // Auto. Vector only when everything qualifies; the spend policy allows a
    // per-question call against a collection already built with this model.
    if (blocker.empty()) {
        out.retriever = Retriever::Vector;
        return out;
    }
    out.retriever = Retriever::Lexical;
    // Surface the reason only when there was something to fall back FROM: a
    // store that never had vectors, or a machine with no embedder, is the
    // ordinary case and not worth a note every turn.
    if (embedder.available && store.dimension > 0) {
        out.note = "using lexical search: " + blocker;
    }
    return out;
}

IngestRetrieval resolve_ingest_retriever(std::string_view flag, std::string_view pin,
                                         const EmbedderFacts& embedder) {
    IngestRetrieval out;
    const std::optional<Retriever> chosen = explicit_choice(flag, pin);

    if (chosen == Retriever::Lexical) {
        out.retriever = Retriever::Lexical;
        return out;
    }
    if (chosen == Retriever::Vector) {
        if (!embedder.available) {
            out.error =
                "vector ingest cannot run: no embedding backend is configured. Set "
                "`default_embedding` under `models:` (or the collection's backend:), or pass "
                "--retriever lexical";
            return out;
        }
        out.retriever = Retriever::Vector;
        return out;
    }

    // Auto, and a hybrid ask resolves like auto here: fusion is a query
    // concern, and a hard vector requirement at write time would be stricter
    // than the read path.
    if (!embedder.available) {
        out.retriever = Retriever::Lexical;
        return out;
    }
    if (embedder.metered) {
        // The user's rule: a whole corpus through a paid embedder needs a yes.
        out.retriever = Retriever::Lexical;
        out.note = "indexed for text search only: '" + embedder.model +
                   "' is a paid embedder, and vectorising a whole collection through it "
                   "takes --retriever vector (or a retriever: vector pin)";
        return out;
    }
    out.retriever = Retriever::Vector;
    return out;
}

namespace {

/// Words a question is made of that say nothing about what it asks.
constexpr std::array<std::string_view, 64> kStopWords{
    "a",      "about", "all",   "an",   "and",   "any", "are",   "as",   "at",    "be",
    "been",   "but",   "by",    "can",  "could", "did", "do",    "does", "for",   "from",
    "had",    "has",   "have",  "how",  "i",     "if",  "in",    "into", "is",    "it",
    "its",    "me",    "my",    "no",   "not",   "of",  "on",    "or",   "our",   "please",
    "should", "so",    "some",  "tell", "that",  "the", "their", "them", "there", "these",
    "they",   "this",  "those", "to",   "was",   "we",  "were",  "what", "when",  "where",
    "which",  "who",   "why",   "you"};

/// The lowercase words of `text`: runs of letters, digits and any byte past
/// ASCII, so a word in another script stays a word.
[[nodiscard]] std::vector<std::string> words_of(std::string_view text) {
    std::vector<std::string> out;
    std::string word;
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (std::isalnum(byte) != 0 || byte >= 0x80) {
            word += static_cast<char>(std::tolower(byte));
        } else if (!word.empty()) {
            out.push_back(std::move(word));
            word.clear();
        }
    }
    if (!word.empty()) {
        out.push_back(std::move(word));
    }
    return out;
}

[[nodiscard]] std::string band_of(double value, double strong, double fair) {
    if (value >= strong) {
        return "strong";
    }
    return value >= fair ? "fair" : "weak";
}

[[nodiscard]] std::string percent(double fraction) {
    return std::to_string(std::lround(fraction * 100.0)) + "%";
}

[[nodiscard]] std::string two_places(double value) {
    std::array<char, 32> text{};
    std::snprintf(text.data(), text.size(), "%.2f", value);
    return text.data();
}

}  // namespace

double rrf_ceiling() noexcept {
    return 2.0 / static_cast<double>(embedstore::kRrfK + 1);
}

WordCoverage word_coverage(std::string_view question, std::string_view text) {
    std::set<std::string> asked;
    for (std::string& word : words_of(question)) {
        if (std::ranges::find(kStopWords, word) == kStopWords.end()) {
            asked.insert(std::move(word));
        }
    }
    std::set<std::string> held;
    for (std::string& word : words_of(text)) {
        held.insert(std::move(word));
    }
    WordCoverage coverage;
    coverage.asked = asked.size();
    for (const std::string& word : asked) {
        coverage.found += held.contains(word) ? 1 : 0;
    }
    return coverage;
}

MatchStrength match_strength(const MatchEvidence& evidence) {
    MatchStrength strength;
    const WordCoverage& words = evidence.words;
    const double held = words.asked == 0
                            ? 0.0
                            : static_cast<double>(words.found) / static_cast<double>(words.asked);
    if (evidence.retriever == kLexical) {
        if (words.asked == 0) {
            return strength;  // a question of stop words: nothing to read it by
        }
        strength.floored = words.found < kLexicalFloorWords;
        strength.band = band_of(held, kWordsStrong, kWordsFair);
        strength.measure = words.found == 0 ? std::string{"none of the question's words"}
                                            : std::to_string(words.found) + " of " +
                                                  std::to_string(words.asked) + " question words";
        return strength;
    }
    if (evidence.retriever == kVector) {
        strength.floored = evidence.score < kCosineFloor;
        strength.band = band_of(evidence.score, kCosineStrong, kCosineFair);
        return strength;
    }
    const double fraction = evidence.score / rrf_ceiling();
    strength.band = band_of(fraction, kRrfStrong, kRrfFair);
    strength.measure = percent(std::min(fraction, 1.0)) + " of RRF's ceiling";
    const double cosine = evidence.cosine.value_or(0.0);
    const bool no_evidence = cosine < kCosineFloor && words.found < kLexicalFloorWords;
    strength.floored = fraction < kRrfFloor || no_evidence;
    if (no_evidence && fraction >= kRrfFloor) {
        strength.measure +=
            ", but cosine " + two_places(cosine) + " and none of the question's words";
    }
    return strength;
}

}  // namespace apogee::agentloop
