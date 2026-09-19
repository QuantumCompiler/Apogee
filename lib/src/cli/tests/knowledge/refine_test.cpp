#include "knowledge/refine.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "agentloop/structured.h"
#include "knowledge/clerk.h"
#include "knowledge/record.h"

/// The revision pass: the prompt pinned to the shipped file and sharing the
/// capture schema, the user message carrying exactly the six clerk-owned
/// fields, the instruction guards firing before any clerk call, and the two
/// fields the clerk does not own carried through.
namespace {

using apogee::knowledge::Draft;
using apogee::knowledge::Record;

std::string read_asset(const std::string& relative) {
    std::ifstream in{std::string{APOGEE_ASSETS_DIR} + "/" + relative, std::ios::binary};
    REQUIRE(in);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

struct FakeClerk {
    std::string text;
    std::vector<std::string> systems;
    std::vector<std::string> users;
    int calls = 0;

    [[nodiscard]] apogee::knowledge::ClerkFn fn() {
        return [this](std::string_view system, std::string_view user) {
            ++calls;
            systems.emplace_back(system);
            users.emplace_back(user);
            apogee::knowledge::ClerkOutcome outcome;
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

Record draft_record() {
    Record draft;
    draft.intent = "we dropped the cancel button because testers were confused";
    draft.decision = "remove the cancel button";
    draft.status = "shipped";
    draft.discipline = "ux";
    draft.downstream_link = "PROJ-42";
    draft.provenance.source = "chat";
    draft.provenance.attribution = "Ada";
    draft.supersedes = "kr-old";
    return draft;
}

std::string section(const std::string& message, const std::string& from, const std::string& to) {
    const std::size_t begin = message.find(from);
    REQUIRE(begin != std::string::npos);
    const std::size_t end = to.empty() ? message.size() : message.find(to, begin);
    REQUIRE(end != std::string::npos);
    return message.substr(begin + from.size(), end - begin - from.size());
}

}  // namespace

TEST_CASE(
    "the compiled-in refine prompt byte-matches the shipped file and shares the capture "
    "schema and OUTPUT FORMAT block",
    "[knowledge][refine][assets]") {
    CHECK(std::string{apogee::knowledge::refine_prompt()} ==
          read_asset("clerks/refine_prompt.txt"));
    const std::string prompt = apogee::knowledge::refine_system_prompt();
    CHECK(prompt.starts_with("You are the same disciplined normalization clerk"));
    CHECK(prompt.find("A revision is a diff, not a fresh capture") != std::string::npos);
    CHECK(prompt.find("CURRENT DRAFT") != std::string::npos);
    CHECK(prompt.find("REVIEWER INSTRUCTION") != std::string::npos);
    CHECK(prompt.find("RAW CONVERSATION") != std::string::npos);
    // One output shape for both passes: the identical block, the identical schema.
    const std::string marker = "\n\n---\nOUTPUT FORMAT\n";
    const std::string capture = apogee::knowledge::capture_system_prompt();
    CHECK(prompt.substr(prompt.find(marker)) == capture.substr(capture.find(marker)));
    CHECK(prompt.ends_with(std::string{apogee::knowledge::capture_schema_text()}));
}

TEST_CASE(
    "the user message carries exactly the six schema fields, the instruction, and the raw "
    "conversation or an explicit absence",
    "[knowledge][refine][message]") {
    const Record draft = draft_record();
    const std::string message =
        apogee::knowledge::refine_user_message(draft, "  mark it rejected  ", "Ada: drop it?");
    const nlohmann::json view =
        nlohmann::json::parse(section(message, "CURRENT DRAFT\n", "\n\nREVIEWER INSTRUCTION\n"));
    REQUIRE(view.is_object());
    CHECK(view.size() == 6);
    CHECK(view.contains("intent"));
    CHECK(view.contains("decision"));
    CHECK(view.contains("status"));
    CHECK(view.contains("discipline"));
    CHECK(view.contains("downstream_link"));
    CHECK(view.contains("provenance"));
    CHECK(view["provenance"]["source"] == "chat");
    CHECK(view["provenance"]["attribution"] == "Ada");
    // Nothing the clerk does not own reaches it.
    CHECK(message.find("kr-old") == std::string::npos);
    CHECK(message.find("supersedes") == std::string::npos);
    CHECK(message.find("raw_ref") == std::string::npos);
    CHECK(message.find("timestamp") == std::string::npos);
    CHECK(section(message, "REVIEWER INSTRUCTION\n", "\n\nRAW CONVERSATION\n") ==
          "mark it rejected");
    CHECK(section(message, "RAW CONVERSATION\n", "") == "Ada: drop it?");

    const std::string absent = apogee::knowledge::refine_user_message(draft, "x", "   ");
    CHECK(section(absent, "RAW CONVERSATION\n", "").starts_with("(not supplied"));
    CHECK(absent.find("do not invent") != std::string::npos);
    // An empty attribution is omitted, as the record's own JSON omits it.
    Record nameless = draft;
    nameless.provenance.attribution.clear();
    const nlohmann::json thin =
        nlohmann::json::parse(section(apogee::knowledge::refine_user_message(nameless, "x", ""),
                                      "CURRENT DRAFT\n", "\n\nREVIEWER INSTRUCTION\n"));
    CHECK_FALSE(thin["provenance"].contains("attribution"));
}

TEST_CASE("the instruction cap counts codepoints, and an empty instruction is refused",
          "[knowledge][refine][guards]") {
    using apogee::knowledge::validate_refine_instruction;
    CHECK(validate_refine_instruction("mark it rejected").empty());
    CHECK_FALSE(validate_refine_instruction("").empty());
    CHECK_FALSE(validate_refine_instruction("   \n ").empty());
    std::string accented;
    for (std::size_t i = 0; i < apogee::knowledge::kMaxRefineInstructionLen; ++i) {
        accented += "é";
    }
    CHECK(validate_refine_instruction(accented).empty());
    accented += "é";
    const std::string why = validate_refine_instruction(accented);
    CHECK(why.find("2001") != std::string::npos);
    CHECK(why.find("2000") != std::string::npos);
    CHECK(why.find("raw conversation") != std::string::npos);
    CHECK_FALSE(validate_refine_instruction(std::string(2001, 'a')).empty());
    CHECK(validate_refine_instruction(std::string(2000, 'a')).empty());
}

TEST_CASE("the guards fire BEFORE any clerk call", "[knowledge][refine][guards]") {
    FakeClerk clerk{.text = "{}"};
    const Record draft = draft_record();
    const Draft empty = apogee::knowledge::run_refine(clerk.fn(), draft, "", "raw");
    CHECK_FALSE(empty.ok());
    CHECK(empty.record.intent == draft.intent);
    const Draft long_one =
        apogee::knowledge::run_refine(clerk.fn(), draft, std::string(2001, 'x'), "raw");
    CHECK_FALSE(long_one.ok());
    CHECK(clerk.calls == 0);
}

TEST_CASE(
    "a revision applies the instruction, carries supersedes through, keeps a source the "
    "clerk dropped, and comes back as a draft",
    "[knowledge][refine][revise]") {
    FakeClerk clerk{.text =
                        R"({"intent": "we dropped the cancel button because testers were confused",
                            "decision": "remove the cancel button", "status": "rejected",
                            "discipline": "ux", "downstream_link": "PROJ-42",
                            "provenance": {"source": "", "attribution": "Ada"}})"};
    const Record draft = draft_record();
    const Draft revised =
        apogee::knowledge::run_refine(clerk.fn(), draft, "mark it rejected", "Ada: drop it?");
    REQUIRE(revised.ok());
    REQUIRE(clerk.calls == 1);
    CHECK(clerk.systems.front() == apogee::knowledge::refine_system_prompt());
    CHECK(clerk.users.front() ==
          apogee::knowledge::refine_user_message(draft, "mark it rejected", "Ada: drop it?"));
    CHECK(revised.record.status == "rejected");
    CHECK(revised.record.supersedes == "kr-old");
    CHECK(revised.record.provenance.source == "chat");
    CHECK(revised.record.provenance.attribution == "Ada");
    CHECK(revised.record.id.empty());
    CHECK(revised.record.timestamp.empty());
    CHECK(revised.record.raw_ref.empty());

    // A revision the clerk could not shape is a failure, never a partial.
    FakeClerk prose{.text = "I would rather not."};
    const Draft failed = apogee::knowledge::run_refine(prose.fn(), draft, "x", "");
    CHECK_FALSE(failed.ok());
    CHECK(failed.error.starts_with("the clerk did not return a record"));
    FakeClerk no_intent{.text = R"({"decision": "x", "status": "shipped", "discipline": "eng",
                                   "provenance": {"source": "chat"}})"};
    CHECK_FALSE(apogee::knowledge::run_refine(no_intent.fn(), draft, "x", "").ok());
    // A schema miss whose JSON would still read as a record is a failure
    // too: the schema is the contract, not the parser.
    const apogee::knowledge::ClerkFn loose = [](std::string_view, std::string_view) {
        apogee::knowledge::ClerkOutcome outcome;
        outcome.json = nlohmann::json{{"intent", "x"},
                                      {"decision", "y"},
                                      {"status", "shipped"},
                                      {"discipline", "eng"},
                                      {"provenance", {{"source", "chat"}}},
                                      {"verdict", "extra"}};
        outcome.conforms = false;
        outcome.errors = {"/: additional property 'verdict' is not allowed"};
        outcome.attempts = 2;
        return outcome;
    };
    const Draft loose_failed = apogee::knowledge::run_refine(loose, draft, "x", "");
    CHECK_FALSE(loose_failed.ok());
    CHECK(loose_failed.error.find("additional property 'verdict'") != std::string::npos);
    CHECK(loose_failed.error.ends_with("(after 2 attempts)"));
}
