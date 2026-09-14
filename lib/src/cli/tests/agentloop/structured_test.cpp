#include "agentloop/structured.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <vector>

#include "backends/mock.h"
#include "harness/config.h"
#include "harness/harness.h"

/// Structured output: the validator, the JSON extractor, the instruction
/// block, and the run -- a conforming answer, one corrective retry, and two
/// misses delivered raw with `conforms: false`.
namespace {

using apogee::agentloop::correction_message;
using apogee::agentloop::extract_json;
using apogee::agentloop::run_structured;
using apogee::agentloop::schema_instruction;
using apogee::agentloop::validate_against;
using apogee::agentloop::validate_schema;

const nlohmann::json& schema() {
    static const nlohmann::json value = nlohmann::json::parse(R"({
        "$schema": "http://json-schema.org/draft-07/schema#",
        "type": "object",
        "additionalProperties": false,
        "properties": {
            "summary": {"type": "string"},
            "findings": {"type": "array", "items": {"type": "object",
                "properties": {"severity": {"type": "string", "enum": ["high", "low"]}},
                "required": ["severity"], "additionalProperties": false}},
            "human_summary": {"type": "string"}
        },
        "required": ["summary", "findings", "human_summary"]
    })");
    return value;
}

struct Rig {
    apogee::harness::Config config;
    apogee::harness::Harness harness{config};
    std::shared_ptr<apogee::backends::MockProvider> mock;

    explicit Rig(std::vector<std::string> answers) {
        apogee::backends::MockProvider::Options options;
        for (std::string& answer : answers) {
            options.turns.push_back(
                {std::move(answer), {}, apogee::harness::FinishReason::Stop, {}});
        }
        mock = std::make_shared<apogee::backends::MockProvider>(std::move(options));
        harness.register_provider("mock", mock);
        harness.use_default_router();
    }
};

constexpr std::string_view kGood =
    R"({"summary": "s", "findings": [{"severity": "high"}], "human_summary": "h"})";
constexpr std::string_view kBad = R"({"summary": "s", "findings": [{"severity": "critical"}]})";

}  // namespace

TEST_CASE("the validator names every violation with its pointer", "[agentloop][structured]") {
    const auto ok = validate_against(schema(), nlohmann::json::parse(kGood));
    CHECK(ok.ok);
    CHECK(ok.errors.empty());

    const auto bad = validate_against(schema(), nlohmann::json::parse(kBad));
    CHECK_FALSE(bad.ok);
    REQUIRE(bad.errors.size() >= 2);
    bool missing = false;
    bool bad_enum = false;
    for (const std::string& error : bad.errors) {
        missing = missing ||
                  (error.starts_with("/:") && error.find("human_summary") != std::string::npos);
        bad_enum = bad_enum || error.starts_with("/findings/0/severity:");
    }
    CHECK(missing);
    CHECK(bad_enum);

    // additionalProperties: false is enforced, and a broken schema is an
    // error rather than a throw.
    CHECK_FALSE(validate_against(
                    schema(), nlohmann::json::parse(
                                  R"({"summary":"s","findings":[],"human_summary":"h","extra":1})"))
                    .ok);
    const auto broken = validate_against(nlohmann::json::parse(R"({"type": "nonsense"})"),
                                         nlohmann::json::object());
    CHECK_FALSE(broken.ok);
    CHECK_FALSE(broken.errors.empty());
}

TEST_CASE("a schema is itself validated against draft-07", "[agentloop][structured]") {
    CHECK(validate_schema(schema()).ok);
    CHECK_FALSE(validate_schema(nlohmann::json::parse(R"({"type": 12})")).ok);
    CHECK_FALSE(validate_schema(nlohmann::json::parse(R"({"required": "summary"})")).ok);
    // A `format` keyword in a schema is fine: the built-in checker is wired.
    CHECK(validate_schema(nlohmann::json::parse(R"({"type":"string","format":"uri"})")).ok);
}

TEST_CASE("the extractor strips fences and tolerates prose around the object",
          "[agentloop][structured]") {
    REQUIRE(extract_json("```json\n{\"a\": 1}\n```").has_value());
    CHECK(extract_json("```json\n{\"a\": 1}\n```")->at("a") == 1);
    REQUIRE(extract_json("Here is the report:\n{\"a\": [1, 2]}\nThanks.").has_value());
    CHECK(extract_json("Here is the report:\n{\"a\": [1, 2]}\nThanks.")->at("a").size() == 2);
    CHECK_FALSE(extract_json("no json here").has_value());
    CHECK_FALSE(extract_json("42").has_value());  // a bare scalar is not a report
    CHECK_FALSE(extract_json("{not json}").has_value());
}

TEST_CASE("the instruction block is JSON by default and a checklist for markdown",
          "[agentloop][structured]") {
    const std::string json = schema_instruction({"{\"type\":\"object\"}"}, false);
    CHECK(json.starts_with("---\nOUTPUT FORMAT\n"));
    CHECK(json.find("MUST be valid JSON") != std::string::npos);
    CHECK(json.find("{\"type\":\"object\"}") != std::string::npos);
    CHECK(json.find("Schema 1") == std::string::npos);
    const std::string two = schema_instruction({"{}", "{}"}, false);
    CHECK(two.find("Schema 1:") != std::string::npos);
    CHECK(two.find("Schema 2:") != std::string::npos);
    const std::string markdown = schema_instruction({"{}"}, true);
    CHECK(markdown.find("checklist") != std::string::npos);
    CHECK(markdown.find("do NOT output raw JSON") != std::string::npos);
    const std::string correction = correction_message({"/: required property 'x' not found"});
    CHECK(correction.find("- /: required property 'x' not found") != std::string::npos);
    CHECK(correction.find("ONLY the corrected JSON") != std::string::npos);
}

TEST_CASE("a conforming answer takes one attempt and the schema rode the request",
          "[agentloop][structured]") {
    Rig rig{{std::string{kGood}}};
    std::vector<apogee::harness::ChatMessage> history{apogee::harness::ChatMessage::user("review")};
    apogee::agentloop::Options options;
    options.model = "mock";
    apogee::agentloop::NullReporter reporter;
    const auto result = run_structured(rig.harness, history, options, reporter, schema());
    CHECK(result.conforms);
    CHECK(result.attempts == 1);
    CHECK(result.errors.empty());
    REQUIRE(result.json.has_value());
    CHECK(result.json->at("summary") == "s");
    REQUIRE(rig.mock->requests().size() == 1);
    // The provider was asked: the schema is on the request, as the wires expect it.
    CHECK(nlohmann::json::parse(rig.mock->requests().front().transient.response_schema) ==
          schema());
    CHECK(history.size() == 2);  // user, assistant
}

TEST_CASE("a non-conforming first answer earns one correction and the second is validated",
          "[agentloop][structured][retry]") {
    Rig rig{{std::string{kBad}, std::string{kGood}}};
    std::vector<apogee::harness::ChatMessage> history{apogee::harness::ChatMessage::user("review")};
    apogee::agentloop::Options options;
    options.model = "mock";
    apogee::agentloop::NullReporter reporter;
    const auto result = run_structured(rig.harness, history, options, reporter, schema());
    CHECK(result.conforms);
    CHECK(result.attempts == 2);
    CHECK(result.answer == kGood);
    // The correction is durable history, carrying the validator's words.
    REQUIRE(history.size() == 4);  // user, bad, correction, good
    CHECK(history[2].role == apogee::harness::Role::User);
    CHECK(history[2].content.plain_text().find("did not conform") != std::string::npos);
    CHECK(history[2].content.plain_text().find("human_summary") != std::string::npos);
    CHECK(rig.mock->requests().size() == 2);
}

TEST_CASE("two misses are delivered raw and flagged, never dropped and never a silent pass",
          "[agentloop][structured][retry]") {
    Rig rig{{std::string{kBad}, "I still cannot do JSON."}};
    std::vector<apogee::harness::ChatMessage> history{apogee::harness::ChatMessage::user("review")};
    apogee::agentloop::Options options;
    options.model = "mock";
    apogee::agentloop::NullReporter reporter;
    const auto result = run_structured(rig.harness, history, options, reporter, schema());
    CHECK_FALSE(result.conforms);
    CHECK(result.attempts == 2);
    CHECK(result.answer == "I still cannot do JSON.");
    CHECK_FALSE(result.json.has_value());
    REQUIRE_FALSE(result.errors.empty());
    CHECK(result.errors.front().find("not a JSON object") != std::string::npos);
    // Exactly ONE retry: never a third request.
    CHECK(rig.mock->requests().size() == 2);
}
