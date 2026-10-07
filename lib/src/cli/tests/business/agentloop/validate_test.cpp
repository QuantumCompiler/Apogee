#include "agentloop/validate.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "agentloop/loop.h"
#include "agentloop/reporter.h"
#include "backends/mock.h"
#include "contracts/config.h"
#include "harness/harness.h"

/// Rubber-duck validation's core (27g): cheap first -- a structural catch
/// wakes no model, and the call counts say so -- the bounded rounds with no
/// third, and the briefs and lines a person or a member reads, golden.
namespace {

using apogee::agentloop::Check;
using apogee::agentloop::MemberAnswer;
using apogee::agentloop::read_verdict;
using apogee::agentloop::Rounds;
using apogee::agentloop::run_checks;
using apogee::agentloop::StructuralCheck;
using apogee::agentloop::ToolArgChecks;
using apogee::agentloop::validate_artifact;
using apogee::agentloop::Validated;
using apogee::agentloop::Verdict;
using apogee::agentloop::Verifier;

/// A scripted verifier: each reply in turn (the last repeated), every brief
/// it was sent kept.
struct Script {
    std::vector<std::string> replies;
    std::vector<std::string> briefs;
    std::string backend = "l3b";
    std::string refused;
    std::string failed;

    [[nodiscard]] Verifier verifier(std::string role = "utility") {
        Verifier out;
        out.role = std::move(role);
        out.ask = [this](const std::string& brief) {
            briefs.push_back(brief);
            MemberAnswer answer;
            answer.backend = backend;
            answer.refused = refused;
            answer.failed = failed;
            if (refused.empty() && failed.empty()) {
                const std::size_t at = std::min(briefs.size(), replies.size()) - 1;
                answer.text = replies.empty() ? std::string{} : replies[at];
            }
            if (!refused.empty()) {
                briefs.pop_back();  // a refusal sends nothing
            }
            return answer;
        };
        return out;
    }
};

StructuralCheck passes() {
    return [] { return std::string{}; };
}

StructuralCheck fails(std::string why) {
    return [why = std::move(why)] { return why; };
}

Check objection(std::string by, std::string why) {
    Check out;
    out.outcome = Check::Outcome::Object;
    out.by = std::move(by);
    out.objection = std::move(why);
    out.model_calls = 1;
    return out;
}

Check pass() {
    Check out;
    out.by = "utility (l3b)";
    out.model_calls = 1;
    return out;
}

}  // namespace

TEST_CASE("a verifier's reply is read for its verdict, never guessed at",
          "[agentloop][validate][verdict]") {
    const auto kind = [](std::string_view reply) { return read_verdict(reply).kind; };
    CHECK(kind("AGREE") == Verdict::Kind::Agree);
    CHECK(kind("agree.") == Verdict::Kind::Agree);
    CHECK(kind("  **AGREE** -- the record matches") == Verdict::Kind::Agree);
    CHECK(kind("Agreed") == Verdict::Kind::Agree);
    CHECK(kind("OBJECT") == Verdict::Kind::Object);
    CHECK(kind("Objection: the link is wrong") == Verdict::Kind::Object);
    // Where it was not asked for: exactly one verdict, in capitals, as a word.
    CHECK(kind("The record is right. AGREE") == Verdict::Kind::Agree);
    CHECK(kind("I OBJECT to the status") == Verdict::Kind::Object);
    CHECK(kind("I DISAGREE") == Verdict::Kind::Unread);
    // An agreement that objects in the same breath is no verdict; an
    // objection that agrees in passing is still one.
    CHECK(kind("AGREE on the intent, but OBJECT to the link") == Verdict::Kind::Unread);
    CHECK(kind("OBJECT: I AGREE with the intent, not the link") == Verdict::Kind::Object);
    CHECK(kind("hmm, maybe") == Verdict::Kind::Unread);
    CHECK(kind("") == Verdict::Kind::Unread);
    CHECK(kind("I agree mostly") == Verdict::Kind::Unread);

    // The reason is what follows the word, past its punctuation.
    CHECK(read_verdict("**OBJECT**: the PR is #412, not #421.").reason ==
          "the PR is #412, not #421.");
    CHECK(read_verdict("OBJECT — wrong file").reason == "wrong file");
    CHECK(read_verdict("OBJECT\n\nThe status should be shipped.").reason ==
          "The status should be shipped.");
    CHECK(read_verdict("OBJECT").reason == "(no reason given)");
    CHECK(read_verdict("AGREE").reason.empty());
    CHECK(read_verdict("I OBJECT to the status").reason == "I OBJECT to the status");
}

TEST_CASE("cheap first: a structural catch wakes no model, and builds no brief",
          "[agentloop][validate][cheap]") {
    Script script{.replies = {"AGREE"}};
    const Verifier verifier = script.verifier();
    int briefs_built = 0;
    const auto brief = [&briefs_built] {
        ++briefs_built;
        return std::string{"Check this."};
    };

    SECTION("the first structural failure decides") {
        int later = 0;
        const Check checked = run_checks({passes(), fails("No such file: notes/final.md"),
                                          [&later] {
                                              ++later;
                                              return std::string{};
                                          }},
                                         &verifier, brief);
        CHECK(checked.outcome == Check::Outcome::Object);
        CHECK(checked.structural());
        CHECK(checked.objection == "No such file: notes/final.md");
        CHECK(checked.model_calls == 0);
        CHECK(later == 0);
        CHECK(script.briefs.empty());
        CHECK(briefs_built == 0);
    }
    SECTION("a clean structural pass with the seam off wakes nothing either") {
        const Check checked = run_checks({passes(), passes()}, nullptr, brief);
        CHECK(checked.outcome == Check::Outcome::Pass);
        CHECK(checked.model_calls == 0);
        CHECK(script.briefs.empty());
        CHECK(briefs_built == 0);
    }
    SECTION("structure passing, the verifier is asked once, with the brief") {
        const Check checked = run_checks({passes()}, &verifier, brief);
        CHECK(checked.outcome == Check::Outcome::Pass);
        CHECK(checked.by == "utility (l3b)");
        CHECK(checked.model_calls == 1);
        REQUIRE(script.briefs.size() == 1);
        CHECK(script.briefs.front() == "Check this.");
    }
}

TEST_CASE("a verifier that cannot decide leaves the check unchecked, and says why",
          "[agentloop][validate][degrade]") {
    const auto brief = [] { return std::string{"Check this."}; };
    SECTION("an objection") {
        Script script{.replies = {"OBJECT: wrong file"}};
        const Verifier verifier = script.verifier();
        const Check checked = run_checks({}, &verifier, brief);
        CHECK(checked.outcome == Check::Outcome::Object);
        CHECK(checked.objection == "wrong file");
        CHECK(checked.by == "utility (l3b)");
    }
    SECTION("the budget spent: not asked") {
        Script script{.replies = {"AGREE"}};
        Verifier verifier = script.verifier();
        verifier.unavailable = [] { return std::string{"the turn's member-call budget is spent"}; };
        const Check checked = run_checks({}, &verifier, brief);
        CHECK(checked.outcome == Check::Outcome::Unchecked);
        CHECK(checked.note == "the turn's member-call budget is spent");
        CHECK(checked.model_calls == 0);
        CHECK(script.briefs.empty());
    }
    SECTION("a brief over the suite's cap: not asked") {
        Script script{.replies = {"AGREE"}};
        Verifier verifier = script.verifier();
        verifier.brief_tokens = 10;
        const Check checked =
            run_checks({}, &verifier, [] { return std::string(std::size_t{4} * 20, 'w'); });
        CHECK(checked.outcome == Check::Outcome::Unchecked);
        CHECK(checked.note ==
              "the brief would be about 20 tokens, over the suite's cap of 10 "
              "(consult_caps.brief_tokens) -- not checked by utility");
        CHECK(script.briefs.empty());
    }
    SECTION("refused by the member call") {
        Script script{.refused = "'paid' (utility) is billed per call"};
        const Verifier verifier = script.verifier();
        const Check checked = run_checks({}, &verifier, brief);
        CHECK(checked.outcome == Check::Outcome::Unchecked);
        CHECK(checked.note ==
              "utility (l3b) could not be asked: 'paid' (utility) is billed per call");
        CHECK(checked.model_calls == 0);
    }
    SECTION("a member that failed") {
        Script script{.failed = "'l3b' gave no answer"};
        const Verifier verifier = script.verifier();
        const Check checked = run_checks({}, &verifier, brief);
        CHECK(checked.outcome == Check::Outcome::Unchecked);
        CHECK(checked.note == "utility (l3b) failed to check: 'l3b' gave no answer");
        CHECK(checked.model_calls == 1);
    }
    SECTION("a reply that is neither verdict") {
        Script script{.replies = {"It looks mostly fine to me."}};
        const Verifier verifier = script.verifier();
        const Check checked = run_checks({}, &verifier, brief);
        CHECK(checked.outcome == Check::Outcome::Unchecked);
        CHECK(checked.note ==
              "utility (l3b)'s reply was neither AGREE nor OBJECT: \"It looks mostly fine to "
              "me.\"");
    }
}

TEST_CASE("the rounds: one objection revised, the second surfaced, and no round three",
          "[agentloop][validate][rounds]") {
    SECTION("a pass proceeds, and the rounds are over") {
        Rounds rounds;
        CHECK(rounds.round() == 1);
        CHECK(rounds.next(pass()) == Rounds::Step::Proceed);
        CHECK(rounds.over());
        CHECK_THROWS_AS((void)rounds.next(pass()), std::logic_error);
    }
    SECTION("an objection, a revision, a pass") {
        Rounds rounds;
        CHECK(rounds.next(objection("utility (l3b)", "wrong")) == Rounds::Step::Revise);
        CHECK_FALSE(rounds.over());
        CHECK(rounds.round() == 2);
        CHECK(rounds.next(pass()) == Rounds::Step::Proceed);
        CHECK(rounds.over());
    }
    SECTION("two objections: surfaced, and nothing after") {
        Rounds rounds;
        CHECK(rounds.next(objection("utility (l3b)", "wrong")) == Rounds::Step::Revise);
        CHECK(rounds.next(objection("utility (l3b)", "still wrong")) == Rounds::Step::Surface);
        CHECK(rounds.over());
        CHECK_THROWS_AS((void)rounds.next(objection("utility (l3b)", "again")), std::logic_error);
        CHECK_THROWS_AS((void)rounds.next(pass()), std::logic_error);
    }
    SECTION("a check that could not decide proceeds") {
        Rounds rounds;
        Check unchecked;
        unchecked.outcome = Check::Outcome::Unchecked;
        unchecked.note = "budget";
        CHECK(rounds.next(unchecked) == Rounds::Step::Proceed);
        CHECK(rounds.over());
    }
}

TEST_CASE("an artifact through the rounds: the call counts are the bound",
          "[agentloop][validate][rounds]") {
    int revisions = 0;
    const auto revise_to = [&revisions](std::string next) {
        return [&revisions, next = std::move(next)](const std::string&, const std::string&,
                                                    std::string&) -> std::optional<std::string> {
            ++revisions;
            return next;
        };
    };
    const auto checking = [](Verifier& verifier) {
        return [&verifier](const std::string& artifact) {
            return run_checks({}, &verifier, [artifact] { return "Check: " + artifact; });
        };
    };

    SECTION("agreed with: passed, one call, no revision") {
        Script script{.replies = {"AGREE"}};
        Verifier verifier = script.verifier();
        const Validated out = validate_artifact("A", checking(verifier), revise_to("B"), true);
        CHECK(out.result == Validated::Result::Passed);
        CHECK(out.artifact == "A");
        CHECK(out.model_calls == 1);
        CHECK(out.revisions == 0);
        CHECK(revisions == 0);
    }
    SECTION("objected to, revised, the revision agreed with") {
        Script script{.replies = {"OBJECT: not A", "AGREE"}};
        Verifier verifier = script.verifier();
        const Validated out = validate_artifact("A", checking(verifier), revise_to("B"), true);
        CHECK(out.result == Validated::Result::Revised);
        CHECK(out.artifact == "B");
        CHECK(out.original == "A");
        CHECK(out.objection == "not A");
        CHECK(out.model_calls == 2);
        CHECK(out.revisions == 1);
        REQUIRE(script.briefs.size() == 2);
        CHECK(script.briefs[1] == "Check: B");
    }
    SECTION("a verifier that objects to everything: two checks, one revision, surfaced") {
        Script script{.replies = {"OBJECT: no", "OBJECT: still no", "OBJECT: never"}};
        Verifier verifier = script.verifier();
        const Validated out = validate_artifact("A", checking(verifier), revise_to("B"), true);
        CHECK(out.result == Validated::Result::Disputed);
        CHECK(out.artifact == "B");
        CHECK(out.objection == "still no");
        CHECK(out.by == "utility (l3b)");
        CHECK(out.model_calls == 2);
        CHECK(out.revisions == 1);
        CHECK(revisions == 1);
        CHECK(script.briefs.size() == 2);
    }
    SECTION("the producer standing by it: surfaced, the same question not asked twice") {
        Script script{.replies = {"OBJECT: no"}};
        Verifier verifier = script.verifier();
        const Validated out = validate_artifact("A", checking(verifier), revise_to("A"), true);
        CHECK(out.result == Validated::Result::Disputed);
        CHECK(out.insisted);
        CHECK(out.model_calls == 1);
        CHECK(script.briefs.size() == 1);
    }
    SECTION("a revision that could not be made: the first stands, disputed") {
        Script script{.replies = {"OBJECT: no"}};
        Verifier verifier = script.verifier();
        const Validated out = validate_artifact(
            "A", checking(verifier),
            [](const std::string&, const std::string&,
               std::string& note) -> std::optional<std::string> {
                note = "its revision failed: the clerk did not return a record";
                return std::nullopt;
            },
            true);
        CHECK(out.result == Validated::Result::Disputed);
        CHECK(out.artifact == "A");
        CHECK(out.revisions == 1);
        REQUIRE(out.notes.size() == 1);
        CHECK(out.notes.front() == "its revision failed: the clerk did not return a record");
    }
    SECTION("without a re-check the revision stands beside the objection, unverified") {
        Script script{.replies = {"OBJECT: no", "AGREE"}};
        Verifier verifier = script.verifier();
        const Validated out = validate_artifact("A", checking(verifier), revise_to("B"), false);
        CHECK(out.result == Validated::Result::Disputed);
        CHECK(out.artifact == "B");
        CHECK(out.model_calls == 1);
        CHECK(script.briefs.size() == 1);
    }
    SECTION("a verifier that cannot decide: unchecked, and nothing revised") {
        Script script{.replies = {"no idea"}};
        Verifier verifier = script.verifier();
        const Validated out = validate_artifact("A", checking(verifier), revise_to("B"), true);
        CHECK(out.result == Validated::Result::Unchecked);
        CHECK(out.revisions == 0);
        CHECK(out.notes.size() == 1);
    }
    SECTION("a revision the verifier could not check leaves the objection standing") {
        Script script{.replies = {"OBJECT: no", "hmm"}};
        Verifier verifier = script.verifier();
        const Validated out = validate_artifact("A", checking(verifier), revise_to("B"), true);
        CHECK(out.result == Validated::Result::Disputed);
        CHECK(out.objection == "no");
        CHECK(out.artifact == "B");
    }
}

TEST_CASE("tool arguments: round one's objection is the result, round two runs",
          "[agentloop][validate][tool_args]") {
    const auto brief = [] { return std::string{"Check the call."}; };
    SECTION("an objection, then a revised call that passes") {
        Script script{.replies = {"OBJECT: the user asked for the draft", "AGREE"}};
        const Verifier verifier = script.verifier();
        ToolArgChecks checks;
        const ToolArgChecks::Decision first = checks.check(
            "delete_file", R"({"path": "notes/final.md"})", {passes()}, &verifier, brief);
        REQUIRE(first.result.has_value());
        CHECK(*first.result ==
              "Not run: before delete_file ran, utility (l3b) checked it and objected -- the "
              "user asked for the draft\nCorrect the call if the objection is right. If it is "
              "wrong, make the same call again: it will run, and the user is shown the "
              "objection.");
        REQUIRE(first.said.size() == 1);
        CHECK(first.said.front() ==
              "validate: utility (l3b) objected to delete_file {\"path\":\"notes/final.md\"} "
              "-- the user asked for the draft -- returned to the model for one revision");
        CHECK(first.model_calls == 1);
        CHECK(checks.pending("delete_file"));

        const ToolArgChecks::Decision second = checks.check(
            "delete_file", R"({"path": "notes/draft.md"})", {passes()}, &verifier, brief);
        CHECK_FALSE(second.result.has_value());
        CHECK(second.said.empty());
        CHECK(second.model_calls == 1);
        CHECK_FALSE(checks.pending("delete_file"));
        CHECK(script.briefs.size() == 2);
    }
    SECTION("the same call again: it runs, surfaced, and the verifier is not asked twice") {
        Script script{.replies = {"OBJECT: wrong file"}};
        const Verifier verifier = script.verifier();
        ToolArgChecks checks;
        (void)checks.check("delete_file", R"({"path":"a"})", {}, &verifier, brief);
        // Spaced differently, the same call.
        const ToolArgChecks::Decision again =
            checks.check("delete_file", R"({ "path" : "a" })", {}, &verifier, brief);
        CHECK_FALSE(again.result.has_value());
        REQUIRE(again.said.size() == 1);
        CHECK(again.said.front() ==
              "validate: the model made the same call again, and delete_file {\"path\":\"a\"} "
              "runs over utility (l3b)'s objection -- wrong file");
        CHECK(again.model_calls == 0);
        CHECK(script.briefs.size() == 1);
    }
    SECTION("a revision still objected to: it runs, with the dispute said first") {
        Script script{.replies = {"OBJECT: no", "OBJECT: still no", "OBJECT: never"}};
        const Verifier verifier = script.verifier();
        ToolArgChecks checks;
        CHECK(checks.check("delete_file", R"({"path":"a"})", {}, &verifier, brief)
                  .result.has_value());
        const ToolArgChecks::Decision second =
            checks.check("delete_file", R"({"path":"b"})", {}, &verifier, brief);
        CHECK_FALSE(second.result.has_value());
        REQUIRE(second.said.size() == 1);
        CHECK(second.said.front() ==
              "validate: delete_file {\"path\":\"b\"} runs as the model revised it, over "
              "utility (l3b)'s objection -- still no");
        // Settled: the next call of the tool is a new one, at round one.
        CHECK_FALSE(checks.pending("delete_file"));
        CHECK(checks.check("delete_file", R"({"path":"c"})", {}, &verifier, brief)
                  .result.has_value());
        CHECK(script.briefs.size() == 3);
    }
    SECTION("a structural catch: returned without a model call") {
        Script script{.replies = {"AGREE"}};
        const Verifier verifier = script.verifier();
        ToolArgChecks checks;
        const ToolArgChecks::Decision first =
            checks.check("delete_file", R"({"path":"gone"})", {fails("No such file: /w/gone")},
                         &verifier, brief);
        REQUIRE(first.result.has_value());
        CHECK(*first.result ==
              "Not run: delete_file's arguments failed a check made before it runs -- No such "
              "file: /w/gone\nCorrect the call. If it is right as it is, make the same call "
              "again: it will run.");
        CHECK(first.said.front() ==
              "validate: delete_file {\"path\":\"gone\"} failed its check -- No such file: "
              "/w/gone -- returned to the model for one revision");
        CHECK(first.model_calls == 0);
        CHECK(script.briefs.empty());
    }
    SECTION("a verifier that cannot decide: the call runs, said") {
        Script script{.replies = {"AGREE"}};
        Verifier verifier = script.verifier();
        verifier.unavailable = [] { return std::string{"the budget is spent"}; };
        ToolArgChecks checks;
        const ToolArgChecks::Decision decided =
            checks.check("write_file", R"({"path":"x"})", {}, &verifier, brief);
        CHECK_FALSE(decided.result.has_value());
        REQUIRE(decided.said.size() == 1);
        CHECK(decided.said.front() ==
              "validate: write_file {\"path\":\"x\"} not checked -- the budget is spent; its "
              "structure passed");
    }
    SECTION("a revision nobody could check runs, with the first objection said") {
        Script script{.replies = {"OBJECT: wrong file"}};
        Verifier verifier = script.verifier();
        bool spent = false;
        verifier.unavailable = [&spent] {
            return spent ? std::string{"the budget is spent"} : std::string{};
        };
        ToolArgChecks checks;
        CHECK(checks.check("delete_file", R"({"path":"a"})", {}, &verifier, brief)
                  .result.has_value());
        spent = true;
        const ToolArgChecks::Decision second =
            checks.check("delete_file", R"({"path":"b"})", {}, &verifier, brief);
        CHECK_FALSE(second.result.has_value());
        REQUIRE(second.said.size() == 1);
        CHECK(second.said.front() ==
              "validate: delete_file {\"path\":\"b\"} runs as the model revised it, not checked "
              "-- the budget is spent; utility (l3b) had objected -- wrong file");
        CHECK_FALSE(checks.pending("delete_file"));
    }
    SECTION("each tool keeps its own rounds") {
        Script script{.replies = {"OBJECT: no", "AGREE"}};
        const Verifier verifier = script.verifier();
        ToolArgChecks checks;
        CHECK(checks.check("delete_file", "{}", {}, &verifier, brief).result.has_value());
        CHECK(checks.pending("delete_file"));
        CHECK_FALSE(checks.pending("write_file"));
        CHECK_FALSE(checks.check("write_file", "{}", {}, &verifier, brief).result.has_value());
        CHECK(checks.pending("delete_file"));
    }
}

TEST_CASE("the briefs carry the artifact and the criterion, and nothing else",
          "[agentloop][validate][brief]") {
    CHECK(apogee::agentloop::tool_args_brief("Delete the draft notes.", "delete_file",
                                             "Delete a single file permanently.",
                                             R"({ "path": "notes/final.md" })") ==
          "Check a tool call before it runs: delete_file {\"path\":\"notes/final.md\"}\n\n"
          "The request it serves, the user's latest message:\n<<<\nDelete the draft "
          "notes.\n>>>\n\nWhat delete_file does: Delete a single file permanently.\n\n"
          "Object only if an argument is wrong for the request -- the wrong file or target, an "
          "action the request does not ask for, a value the request contradicts. Not to "
          "style.\n\nReply with AGREE or OBJECT as your first word. After OBJECT, say in one or "
          "two sentences what is wrong and what would be right.");

    CHECK(apogee::agentloop::extraction_brief("We shipped it in PR #412.",
                                              R"({"downstream_link": "PR #421"})",
                                              {"intent", "provenance.source"}) ==
          "Check a record extracted from a source text, against that source.\n\n"
          "Object only to a fact the source does not support:\n"
          "1. A required field (intent, provenance.source) that is empty, or says what the "
          "source does not.\n"
          "2. A name, number, count, date or link that differs from the source's.\n"
          "3. A field the source contradicts.\n"
          "The record paraphrases the source: other wording, a shorter or longer phrasing, or a "
          "summary is not an error. If no field breaks these rules, reply AGREE.\n\n"
          "SOURCE:\n<<<\nWe shipped it in PR #412.\n>>>\n\nRECORD:\n<<<\n{\"downstream_link\": "
          "\"PR #421\"}\n>>>\n\nReply with AGREE or OBJECT as your first word. After OBJECT, "
          "say in one or two sentences what is wrong and what would be right. Name the field "
          "that is wrong and what the source says.");

    CHECK(apogee::agentloop::answer_brief("What is 17 x 23?", "381") ==
          "Check an answer against the question it answers.\n\nQUESTION:\n<<<\nWhat is 17 x "
          "23?\n>>>\n\nANSWER:\n<<<\n381\n>>>\n\nObject only if the answer is wrong, "
          "contradicts itself, or does not answer the question -- not to style or length.\n\n"
          "Reply with AGREE or OBJECT as your first word. After OBJECT, say in one or two "
          "sentences what is wrong and what would be right.");

    // A long argument is cut and said to be.
    const std::string brief = apogee::agentloop::tool_args_brief(
        "write it", "write_file", "Write a file.",
        nlohmann::json{{"content", std::string(3000, 'x')}}.dump());
    CHECK_THAT(brief, Catch::Matchers::ContainsSubstring("more characters not shown]"));
}

TEST_CASE("the required fields are the schema's, nested ones dotted",
          "[agentloop][validate][brief]") {
    const nlohmann::json schema = nlohmann::json::parse(R"({
      "type": "object",
      "properties": {
        "intent": {"type": "string"},
        "provenance": {"type": "object", "properties": {"source": {"type": "string"}},
                       "required": ["source"]}
      },
      "required": ["intent", "provenance"]
    })");
    CHECK(apogee::agentloop::required_fields(schema) ==
          std::vector<std::string>{"intent", "provenance", "provenance.source"});
    CHECK(apogee::agentloop::required_fields(nlohmann::json::object()).empty());
}

TEST_CASE("what a surfaced disagreement says, golden", "[agentloop][validate][lines]") {
    Validated disputed;
    disputed.result = Validated::Result::Disputed;
    disputed.original = "381";
    disputed.artifact = "You're right: 17 x 23 is 391.";
    disputed.objection = "17 x 23 is 391, not 381.";
    disputed.by = "utility (l3b)";
    disputed.revisions = 1;
    disputed.model_calls = 1;
    CHECK(apogee::agentloop::answer_lines(disputed) ==
          std::vector<std::string>{
              "check: utility (l3b) objects to the answer -- \"17 x 23 is 391, not 381.\"",
              "check: shown the objection, the model answered -- \"You're right: 17 x 23 is "
              "391.\""});
    CHECK(apogee::agentloop::answer_lines(disputed, "validate").front().starts_with("validate: "));

    Validated passed;
    passed.by = "utility (l3b)";
    CHECK(apogee::agentloop::answer_lines(passed) ==
          std::vector<std::string>{"check: utility (l3b) agrees with the answer"});

    Validated unchecked;
    unchecked.result = Validated::Result::Unchecked;
    unchecked.notes = {"the turn's member-call budget is spent"};
    CHECK(apogee::agentloop::answer_lines(unchecked) ==
          std::vector<std::string>{"check: not checked -- the turn's member-call budget is spent"});

    Validated record = disputed;
    record.original = R"({"downstream_link":"PR #421"})";
    record.artifact = record.original;
    record.objection = "downstream_link is PR #421; the source says PR #412.";
    record.insisted = true;
    CHECK(apogee::agentloop::extraction_lines(record) ==
          std::vector<std::string>{"disputed by utility (l3b): \"downstream_link is PR #421; the "
                                   "source says PR #412.\" -- the clerk returned the same record"});
    record.insisted = false;
    record.artifact = R"({"downstream_link":"PR #413"})";
    CHECK(apogee::agentloop::extraction_lines(record).front().ends_with(
        "-- the record is the clerk's revision"));
    Validated revised = record;
    revised.result = Validated::Result::Revised;
    CHECK(apogee::agentloop::extraction_lines(revised) ==
          std::vector<std::string>{"validated by utility (l3b) after one revision -- it had "
                                   "objected: \"downstream_link is PR #421; the source says PR "
                                   "#412.\""});

    const nlohmann::json json = apogee::agentloop::validation_json(record);
    CHECK(json["result"] == "disputed");
    CHECK(json["verifier"] == "utility (l3b)");
    CHECK(json["objection"] == "downstream_link is PR #421; the source says PR #412.");
    CHECK(json["revisions"] == 1);
    CHECK(json["verifier_calls"] == 1);
    CHECK_FALSE(json.contains("insisted"));

    // Verbatim where short; cut at a word where not.
    CHECK(apogee::agentloop::excerpt("a  b\n c") == "a b c");
    CHECK(apogee::agentloop::excerpt("one two three four", 10) == "one two…");
}

TEST_CASE("the policy: each seam opted into, the verifier a member of the suite",
          "[agentloop][validate][policy]") {
    const apogee::harness::Config config = apogee::harness::parse_config(R"YAML(models:
  default: root
  default_suite: checked
backends:
  root: {type: mock}
  helper: {type: mock}
  scribe: {type: mock}
suites:
  checked:
    members:
      chat: root
      utility: helper
      extraction: scribe
    validate:
      tool_args: on
      answers: always
  plain:
    members:
      chat: root
      utility: helper
  by_scribe:
    members:
      chat: root
      extraction: scribe
    validate:
      verifier: extraction
      extraction: on
  lonely:
    members:
      chat: root
)YAML",
                                                                         "<test>");
    using apogee::agentloop::Seam;
    using apogee::agentloop::seam_on;
    using apogee::agentloop::verifier_role;
    CHECK(seam_on(config, Seam::ToolArgs));
    CHECK_FALSE(seam_on(config, Seam::Extraction));
    CHECK(seam_on(config, Seam::Answer));
    CHECK(verifier_role(config).role == "utility");

    apogee::harness::Config other = config;
    other.models.default_suite = "plain";
    CHECK_FALSE(seam_on(other, Seam::ToolArgs));
    CHECK_FALSE(seam_on(other, Seam::Answer));
    // No block: /check still has the utility member to ask.
    CHECK(verifier_role(other).role == "utility");

    other.models.default_suite = "by_scribe";
    CHECK(seam_on(other, Seam::Extraction));
    CHECK(verifier_role(other).role == "extraction");

    other.models.default_suite = "lonely";
    CHECK(verifier_role(other).role.empty());
    CHECK(verifier_role(other).missing ==
          "suite lonely has no utility member to check with -- name one, or another verifier: "
          "apogee config set-suite lonely --verifier <role>");

    other.models.default_suite = "";
    CHECK_FALSE(seam_on(other, Seam::ToolArgs));
    CHECK_THAT(verifier_role(other).missing, Catch::Matchers::ContainsSubstring("no suite"));
}

TEST_CASE("the verifier is a member call: brief only, narrated, from the turn's own budget",
          "[agentloop][validate][budget]") {
    apogee::harness::Harness harness{apogee::harness::parse_config(R"YAML(models:
  default: root
  default_suite: checked
backends:
  root: {type: mock}
  helper: {type: mock}
suites:
  checked:
    members:
      chat: root
      utility: helper
    consultable: [utility]
    consult_caps:
      per_turn: 2
    validate:
      tool_args: on
)YAML",
                                                                   "<test>")};
    using apogee::backends::MockProvider;
    using apogee::backends::MockTurn;
    std::map<std::string, std::shared_ptr<MockProvider>> mocks;
    for (const char* name : {"root", "helper"}) {
        MockProvider::Options options;
        options.backend_name = name;
        options.turns = {MockTurn{.text = "AGREE"}};
        mocks[name] = std::make_shared<MockProvider>(std::move(options));
        harness.register_provider(name, mocks[name]);
    }
    harness.use_default_router();

    apogee::agentloop::MemberCalls calls{harness};
    std::vector<apogee::agentloop::SideCall> said;
    const apogee::agentloop::MemberCalls::Turn turn = calls.begin_turn(
        [&said](const apogee::agentloop::SideCall& call) { said.push_back(call); }, {});
    const Verifier verifier = apogee::agentloop::bind_verifier(harness, calls, "utility");
    CHECK(verifier.role == "utility");

    // A consult spends one of the two...
    CHECK(calls.consult("utility", "a question").ok());
    // ...the check spends the other, on the utility member, with the brief alone.
    const Check checked =
        run_checks({}, &verifier, [] { return std::string{"Check a tool call before it runs."}; });
    CHECK(checked.outcome == Check::Outcome::Pass);
    CHECK(checked.by == "utility (helper)");
    REQUIRE(mocks["helper"]->requests().size() == 2);
    const apogee::harness::ChatRequest& sent = mocks["helper"]->requests().back();
    REQUIRE(sent.messages.size() == 1);
    CHECK(sent.messages.front().content.plain_text() == "Check a tool call before it runs.");
    CHECK(sent.tools.empty());
    CHECK(sent.transient.side_request);
    REQUIRE(said.size() == 4);
    CHECK(said[2].role == "validate");
    CHECK(said[2].detail == "asking utility (helper): Check a tool call before it runs.");
    CHECK(mocks["root"]->requests().empty());

    // The budget spent, the check degrades to structure, said -- never silent.
    const Check spent = run_checks({}, &verifier, [] { return std::string{"Check again."}; });
    CHECK(spent.outcome == Check::Outcome::Unchecked);
    CHECK(spent.note ==
          "the turn's member-call budget is spent (2 of 2, plays, consults and checks together) "
          "-- checked by structure only");
    CHECK(mocks["helper"]->requests().size() == 2);
    // And a consult finds it spent too.
    CHECK_THAT(calls.consult("utility", "more").refused,
               Catch::Matchers::ContainsSubstring("2 of 2 member calls made"));
}

// ---- the answer seam, through the loop -------------------------------------

namespace {

constexpr std::string_view kAnswerSuites = R"YAML(models:
  default: root
  default_suite: always
backends:
  root: {type: mock}
  helper: {type: mock}
suites:
  always:
    members:
      chat: root
      utility: helper
    validate:
      answers: always
  request:
    members:
      chat: root
      utility: helper
    validate:
      tool_args: on
)YAML";

struct AnswerWorld {
    apogee::harness::Harness harness{apogee::harness::parse_config(kAnswerSuites, "<test>")};
    std::shared_ptr<apogee::backends::MockProvider> root;
    std::shared_ptr<apogee::backends::MockProvider> helper;
    apogee::agentloop::MemberCalls calls{harness};

    AnswerWorld(std::vector<apogee::backends::MockTurn> root_turns,
                std::vector<apogee::backends::MockTurn> helper_turns) {
        apogee::backends::MockProvider::Options r;
        r.backend_name = "root";
        r.turns = std::move(root_turns);
        root = std::make_shared<apogee::backends::MockProvider>(std::move(r));
        apogee::backends::MockProvider::Options h;
        h.backend_name = "helper";
        h.turns = std::move(helper_turns);
        helper = std::make_shared<apogee::backends::MockProvider>(std::move(h));
        harness.register_provider("root", root);
        harness.register_provider("helper", helper);
        harness.use_default_router();
    }
};

struct Heard : apogee::agentloop::Reporter {
    std::vector<std::string> notices;
    std::vector<apogee::agentloop::SideCall> side_calls;
    std::string answer;

    void on_notice(std::string_view text) override {
        notices.emplace_back(text);
    }

    void on_side_call(const apogee::agentloop::SideCall& call) override {
        side_calls.push_back(call);
    }

    void on_answer_token(std::string_view chunk) override {
        answer += chunk;
    }
};

}  // namespace

TEST_CASE("answers always: the verifier once, the model's reply beside it, history untouched",
          "[agentloop][validate][answers]") {
    using apogee::backends::MockTurn;
    using apogee::harness::ChatMessage;
    AnswerWorld world{{MockTurn{.text = "381"}, MockTurn{.text = "You're right: 391."}},
                      {MockTurn{.text = "OBJECT: 17 x 23 is 391, not 381."}}};
    std::vector<ChatMessage> history{ChatMessage::user("What is 17 x 23?")};
    apogee::agentloop::Options options;
    options.model = "root";
    options.member_calls = &world.calls;
    Heard heard;
    const apogee::agentloop::RunResult result =
        apogee::agentloop::run(world.harness, history, options, heard);
    CHECK(result.answer == "381");
    CHECK(heard.answer == "381");
    // Never a transcript mutation: the question and the answer, as given.
    REQUIRE(history.size() == 2);
    CHECK(history.back().content.plain_text() == "381");
    // The verifier, once, on exactly the brief.
    REQUIRE(world.helper->requests().size() == 1);
    REQUIRE(world.helper->requests()[0].messages.size() == 1);
    CHECK(world.helper->requests()[0].messages[0].content.plain_text() ==
          apogee::agentloop::answer_brief("What is 17 x 23?", "381"));
    // The model, shown the objection once, on a side request.
    REQUIRE(world.root->requests().size() == 2);
    const apogee::harness::ChatRequest& asked = world.root->requests()[1];
    CHECK(asked.transient.side_request);
    CHECK(asked.tools.empty());
    CHECK_THAT(asked.messages.back().content.plain_text(),
               Catch::Matchers::ContainsSubstring("objected:\n\n17 x 23 is 391, not 381."));
    // Both positions, said after the answer.
    CHECK(heard.notices ==
          std::vector<std::string>{
              "validate: utility (helper) objects to the answer -- \"17 x 23 is 391, not 381.\"",
              "validate: shown the objection, the model answered -- \"You're right: 391.\""});
    REQUIRE(result.answer_check.has_value());
    CHECK(result.answer_check->result == Validated::Result::Disputed);
    CHECK(result.answer_check->model_calls == 1);
}

TEST_CASE("answers always: an answer agreed with is its line in the thinking block, no more",
          "[agentloop][validate][answers]") {
    using apogee::backends::MockTurn;
    using apogee::harness::ChatMessage;
    AnswerWorld world{{MockTurn{.text = "391"}}, {MockTurn{.text = "AGREE"}}};
    std::vector<ChatMessage> history{ChatMessage::user("What is 17 x 23?")};
    apogee::agentloop::Options options;
    options.model = "root";
    options.member_calls = &world.calls;
    Heard heard;
    const apogee::agentloop::RunResult result =
        apogee::agentloop::run(world.harness, history, options, heard);
    CHECK(heard.notices.empty());
    REQUIRE(heard.side_calls.size() == 2);
    CHECK(heard.side_calls[0].role == "validate");
    CHECK(world.root->requests().size() == 1);
    REQUIRE(result.answer_check.has_value());
    CHECK(result.answer_check->result == Validated::Result::Passed);
}

TEST_CASE("answers are checked only where asked, and never a schema-held one",
          "[agentloop][validate][answers]") {
    using apogee::backends::MockTurn;
    using apogee::harness::ChatMessage;
    AnswerWorld world{{MockTurn{.text = R"({"n": 391})"}}, {MockTurn{.text = "OBJECT: no"}}};
    apogee::agentloop::Options options;
    options.model = "root";
    SECTION("no member calls handed in") {
        std::vector<ChatMessage> history{ChatMessage::user("q")};
        CHECK_FALSE(apogee::agentloop::run(world.harness, history, options).answer_check);
    }
    SECTION("a structured answer: its schema is the check") {
        options.member_calls = &world.calls;
        options.response_schema = R"({"type":"object"})";
        std::vector<ChatMessage> history{ChatMessage::user("q")};
        CHECK_FALSE(apogee::agentloop::run(world.harness, history, options).answer_check);
    }
    SECTION("a side run") {
        options.member_calls = &world.calls;
        options.side_request = true;
        std::vector<ChatMessage> history{ChatMessage::user("q")};
        CHECK_FALSE(apogee::agentloop::run(world.harness, history, options).answer_check);
    }
    SECTION("a suite whose answers are on request") {
        options.member_calls = &world.calls;
        world.harness.set_active_suite("request");
        std::vector<ChatMessage> history{ChatMessage::user("q")};
        CHECK_FALSE(apogee::agentloop::run(world.harness, history, options).answer_check);
    }
    CHECK(world.helper->requests().empty());
}

TEST_CASE("check_answer on request: the verifier once, round three unreachable",
          "[agentloop][validate][answers][check]") {
    using apogee::backends::MockTurn;
    using apogee::harness::ChatMessage;
    // A verifier that would object forever, and a model that would revise
    // forever: one call each, then the two positions.
    AnswerWorld world{{MockTurn{.text = "Still 381."}}, {MockTurn{.text = "OBJECT: wrong"}}};
    world.harness.set_active_suite("request");
    const std::vector<ChatMessage> history{ChatMessage::user("What is 17 x 23?"),
                                           ChatMessage::assistant("381")};
    const apogee::agentloop::MemberCalls::Turn turn = world.calls.begin_turn({}, {});
    const Verifier verifier =
        apogee::agentloop::bind_verifier(world.harness, world.calls, "utility");
    const Validated checked = apogee::agentloop::check_answer(world.harness, "root", history,
                                                              verifier, {}, std::nullopt, {});
    CHECK(checked.result == Validated::Result::Disputed);
    CHECK(checked.model_calls == 1);
    CHECK(checked.revisions == 1);
    CHECK(checked.artifact == "Still 381.");
    CHECK(world.helper->requests().size() == 1);
    CHECK(world.root->requests().size() == 1);
    CHECK(apogee::agentloop::answer_lines(checked) ==
          std::vector<std::string>{"check: utility (helper) objects to the answer -- \"wrong\"",
                                   "check: shown the objection, the model answered -- \"Still "
                                   "381.\""});
    // Nothing to check: said, nobody asked.
    const std::vector<ChatMessage> fresh{ChatMessage::user("hi")};
    CHECK(apogee::agentloop::check_answer(world.harness, "root", fresh, verifier, {}, std::nullopt,
                                          {})
              .result == Validated::Result::Unchecked);
    CHECK(world.helper->requests().size() == 1);
}
