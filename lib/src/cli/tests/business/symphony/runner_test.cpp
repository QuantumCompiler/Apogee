#include "symphony/runner.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "agentloop/member_call.h"
#include "backends/mock.h"
#include "contracts/assets.h"
#include "contracts/config.h"
#include "contracts/errors.h"
#include "harness/harness.h"
#include "support/media_fakes.h"
#include "symphony/definition.h"

/// The stage walk (27q) over scripted members: stage order, output threading
/// and call counts asserted; each recorded request exactly its rendered
/// template -- no history, no system prompt, no tools; a failing stage stops
/// the walk, named; a schema stage held to its schema.
namespace {

using apogee::agentloop::MemberCalls;
using apogee::agentloop::SideCall;
using apogee::backends::MockProvider;
using apogee::backends::MockTurn;
using apogee::harness::ChatRequest;
using apogee::harness::SymphonySpec;
using apogee::symphony::play;
using apogee::symphony::PlayInput;
using apogee::symphony::PlayResult;

constexpr std::string_view kConfig = R"YAML(models:
  default: root
  default_suite: duo
backends:
  root:
    type: mock
  helper:
    type: mock
  scribe:
    type: mock
  paid:
    type: mock
suites:
  duo:
    members:
      chat: root
      utility: helper
      extraction: scribe
  billed:
    members:
      chat: root
      utility: paid
)YAML";

/// A harness over `kConfig`, each backend a mock answering from its script.
struct Members {
    apogee::harness::Harness harness{apogee::harness::parse_config(kConfig, "<test>")};
    std::map<std::string, std::shared_ptr<MockProvider>> mocks;

    explicit Members(std::map<std::string, std::vector<std::string>> answers = {}) {
        for (const char* name : {"root", "helper", "scribe", "paid"}) {
            MockProvider::Options options;
            options.backend_name = name;
            const std::vector<std::string>& texts = answers[name];
            for (const std::string& text : texts) {
                options.turns.push_back(MockTurn{.text = text});
            }
            if (options.turns.empty()) {
                options.turns.push_back(MockTurn{.text = std::string{name} + " says hi"});
            }
            options.metered = std::string_view{name} == "paid";
            mocks[name] = std::make_shared<MockProvider>(std::move(options));
            harness.register_provider(name, mocks[name]);
        }
        harness.use_default_router();
    }

    [[nodiscard]] std::size_t sent() const {
        std::size_t total = 0;
        for (const auto& [name, mock] : mocks) {
            total += mock->requests().size();
        }
        return total;
    }
};

SymphonySpec parse(std::string_view text) {
    return apogee::harness::parse_symphony_spec(text, "<test>", "test");
}

constexpr std::string_view kDuo = R"YAML(name: duo
stages:
  - name: summarize
    role: utility
    answer_tokens: 300
    prompt: "Summarize: {{input}}"
  - name: verify
    role: chat
    prompt: "Passage: {{input}}\nSummary: {{summarize}}\nFix it."
)YAML";

/// Plays `spec` in a turn of its own, the narration collected.
PlayResult played(const apogee::harness::Harness& harness, const SymphonySpec& spec,
                  const PlayInput& input, std::vector<SideCall>* said = nullptr,
                  const apogee::symphony::PlayOptions& options = {}) {
    MemberCalls calls{harness};
    const MemberCalls::Turn turn = calls.begin_turn(
        [said](const SideCall& call) {
            if (said != nullptr) {
                said->push_back(call);
            }
        },
        {});
    return play(spec, input, calls, options);
}

/// A request's one message, as text.
std::string brief_of(const ChatRequest& request) {
    REQUIRE(request.messages.size() == 1);
    return request.messages.front().content.plain_text();
}

}  // namespace

TEST_CASE("stages play in order, each answer threaded into the next stage's brief",
          "[symphony][runner]") {
    Members members{{{"helper", {"A cat sat."}}, {"root", {"A cat sat on a mat."}}}};
    std::vector<SideCall> said;
    const PlayResult result =
        played(members.harness, parse(kDuo), PlayInput{.text = "The cat sat on the mat."}, &said);
    REQUIRE(result.ok());
    CHECK(result.output == "A cat sat on a mat.");
    REQUIRE(result.stages.size() == 2);
    CHECK(result.stages[0].name == "summarize");
    CHECK(result.stages[0].role == "utility");
    CHECK(result.stages[0].backend == "helper");
    CHECK(result.stages[0].answer == "A cat sat.");
    CHECK(result.stages[1].backend == "root");

    // The wire: each member's one request is exactly its rendered template,
    // stage two's carrying stage one's answer -- and nothing else.
    REQUIRE(members.mocks["helper"]->requests().size() == 1);
    REQUIRE(members.mocks["root"]->requests().size() == 1);
    const ChatRequest& first = members.mocks["helper"]->requests().front();
    const ChatRequest& second = members.mocks["root"]->requests().front();
    CHECK(brief_of(first) == "Summarize: The cat sat on the mat.");
    CHECK(brief_of(second) == "Passage: The cat sat on the mat.\nSummary: A cat sat.\nFix it.");
    for (const ChatRequest* request : {&first, &second}) {
        CHECK(request->messages.front().role == apogee::harness::Role::User);
        CHECK(request->tools.empty());
        CHECK(request->thinking.off());
        CHECK(request->transient.side_request);
        CHECK(request->transient.response_schema.empty());
    }
    CHECK(first.max_tokens == 300);
    CHECK(second.max_tokens == apogee::symphony::kStageAnswerTokens);
    CHECK(members.sent() == 2);

    // Each stage said as 26n's side calls are: started, then done.
    REQUIRE(said.size() == 4);
    CHECK(said[0].role == "stage 1/2 summarize");
    CHECK(said[0].detail == "asking utility (helper): Summarize: The cat sat on the mat.");
    CHECK_FALSE(said[0].done);
    CHECK(said[1].done);
    CHECK(said[2].role == "stage 2/2 verify");
    CHECK(said[3].done);
}

TEST_CASE("the same symphony plays on whichever suite is active", "[symphony][runner]") {
    Members members;
    members.harness.set_active_suite("billed");
    const PlayResult billed = played(members.harness, parse(kDuo), PlayInput{.text = "x"});
    // The user's initiative spends: a play reaches a billed member...
    REQUIRE(billed.ok());
    CHECK(billed.stages[0].backend == "paid");
    // ...unless the caller plays on a model's initiative (27t's business).
    Members again;
    again.harness.set_active_suite("billed");
    const PlayResult refused =
        played(again.harness, parse(kDuo), PlayInput{.text = "x"}, nullptr, {.local_only = true});
    CHECK_FALSE(refused.ok());
    CHECK_THAT(refused.failure,
               Catch::Matchers::StartsWith("stage 1/2 summarize (utility): 'paid' (utility) is "
                                           "billed per call"));
    CHECK(again.sent() == 0);
}

TEST_CASE("a failing stage stops the walk with the stage named, and no output",
          "[symphony][runner]") {
    SECTION("a member that is not there") {
        Members members;
        const SymphonySpec spec = parse(R"YAML(stages:
  - {name: one, role: utility, prompt: "{{input}}"}
  - {name: two, role: vision, prompt: "{{one}}"}
  - {name: three, role: chat, prompt: "{{two}}"}
)YAML");
        apogee::harness::Harness bare{apogee::harness::parse_config(R"YAML(models:
  default: root
  default_utility: helper
  default_vision: ghost
backends:
  root:
    type: mock
  helper:
    type: mock
  ghost:
    type: mock
)YAML",
                                                                    "<test>")};
        bare.register_provider("root", members.mocks["root"]);
        bare.register_provider("helper", members.mocks["helper"]);
        bare.use_default_router();
        const PlayResult result = played(bare, spec, PlayInput{.text = "x"});
        CHECK_FALSE(result.ok());
        CHECK(result.output.empty());
        CHECK(result.stages.size() == 1);
        CHECK(result.failed_stage == 2);
        CHECK_FALSE(result.member_failure);
        CHECK_THAT(result.failure, Catch::Matchers::StartsWith("stage 2/3 two (vision): "));
    }
    SECTION("a member that fails once asked") {
        Members members{{{"root", {""}}}};
        const PlayResult result = played(members.harness, parse(kDuo), PlayInput{.text = "x"});
        CHECK_FALSE(result.ok());
        CHECK(result.member_failure);
        CHECK(result.failed_stage == 2);
        CHECK(result.output.empty());
        CHECK_THAT(result.failure,
                   Catch::Matchers::Equals("stage 2/2 verify (chat): 'root' gave no answer"));
    }
    SECTION("a brief over its stage's cap, refused unsent") {
        Members members;
        SymphonySpec spec = parse(kDuo);
        spec.stages[0].brief_tokens = 2;
        const PlayResult result =
            played(members.harness, spec, PlayInput{.text = "a long enough passage"});
        CHECK(result.failed_stage == 1);
        CHECK_THAT(result.failure, Catch::Matchers::ContainsSubstring("a brief is at most 2"));
        CHECK(members.sent() == 0);
    }
}

TEST_CASE("a play refuses what its definition cannot take, before any call", "[symphony][runner]") {
    Members members;
    CHECK_THAT(played(members.harness, parse(kDuo), PlayInput{}).failure,
               Catch::Matchers::Equals("'duo' reads its input, and none was given"));
    PlayInput pictured{.text = "x"};
    pictured.image = {apogee::harness::ContentPart::from_image_url("data:image/png;base64,AA")};
    CHECK_THAT(played(members.harness, parse(kDuo), pictured).failure,
               Catch::Matchers::ContainsSubstring("takes no image"));
    SymphonySpec broken = parse(kDuo);
    broken.stages[1].prompt = "{{nope}}";
    CHECK_THAT(played(members.harness, broken, PlayInput{.text = "x"}).failure,
               Catch::Matchers::StartsWith("'duo' cannot be played: stage 2 (verify): {{nope}}"));
    CHECK(members.sent() == 0);
}

TEST_CASE("a schema stage's answer is held to its schema, and checked", "[symphony][runner]") {
    const SymphonySpec spec = parse(R"YAML(stages:
  - name: extract
    role: extraction
    prompt: "Facts of: {{input}}"
    schema: '{"type": "object", "properties": {"topic": {"type": "string"}}, "required": ["topic"]}'
  - name: tell
    role: chat
    prompt: "Say {{extract}}"
)YAML");
    SECTION("an answer that holds is threaded on as the member wrote it") {
        Members members{{{"scribe", {R"({"topic": "cats", "z": 1})"}}}};
        const PlayResult result = played(members.harness, spec, PlayInput{.text = "x"});
        REQUIRE(result.ok());
        const ChatRequest& sent = members.mocks["scribe"]->requests().front();
        CHECK(sent.transient.response_schema == spec.stages[0].schema);
        // The schema rides beside the brief, never in it.
        CHECK(brief_of(sent) == "Facts of: x");
        CHECK(brief_of(members.mocks["root"]->requests().front()) ==
              R"(Say {"topic": "cats", "z": 1})");
    }
    SECTION("an answer in a fence is threaded on as its JSON") {
        Members members{{{"scribe", {"```json\n{\"topic\": \"cats\"}\n```"}}}};
        const PlayResult result = played(members.harness, spec, PlayInput{.text = "x"});
        REQUIRE(result.ok());
        CHECK(result.stages[0].answer == R"({"topic":"cats"})");
    }
    SECTION("an answer outside the schema stops the walk") {
        Members members{{{"scribe", {R"({"title": "cats"})"}}}};
        const PlayResult result = played(members.harness, spec, PlayInput{.text = "x"});
        CHECK_FALSE(result.ok());
        CHECK(result.member_failure);
        CHECK_THAT(result.failure,
                   Catch::Matchers::StartsWith("stage 1/2 extract (extraction): 'scribe' "
                                               "answered outside the stage's schema"));
        CHECK(members.mocks["root"]->requests().empty());
    }
    SECTION("an answer with no JSON at all stops it too") {
        Members members{{{"scribe", {"cats, mostly"}}}};
        CHECK_THAT(played(members.harness, spec, PlayInput{.text = "x"}).failure,
                   Catch::Matchers::ContainsSubstring("answered with no JSON"));
    }
}

TEST_CASE("a stage marked for the image is sent it, ahead of its brief; the others are not",
          "[symphony][runner]") {
    apogee::harness::Harness harness{apogee::harness::parse_config(R"YAML(models:
  default: root
  default_vision: eye
backends:
  root:
    type: mock
  eye:
    type: mock
)YAML",
                                                                   "<test>")};
    auto eye = std::make_shared<apogee::testing::MediaProvider>();
    eye->sees = true;
    eye->reply = [](const ChatRequest&) { return std::string{"two red apples"}; };
    MockProvider::Options options;
    options.backend_name = "root";
    options.turns = {MockTurn{.text = "Two."}};
    auto root = std::make_shared<MockProvider>(std::move(options));
    harness.register_provider("root", root);
    harness.register_provider("eye", eye);
    harness.use_default_router();

    const SymphonySpec spec = apogee::harness::parse_symphony_spec(
        apogee::harness::find_bundled_symphony("describe-answer")->text, "starter",
        "describe-answer");
    PlayInput input{.text = "How many apples?"};
    input.image = {apogee::harness::ContentPart::from_image_url("data:image/png;base64,AAAA")};
    const PlayResult result = played(harness, spec, input);
    REQUIRE(result.ok());
    CHECK(result.output == "Two.");
    const std::vector<ChatRequest> seen = eye->requests();
    REQUIRE(seen.size() == 1);
    const std::vector<apogee::harness::ContentPart>& parts =
        seen.front().messages.front().content.parts();
    REQUIRE(parts.size() == 2);
    CHECK(parts[0].image_url == "data:image/png;base64,AAAA");
    CHECK_THAT(parts[1].text, Catch::Matchers::EndsWith("bears on this question: How many "
                                                        "apples?\n"));
    // The second stage reads the description, not the picture.
    REQUIRE(root->requests().size() == 1);
    CHECK_FALSE(root->requests().front().messages.front().content.is_rich());
    CHECK_THAT(brief_of(root->requests().front()),
               Catch::Matchers::ContainsSubstring("Description:\ntwo red apples\n"));

    // An image symphony played with none is refused before anything is sent.
    CHECK_THAT(played(harness, spec, PlayInput{.text = "x"}).failure,
               Catch::Matchers::ContainsSubstring("takes an image with its input"));
}

TEST_CASE("a play is one member call per stage, inside the turn's budget", "[symphony][runner]") {
    Members members;
    const SymphonySpec spec = parse(R"YAML(stages:
  - {name: a, role: utility, prompt: "{{input}}"}
  - {name: b, role: utility, prompt: "{{a}}"}
  - {name: c, role: utility, prompt: "{{b}}"}
  - {name: d, role: utility, prompt: "{{c}}"}
  - {name: e, role: utility, prompt: "{{d}}"}
)YAML");
    // Five stages run past the consult default of four: a play's own budget
    // is its stage count.
    REQUIRE(played(members.harness, spec, PlayInput{.text = "x"}).ok());
    CHECK(members.mocks["helper"]->requests().size() == 5);
    // A caller with a smaller budget of its own stops the walk at it.
    Members bounded;
    const PlayResult stopped =
        played(bounded.harness, spec, PlayInput{.text = "x"}, nullptr, {.per_turn = 3});
    CHECK(stopped.failed_stage == 4);
    CHECK_THAT(stopped.failure, Catch::Matchers::ContainsSubstring("3 of 3 member calls made"));
    CHECK(bounded.mocks["helper"]->requests().size() == 3);
}

TEST_CASE("a cancelled play throws through, mid-walk", "[symphony][runner]") {
    Members members;
    const apogee::harness::CancellationToken token = apogee::harness::CancellationToken::create();
    MemberCalls calls{members.harness};
    const MemberCalls::Turn turn = calls.begin_turn(
        [token](const SideCall& call) {
            if (call.done) {
                token.cancel();
            }
        },
        token);
    CHECK_THROWS_AS(play(parse(kDuo), PlayInput{.text = "x"}, calls),
                    apogee::harness::CancelledError);
    CHECK(members.mocks["root"]->requests().empty());
}
