#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "harness/cancellation.h"

namespace apogee::harness {
class Harness;
}

/// The extraction contract of the knowledge-graph layer: one chunk in, the
/// entities it is about and the relations it states out -- as ONE
/// provider-native structured-output call, validated client-side always, with
/// the closed type set and the per-chunk caps enforced **in host code**
/// whatever the schema asked of the model.
///
/// Like `knowledge/`, this package is model-free: generation and embedding
/// arrive as injected closures (`ExtractFn`, `EmbedFn`), so every build test
/// runs on fakes; the CLI and the admin plane bind them to `run_structured`
/// and the embedder resolution.
namespace apogee::graph {

/// Per-chunk caps, clipped after validation: extraction quality drops
/// sharply when a model pads, so overflow is discarded rather than stored.
inline constexpr std::size_t kMaxEntitiesPerChunk = 12;
inline constexpr std::size_t kMaxRelationsPerChunk = 16;

/// Extraction, not creativity: Ommi's numbers. The generation cap exists
/// because an untuned local model that misses its stop token would otherwise
/// decode toward its context limit on a single chunk -- a minutes-long stall
/// per chunk that reads as a hang. A capped extraction fits comfortably.
inline constexpr double kExtractTemperature = 0.2;
inline constexpr std::int64_t kExtractMaxTokens = 2048;

/// The closed set of entity types the extractor may emit. Anything else --
/// the reserved `decision` included -- is dropped with its relations.
[[nodiscard]] std::span<const std::string_view> valid_entity_types() noexcept;
[[nodiscard]] bool is_valid_entity_type(std::string_view type) noexcept;

struct Entity {
    std::string name;
    std::string type;
    std::string description;
};

/// One directed relation between two entities of the same chunk, by name.
struct Relation {
    std::string source;
    std::string target;
    std::string relation;
    std::string description;
};

struct ExtractResult {
    std::vector<Entity> entities;
    std::vector<Relation> relations;
};

/// JSON, found by ADL. Reading is tolerant of a missing list (empty), so a
/// conforming answer and a slightly short one decode alike; `normalize`
/// decides what survives.
void to_json(nlohmann::json& out, const ExtractResult& result);
void from_json(const nlohmann::json& in, ExtractResult& result);

/// The compiled-in prompt and schema, byte-identical to the shipped files
/// under `assets/clerks/`.
[[nodiscard]] std::string_view extract_prompt() noexcept;
[[nodiscard]] std::string_view extract_schema_text() noexcept;
[[nodiscard]] nlohmann::json extract_schema();

/// The extractor's whole system prompt: the prompt, then the OUTPUT FORMAT
/// block and the schema worded by the one function every structured caller
/// uses. Shared by every surface that runs extraction, so they extract
/// identically.
[[nodiscard]] std::string extract_system_prompt();

/// Validates and canonicalises an extraction in place -- enforcing in host
/// code what the schema can only request:
///   - names trimmed; an empty name dropped
///   - types lower-cased and checked against the closed set; a non-conforming
///     entity dropped, not errored
///   - duplicates within the chunk (same normalised name and type) merged
///     first-non-empty-description-wins
///   - entities clipped to `kMaxEntitiesPerChunk`
///   - relation verbs lower-cased and trimmed; an empty verb, a self-loop, or
///     an endpoint that does not resolve to a surviving entity dropped
///   - duplicate relations dropped, so one chunk never double-increments a
///     weight; clipped to `kMaxRelationsPerChunk`
void normalize(ExtractResult& result);

/// What one extractor call produced.
struct ExtractOutcome {
    /// The parsed result, not yet normalised; nullopt on failure.
    std::optional<ExtractResult> result;
    /// Why it failed, when it did.
    std::string error;
    /// Model turns taken: 1, or 2 after a correction.
    int attempts = 0;

    [[nodiscard]] bool ok() const noexcept {
        return result.has_value();
    }
};

/// The extractor as a function: a chunk's text in, the outcome out. The
/// build owns validation and the retry; the closure owns generation.
using ExtractFn =
    std::function<ExtractOutcome(std::string_view chunk_text, const harness::CancellationToken&)>;

/// Embeds one entity's text. Throws on failure; a null function disables the
/// embed phase and the graph stays full-text searchable.
using EmbedFn =
    std::function<std::vector<float>(std::string_view text, const harness::CancellationToken&)>;

/// The production extractor: `agentloop::run_structured` on `harness` with
/// `model`, the extraction schema, `kExtractTemperature`, `kExtractMaxTokens`,
/// no tools, a throwaway history, and a side-request mark -- so a build never
/// touches a session's cache. A non-conforming answer after the one retry is
/// a failed outcome carrying the validator's message.
[[nodiscard]] ExtractFn make_structured_extractor(const harness::Harness& harness,
                                                  std::string model);

}  // namespace apogee::graph
