#include "symphony/runner.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
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
/// the walk, named; a schema stage held to its schema. Since 27r a chain:
/// two shipped starters played in one walk, the first's output on the
/// second's wire; a played symphony's requests identical to the same
/// symphony played alone; nesting to the cap and past it; a late-bound loop
/// refused before any call; one budget across the walk; positions named.
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

/// The shipped starters, as the catalog finds them with no config and no
/// files, and `texts` as spec files beside them (27r).
apogee::symphony::Catalog catalog_of(std::initializer_list<std::string_view> texts,
                                     std::int64_t depth = apogee::harness::kSymphonyDepth) {
    apogee::symphony::Catalog out =
        apogee::symphony::catalog(apogee::harness::Config{}, "/nonexistent/apogee/symphonies");
    for (const std::string_view text : texts) {
        apogee::symphony::Definition definition;
        definition.spec = apogee::harness::parse_symphony_spec(text, "<test>");
        definition.source = apogee::symphony::Source::File;
        out.definitions.push_back(std::move(definition));
    }
    out.max_depth = depth;
    return out;
}

/// Plays `spec` against `catalog` in a turn of its own, the narration
/// collected.
PlayResult chained(const apogee::harness::Harness& harness, const SymphonySpec& spec,
                   const apogee::symphony::Catalog& catalog, const PlayInput& input,
                   std::vector<SideCall>* said = nullptr,
                   const apogee::symphony::PlayOptions& options = {}) {
    MemberCalls calls{harness};
    const MemberCalls::Turn turn = calls.begin_turn(
        [said](const SideCall& call) {
            if (said != nullptr) {
                said->push_back(call);
            }
        },
        {});
    return play(spec, catalog, input, calls, options);
}

/// The roles the narration said each call under, starts only.
std::vector<std::string> started(const std::vector<SideCall>& said) {
    std::vector<std::string> out;
    for (const SideCall& call : said) {
        if (!call.done) {
            out.push_back(call.role);
        }
    }
    return out;
}

/// The extraction starter's answer, held to its schema.
constexpr std::string_view kFacts =
    R"({"topic": "a cat", "people": [], "places": ["the mat"], "dates": ["4 May"], )"
    R"("facts": ["a cat sat on the mat"]})";

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

// ---- Chains (27r) -------------------------------------------------------------

TEST_CASE("a chain of two shipped starters: the first's output is the second's input, on the wire",
          "[symphony][runner][chain]") {
    Members members{{{"helper", {"A cat sat."}},
                     {"root", {"A cat sat on the mat on 4 May."}},
                     {"scribe", {std::string{kFacts}}}}};
    const SymphonySpec digest = parse(R"YAML(name: digest
stages:
  - name: summary
    play: summarize-verify
  - name: facts
    play: extract-facts
)YAML");
    const apogee::symphony::Catalog catalog = catalog_of({});
    std::vector<SideCall> said;
    const PlayResult result = chained(members.harness, digest, catalog,
                                      PlayInput{.text = "The cat sat on the mat on 4 May."}, &said);
    REQUIRE(result.ok());
    CHECK(result.output == kFacts);
    CHECK(result.calls == 3);
    CHECK(members.sent() == 3);

    // The wire: extract-facts' one stage was sent exactly its template
    // rendered with summarize-verify's output as its {{input}} -- nothing of
    // the chain's own input, nothing else.
    const SymphonySpec facts = apogee::harness::parse_symphony_spec(
        apogee::harness::find_bundled_symphony("extract-facts")->text, "starter", "extract-facts");
    REQUIRE(members.mocks["scribe"]->requests().size() == 1);
    const ChatRequest& extraction = members.mocks["scribe"]->requests().front();
    CHECK(brief_of(extraction) ==
          apogee::symphony::render_template(facts.stages[0].prompt,
                                            {{"input", "A cat sat on the mat on 4 May."}}));
    CHECK(extraction.transient.response_schema == facts.stages[0].schema);
    // And summarize-verify's stages were sent the chain's input.
    CHECK_THAT(brief_of(members.mocks["helper"]->requests().front()),
               Catch::Matchers::EndsWith("Passage:\nThe cat sat on the mat on 4 May.\n"));
    CHECK_THAT(brief_of(members.mocks["root"]->requests().front()),
               Catch::Matchers::ContainsSubstring("Summary:\nA cat sat.\n"));

    // Every line names its position: the chain, the symphony, the stage.
    CHECK(started(said) == std::vector<std::string>{
                               "digest → summarize-verify, stage 1/2 summarize",
                               "digest → summarize-verify, stage 2/2 verify",
                               "digest → extract-facts, stage 1/1 extract",
                           });
    // The result nests each played symphony's stages in the stage that played it.
    REQUIRE(result.stages.size() == 2);
    CHECK(result.stages[0].play == "summarize-verify");
    CHECK(result.stages[0].answer == "A cat sat on the mat on 4 May.");
    REQUIRE(result.stages[0].stages.size() == 2);
    CHECK(result.stages[0].stages[0].backend == "helper");
    CHECK(result.stages[1].play == "extract-facts");
    CHECK(result.stages[1].stages.front().role == "extraction");
}

TEST_CASE("a played symphony's stages are indistinguishable on the wire from the same played alone",
          "[symphony][runner][chain]") {
    const auto requests = [](bool wrapped) {
        Members members{{{"helper", {"A cat sat."}}, {"root", {"A cat sat on a mat."}}}};
        const apogee::symphony::Catalog catalog = catalog_of({});
        const SymphonySpec alone = catalog.find("summarize-verify")->spec;
        const SymphonySpec wrapper = parse(
            "name: wrapper\nstages:\n  - {name: s, play: "
            "summarize-verify}\n");
        const PlayResult result = chained(members.harness, wrapped ? wrapper : alone, catalog,
                                          PlayInput{.text = "The passage."});
        REQUIRE(result.ok());
        CHECK(result.output == "A cat sat on a mat.");
        std::vector<ChatRequest> out = members.mocks["helper"]->requests();
        for (const ChatRequest& request : members.mocks["root"]->requests()) {
            out.push_back(request);
        }
        return out;
    };
    const std::vector<ChatRequest> inline_requests = requests(false);
    const std::vector<ChatRequest> played_requests = requests(true);
    REQUIRE(inline_requests.size() == 2);
    REQUIRE(played_requests.size() == inline_requests.size());
    for (std::size_t index = 0; index < inline_requests.size(); ++index) {
        const ChatRequest& a = inline_requests[index];
        const ChatRequest& b = played_requests[index];
        CHECK(brief_of(a) == brief_of(b));
        CHECK(a.model == b.model);
        CHECK(a.max_tokens == b.max_tokens);
        CHECK(a.tools.size() == b.tools.size());
        CHECK(a.thinking.off() == b.thinking.off());
        CHECK(a.transient.side_request == b.transient.side_request);
        CHECK(a.transient.response_schema == b.transient.response_schema);
        CHECK(a.messages.front().role == b.messages.front().role);
    }
}

TEST_CASE("role and play stages mix: the stage after a play reads its output verbatim",
          "[symphony][runner][chain]") {
    Members members{{{"helper", {"PREPPED"}}, {"root", {"INNER {braces} {{kept}}", "FINAL"}}}};
    const apogee::symphony::Catalog catalog = catalog_of({R"YAML(name: inner
stages:
  - name: think
    role: chat
    prompt: "Think about: {{input}}"
)YAML"});
    const SymphonySpec outer = parse(R"YAML(name: outer
stages:
  - name: prep
    role: utility
    prompt: "Prep {{input}}"
  - name: middle
    play: inner
    input: "<{{prep}}|{{input}}>"
  - name: final
    role: chat
    prompt: "Final: {{middle}} / {{prep}}"
)YAML");
    std::vector<SideCall> said;
    const PlayResult result =
        chained(members.harness, outer, catalog, PlayInput{.text = "X"}, &said);
    REQUIRE(result.ok());
    CHECK(result.output == "FINAL");
    const std::vector<ChatRequest> root = members.mocks["root"]->requests();
    REQUIRE(root.size() == 2);
    // The play stage's input template rendered from the outer values...
    CHECK(brief_of(root[0]) == "Think about: <PREPPED|X>");
    // ...and the inner output threaded on verbatim -- braces and all.
    CHECK(brief_of(root[1]) == "Final: INNER {braces} {{kept}} / PREPPED");
    CHECK(started(said) == std::vector<std::string>{"outer, stage 1/3 prep",
                                                    "outer → inner, stage 1/1 think",
                                                    "outer, stage 3/3 final"});
}

TEST_CASE("four deep plays; five deep is refused before any call, its path named",
          "[symphony][runner][chain]") {
    const apogee::symphony::Catalog catalog = catalog_of({
        "name: l5\nstages:\n  - {name: leaf, role: utility, prompt: 'Leaf {{input}}'}\n",
        "name: l4\nstages:\n  - {name: down, play: l5}\n",
        "name: l3\nstages:\n  - {name: down, play: l4}\n",
        "name: l2\nstages:\n  - {name: down, play: l3}\n",
    });
    Members four{{{"helper", {"LEAF"}}}};
    std::vector<SideCall> said;
    const PlayResult played =
        chained(four.harness, catalog.find("l2")->spec, catalog, PlayInput{.text = "go"}, &said);
    REQUIRE(played.ok());
    CHECK(played.output == "LEAF");
    CHECK(brief_of(four.mocks["helper"]->requests().front()) == "Leaf go");
    CHECK(started(said) == std::vector<std::string>{"l2 → l3 → l4 → l5, stage 1/1 leaf"});

    const Members five;
    const SymphonySpec l1 = parse("name: l1\nstages:\n  - {name: down, play: l2}\n");
    const PlayResult refused = chained(five.harness, l1, catalog, PlayInput{.text = "go"});
    CHECK_FALSE(refused.ok());
    CHECK(refused.failure ==
          "'l1' cannot be played: l1 → l2 → l3 → l4, stage 1 (down): plays 'l5', which nests 5 "
          "deep -- l1 → l2 → l3 → l4 → l5, and the cap is 4 (symphony_caps.depth)");
    CHECK(five.sent() == 0);
}

TEST_CASE("a spec file that loops through a late-bound name is refused at play start, nothing sent",
          "[symphony][runner][chain]") {
    // Each file parses on its own -- neither plays its own name -- and the
    // loop exists only between them, found when the walk can see both.
    const apogee::symphony::Catalog catalog = catalog_of({
        "name: ping\nstages:\n  - {name: a, role: utility, prompt: '{{input}}'}\n"
        "  - {name: b, play: pong}\n",
        "name: pong\nstages:\n  - {name: back, play: ping}\n",
    });
    const Members members;
    const PlayResult result =
        chained(members.harness, catalog.find("ping")->spec, catalog, PlayInput{.text = "x"});
    CHECK_FALSE(result.ok());
    CHECK_THAT(result.failure, Catch::Matchers::ContainsSubstring("a loop, ping → pong → ping"));
    CHECK(members.sent() == 0);
    // A name nothing defines is refused the same way, before anything runs.
    const PlayResult ghost =
        chained(members.harness, parse("name: g\nstages:\n  - {name: a, play: nowhere}\n"), catalog,
                PlayInput{.text = "x"});
    CHECK(ghost.failure ==
          "'g' cannot be played: stage 1 (a): plays 'nowhere', and no symphony is named 'nowhere'");
    CHECK(members.sent() == 0);
}

TEST_CASE("one budget for the whole walk: a chain stops where its aggregate crosses the cap",
          "[symphony][runner][chain]") {
    const apogee::symphony::Catalog catalog = catalog_of({R"YAML(name: pair
stages:
  - {name: first, role: utility, prompt: "One {{input}}"}
  - {name: second, role: utility, prompt: "Two {{first}}"}
)YAML"});
    const SymphonySpec twice = parse(R"YAML(name: twice
stages:
  - {name: a, play: pair}
  - {name: b, play: pair}
)YAML");
    // The inner symphony passes the cap on its own...
    const Members alone;
    const PlayResult inner = chained(alone.harness, catalog.find("pair")->spec, catalog,
                                     PlayInput{.text = "x"}, nullptr, {.per_turn = 3});
    REQUIRE(inner.ok());
    CHECK(inner.calls == 2);
    // ...and played twice, the walk's aggregate crosses it at the fourth call.
    Members members{{{"helper", {"h"}}}};
    const PlayResult result =
        chained(members.harness, twice, catalog, PlayInput{.text = "x"}, nullptr, {.per_turn = 3});
    CHECK_FALSE(result.ok());
    CHECK(result.budget_spent);
    CHECK_FALSE(result.member_failure);
    CHECK(result.output.empty());
    CHECK(result.calls == 3);
    CHECK(members.mocks["helper"]->requests().size() == 3);
    CHECK(result.failed_stage == 2);
    CHECK(result.failure ==
          "twice → pair, stage 2/2 second (utility): the play's budget is spent -- 3 of 3 member "
          "calls made, 3 answer tokens; the play stops here, with no answer");
    // With no cap of its own a play's budget is its walk's count: four calls.
    Members free{{{"helper", {"h"}}}};
    REQUIRE(chained(free.harness, twice, catalog, PlayInput{.text = "x"}).ok());
    CHECK(free.mocks["helper"]->requests().size() == 4);
}

TEST_CASE("the answer tokens aggregate across the walk too: the play stops once they pass the cap",
          "[symphony][runner][chain]") {
    const apogee::symphony::Catalog catalog = catalog_of({R"YAML(name: pair
stages:
  - {name: first, role: utility, prompt: "One {{input}}"}
  - {name: second, role: utility, prompt: "Two {{first}}"}
)YAML"});
    const SymphonySpec twice = parse(R"YAML(name: twice
stages:
  - {name: a, play: pair}
  - {name: b, play: pair}
)YAML");
    // Each answer said to cost 40 tokens: one pair is 80, under a cap of 100;
    // the chain's third answer takes it to 120.
    apogee::harness::Harness harness{apogee::harness::parse_config(kConfig, "<test>")};
    MockProvider::Options options;
    options.backend_name = "helper";
    MockTurn turn{.text = "h"};
    turn.usage.completion_tokens = 40;
    options.turns = {turn};
    auto helper = std::make_shared<MockProvider>(std::move(options));
    harness.register_provider("helper", helper);
    harness.use_default_router();

    const PlayResult inner = chained(harness, catalog.find("pair")->spec, catalog,
                                     PlayInput{.text = "x"}, nullptr, {.answer_tokens = 100});
    REQUIRE(inner.ok());
    CHECK(inner.tokens == 80);
    const PlayResult result =
        chained(harness, twice, catalog, PlayInput{.text = "x"}, nullptr, {.answer_tokens = 100});
    CHECK_FALSE(result.ok());
    CHECK(result.budget_spent);
    CHECK(result.output.empty());
    CHECK(result.tokens == 120);
    CHECK(result.failure ==
          "twice → pair, stage 1/2 first (utility): the play's budget is spent -- 120 of 100 "
          "answer tokens used, in 3 member calls; the play stops here, with no answer");
    // The two plays' calls together: 2 + 3, no fourth sent.
    CHECK(helper->requests().size() == 5);

    // A budget spent to the token is spent: the next call is never made.
    const PlayResult exact =
        chained(harness, twice, catalog, PlayInput{.text = "x"}, nullptr, {.answer_tokens = 80});
    CHECK(exact.budget_spent);
    CHECK(exact.calls == 2);
    CHECK(exact.failure ==
          "twice → pair, stage 1/2 first (utility): the play's budget is spent -- 80 of 80 "
          "answer tokens used, in 2 member calls; the play stops here, with no answer");
    CHECK(helper->requests().size() == 7);
}

TEST_CASE("a play stage whose input renders blank stops before the symphony it plays is asked",
          "[symphony][runner][chain]") {
    // Only a definition built in code can get here -- the parser refuses a
    // blank `input:` -- and the walk says so rather than playing a hole.
    const apogee::symphony::Catalog catalog = catalog_of({});
    SymphonySpec spec = parse(R"YAML(name: hollow
stages:
  - {name: first, role: utility, prompt: "Read {{input}}"}
  - {name: second, play: summarize-verify}
)YAML");
    spec.stages[1].input = "   ";
    const Members members;
    const PlayResult result = chained(members.harness, spec, catalog, PlayInput{.text = "x"});
    CHECK_FALSE(result.ok());
    CHECK(result.failure ==
          "hollow, stage 2/2 second (plays summarize-verify): "
          "'summarize-verify' reads its input, and the stage gave it nothing");
    CHECK(members.sent() == 1);
}

TEST_CASE("a failure deep in a chain names its position, and returns no answer",
          "[symphony][runner][chain]") {
    const apogee::symphony::Catalog catalog = catalog_of({R"YAML(name: inner
stages:
  - {name: draft, role: utility, prompt: "Draft {{input}}"}
  - {name: verify, role: chat, prompt: "Verify {{draft}}"}
  - {name: polish, role: utility, prompt: "Polish {{verify}}"}
)YAML"});
    const SymphonySpec outer = parse(R"YAML(name: outer
stages:
  - {name: first, role: utility, prompt: "Begin {{input}}"}
  - {name: then, role: utility, prompt: "Then {{first}}"}
  - {name: third, play: inner}
)YAML");
    // The chat member gives nothing: inner's stage 2 fails.
    Members members{{{"helper", {"fine"}}, {"root", {""}}}};
    std::vector<SideCall> said;
    const PlayResult result =
        chained(members.harness, outer, catalog, PlayInput{.text = "x"}, &said);
    CHECK_FALSE(result.ok());
    CHECK(result.member_failure);
    CHECK(result.output.empty());
    // The root's stage the walk stopped in, not the inner one's.
    CHECK(result.failed_stage == 3);
    CHECK(result.failure == "outer → inner, stage 2/3 verify (chat): 'root' gave no answer");
    CHECK(started(said) == std::vector<std::string>{"outer, stage 1/3 first",
                                                    "outer, stage 2/3 then",
                                                    "outer → inner, stage 1/3 draft",
                                                    "outer → inner, stage 2/3 verify"});
    // The stages that answered are kept; inner's third was never asked.
    CHECK(members.mocks["helper"]->requests().size() == 3);
    REQUIRE(result.stages.size() == 2);
    CHECK(result.stages.back().name == "then");
}

TEST_CASE("an image passes through a play stage marked for it, to the played symphony's stage",
          "[symphony][runner][chain]") {
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

    const apogee::symphony::Catalog catalog = catalog_of({});
    const SymphonySpec look = parse(R"YAML(name: look
input:
  image: true
stages:
  - name: seen
    play: describe-answer
    image: true
)YAML");
    PlayInput input{.text = "How many apples?"};
    input.image = {apogee::harness::ContentPart::from_image_url("data:image/png;base64,AAAA")};
    const PlayResult result = chained(harness, look, catalog, input);
    REQUIRE(result.ok());
    CHECK(result.output == "Two.");
    REQUIRE(eye->requests().size() == 1);
    CHECK(eye->requests().front().messages.front().content.parts().front().image_url ==
          "data:image/png;base64,AAAA");
    // A play stage that does not pass the image to a symphony that needs one
    // is refused before anything is sent.
    const SymphonySpec blind = parse(R"YAML(name: blind
input:
  image: true
stages:
  - {name: seen, play: describe-answer}
  - {name: other, role: vision, image: true, prompt: "{{input}}"}
)YAML");
    CHECK_THAT(chained(harness, blind, catalog, input).failure,
               Catch::Matchers::ContainsSubstring(
                   "stage 1 (seen): plays 'describe-answer', which takes an image, and the stage "
                   "does not pass it on"));
    CHECK(eye->requests().size() == 1);
}
