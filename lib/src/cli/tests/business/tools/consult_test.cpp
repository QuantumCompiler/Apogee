#include "tools/consult.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "agent/tool.h"
#include "agentloop/loop.h"
#include "agentloop/member_call.h"
#include "agentloop/reporter.h"
#include "backends/mock.h"
#include "contracts/config.h"
#include "harness/harness.h"

/// The consult tool (27f): offered exactly when the session's suite
/// designates members, naming them; one call runs the member on exactly the
/// brief and comes back as an ordinary tool result; the turn's budget refuses
/// in words, as a result, never an error.
namespace {

using apogee::agentloop::MemberCalls;
using apogee::agentloop::SideCall;
using apogee::backends::MockProvider;
using apogee::backends::MockTurn;
using apogee::harness::ChatMessage;
using apogee::harness::ToolCall;

constexpr std::string_view kConfig = R"YAML(models:
  default: root
  default_suite: research
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
  research:
    members:
      chat: root
      utility: helper
      extraction: scribe
    consultable: [utility, extraction]
  plain:
    members:
      chat: root
      utility: helper
  billed:
    members:
      chat: root
      utility: paid
    consultable: [utility]
)YAML";

/// A suite of mocks: the root answers `root_turns`, each helper `answer`.
struct World {
    apogee::harness::Harness harness{apogee::harness::parse_config(kConfig, "<test>")};
    std::map<std::string, std::shared_ptr<MockProvider>> mocks;
    std::shared_ptr<MemberCalls> calls = std::make_shared<MemberCalls>(harness);

    explicit World(const std::vector<MockTurn>& root_turns = {MockTurn{.text = "done"}}) {
        for (const char* name : {"root", "helper", "scribe", "paid"}) {
            MockProvider::Options options;
            options.backend_name = name;
            options.turns = std::string_view{name} == "root"
                                ? root_turns
                                : std::vector<MockTurn>{MockTurn{.text = "MARMALADE-7"}};
            options.metered = std::string_view{name} == "paid";
            mocks[name] = std::make_shared<MockProvider>(std::move(options));
            harness.register_provider(name, mocks[name]);
        }
        harness.use_default_router();
    }
};

ToolCall consult_call(std::string id, std::string member, std::string question) {
    return ToolCall{.id = std::move(id),
                    .name = "consult",
                    .arguments = nlohmann::json{{"member", member}, {"question", question}}.dump()};
}

MockTurn calling(std::vector<ToolCall> calls) {
    return MockTurn{.text = "",
                    .tool_calls = std::move(calls),
                    .finish_reason = apogee::harness::FinishReason::ToolCalls};
}

struct Said : apogee::agentloop::Reporter {
    std::vector<SideCall> side_calls;

    void on_side_call(const SideCall& call) override {
        side_calls.push_back(call);
    }
};

/// The tool results the root was shown, in order.
std::vector<std::string> results(const std::vector<ChatMessage>& history) {
    std::vector<std::string> out;
    for (const ChatMessage& message : history) {
        if (message.role == apogee::harness::Role::Tool) {
            out.push_back(message.content.plain_text());
        }
    }
    return out;
}

}  // namespace

TEST_CASE("consult exists only while the active suite designates members, naming them",
          "[tools][consult]") {
    World world;
    apogee::agent::ToolRegistry registry;
    const apogee::tools::ConsultOffer offer =
        apogee::tools::register_consult_tool(registry, world.harness, world.calls);
    REQUIRE(registry.find("consult") != nullptr);
    CHECK(offer.notes.empty());
    // The description names each member with its backend, and the caps --
    // what a model reads and what selection (26g) ranks.
    const std::string golden =
        "Ask another model in your suite a question and get its answer back. Members you can "
        "consult: utility (helper), extraction (scribe). The member sees only the question you "
        "write -- not this conversation, not any file, not your tools -- so put every fact it "
        "needs into the question. It answers in at most 512 tokens; a question may run to about "
        "1024 tokens; at most 4 consults per turn.";
    CHECK(registry.find("consult")->description == golden);
    CHECK(offer.description == golden);
    const nlohmann::json schema =
        nlohmann::json::parse(registry.find("consult")->parameters_schema);
    CHECK(schema["properties"]["member"]["enum"] == nlohmann::json({"utility", "extraction"}));
    CHECK(schema["required"] == nlohmann::json({"member", "question"}));
    // Ungated: read-only, local, side-effect-free.
    CHECK_FALSE(apogee::agent::gated(*registry.find("consult")));
    // Registering twice leaves one.
    (void)apogee::tools::register_consult_tool(registry, world.harness, world.calls);
    CHECK(registry.size() == 1);

    // A suite without `consultable:`: no tool.
    world.harness.set_active_suite("plain");
    apogee::agent::ToolRegistry plain;
    CHECK(apogee::tools::register_consult_tool(plain, world.harness, world.calls)
              .description.empty());
    CHECK(plain.find("consult") == nullptr);
    // No suite: no tool.
    world.harness.set_active_suite("");
    apogee::agent::ToolRegistry none;
    (void)apogee::tools::register_consult_tool(none, world.harness, world.calls);
    CHECK(none.empty());
    // A suite whose only consultable member bills per call: no tool, and why.
    world.harness.set_active_suite("billed");
    apogee::agent::ToolRegistry billed;
    const apogee::tools::ConsultOffer refused =
        apogee::tools::register_consult_tool(billed, world.harness, world.calls);
    CHECK(billed.empty());
    REQUIRE(refused.notes.size() == 1);
    CHECK_THAT(refused.notes.front(),
               Catch::Matchers::StartsWith("consult: not offering utility -- 'paid' (utility) is "
                                           "billed per call"));
}

TEST_CASE("a consult runs the member on exactly the brief and returns its answer as the result",
          "[tools][consult]") {
    World world{{calling({consult_call("c1", "utility", "What is the codeword?")}),
                 MockTurn{.text = "The codeword is MARMALADE-7."}}};
    apogee::agent::ToolRegistry registry;
    (void)apogee::tools::register_consult_tool(registry, world.harness, world.calls);
    // A conversation with everything a brief must not carry: a system prompt,
    // earlier turns, retrieval riding the transient path.
    std::vector<ChatMessage> history{
        ChatMessage::system("You are the root."), ChatMessage::user("an earlier question"),
        ChatMessage::assistant("an earlier answer"), ChatMessage::user("What is the codeword?")};
    apogee::agentloop::Options options;
    options.model = "root";
    options.tools = &registry;
    options.member_calls = world.calls.get();
    options.transient_prefix = {ChatMessage::system("retrieved: a secret document")};
    options.transient_at = 1;
    Said said;
    const apogee::agentloop::RunResult result =
        apogee::agentloop::run(world.harness, history, options, said);
    CHECK(result.answer == "The codeword is MARMALADE-7.");

    // The member's wire: one user message, the brief, nothing else.
    REQUIRE(world.mocks["helper"]->requests().size() == 1);
    const apogee::harness::ChatRequest& sent = world.mocks["helper"]->requests().front();
    REQUIRE(sent.messages.size() == 1);
    CHECK(sent.messages.front().role == apogee::harness::Role::User);
    CHECK(sent.messages.front().content.plain_text() == "What is the codeword?");
    CHECK(sent.tools.empty());
    CHECK(sent.transient.side_request);
    CHECK(world.mocks["scribe"]->requests().empty());

    // An ordinary tool result in the root's history: call and result paired.
    CHECK(results(history) == std::vector<std::string>{"utility (helper) answered:\nMARMALADE-7"});
    const auto call = std::ranges::find_if(
        history, [](const ChatMessage& message) { return !message.tool_calls.empty(); });
    REQUIRE(call != history.end());
    CHECK(call->tool_calls.front().name == "consult");
    // One labelled line in the thinking block, opened and closed.
    REQUIRE(said.side_calls.size() == 2);
    CHECK(said.side_calls[0].role == "consult");
    CHECK(said.side_calls[0].detail == "asking utility (helper): What is the codeword?");
    CHECK(said.side_calls[1].done);
}

TEST_CASE("the fifth consult in a turn is refused with the reason, as a result",
          "[tools][consult]") {
    std::vector<ToolCall> five;
    for (int i = 1; i <= 5; ++i) {
        five.push_back(consult_call("c" + std::to_string(i), "utility",
                                    "question number " + std::to_string(i)));
    }
    World world{{calling(five), calling({consult_call("c6", "extraction", "and next turn?")}),
                 MockTurn{.text = "done"}, calling({consult_call("c7", "extraction", "fresh")}),
                 MockTurn{.text = "ok"}}};
    apogee::agent::ToolRegistry registry;
    (void)apogee::tools::register_consult_tool(registry, world.harness, world.calls);
    std::vector<ChatMessage> history{ChatMessage::user("ask away")};
    apogee::agentloop::Options options;
    options.model = "root";
    options.tools = &registry;
    options.member_calls = world.calls.get();
    (void)apogee::agentloop::run(world.harness, history, options);
    const std::vector<std::string> shown = results(history);
    REQUIRE(shown.size() == 6);
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(shown[i] == "utility (helper) answered:\nMARMALADE-7");
    }
    CHECK(shown[4] ==
          "Not consulted: consult budget spent this turn: 4 of 4 member calls made -- answer "
          "with what you have.");
    // Still this turn: the step after the cap is refused too, and nothing more is sent.
    CHECK_THAT(shown[5], Catch::Matchers::StartsWith("Not consulted: consult budget spent"));
    CHECK(world.mocks["helper"]->requests().size() == 4);
    CHECK(world.mocks["scribe"]->requests().empty());

    // The next turn starts over.
    std::vector<ChatMessage> next{ChatMessage::user("one more")};
    (void)apogee::agentloop::run(world.harness, next, options);
    REQUIRE(results(next).size() == 1);
    CHECK(results(next).front() == "extraction (scribe) answered:\nMARMALADE-7");
    CHECK(world.mocks["scribe"]->requests().size() == 1);
}

TEST_CASE("a consult's refusals and failures are results the model can read", "[tools][consult]") {
    const World world;
    const apogee::agent::Tool tool = apogee::tools::make_consult_tool(
        world.calls, apogee::agentloop::consultable_members(world.harness), {});
    // Outside a turn: refused, never run unbounded.
    CHECK(tool.run(R"({"member":"utility","question":"x"})").content ==
          "Not consulted: no turn is open to make a member call in.");
    const MemberCalls::Turn turn = world.calls->begin_turn({}, {});
    const apogee::agent::ToolOutcome bad = tool.run("not json");
    CHECK(bad.is_error);
    CHECK(tool.run(R"({"question":"x"})").content ==
          "Error: member is required: the role of the member to ask");
    CHECK(tool.run(R"({"member":"utility"})").content ==
          "Error: question is required: the whole brief the member needs");
    const apogee::agent::ToolOutcome refused = tool.run(R"({"member":"vision","question":"x"})");
    CHECK_FALSE(refused.is_error);
    CHECK_THAT(refused.content,
               Catch::Matchers::StartsWith("Not consulted: 'vision' is not a member you can "
                                           "consult (consultable: utility, extraction)"));
    CHECK(world.calls->used() == 0);
}
