#include "knowledge/clerk.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "agentloop/structured.h"
#include "backends/mock.h"
#include "harness/config.h"
#include "harness/harness.h"
#include "knowledge/record.h"

/// The capture clerk: the compiled-in prompt and schema pinned to the shipped
/// files, the overrides, the failure contract, and the production clerk over
/// the loop -- one structured-output call, marked a side request.
namespace {

using apogee::backends::MockProvider;
using apogee::backends::MockTurn;
using apogee::knowledge::ClerkFn;
using apogee::knowledge::ClerkOutcome;
using apogee::knowledge::Draft;
using apogee::knowledge::Overrides;
using apogee::knowledge::Record;

std::string read_asset(const std::string& relative) {
    std::ifstream in{std::string{APOGEE_ASSETS_DIR} + "/" + relative, std::ios::binary};
    REQUIRE(in);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

/// A clerk's conforming answer, as JSON text.
const std::string kAnswer =
    R"({"intent": "We dropped the cancel button because testers kept mistaking it for back.",
 "decision": "Remove the cancel button.", "status": "shipped", "discipline": "ux",
 "downstream_link": "PROJ-42", "provenance": {"source": "meeting", "attribution": "Ada"}})";

/// A fake clerk that answers with `text` and records what it was asked.
struct FakeClerk {
    std::string text;
    std::vector<std::string> systems;
    std::vector<std::string> users;
    int calls = 0;

    [[nodiscard]] ClerkFn fn() {
        return [this](std::string_view system, std::string_view user) {
            ++calls;
            systems.emplace_back(system);
            users.emplace_back(user);
            ClerkOutcome outcome;
            outcome.answer = text;
            outcome.attempts = 1;
            outcome.json = apogee::agentloop::extract_json(text);
            if (outcome.json.has_value()) {
                const apogee::agentloop::ValidationResult valid =
                    apogee::agentloop::validate_against(apogee::knowledge::capture_schema(),
                                                        *outcome.json);
                outcome.conforms = valid.ok;
                outcome.errors = valid.errors;
            } else {
                outcome.errors = {"/: the answer is not a JSON object"};
            }
            return outcome;
        };
    }
};

}  // namespace

TEST_CASE("the compiled-in prompt and schema byte-match the shipped clerk files",
          "[knowledge][clerk][assets]") {
    CHECK(std::string{apogee::knowledge::capture_prompt()} ==
          read_asset("clerks/capture_prompt.txt"));
    CHECK(std::string{apogee::knowledge::capture_schema_text()} ==
          read_asset("clerks/capture_schema.json"));
}

TEST_CASE("the capture schema is a valid, closed draft-07 schema that requires the intent",
          "[knowledge][clerk][schema]") {
    const nlohmann::json schema = apogee::knowledge::capture_schema();
    const apogee::agentloop::ValidationResult valid = apogee::agentloop::validate_schema(schema);
    INFO((valid.errors.empty() ? std::string{} : valid.errors.front()));
    CHECK(valid.ok);
    CHECK(schema.at("type") == "object");
    CHECK(schema.at("additionalProperties") == false);
    const nlohmann::json required = schema.at("required");
    CHECK(std::find(required.begin(), required.end(), "intent") != required.end());
    CHECK(std::find(required.begin(), required.end(), "status") != required.end());
    CHECK(std::find(required.begin(), required.end(), "provenance") != required.end());
    // id, raw_ref and timestamp are the system's: the model is never asked
    // for them, and the closed schema refuses them if it volunteers them.
    CHECK_FALSE(schema.at("properties").contains("id"));
    CHECK_FALSE(schema.at("properties").contains("raw_ref"));
    CHECK_FALSE(schema.at("properties").contains("timestamp"));
    CHECK(schema.at("properties").at("status").at("enum").size() == 3);
    const nlohmann::json instance = nlohmann::json::parse(kAnswer);
    CHECK(apogee::agentloop::validate_against(schema, instance).ok);
    nlohmann::json extra = instance;
    extra["id"] = "kr-x";
    CHECK_FALSE(apogee::agentloop::validate_against(schema, extra).ok);
}

TEST_CASE(
    "the system prompt is the prompt, the restraint rule, then the OUTPUT FORMAT block "
    "worded once for the whole product",
    "[knowledge][clerk][prompt]") {
    const std::string prompt = apogee::knowledge::capture_system_prompt();
    CHECK(prompt.starts_with("You are a disciplined normalization clerk"));
    CHECK(prompt.find("Accuracy and restraint matter more than completeness") != std::string::npos);
    CHECK(prompt.find("Keep attribution in its own field; never weave names into the") !=
          std::string::npos);
    CHECK(prompt.find("\n\n---\nOUTPUT FORMAT\n") != std::string::npos);
    CHECK(prompt.ends_with(std::string{apogee::knowledge::capture_schema_text()}));
    // The same block an agent's persona ends with: one wording, one function.
    CHECK(prompt.find(apogee::agentloop::schema_instruction(
              {std::string{apogee::knowledge::capture_schema_text()}}, false)) !=
          std::string::npos);
    CHECK(apogee::knowledge::with_output_format("  hello  ").starts_with("hello\n\n---\n"));
}

TEST_CASE("run_capture hands the clerk the system prompt and the raw text and drafts its answer",
          "[knowledge][clerk][capture]") {
    FakeClerk clerk{.text = kAnswer};
    const Draft draft = apogee::knowledge::run_capture(clerk.fn(), "the raw conversation", {});
    REQUIRE(draft.ok());
    REQUIRE(clerk.calls == 1);
    CHECK(clerk.systems.front() == apogee::knowledge::capture_system_prompt());
    CHECK(clerk.users.front() == "the raw conversation");
    CHECK(draft.record.intent.starts_with("We dropped the cancel button"));
    CHECK(draft.record.decision == "Remove the cancel button.");
    CHECK(draft.record.status == "shipped");
    CHECK(draft.record.discipline == "ux");
    CHECK(draft.record.downstream_link == "PROJ-42");
    CHECK(draft.record.provenance.source == "meeting");
    CHECK(draft.record.provenance.attribution == "Ada");
    // A draft: nothing the store mints.
    CHECK(draft.record.id.empty());
    CHECK(draft.record.timestamp.empty());
    CHECK(draft.record.raw_ref.empty());
    CHECK(draft.record.supersedes.empty());
}

TEST_CASE("every override beats the clerk for its field, and only its field",
          "[knowledge][clerk][overrides]") {
    FakeClerk clerk{.text = kAnswer};
    const Overrides overrides{.status = "abandoned",
                              .discipline = "Eng",
                              .source = "chat",
                              .link = "T-7",
                              .supersedes = "kr-old"};
    const Draft draft = apogee::knowledge::run_capture(clerk.fn(), "raw", overrides);
    REQUIRE(draft.ok());
    // Normalised after the override: a synonym typed by the user folds too.
    CHECK(draft.record.status == "rejected");
    CHECK(draft.record.discipline == "eng");
    CHECK(draft.record.provenance.source == "chat");
    CHECK(draft.record.downstream_link == "T-7");
    CHECK(draft.record.supersedes == "kr-old");
    // Untouched: what the clerk said stays.
    CHECK(draft.record.intent.starts_with("We dropped"));
    CHECK(draft.record.provenance.attribution == "Ada");

    // Each alone, so a field cannot leak into its neighbour.
    CHECK(apogee::knowledge::run_capture(clerk.fn(), "raw", Overrides{.status = "rejected"})
              .record.discipline == "ux");
    CHECK(apogee::knowledge::run_capture(clerk.fn(), "raw", Overrides{.discipline = "eng"})
              .record.status == "shipped");
    CHECK(apogee::knowledge::run_capture(clerk.fn(), "raw", Overrides{.source = "x"})
              .record.downstream_link == "PROJ-42");
    CHECK(apogee::knowledge::run_capture(clerk.fn(), "raw", Overrides{.link = "L"})
              .record.provenance.source == "meeting");
    CHECK(apogee::knowledge::run_capture(clerk.fn(), "raw", Overrides{.supersedes = "s"})
              .record.downstream_link == "PROJ-42");

    // An override that does not validate is the user's mistake, said so.
    const Draft bad =
        apogee::knowledge::run_capture(clerk.fn(), "raw", Overrides{.status = "perhaps"});
    CHECK_FALSE(bad.ok());
    CHECK(bad.error.find("perhaps") != std::string::npos);
}

TEST_CASE("a non-conforming clerk answer is a failed capture carrying the validator's words",
          "[knowledge][clerk][failure]") {
    FakeClerk prose{.text = "I cannot produce JSON."};
    const Draft failed = apogee::knowledge::run_capture(prose.fn(), "raw", {});
    CHECK_FALSE(failed.ok());
    CHECK(failed.error.starts_with("the clerk did not return a record"));
    CHECK(failed.error.find("not a JSON object") != std::string::npos);
    CHECK(failed.error.find("(after 1 attempt)") != std::string::npos);

    // Valid JSON that misses the load-bearing field is a schema miss, not a
    // record with no why.
    FakeClerk no_intent{.text = R"({"decision": "x", "status": "shipped", "discipline": "eng",
                                     "provenance": {"source": "chat"}})"};
    const Draft missing = apogee::knowledge::run_capture(no_intent.fn(), "raw", {});
    CHECK_FALSE(missing.ok());
    CHECK(missing.error.find("intent") != std::string::npos);
    CHECK(missing.record.intent.empty());
}

TEST_CASE("a schema miss is a failed capture even when the record itself would validate",
          "[knowledge][clerk][failure]") {
    // Valid JSON, an intent, a canonical status -- and one key the closed
    // schema does not allow. The record would pass `validate`; the clerk's
    // contract is the schema, and a non-conforming answer is what the
    // corrective retry exists for, never something to store.
    FakeClerk extra{.text = R"({"intent": "why", "decision": "what", "status": "shipped",
                                "discipline": "eng", "downstream_link": "",
                                "provenance": {"source": "chat"}, "id": "kr-invented"})"};
    const Draft draft = apogee::knowledge::run_capture(extra.fn(), "raw", {});
    CHECK_FALSE(draft.ok());
    CHECK(draft.error.starts_with("the clerk did not return a record"));
    CHECK(draft.error.find("id") != std::string::npos);
}

TEST_CASE("draft_record clears what only persistence mints, whatever the clerk volunteered",
          "[knowledge][clerk][draft]") {
    Record volunteered;
    volunteered.id = "kr-invented";
    volunteered.timestamp = "2026-01-01T00:00:00.000000Z";
    volunteered.raw_ref = "/somewhere/else.md";
    volunteered.intent = "why";
    volunteered.status = "shipped";
    const Draft draft = apogee::knowledge::draft_record(volunteered, {});
    REQUIRE(draft.ok());
    CHECK(draft.record.id.empty());
    CHECK(draft.record.timestamp.empty());
    CHECK(draft.record.raw_ref.empty());
    CHECK(draft.record.intent == "why");
}

TEST_CASE("finalize mints the id and the timestamp from one clock reading",
          "[knowledge][clerk][finalize]") {
    FakeClerk clerk{.text = kAnswer};
    const Draft draft = apogee::knowledge::run_capture(clerk.fn(), "raw", {});
    const auto when = std::chrono::system_clock::from_time_t(1789300000);
    const Record record = apogee::knowledge::finalize(draft.record, when);
    CHECK(record.id.starts_with("kr-"));
    CHECK(record.timestamp.starts_with(record.id.substr(3, 4) + "-"));
    CHECK(record.intent == draft.record.intent);
}

TEST_CASE(
    "the production clerk is one structured-output call on the loop: the schema on the "
    "request, the clerk's temperature and budget, no tools, a side request, one retry",
    "[knowledge][clerk][structured]") {
    const apogee::harness::Config config = apogee::harness::parse_config(
        "models:\n  default: mock\nbackends:\n  mock:\n    type: mock\n", "<test>");
    apogee::harness::Harness harness{config};
    MockProvider::Options options;
    options.backend_name = "mock";
    options.turns = {MockTurn{"not json at all"}, MockTurn{kAnswer}};
    auto provider = std::make_shared<MockProvider>(std::move(options));
    harness.register_provider("mock", provider);
    harness.use_default_router();

    const ClerkFn clerk = apogee::knowledge::make_structured_clerk(harness, "mock");
    const Draft draft = apogee::knowledge::run_capture(clerk, "the raw conversation", {});
    REQUIRE(draft.ok());
    CHECK(draft.record.provenance.attribution == "Ada");

    // Two model turns: the miss, then the corrected answer.
    REQUIRE(provider->requests().size() == 2);
    const apogee::harness::ChatRequest& first = provider->requests().front();
    CHECK(first.model == "mock");
    REQUIRE(first.temperature.has_value());
    CHECK(*first.temperature == apogee::knowledge::kClerkTemperature);
    REQUIRE(first.max_tokens.has_value());
    CHECK(*first.max_tokens == apogee::knowledge::kClerkMaxTokens);
    CHECK(first.tools.empty());
    CHECK(first.transient.side_request);
    CHECK(first.transient.response_schema == apogee::knowledge::capture_schema().dump());
    REQUIRE(first.messages.size() == 2);
    CHECK(first.messages[0].role == apogee::harness::Role::System);
    CHECK(first.messages[0].content.plain_text() == apogee::knowledge::capture_system_prompt());
    CHECK(first.messages[1].content.plain_text() == "the raw conversation");
    // The correction rides the second request as durable history.
    const apogee::harness::ChatRequest& second = provider->requests().back();
    CHECK(second.transient.side_request);
    CHECK(second.messages.back().content.plain_text().find("did not conform") != std::string::npos);

    // Twice non-conforming: a failed capture, never a stored guess.
    MockProvider::Options stubborn;
    stubborn.backend_name = "mock";
    stubborn.turns = {MockTurn{"no"}, MockTurn{"still no"}};
    harness.register_provider("mock", std::make_shared<MockProvider>(std::move(stubborn)));
    harness.use_default_router();
    const Draft failed = apogee::knowledge::run_capture(
        apogee::knowledge::make_structured_clerk(harness, "mock"), "raw", {});
    CHECK_FALSE(failed.ok());
    CHECK(failed.error.find("after 2 attempts") != std::string::npos);
}
