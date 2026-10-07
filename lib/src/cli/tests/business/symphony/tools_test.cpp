#include "symphony/tools.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "agent/tool.h"
#include "agentloop/loop.h"
#include "agentloop/member_call.h"
#include "agentloop/reporter.h"
#include "backends/mock.h"
#include "contracts/config.h"
#include "harness/harness.h"
#include "symphony/definition.h"
#include "tools/consult.h"

/// The Orchestrator (27t) over scripted members: each symphony projects an
/// ordinary tool -- its name, its own description, its input contract as the
/// schema -- offered only where it can be played on local members; a play the
/// model starts runs through the one walk on the turn's member calls, its
/// labeled line and stage lines said; plays, consults and validation draw one
/// per-turn budget, and a play it cannot afford is refused before its first
/// call, said; and the model's initiative never reaches a billed member.
namespace {

using apogee::agentloop::MemberCalls;
using apogee::agentloop::SideCall;
using apogee::backends::MockProvider;
using apogee::backends::MockTurn;
using apogee::harness::ChatMessage;
using apogee::harness::ToolCall;
using apogee::symphony::Catalog;
using apogee::symphony::OrchestraOffer;

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
  ghost:
    type: mock
suites:
  duo:
    members:
      chat: root
      utility: helper
      extraction: scribe
    orchestrate: true
  checked:
    members:
      chat: root
      utility: helper
      extraction: scribe
    consultable: [utility]
    validate:
      answers: always
  billed:
    members:
      chat: root
      utility: paid
  tight:
    members:
      chat: root
      utility: helper
      extraction: scribe
    consult_caps:
      per_turn: 1
  ghosted:
    members:
      chat: root
      utility: ghost
  dangling:
    members:
      chat: root
      utility: nowhere
  solo:
    members:
      chat: paid
symphonies:
  digest:
    description: Summarize, then pull out the facts
    stages:
      - name: summary
        play: summarize-verify
      - name: facts
        play: extract-facts
)YAML";

/// What extract-facts' schema stage answers with: a record its schema holds.
constexpr std::string_view kFacts =
    R"({"topic":"a cat","people":[],"places":["the mat"],"dates":["noon"],"facts":["The cat sat."]})";

/// A suite of mocks: the root answers `root_turns`, the helper summarizes,
/// the scribe records facts, and `paid` bills per call.
struct World {
    apogee::harness::Harness harness;
    std::map<std::string, std::shared_ptr<MockProvider>> mocks;
    std::shared_ptr<MemberCalls> calls = std::make_shared<MemberCalls>(harness);

    explicit World(const std::vector<MockTurn>& root_turns = {MockTurn{.text = "done"}},
                   std::string_view config = kConfig, std::string_view scribe = kFacts)
        : harness{apogee::harness::parse_config(config, "<test>")} {
        for (const char* name : {"root", "helper", "scribe", "paid"}) {
            MockProvider::Options options;
            options.backend_name = name;
            const std::string_view which{name};
            if (which == "root") {
                options.turns = root_turns;
            } else if (which == "scribe") {
                options.turns = {MockTurn{.text = std::string{scribe}}};
            } else {
                options.turns = {MockTurn{.text = "SUMMARY: a cat sat on the mat at noon.",
                                          .usage = {.prompt_tokens = 30, .completion_tokens = 7}}};
            }
            options.metered = which == "paid";
            mocks[name] = std::make_shared<MockProvider>(std::move(options));
            harness.register_provider(name, mocks[name]);
        }
        harness.use_default_router();
    }

    /// Every definition there is: the starters (compiled in -- no directory)
    /// and the config's.
    [[nodiscard]] Catalog symphonies() const {
        return apogee::symphony::catalog(harness.config(), "/nonexistent/symphonies");
    }

    [[nodiscard]] apogee::symphony::PlayToolContext context() const {
        return apogee::symphony::PlayToolContext{.calls = calls,
                                                 .conversation = [] { return "root"; }};
    }
};

ToolCall play_call(std::string id, std::string_view symphony, std::string input) {
    return ToolCall{.id = std::move(id),
                    .name = "play_" + std::string{symphony},
                    .arguments = nlohmann::json{{"input", std::move(input)}}.dump()};
}

MockTurn calling(std::vector<ToolCall> calls) {
    return MockTurn{.text = "",
                    .tool_calls = std::move(calls),
                    .finish_reason = apogee::harness::FinishReason::ToolCalls};
}

struct Said : apogee::agentloop::Reporter {
    std::vector<SideCall> side_calls;
    std::vector<std::string> notices;

    void on_side_call(const SideCall& call) override {
        side_calls.push_back(call);
    }

    void on_notice(std::string_view text) override {
        notices.emplace_back(text);
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

std::vector<std::string> play_tools(const apogee::agent::ToolRegistry& registry) {
    std::vector<std::string> out;
    for (const std::string& name : registry.names()) {
        if (name.starts_with("play_")) {
            out.push_back(name);
        }
    }
    return out;
}

apogee::agentloop::Options options_for(const World& world,
                                       const apogee::agent::ToolRegistry& registry) {
    apogee::agentloop::Options options;
    options.model = "root";
    options.tools = &registry;
    options.member_calls = world.calls.get();
    return options;
}

}  // namespace

TEST_CASE("a symphony's tool is its name, its own description and its input contract",
          "[symphony][tools]") {
    const World world;
    const Catalog catalog = world.symphonies();
    const apogee::symphony::Definition* summarize = catalog.find("summarize-verify");
    REQUIRE(summarize != nullptr);
    const apogee::agent::Tool tool =
        apogee::symphony::make_play_tool(world.harness, *summarize, catalog, world.context());
    CHECK(tool.name == "play_summarize-verify");
    CHECK(tool.description ==
          "Summarize a passage, then check the summary against the passage and correct it. Plays "
          "the summarize-verify symphony: 2 member calls on your suite's models, one after "
          "another (utility → chat). It sees only the input you give it -- not this "
          "conversation, not any file -- so put the whole text it needs there.");
    CHECK(nlohmann::json::parse(tool.parameters_schema) ==
          nlohmann::json::parse(R"({"type":"object","properties":{"input":{"type":"string",
              "description":"The passage to summarize."}},"required":["input"]})"));
    // Ungated: a symphony's stages call no tools.
    CHECK_FALSE(apogee::agent::gated(tool));

    // A chain projects the same way: its cost is the whole walk's.
    const apogee::symphony::Definition* digest = catalog.find("digest");
    REQUIRE(digest != nullptr);
    CHECK(apogee::symphony::play_tool_description(digest->spec, catalog) ==
          "Summarize, then pull out the facts. Plays the digest symphony: 3 member calls on your "
          "suite's models, one after another (play:summarize-verify → play:extract-facts). It "
          "sees only the input you give it -- not this conversation, not any file -- so put the "
          "whole text it needs there.");
    // No input description: the schema still names what it is.
    CHECK(nlohmann::json::parse(apogee::symphony::play_tool_schema(
              digest->spec))["properties"]["input"]["description"] ==
          "The text the symphony works on");

    // The projection is the definition's: an edit is the next registry's.
    apogee::harness::SymphonySpec edited = summarize->spec;
    edited.description = "Boil a passage down";
    edited.input.description = "The passage, whole.";
    CHECK_THAT(apogee::symphony::play_tool_description(edited, catalog),
               Catch::Matchers::StartsWith("Boil a passage down. Plays the summarize-verify"));
    CHECK(nlohmann::json::parse(apogee::symphony::play_tool_schema(
              edited))["properties"]["input"]["description"] == "The passage, whole.");
}

TEST_CASE("orchestration offers each symphony it can play here, and says why not the rest",
          "[symphony][tools]") {
    World world;
    const Catalog catalog = world.symphonies();

    // On: every starter that takes text, and the chain; the vision starter
    // withheld for its image -- a property of the definition, not the session.
    apogee::agent::ToolRegistry registry;
    const OrchestraOffer offer =
        apogee::symphony::register_play_tools(registry, world.harness, catalog, world.context());
    CHECK(offer.offered == std::vector<std::string>{"extract-facts", "summarize-verify", "digest"});
    CHECK(play_tools(registry) ==
          std::vector<std::string>{"play_digest", "play_extract-facts", "play_summarize-verify"});
    REQUIRE(offer.withheld.size() == 1);
    CHECK(offer.withheld.front().symphony == "describe-answer");
    CHECK(offer.withheld.front().structural);
    CHECK_THAT(offer.withheld.front().reason,
               Catch::Matchers::StartsWith("it takes an image, which a tool call cannot give"));
    // Registering again adds nothing.
    (void)apogee::symphony::register_play_tools(registry, world.harness, catalog, world.context());
    CHECK(play_tools(registry).size() == 3);

    // No suite active: absent.
    world.harness.set_active_suite("");
    apogee::agent::ToolRegistry none;
    CHECK(apogee::symphony::register_play_tools(none, world.harness, catalog, world.context()) ==
          OrchestraOffer{});
    CHECK(none.empty());

    // A suite whose utility member bills per call: whatever reaches it is not
    // offered, naming the stage, the role and the backend; extract-facts --
    // its extraction falling back to the conversation -- still is.
    world.harness.set_active_suite("billed");
    const OrchestraOffer billed = apogee::symphony::orchestra_offer(world.harness, catalog, "root");
    CHECK(billed.offered == std::vector<std::string>{"extract-facts"});
    const auto withheld = [&](std::string_view name) {
        const auto found = std::ranges::find_if(
            billed.withheld,
            [&](const apogee::symphony::Withheld& w) { return w.symphony == name; });
        REQUIRE(found != billed.withheld.end());
        return *found;
    };
    CHECK(withheld("summarize-verify").reason ==
          "its summarize stage (utility) is 'paid', billed per call -- a play the model starts "
          "runs on its initiative, which never spends");
    CHECK_FALSE(withheld("summarize-verify").structural);
    CHECK(withheld("digest").reason ==
          "summarize-verify's summarize stage (utility) is 'paid', billed per call -- a play the "
          "model starts runs on its initiative, which never spends");

    // A turn's budget of one call: a two-call symphony can never be played,
    // so it is not offered, and the chain neither.
    world.harness.set_active_suite("tight");
    const OrchestraOffer tight = apogee::symphony::orchestra_offer(world.harness, catalog, "root");
    CHECK(tight.offered == std::vector<std::string>{"extract-facts"});

    // Nothing to offer: no tool, no framing.
    world.harness.set_active_suite("duo");
    apogee::agent::ToolRegistry empty;
    CHECK(apogee::symphony::register_play_tools(empty, world.harness, Catalog{}, world.context())
              .offered.empty());
    CHECK(empty.empty());
    CHECK(empty.environment().empty());
}

TEST_CASE(
    "a symphony whose member cannot answer here, or whose play the caps cannot hold, is "
    "withheld",
    "[symphony][tools]") {
    // A member configured and never built, one the config does not have, and
    // a whole play's cap of one call.
    const std::string config = std::string{kConfig} + "symphony_caps:\n  stage_calls: 1\n";
    World world{{MockTurn{.text = "done"}}, config};
    const Catalog catalog = world.symphonies();
    const auto reason = [&](std::string_view suite, std::string_view name) {
        world.harness.set_active_suite(std::string{suite});
        const OrchestraOffer offer =
            apogee::symphony::orchestra_offer(world.harness, catalog, "root");
        const auto found = std::ranges::find_if(
            offer.withheld,
            [&](const apogee::symphony::Withheld& w) { return w.symphony == name; });
        return found == offer.withheld.end() ? std::string{"offered"} : found->reason;
    };
    CHECK(reason("duo", "summarize-verify") ==
          "a play of it makes 2 member calls, more than symphony_caps.stage_calls allows (1)");
    CHECK(reason("duo", "extract-facts") == "offered");
    // A hand-built tool refuses the same, before its first call.
    world.harness.set_active_suite("duo");
    const apogee::agent::Tool tool = apogee::symphony::make_play_tool(
        world.harness, *catalog.find("summarize-verify"), catalog, world.context());
    const MemberCalls::Turn turn = world.calls->begin_turn({}, {});
    CHECK(tool.run(R"({"input":"x"})").content ==
          "Not played: a play of it makes 2 member calls, and symphony_caps.stage_calls allows 1. "
          "Answer without it.");
    CHECK(world.calls->used() == 0);

    World members;
    const Catalog all = members.symphonies();
    const auto withheld = [&](std::string_view suite) {
        members.harness.set_active_suite(std::string{suite});
        const OrchestraOffer offer =
            apogee::symphony::orchestra_offer(members.harness, all, "root");
        const auto found = std::ranges::find_if(
            offer.withheld,
            [](const apogee::symphony::Withheld& w) { return w.symphony == "summarize-verify"; });
        return found == offer.withheld.end() ? std::string{"offered"} : found->reason;
    };
    CHECK(withheld("ghosted") ==
          "its summarize stage (utility) is 'ghost', which could not be built in this session");
    CHECK(withheld("dangling") ==
          "its summarize stage (utility) names 'nowhere', which is not a configured backend");
}

TEST_CASE("a symphony that cannot be played, or whose tool name is too long, is withheld, said",
          "[symphony][tools]") {
    const std::string long_name(60, 'x');
    const std::string config = std::string{kConfig} + "  " + long_name +
                               ":\n    stages:\n      - {name: one, role: utility, prompt: "
                               "'Say: {{input}}'}\n" +
                               "  broken:\n    stages:\n      - {name: one, role: utility, "
                               "prompt: 'Say {{nothing}} of {{input}}'}\n";
    const World world{{MockTurn{.text = "done"}}, config};
    const OrchestraOffer offer =
        apogee::symphony::orchestra_offer(world.harness, world.symphonies(), "root");
    const auto found = std::ranges::find_if(
        offer.withheld,
        [&](const apogee::symphony::Withheld& w) { return w.symphony == long_name; });
    REQUIRE(found != offer.withheld.end());
    CHECK(found->reason ==
          "its name makes a tool name past 64 characters, which some providers refuse -- a "
          "shorter name offers it");
    CHECK(std::ranges::find(offer.offered, long_name) == offer.offered.end());
    // One whose own template names nothing: what `symphonies show` says, why.
    const auto broken = std::ranges::find_if(
        offer.withheld, [](const apogee::symphony::Withheld& w) { return w.symphony == "broken"; });
    REQUIRE(broken != offer.withheld.end());
    CHECK_THAT(broken->reason, Catch::Matchers::StartsWith("it cannot be played: stage 1 (one): "));
    CHECK_FALSE(broken->structural);
}

TEST_CASE("whether a session orchestrates is the flag or the active suite's word",
          "[symphony][tools]") {
    World world;
    CHECK(apogee::symphony::orchestrating(world.harness.config(), false));  // duo: orchestrate
    world.harness.set_active_suite("checked");
    CHECK_FALSE(apogee::symphony::orchestrating(world.harness.config(), false));
    CHECK(apogee::symphony::orchestrating(world.harness.config(), true));
    world.harness.set_active_suite("");
    CHECK_FALSE(apogee::symphony::orchestrating(world.harness.config(), false));
}

TEST_CASE("the note frames the root as the orchestrator while the registry holds a play",
          "[symphony][tools]") {
    const World world;
    apogee::agent::ToolRegistry registry;
    registry.set_environment([](const apogee::agent::ToolRegistry&) { return "Environment: x"; });
    (void)apogee::symphony::register_play_tools(registry, world.harness, world.symphonies(),
                                                world.context());
    const std::string framing{apogee::symphony::orchestrator_framing()};
    CHECK(framing ==
          "You are the orchestrator of a suite of models. Each play_ tool plays a symphony: a "
          "staged process in which your suite's models work through an input in turn. When a "
          "request is the work a symphony is for, play it rather than doing that work yourself "
          "-- give it the whole text it needs as its input, since it sees nothing else -- then "
          "answer the user from its output. Anything else, answer yourself, without a tool.");
    CHECK(framing.size() < 512);
    CHECK(registry.environment() == "Environment: x\n\n" + framing);
    // A copy narrowed to no play says nothing of them (26p's composition).
    apogee::agent::ToolRegistry narrowed;
    narrowed.set_environment(registry.environment_source());
    CHECK(narrowed.environment() == "Environment: x");
    // Without a note of its own, the framing alone.
    apogee::agent::ToolRegistry bare;
    (void)apogee::symphony::register_play_tools(bare, world.harness, world.symphonies(),
                                                world.context());
    CHECK(bare.environment() == framing);
}

TEST_CASE("a play the model starts runs the walk, said as one labeled line and its stages",
          "[symphony][tools]") {
    World world{{calling({play_call("p1", "summarize-verify", "The cat sat on the mat at noon.")}),
                 MockTurn{.text = "A cat sat on the mat at noon.",
                          .usage = {.prompt_tokens = 40, .completion_tokens = 5}},
                 MockTurn{.text = "Done: a cat sat on the mat at noon."}}};
    apogee::agent::ToolRegistry registry;
    (void)apogee::symphony::register_play_tools(registry, world.harness, world.symphonies(),
                                                world.context());
    std::vector<ChatMessage> history{ChatMessage::system("You are the root."),
                                     ChatMessage::user("Summarize: the cat sat on the mat.")};
    Said said;
    const apogee::agentloop::RunResult result =
        apogee::agentloop::run(world.harness, history, options_for(world, registry), said);
    CHECK(result.answer == "Done: a cat sat on the mat at noon.");

    // Each stage is the member call 27q pinned: the brief alone, on its role.
    REQUIRE(world.mocks["helper"]->requests().size() == 1);
    const apogee::harness::ChatRequest& first = world.mocks["helper"]->requests().front();
    REQUIRE(first.messages.size() == 1);
    CHECK_THAT(first.messages.front().content.plain_text(),
               Catch::Matchers::StartsWith("Summarize the passage below") &&
                   Catch::Matchers::ContainsSubstring("The cat sat on the mat at noon."));
    CHECK(first.tools.empty());
    // The root answered its own stage -- a member call, brief-only -- then
    // read the play's output as an ordinary tool result.
    REQUIRE(world.mocks["root"]->requests().size() == 3);
    const apogee::harness::ChatRequest& verify = world.mocks["root"]->requests()[1];
    REQUIRE(verify.messages.size() == 1);
    CHECK(verify.tools.empty());
    CHECK_THAT(verify.messages.front().content.plain_text(),
               Catch::Matchers::ContainsSubstring("SUMMARY: a cat sat on the mat at noon."));
    CHECK(results(history) ==
          std::vector<std::string>{"summarize-verify answered:\nA cat sat on the mat at noon."});

    // The labeled line -- the model's choice and its cost -- opened first,
    // closed last with the stages' tokens, the stage lines beneath it.
    REQUIRE(said.side_calls.size() == 6);
    CHECK(said.side_calls[0].role == "play");
    CHECK(said.side_calls[0].detail ==
          "the model chose summarize-verify: 2 member calls, utility → chat");
    CHECK_FALSE(said.side_calls[0].done);
    CHECK(said.side_calls[1].role == "stage 1/2 summarize");
    CHECK(said.side_calls[3].role == "stage 2/2 verify");
    CHECK(said.side_calls[5].role == "play");
    CHECK(said.side_calls[5].done);
    CHECK(said.side_calls[5].tokens == std::optional<std::int64_t>{12});
    CHECK(said.notices.empty());
    CHECK(world.mocks["paid"]->requests().empty());
}

TEST_CASE("plays, consults and checks draw one turn's budget, and a play it cannot afford is said",
          "[symphony][tools]") {
    // One step: a consult (1), summarize-verify (2), extract-facts (1) -- the
    // turn's four -- then extract-facts again, which the budget refuses before
    // its first call; and `answers: always` finds the budget spent too.
    World world{{calling({ToolCall{.id = "c1",
                                   .name = "consult",
                                   .arguments = R"({"member":"utility","question":"Codeword?"})"},
                          play_call("p1", "summarize-verify", "The cat sat on the mat at noon."),
                          play_call("p2", "extract-facts", "The cat sat on the mat at noon."),
                          play_call("p3", "extract-facts", "And once more.")}),
                 MockTurn{.text = "A cat sat on the mat."}, MockTurn{.text = "All done."}}};
    world.harness.set_active_suite("checked");
    apogee::agent::ToolRegistry registry;
    (void)apogee::tools::register_consult_tool(registry, world.harness, world.calls);
    (void)apogee::symphony::register_play_tools(registry, world.harness, world.symphonies(),
                                                world.context());
    REQUIRE(registry.find("consult") != nullptr);
    std::vector<ChatMessage> history{ChatMessage::user("Go.")};
    Said said;
    const apogee::agentloop::RunResult result =
        apogee::agentloop::run(world.harness, history, options_for(world, registry), said);
    CHECK(result.answer == "All done.");

    const std::vector<std::string> shown = results(history);
    REQUIRE(shown.size() == 4);
    CHECK_THAT(shown[0], Catch::Matchers::StartsWith("utility (helper) answered:"));
    CHECK_THAT(shown[1], Catch::Matchers::StartsWith("summarize-verify answered:"));
    CHECK_THAT(shown[2], Catch::Matchers::StartsWith("extract-facts answered:\n{"));
    CHECK(shown[3] ==
          "Not played: this turn's member-call budget has 0 of 4 calls left -- plays, consults "
          "and checks share it -- and a play of extract-facts makes 1. Answer without it.");
    // Four member calls in all: nothing past the budget was sent.
    CHECK(world.mocks["helper"]->requests().size() == 2);
    CHECK(world.mocks["scribe"]->requests().size() == 1);
    CHECK(world.mocks["root"]->requests().size() == 3);  // two turns' steps and the verify stage
    // Said, never silent: the refused play, and the answer check the spent
    // budget left undone.
    REQUIRE_FALSE(said.notices.empty());
    CHECK(said.notices.front() ==
          "orchestrate: extract-facts not played -- this turn's member-call budget has 0 of 4 "
          "calls left -- plays, consults and checks share it -- and a play of extract-facts "
          "makes 1");
    // The play line's tokens only when every stage said: the root's verify
    // turn reported none.
    const auto play_done = std::ranges::find_if(
        said.side_calls, [](const SideCall& call) { return call.role == "play" && call.done; });
    REQUIRE(play_done != said.side_calls.end());
    CHECK_FALSE(play_done->tokens.has_value());
    CHECK(said.notices.back() ==
          "validate: not checked -- the turn's member-call budget is spent (4 of 4, plays, "
          "consults and checks together) -- checked by structure only");
}

TEST_CASE("a chosen chain obeys the whole play's budget, and stops said", "[symphony][tools]") {
    const std::string config = std::string{kConfig} + "symphony_caps:\n  answer_tokens: 5\n";
    World world{{calling({play_call("p1", "digest", "The cat sat on the mat at noon.")}),
                 MockTurn{.text = "Answered without it."}},
                config};
    apogee::agent::ToolRegistry registry;
    (void)apogee::symphony::register_play_tools(registry, world.harness, world.symphonies(),
                                                world.context());
    REQUIRE(registry.find("play_digest") != nullptr);
    std::vector<ChatMessage> history{ChatMessage::user("Digest this.")};
    Said said;
    (void)apogee::agentloop::run(world.harness, history, options_for(world, registry), said);
    // The helper's seven tokens cross the play's five: the walk stops there,
    // its position named, with no output.
    REQUIRE(results(history).size() == 1);
    CHECK_THAT(results(history).front(),
               Catch::Matchers::StartsWith("Not played: digest → summarize-verify, stage 1/2 "
                                           "summarize (utility): the play's budget is spent"));
    CHECK(world.mocks["scribe"]->requests().empty());
    REQUIRE(said.notices.size() == 1);
    CHECK_THAT(said.notices.front(),
               Catch::Matchers::StartsWith("orchestrate: digest stopped -- digest → "
                                           "summarize-verify, stage 1/2 summarize"));
}

TEST_CASE("the model's initiative never reaches a billed member, offered or not",
          "[symphony][tools]") {
    World world;
    world.harness.set_active_suite("billed");
    const Catalog catalog = world.symphonies();
    // Built by hand, past the offer that would have withheld it.
    const apogee::agent::Tool tool = apogee::symphony::make_play_tool(
        world.harness, *catalog.find("summarize-verify"), catalog, world.context());
    std::vector<std::string> notices;
    const MemberCalls::Turn turn = world.calls->begin_turn(
        {}, {}, [&notices](std::string_view line) { notices.emplace_back(line); });
    const apogee::agent::ToolOutcome outcome = tool.run(R"({"input":"The cat sat."})");
    CHECK_FALSE(outcome.is_error);
    CHECK_THAT(outcome.content,
               Catch::Matchers::StartsWith("Not played: stage 1/2 summarize (utility): 'paid' "
                                           "(utility) is billed per call"));
    CHECK(world.mocks["paid"]->requests().empty());
    REQUIRE(notices.size() == 1);
    CHECK_THAT(notices.front(),
               Catch::Matchers::StartsWith("orchestrate: summarize-verify stopped"));
}

TEST_CASE("a play whose member fails is an error the model reads, and said", "[symphony][tools]") {
    // The scribe answers with no JSON: extract-facts' schema stage stops the
    // walk, a member's failure.
    const World world{{MockTurn{.text = "done"}}, kConfig, "Sorry, no record."};
    const Catalog catalog = world.symphonies();
    const apogee::agent::Tool tool = apogee::symphony::make_play_tool(
        world.harness, *catalog.find("extract-facts"), catalog, world.context());
    std::vector<std::string> notices;
    const MemberCalls::Turn turn = world.calls->begin_turn(
        {}, {}, [&notices](std::string_view line) { notices.emplace_back(line); });
    const apogee::agent::ToolOutcome outcome = tool.run(R"({"input":"The cat sat."})");
    CHECK(outcome.is_error);
    CHECK(outcome.content ==
          "Error: extract-facts stopped with no output -- stage 1/1 extract (extraction): "
          "'scribe' answered with no JSON, and the stage holds its answer to a schema");
    REQUIRE(notices.size() == 1);
    CHECK(notices.front() ==
          "orchestrate: extract-facts stopped -- stage 1/1 extract (extraction): 'scribe' "
          "answered with no JSON, and the stage holds its answer to a schema");
}

TEST_CASE("a play's arguments are checked, and a play outside a turn is refused",
          "[symphony][tools]") {
    const World world;
    const Catalog catalog = world.symphonies();
    const apogee::agent::Tool tool = apogee::symphony::make_play_tool(
        world.harness, *catalog.find("extract-facts"), catalog, world.context());
    const apogee::agent::ToolOutcome no_turn = tool.run(R"({"input":"x"})");
    CHECK(no_turn.content == "Not played: no turn is open to play it in. Answer without it.");
    CHECK_FALSE(no_turn.is_error);  // a stated limit, not a failure
    const MemberCalls::Turn turn = world.calls->begin_turn({}, {});
    const apogee::agent::ToolOutcome missing = tool.run(R"({"text":"x"})");
    CHECK(missing.is_error);
    CHECK(missing.content ==
          "Error: input is required -- the whole text extract-facts works on, "
          "as a string: {\"input\": \"...\"}");
    CHECK(tool.run("not json").is_error);
    const apogee::agent::ToolOutcome empty = tool.run(R"({"input":"  "})");
    CHECK(empty.is_error);
    CHECK_THAT(empty.content, Catch::Matchers::StartsWith("Error: input is empty"));
    CHECK(world.calls->used() == 0);
    CHECK(world.mocks.at("scribe")->requests().empty());
}

TEST_CASE("the members a symphony reaches are its stages and those of every one it plays",
          "[symphony][tools]") {
    const World world;
    const Catalog catalog = world.symphonies();
    const std::vector<apogee::symphony::ReachedMember> reached = apogee::symphony::reached_members(
        world.harness.config(), catalog.find("digest")->spec, catalog, "root");
    REQUIRE(reached.size() == 3);
    CHECK(reached[0].symphony == "summarize-verify");
    CHECK(reached[0].stage == "summarize");
    CHECK(reached[0].role == "utility");
    CHECK(reached[0].backend == "helper");
    CHECK(reached[1].backend == "root");
    CHECK(reached[2].symphony == "extract-facts");
    CHECK(reached[2].backend == "scribe");
    CHECK(apogee::symphony::reached_at("digest", reached[2]) ==
          "extract-facts's extract stage (extraction)");
    CHECK(apogee::symphony::reached_at("extract-facts", reached[2]) ==
          "its extract stage (extraction)");
    // A helper role the suite leaves out answers on the conversation, as the
    // one chain says.
    World solo;
    solo.harness.set_active_suite("solo");
    const std::vector<apogee::symphony::ReachedMember> fallback = apogee::symphony::reached_members(
        solo.harness.config(), catalog.find("summarize-verify")->spec, catalog, "paid");
    REQUIRE(fallback.size() == 2);
    CHECK(fallback[0].role == "utility");
    CHECK(fallback[0].backend == "paid");
}
