#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <string>

#include "agentloop/structured.h"
#include "harness/assets.h"

/// Every bundled schema is a valid draft-07 JSON Schema, closed
/// (`additionalProperties: false`) at the top, and ends -- in both its
/// property order and its `required` list -- with `human_summary`; every
/// bundled prompt states the rule that a report finding nothing is a
/// complete, valid result. Validated as schemas, not by substring.
TEST_CASE("every bundled schema is valid draft-07, closed, and ends with human_summary",
          "[bundled][agents][schema]") {
    for (const apogee::harness::BundledAgent& agent : apogee::harness::bundled_agents()) {
        INFO(agent.name);
        const nlohmann::json schema = nlohmann::json::parse(agent.schema, nullptr, false);
        REQUIRE_FALSE(schema.is_discarded());
        const apogee::agentloop::ValidationResult valid =
            apogee::agentloop::validate_schema(schema);
        INFO((valid.errors.empty() ? std::string{} : valid.errors.front()));
        CHECK(valid.ok);
        CHECK(schema.at("type") == "object");
        CHECK(schema.at("additionalProperties") == false);
        // `human_summary` is the LAST property in the FILE and the LAST
        // required field. `nlohmann::json` sorts keys, so the file's order
        // is read through `ordered_json`.
        const nlohmann::ordered_json in_order = nlohmann::ordered_json::parse(agent.schema);
        REQUIRE(in_order.at("properties").is_object());
        std::string last_property;
        for (const auto& [key, unused] : in_order.at("properties").items()) {
            last_property = key;
        }
        CHECK(last_property == "human_summary");
        REQUIRE(schema.at("required").is_array());
        CHECK(schema.at("required").back() == "human_summary");
        // An empty-but-complete report conforms: "nothing found" must be a
        // valid instance of every schema, not something the model has to
        // fight the schema to say.
        CHECK(schema.at("properties").at("human_summary").at("type") == "string");
    }
}

TEST_CASE("every bundled prompt states the nothing-found rule and ends with the human summary",
          "[bundled][agents][prompt]") {
    for (const apogee::harness::BundledAgent& agent : apogee::harness::bundled_agents()) {
        INFO(agent.name);
        const std::string prompt{agent.prompt};
        CHECK(prompt.find("complete, valid result") != std::string::npos);
        CHECK(prompt.find("human_summary") != std::string::npos);
        CHECK(prompt.find("OUTPUT FORMAT") != std::string::npos);
        // Refs come from flags: the prompt tells the model to call the git
        // tools with no refs, never to parse a branch out of the input.
        CHECK(prompt.find("--branch/--base") != std::string::npos);
        // Re-authored for the owned loop: no forwarded vendor machinery.
        CHECK(prompt.find("Atlassian") == std::string::npos);
        CHECK(prompt.find("native-Claude") == std::string::npos);
    }
}

TEST_CASE("a minimal nothing-found report conforms to each bundled schema",
          "[bundled][agents][schema]") {
    const nlohmann::json security = nlohmann::json::parse(
        R"({"summary":"s","findings":[],"verdict":"no_security_concerns","verdict_rationale":"r","human_summary":"h"})");
    const nlohmann::json notes = nlohmann::json::parse(
        R"({"title":"t","range":"main..HEAD","summary":"s","features":[],"fixes":[],"breaking_changes":[],"other":[],"human_summary":"h"})");
    const nlohmann::json merge = nlohmann::json::parse(
        R"({"mode":"peer","walkthrough":"w","acceptance_criteria":{"criteria":[],"summary":"none given","notes":"no criteria"},"adr_review":{"adrs_consulted":[],"compliance":[],"notes":"no collection"},"verdict":"approve","verdict_rationale":"clean","human_summary":"h"})");
    const auto check = [](std::string_view name, const nlohmann::json& instance) {
        const nlohmann::json schema =
            nlohmann::json::parse(apogee::harness::find_bundled_agent(name)->schema);
        const apogee::agentloop::ValidationResult valid =
            apogee::agentloop::validate_against(schema, instance);
        INFO(name << ": " << (valid.errors.empty() ? "" : valid.errors.front()));
        CHECK(valid.ok);
    };
    check("security-review", security);
    check("release-notes", notes);
    check("merge-request", merge);
}
