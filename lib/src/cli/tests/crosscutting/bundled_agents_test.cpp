#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <string>

#include "agentloop/structured.h"
#include "contracts/assets.h"

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
        // Refs come from flags: a prompt that reaches for the git tools tells
        // the model to call them with no refs, never to parse a branch out of
        // the input. (`meeting-minutes` reads a transcript and no git.)
        if (prompt.find("git_diff") != std::string::npos) {
            CHECK(prompt.find("--branch/--base") != std::string::npos);
        }
        // Re-authored for the owned loop: no forwarded vendor machinery, and
        // model-agnostic -- no vendor or family a brief would lean on (40a).
        for (const std::string_view vendor : {"Atlassian", "native-Claude", "Claude", "Anthropic",
                                              "OpenAI", "GPT", "Gemini", "Llama", "Qwen"}) {
            INFO(vendor);
            CHECK(prompt.find(vendor) == std::string::npos);
        }
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
    // The four added in 40a.
    const nlohmann::json review = nlohmann::json::parse(
        R"({"summary":"s","findings":[],"verdict":"approve","verdict_rationale":"r","human_summary":"h"})");
    const nlohmann::json gaps =
        nlohmann::json::parse(R"({"summary":"s","tests_read":[],"gaps":[],"human_summary":"h"})");
    const nlohmann::json drift = nlohmann::json::parse(
        R"({"summary":"s","docs_source":"none","drifts":[],"human_summary":"h"})");
    const nlohmann::json minutes = nlohmann::json::parse(
        R"({"summary":"s","decisions":[],"action_items":[],"open_questions":[],"human_summary":"h"})");
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
    check("code-review", review);
    check("test-gaps", gaps);
    check("doc-drift", drift);
    check("meeting-minutes", minutes);
}

TEST_CASE("a full report conforms to each added schema, and a stray field does not",
          "[bundled][agents][schema]") {
    // Each item object is closed and flat (40a: no nesting a small model
    // fumbles): one populated report per schema conforms, and the same report
    // with an unknown field in an item is refused.
    const auto check = [](std::string_view name, nlohmann::json instance, std::string_view array) {
        INFO(name);
        const nlohmann::json schema =
            nlohmann::json::parse(apogee::harness::find_bundled_agent(name)->schema);
        CHECK(apogee::agentloop::validate_against(schema, instance).ok);
        instance[std::string{array}][0]["stray"] = "x";
        CHECK_FALSE(apogee::agentloop::validate_against(schema, instance).ok);
    };
    check(
        "code-review",
        nlohmann::json::parse(
            R"({"summary":"s","findings":[{"severity":"high","kind":"correctness","file":"a.cpp","location":"12-14","claim":"c","suggested_fix":"f","confidence":"high"}],"verdict":"changes_requested","verdict_rationale":"r","human_summary":"h"})"),
        "findings");
    check(
        "test-gaps",
        nlohmann::json::parse(
            R"({"summary":"s","tests_read":["t.cpp"],"gaps":[{"priority":"medium","file":"a.cpp","location":"the total function","behavior":"b","suggested_test":"an empty list totals zero"}],"human_summary":"h"})"),
        "gaps");
    check(
        "doc-drift",
        nlohmann::json::parse(
            R"({"summary":"s","docs_source":"both","drifts":[{"document":"README.md","statement":"st","why_stale":"w","suggested_edit":"e","kind":"stale"}],"human_summary":"h"})"),
        "drifts");
    check(
        "meeting-minutes",
        nlohmann::json::parse(
            R"({"summary":"s","decisions":[{"decision":"d","reason":"not stated"}],"action_items":[{"owner":"unassigned","task":"t","due":"not stated"}],"open_questions":[{"question":"q","raised_by":"Ana"}],"human_summary":"h"})"),
        "action_items");
}
