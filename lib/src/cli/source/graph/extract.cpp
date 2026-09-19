#include "graph/extract.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cctype>
#include <map>
#include <set>
#include <utility>

#include "agentloop/loop.h"
#include "agentloop/reporter.h"
#include "agentloop/structured.h"
#include "embedstore/graph.h"
#include "harness/errors.h"
#include "harness/harness.h"
#include "harness/types.h"

// The extractor's prompt and schema are GENERATED from lib/src/cli/assets/
// clerks/extract_prompt.txt and extract_schema.json. The shipped files and
// these literals are byte-identical, and tests/graph/extract_test.cpp fails
// the build the moment they drift -- the same contract the capture clerk, the
// config template and the bundled agents keep. Edit the FILES, then
// regenerate; never edit a literal here.
//
// Compiled in rather than seeded under the data directory on purpose: the
// extractor is a fixed system concern, not a user-editable agent, and so it
// adds no install-parity surface (Ommi's rule, kept).

namespace apogee::graph {
namespace {

constexpr std::string_view kExtractPrompt =
    R"PROMPT(You are a disciplined entity-and-relation extraction clerk for a knowledge
graph built over a document collection. You are given one text chunk. Your
single job is to extract the entities the chunk is actually about and the
relations the chunk directly states between them, and emit them as strict JSON.

You are not a summarizer and not a brainstormer. You record only what the text
supports; you do not infer, generalize, or invent. Follow these rules exactly:

1. ENTITIES. Extract the distinct named things the chunk discusses — at most 12,
   preferring the most central ones. Each entity has:
   - name: the canonical display form as the text names it (full name, no
     pronouns, no leading articles). Use one consistent name per entity.
   - type: exactly one of: person, organization, system, component, location,
     event, concept, artifact.
       person        — an individual human
       organization  — a company, team, institution, or group
       system        — a complete product, service, platform, or codebase
       component     — a part of a system: a module, package, file, tool, API
       location      — a physical or geographic place
       event         — something that happened or is scheduled at a time
       concept       — an abstract idea, principle, policy, or practice
       artifact      — a produced thing: a document, dataset, model, release
   - description: one factual sentence about the entity drawn from this chunk
     only. Leave it an empty string when the chunk names the entity without
     saying anything about it.

2. RELATIONS. Extract directed relations the chunk explicitly states — at most
   16. Each relation has:
   - source, target: the names of two entities from your entities list,
     spelled exactly as you listed them. Never reference an entity you did not
     extract.
   - relation: a lowercase verb phrase of at most a few words ("operates",
     "depends on", "was built by", "replaced").
   - description: one factual sentence of supporting context from the chunk,
     or an empty string.

3. RESTRAINT. A chunk about nothing in particular yields few or no entities —
   empty lists are a correct answer. Do not pad. Do not extract generic words
   ("the user", "the code", "the meeting") unless the chunk treats them as a
   specific named thing. Accuracy matters more than coverage.

Output ONLY the JSON object described by the schema — no commentary, no
markdown code fences.
)PROMPT";

constexpr std::string_view kExtractSchema = R"SCHEMA({
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "Knowledge-graph chunk extraction",
  "description": "Entities and directed relations extracted from one text chunk. Relations may only reference entities present in the entities list.",
  "type": "object",
  "additionalProperties": false,
  "required": ["entities", "relations"],
  "properties": {
    "entities": {
      "type": "array",
      "maxItems": 12,
      "items": {
        "type": "object",
        "additionalProperties": false,
        "required": ["name", "type", "description"],
        "properties": {
          "name": {
            "type": "string",
            "description": "Canonical display name as the text names it — full name, no pronouns, no leading articles."
          },
          "type": {
            "type": "string",
            "enum": ["person", "organization", "system", "component", "location", "event", "concept", "artifact"]
          },
          "description": {
            "type": "string",
            "description": "One factual sentence about the entity drawn from this chunk only; empty when the chunk says nothing about it."
          }
        }
      }
    },
    "relations": {
      "type": "array",
      "maxItems": 16,
      "items": {
        "type": "object",
        "additionalProperties": false,
        "required": ["source", "target", "relation", "description"],
        "properties": {
          "source": {
            "type": "string",
            "description": "Name of an entity from the entities list, spelled exactly as listed."
          },
          "target": {
            "type": "string",
            "description": "Name of an entity from the entities list, spelled exactly as listed."
          },
          "relation": {
            "type": "string",
            "description": "Lowercase verb phrase of at most a few words, e.g. \"operates\", \"depends on\"."
          },
          "description": {
            "type": "string",
            "description": "One factual sentence of supporting context from the chunk, or empty."
          }
        }
      }
    }
  }
}
)SCHEMA";

constexpr std::array<std::string_view, 8> kValidEntityTypes{
    "person", "organization", "system", "component", "location", "event", "concept", "artifact",
};

[[nodiscard]] std::string trim(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return std::string{text.substr(begin, end - begin)};
}

[[nodiscard]] std::string lower(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

}  // namespace

std::span<const std::string_view> valid_entity_types() noexcept {
    return kValidEntityTypes;
}

bool is_valid_entity_type(std::string_view type) noexcept {
    const std::string folded = lower(trim(type));
    for (const std::string_view candidate : kValidEntityTypes) {
        if (folded == candidate) {
            return true;
        }
    }
    return false;
}

void to_json(nlohmann::json& out, const ExtractResult& result) {
    out = nlohmann::json::object();
    nlohmann::json entities = nlohmann::json::array();
    for (const Entity& entity : result.entities) {
        entities.push_back(
            {{"name", entity.name}, {"type", entity.type}, {"description", entity.description}});
    }
    nlohmann::json relations = nlohmann::json::array();
    for (const Relation& relation : result.relations) {
        relations.push_back({{"source", relation.source},
                             {"target", relation.target},
                             {"relation", relation.relation},
                             {"description", relation.description}});
    }
    out["entities"] = std::move(entities);
    out["relations"] = std::move(relations);
}

void from_json(const nlohmann::json& in, ExtractResult& result) {
    result = ExtractResult{};
    if (!in.is_object()) {
        return;
    }
    if (const auto entities = in.find("entities"); entities != in.end() && entities->is_array()) {
        for (const nlohmann::json& item : *entities) {
            if (!item.is_object()) {
                continue;
            }
            Entity entity;
            entity.name = item.value("name", std::string{});
            entity.type = item.value("type", std::string{});
            entity.description = item.value("description", std::string{});
            result.entities.push_back(std::move(entity));
        }
    }
    if (const auto relations = in.find("relations");
        relations != in.end() && relations->is_array()) {
        for (const nlohmann::json& item : *relations) {
            if (!item.is_object()) {
                continue;
            }
            Relation relation;
            relation.source = item.value("source", std::string{});
            relation.target = item.value("target", std::string{});
            relation.relation = item.value("relation", std::string{});
            relation.description = item.value("description", std::string{});
            result.relations.push_back(std::move(relation));
        }
    }
}

std::string_view extract_prompt() noexcept {
    return kExtractPrompt;
}

std::string_view extract_schema_text() noexcept {
    return kExtractSchema;
}

nlohmann::json extract_schema() {
    return nlohmann::json::parse(kExtractSchema);
}

std::string extract_system_prompt() {
    // The OUTPUT FORMAT block is worded in ONE place for the whole product --
    // `agentloop::schema_instruction` -- so the extractor asks for JSON in
    // exactly the words every agent and clerk does.
    return trim(kExtractPrompt) + "\n\n" +
           agentloop::schema_instruction({std::string{kExtractSchema}}, false);
}

void normalize(ExtractResult& result) {
    // Entities: trimmed, typed against the closed set, deduplicated by
    // (normalised name, type) under first-non-empty-description-wins, capped.
    std::vector<Entity> kept;
    std::map<std::pair<std::string, std::string>, std::size_t> seen;
    for (Entity entity : result.entities) {
        entity.name = trim(entity.name);
        entity.type = lower(trim(entity.type));
        entity.description = trim(entity.description);
        if (entity.name.empty() || !is_valid_entity_type(entity.type)) {
            continue;
        }
        const auto key = std::pair{embedstore::normalize_entity_name(entity.name), entity.type};
        if (const auto it = seen.find(key); it != seen.end()) {
            if (kept[it->second].description.empty() && !entity.description.empty()) {
                kept[it->second].description = entity.description;
            }
            continue;
        }
        if (kept.size() == kMaxEntitiesPerChunk) {
            continue;
        }
        seen.emplace(key, kept.size());
        kept.push_back(std::move(entity));
    }
    result.entities = std::move(kept);

    // Surviving entity names, normalised -- the only legal relation endpoints.
    std::set<std::string> names;
    for (const Entity& entity : result.entities) {
        names.insert(embedstore::normalize_entity_name(entity.name));
    }
    std::vector<Relation> kept_relations;
    std::set<std::tuple<std::string, std::string, std::string>> seen_relations;
    for (Relation relation : result.relations) {
        relation.source = trim(relation.source);
        relation.target = trim(relation.target);
        relation.relation = lower(trim(relation.relation));
        relation.description = trim(relation.description);
        const std::string source = embedstore::normalize_entity_name(relation.source);
        const std::string target = embedstore::normalize_entity_name(relation.target);
        if (relation.relation.empty() || source == target || !names.contains(source) ||
            !names.contains(target)) {
            continue;
        }
        if (!seen_relations.emplace(source, target, relation.relation).second ||
            kept_relations.size() == kMaxRelationsPerChunk) {
            continue;
        }
        kept_relations.push_back(std::move(relation));
    }
    result.relations = std::move(kept_relations);
}

ExtractFn make_structured_extractor(const harness::Harness& harness, std::string model) {
    return [&harness, model = std::move(model), schema = extract_schema()](
               std::string_view chunk_text, const harness::CancellationToken& cancellation) {
        ExtractOutcome outcome;
        std::vector<harness::ChatMessage> history{
            harness::ChatMessage::system(extract_system_prompt()),
            harness::ChatMessage::user(std::string{chunk_text})};
        agentloop::Options options;
        options.model = model;
        options.temperature = kExtractTemperature;
        options.max_tokens = kExtractMaxTokens;
        options.stream_answer = false;
        // Not a turn of anyone's conversation: a local backend runs it on
        // its own context, and a session's cache is never touched.
        options.side_request = true;
        options.cancellation = cancellation;
        agentloop::NullReporter reporter;
        try {
            const agentloop::StructuredResult result =
                agentloop::run_structured(harness, history, options, reporter, schema);
            outcome.attempts = result.attempts;
            if (!result.conforms || !result.json.has_value()) {
                outcome.error = "the extractor did not return entities and relations";
                for (const std::string& error : result.errors) {
                    outcome.error += "; " + error;
                }
                return outcome;
            }
            outcome.result = result.json->get<ExtractResult>();
        } catch (const harness::HarnessError& e) {
            outcome.error = std::string{"the extractor could not run: "} + e.what();
        }
        return outcome;
    };
}

}  // namespace apogee::graph
