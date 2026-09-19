#include "graph/extract.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "agentloop/structured.h"
#include "backends/mock.h"
#include "harness/config.h"
#include "harness/harness.h"

/// The extraction contract: the prompt and schema pinned to the shipped
/// files and closed; validation in host code -- the whitelist (the reserved
/// `decision` included), the caps, self-loops, endpoints, duplicates; and the
/// production extractor as one structured-output call.
namespace {

using apogee::graph::Entity;
using apogee::graph::ExtractResult;
using apogee::graph::Relation;

std::string read_asset(const std::string& relative) {
    const std::filesystem::path path = std::filesystem::path{APOGEE_ASSETS_DIR} / relative;
    std::ifstream in{path, std::ios::binary};
    REQUIRE(in);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

Entity entity(std::string name, std::string type, std::string description = {}) {
    return Entity{
        .name = std::move(name), .type = std::move(type), .description = std::move(description)};
}

Relation relation(std::string source, std::string target, std::string verb) {
    return Relation{
        .source = std::move(source), .target = std::move(target), .relation = std::move(verb)};
}

constexpr std::string_view kAnswer =
    R"({"entities": [{"name": "Atlas", "type": "system", "description": "collects readings"},
                     {"name": "Vault", "type": "system", "description": ""}],
        "relations": [{"source": "Atlas", "target": "Vault", "relation": "stores readings in",
                       "description": ""}]})";

}  // namespace

TEST_CASE(
    "the compiled-in extraction prompt and schema byte-match the shipped files, and the "
    "schema is closed over eight types with no reserved one",
    "[graph][extract][assets]") {
    CHECK(std::string{apogee::graph::extract_prompt()} == read_asset("clerks/extract_prompt.txt"));
    CHECK(std::string{apogee::graph::extract_schema_text()} ==
          read_asset("clerks/extract_schema.json"));
    const nlohmann::json schema = apogee::graph::extract_schema();
    CHECK(apogee::agentloop::validate_schema(schema).ok);
    CHECK(schema.at("additionalProperties") == false);
    const nlohmann::json types =
        schema.at("properties").at("entities").at("items").at("properties").at("type").at("enum");
    CHECK(types.size() == 8);
    CHECK(types.size() == apogee::graph::valid_entity_types().size());
    for (const nlohmann::json& type : types) {
        CHECK(apogee::graph::is_valid_entity_type(type.get<std::string>()));
    }
    CHECK_FALSE(apogee::graph::is_valid_entity_type("decision"));
    CHECK(apogee::graph::is_valid_entity_type(" Person "));
    CHECK(schema.at("properties").at("entities").at("maxItems") ==
          apogee::graph::kMaxEntitiesPerChunk);
    CHECK(schema.at("properties").at("relations").at("maxItems") ==
          apogee::graph::kMaxRelationsPerChunk);

    // The system prompt is the prompt, then the one OUTPUT FORMAT wording,
    // then the schema -- the same block every structured caller ends with.
    const std::string prompt = apogee::graph::extract_system_prompt();
    CHECK(prompt.starts_with("You are a disciplined entity-and-relation extraction clerk"));
    CHECK(prompt.find(apogee::agentloop::schema_instruction(
              {std::string{apogee::graph::extract_schema_text()}}, false)) != std::string::npos);
    CHECK(prompt.ends_with(std::string{apogee::graph::extract_schema_text()}));
}

TEST_CASE(
    "normalize enforces the whitelist, drops the reserved type with its relations, and "
    "merges duplicates",
    "[graph][extract][normalize]") {
    ExtractResult result;
    result.entities = {entity(" Atlas ", "System", "collects readings"),
                       entity("atlas", "system", "ignored: the first description wins"),
                       entity("Vault", "warehouse"),  // not a type
                       entity("kr-1", "decision", "the extractor may never emit this"),
                       entity("", "person"),
                       entity("Ada", "Person", ""),
                       entity("ADA", "person", "fills the empty description")};
    result.relations = {relation("Atlas", "Vault", "stores readings in"),  // Vault dropped
                        relation("kr-1", "Atlas", "concerns"),             // decision dropped
                        relation("Atlas", "Ada", " Operated By "),
                        relation("atlas", "ada", "operated by"),  // a duplicate
                        relation("Atlas", "Atlas", "loops"),      // a self-loop
                        relation("Atlas", "Ada", ""),             // no verb
                        relation("Ghost", "Ada", "haunts")};      // unknown endpoint
    apogee::graph::normalize(result);
    REQUIRE(result.entities.size() == 2);
    CHECK(result.entities[0].name == "Atlas");
    CHECK(result.entities[0].type == "system");
    CHECK(result.entities[0].description == "collects readings");
    CHECK(result.entities[1].name == "Ada");
    CHECK(result.entities[1].description == "fills the empty description");
    REQUIRE(result.relations.size() == 1);
    CHECK(result.relations[0].source == "Atlas");
    CHECK(result.relations[0].target == "Ada");
    CHECK(result.relations[0].relation == "operated by");
}

TEST_CASE("normalize clips to the per-chunk caps", "[graph][extract][caps]") {
    ExtractResult result;
    for (int i = 0; i < 20; ++i) {
        result.entities.push_back(entity("E" + std::to_string(i), "concept"));
    }
    for (int i = 0; i < 20; ++i) {
        result.relations.push_back(
            relation("E0", "E" + std::to_string(i + 1), "r" + std::to_string(i)));
    }
    apogee::graph::normalize(result);
    CHECK(result.entities.size() == apogee::graph::kMaxEntitiesPerChunk);
    // Relations to entities beyond the cap fell out with them; the rest are
    // capped in turn.
    for (const Relation& relation : result.relations) {
        CHECK(relation.target != "E13");
    }
    CHECK(result.relations.size() <= apogee::graph::kMaxRelationsPerChunk);
    CHECK(result.relations.size() == 11);  // E1..E11 survive
}

TEST_CASE("an extraction decodes tolerantly and round-trips", "[graph][extract][json]") {
    const ExtractResult decoded =
        nlohmann::json::parse(R"({"entities": [{"name": "A", "type": "system"}]})")
            .get<ExtractResult>();
    REQUIRE(decoded.entities.size() == 1);
    CHECK(decoded.entities.front().description.empty());
    CHECK(decoded.relations.empty());
    CHECK(nlohmann::json::parse("[]").get<ExtractResult>().entities.empty());
    const nlohmann::json round = nlohmann::json::parse(kAnswer).get<ExtractResult>();
    CHECK(round.at("entities").size() == 2);
    CHECK(round.at("relations").size() == 1);
}

TEST_CASE(
    "the production extractor is one structured-output call with the schema, a side "
    "request, and one corrective retry",
    "[graph][extract][structured]") {
    apogee::harness::Config config;
    apogee::harness::BackendConfig mock;
    mock.type = apogee::harness::BackendType::Mock;
    config.backends.emplace("mock", mock);
    config.models.default_backend = "mock";

    apogee::backends::MockProvider::Options options;
    options.backend_name = "mock";
    options.turns = {apogee::backends::MockTurn{.text = std::string{kAnswer}}};
    std::vector<apogee::harness::ChatRequest> seen;
    options.on_request = [&seen](const apogee::harness::ChatRequest& request) {
        seen.push_back(request);
    };
    apogee::harness::Harness harness{config};
    harness.register_provider("mock",
                              std::make_shared<apogee::backends::MockProvider>(std::move(options)));
    harness.use_default_router();

    const apogee::graph::ExtractFn extract =
        apogee::graph::make_structured_extractor(harness, "mock");
    const apogee::graph::ExtractOutcome outcome =
        extract("Atlas collects readings into the Vault.", {});
    REQUIRE(outcome.ok());
    CHECK(outcome.attempts == 1);
    CHECK(outcome.result->entities.size() == 2);
    CHECK(outcome.result->relations.size() == 1);
    REQUIRE(seen.size() == 1);
    CHECK(seen.front().transient.side_request);
    CHECK(nlohmann::json::parse(seen.front().transient.response_schema) ==
          apogee::graph::extract_schema());
    CHECK(seen.front().temperature == apogee::graph::kExtractTemperature);
    CHECK(seen.front().max_tokens == apogee::graph::kExtractMaxTokens);
    CHECK(seen.front().tools.empty());
    REQUIRE(seen.front().messages.size() == 2);
    CHECK(seen.front().messages.front().content.plain_text() ==
          apogee::graph::extract_system_prompt());
    CHECK(seen.front().messages.back().content.plain_text() ==
          "Atlas collects readings into the Vault.");

    // Prose twice is a failed outcome after the one correction, never a partial.
    apogee::backends::MockProvider::Options prose;
    prose.backend_name = "mock";
    prose.turns = {apogee::backends::MockTurn{.text = "I would rather not."},
                   apogee::backends::MockTurn{.text = "still prose"}};
    apogee::harness::Harness stubborn{config};
    stubborn.register_provider("mock",
                               std::make_shared<apogee::backends::MockProvider>(std::move(prose)));
    stubborn.use_default_router();
    const apogee::graph::ExtractOutcome failed =
        apogee::graph::make_structured_extractor(stubborn, "mock")("x", {});
    CHECK_FALSE(failed.ok());
    CHECK(failed.attempts == 2);
    CHECK(failed.error.starts_with("the extractor did not return"));

    // A schema miss whose JSON still decodes -- an extra property under a
    // closed schema -- is a failure too: the schema is the contract, not
    // the parser.
    apogee::backends::MockProvider::Options loose;
    loose.backend_name = "mock";
    loose.turns = {
        apogee::backends::MockTurn{.text = R"({"entities": [], "relations": [], "verdict": "x"})"},
        apogee::backends::MockTurn{.text = R"({"entities": [], "relations": [], "verdict": "y"})"}};
    apogee::harness::Harness lax{config};
    lax.register_provider("mock",
                          std::make_shared<apogee::backends::MockProvider>(std::move(loose)));
    lax.use_default_router();
    const apogee::graph::ExtractOutcome miss =
        apogee::graph::make_structured_extractor(lax, "mock")("x", {});
    CHECK_FALSE(miss.ok());
    CHECK(miss.attempts == 2);
    CHECK(miss.error.find("verdict") != std::string::npos);

    // An unroutable backend is a failed outcome too, not an exception.
    apogee::harness::Harness empty{apogee::harness::Config{}};
    empty.use_default_router();
    const apogee::graph::ExtractOutcome unroutable =
        apogee::graph::make_structured_extractor(empty, "ghost")("x", {});
    CHECK_FALSE(unroutable.ok());
    CHECK(unroutable.error.starts_with("the extractor could not run"));
}
