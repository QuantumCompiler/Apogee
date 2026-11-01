#include "tools/delegate.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "agent/tool.h"
#include "agentloop/side_call.h"
#include "agentloop/tool_selection.h"
#include "backends/mock.h"
#include "contracts/config.h"
#include "contracts/errors.h"
#include "harness/harness.h"

/// The delegate (41e): the one loop re-entered on a fresh context with the
/// parent's tools less itself, under the parent's gate, bounded, cancelled
/// with the parent, narrated as a side call with its tool lines -- and its
/// answer and spend returned as the tool's result.
namespace {

namespace t = apogee::tools;
using apogee::backends::MockProvider;
using apogee::backends::MockTurn;
using apogee::harness::ChatRequest;
using apogee::harness::ToolCall;
using Catch::Matchers::ContainsSubstring;

constexpr std::string_view kConfig = R"YAML(models:
  default: root
  default_utility: helper
backends:
  root:
    type: mock
  helper:
    type: mock
  paid:
    type: mock
)YAML";

ToolCall call(std::string name, std::string arguments = "{}") {
    static int next = 0;
    return ToolCall{.id = "call_" + std::to_string(++next),
                    .name = std::move(name),
                    .arguments = std::move(arguments)};
}

/// A harness of mocks: `root` the session's model, `helper` the utility
/// role's, `paid` billed per call.
struct World {
    apogee::harness::Harness harness;
    std::map<std::string, std::shared_ptr<MockProvider>> mocks;
    apogee::agent::ToolRegistry registry;
    int peeked = 0;
    int poked = 0;

    explicit World(std::vector<MockTurn> root, std::vector<MockTurn> helper = {},
                   std::string_view config = kConfig)
        : harness{apogee::harness::parse_config(config, "<test>")} {
        for (auto& [name, turns] : std::map<std::string, std::vector<MockTurn>>{
                 {"root", std::move(root)}, {"helper", std::move(helper)}, {"paid", {}}}) {
            MockProvider::Options options;
            options.backend_name = name;
            options.turns = turns;
            if (options.turns.empty()) {
                options.turns.push_back(MockTurn{.text = name + " says hi"});
            }
            options.metered = name == "paid";
            mocks[name] = std::make_shared<MockProvider>(std::move(options));
            harness.register_provider(name, mocks[name]);
        }
        harness.use_default_router();
        apogee::agent::Tool peek;
        peek.name = "peek";
        peek.description = "Look";
        peek.run = [this](std::string_view) {
            ++peeked;
            return apogee::agent::ToolOutcome{"PEEKED"};
        };
        registry.add(std::move(peek));
        apogee::agent::Tool poke;
        poke.name = "poke";
        poke.description = "Change something";
        poke.writes = true;
        poke.run = [this](std::string_view) {
            ++poked;
            return apogee::agent::ToolOutcome{"POKED"};
        };
        registry.add(std::move(poke));
        t::register_delegate_tool(registry);
    }

    /// The context a parent turn on `root` would make.
    [[nodiscard]] t::DelegateContext context(apogee::agentloop::Reporter* reporter = nullptr) {
        t::DelegateContext out;
        out.harness = &harness;
        out.tools = &registry;
        out.parent.model = "root";
        out.parent.tools = &registry;
        out.parent.permission = [](const apogee::agent::GateRequest&) {
            return apogee::agent::Permission::Ask;
        };
        out.reporter = reporter;
        return out;
    }
};

/// Every side call the delegation said, and every call's state (56a).
struct Recorder final : apogee::agentloop::Reporter {
    std::vector<apogee::agentloop::SideCall> calls;
    std::vector<std::pair<std::string, apogee::agent::ToolCallState>> states;

    void on_side_call(const apogee::agentloop::SideCall& call) override {
        calls.push_back(call);
    }

    void on_tool_call(const apogee::agent::ToolCallReport& report) override {
        states.emplace_back(std::string{report.id}, report.state);
    }
};

std::vector<std::string> tool_names(const ChatRequest& request) {
    std::vector<std::string> out;
    for (const auto& tool : request.tools) {
        out.push_back(tool.name);
    }
    return out;
}

}  // namespace

TEST_CASE("a delegation runs the one loop on a fresh context and returns its answer and spend",
          "[tools][delegate]") {
    World world{{MockTurn{.tool_calls = {call("peek")}}, MockTurn{.text = "Found it in a.cpp."}}};
    Recorder said;
    t::DelegateContext context = world.context(&said);
    context.notes = {"PROJECT RULES"};
    const apogee::agent::ToolOutcome outcome =
        t::run_delegate(context, R"({"brief": "Find where it is defined"})");
    REQUIRE_FALSE(outcome.is_error);
    CHECK_THAT(outcome.content, Catch::Matchers::StartsWith("Found it in a.cpp.\n\n[delegate on "
                                                            "root: 2 model calls, tools peek, "));
    CHECK(world.peeked == 1);

    // A fresh context: the brief, framed, and nothing of any parent history;
    // the standing notes as system context; the kit the parent's less itself.
    const ChatRequest& first = world.mocks["root"]->requests().front();
    CHECK_THAT(first.messages.back().content.plain_text(),
               ContainsSubstring("You are a delegate") &&
                   ContainsSubstring("The task:\nFind where it is defined"));
    bool noted = false;
    for (const auto& message : first.messages) {
        noted = noted || message.content.plain_text() == "PROJECT RULES";
    }
    CHECK(noted);
    const std::vector<std::string> offered = tool_names(first);
    CHECK(std::ranges::find(offered, "peek") != offered.end());
    CHECK(std::ranges::find(offered, "delegate") == offered.end());  // depth one
    CHECK(first.max_tokens == t::kDelegateAnswerTokens);

    // Narrated: the delegation as a side call, its tool line opened and closed
    // inside it, the whole closed with its spend.
    REQUIRE(said.calls.size() == 4);
    CHECK(said.calls[0].role == "delegate");
    CHECK(said.calls[0].detail == "a sub-session on root: Find where it is defined");
    CHECK_FALSE(said.calls[0].done);
    CHECK(said.calls[1].role == "delegate");
    CHECK_FALSE(said.calls[1].done);
    CHECK(said.calls[2].done);
    CHECK(said.calls[2].detail == said.calls[1].detail);
    CHECK(said.calls[3].done);
    CHECK(said.calls[3].detail == said.calls[0].detail);
    CHECK(said.calls[3].tokens.has_value());
}

TEST_CASE("depth one: a delegate's kit never holds delegate, its environment kept",
          "[tools][delegate]") {
    World world{{}};
    world.registry.set_environment([](const apogee::agent::ToolRegistry& tools) {
        return "NOTE with " + std::to_string(tools.size()) + " tools";
    });
    const apogee::agent::ToolRegistry kit = t::delegate_kit(world.registry);
    CHECK(kit.find("delegate") == nullptr);
    CHECK(kit.find("peek") != nullptr);
    CHECK(kit.find("poke") != nullptr);
    CHECK(kit.environment() == "NOTE with 2 tools");
}

TEST_CASE("a delegation is bounded: past its call cap it answers without tools, spend said",
          "[tools][delegate][caps]") {
    std::vector<MockTurn> forever(20, MockTurn{.tool_calls = {call("peek")}});
    forever.push_back(MockTurn{.text = "gave up"});
    World world{forever};
    const apogee::agent::ToolOutcome outcome =
        t::run_delegate(world.context(), R"({"brief": "Loop forever"})");
    REQUIRE_FALSE(outcome.is_error);
    CHECK_THAT(outcome.content,
               ContainsSubstring("stopped at its 8-call cap and answered without tools"));
    CHECK(world.peeked <= t::kDelegateSubTurns);
    CHECK(world.mocks["root"]->requests().size() <=
          static_cast<std::size_t>(t::kDelegateSubTurns) + 1);
}

TEST_CASE("the parent's gate, whole: a sub-tool that asks reaches the parent's prompt",
          "[tools][delegate][gate]") {
    SECTION("someone to ask: the parent's confirm decides, once per call") {
        World world{{MockTurn{.tool_calls = {call("poke")}}, MockTurn{.text = "Poked."}}};
        t::DelegateContext context = world.context();
        int asked = 0;
        context.parent.confirm = [&asked](const apogee::agent::GateRequest& request) {
            ++asked;
            CHECK(request.tool == "poke");
            return true;
        };
        const apogee::agent::ToolOutcome outcome =
            t::run_delegate(context, R"({"brief": "Change it"})");
        CHECK_FALSE(outcome.is_error);
        CHECK(asked == 1);
        CHECK(world.poked == 1);
    }
    SECTION("nobody to ask: denied, as the parent's own call would be, and the delegate goes on") {
        World world{{MockTurn{.tool_calls = {call("poke")}},
                     MockTurn{.text = "Could not: {{last_tool_result}}"}}};
        const apogee::agent::ToolOutcome outcome =
            t::run_delegate(world.context(), R"({"brief": "Change it"})");
        CHECK_FALSE(outcome.is_error);
        CHECK(world.poked == 0);
        CHECK_THAT(outcome.content, ContainsSubstring("Could not: "));
    }
}

TEST_CASE("each sub-call's states reach the parent's surface, under the id its question names",
          "[tools][delegate][gate][calls]") {
    // 56a's review: the sub-session asked the parent's gate under its call's
    // id, but its states stopped at the delegation -- an ACP host held a
    // permission card no tool_call ever drew or ended.
    using State = apogee::agent::ToolCallState;
    const auto delegated = [](bool allow) {
        World world{
            {MockTurn{.tool_calls = {ToolCall{.id = "s-1", .name = "poke", .arguments = "{}"}}},
             MockTurn{.text = "Done: {{last_tool_result}}"}}};
        Recorder said;
        t::DelegateContext context = world.context(&said);
        std::vector<std::string> asked;
        context.parent.confirm = [&asked, allow](const apogee::agent::GateRequest& request) {
            asked.emplace_back(request.call_id);
            return allow;
        };
        const apogee::agent::ToolOutcome outcome =
            t::run_delegate(context, R"({"brief": "Change it"})");
        CHECK_FALSE(outcome.is_error);
        CHECK(asked == std::vector<std::string>{"s-1"});
        CHECK(world.poked == (allow ? 1 : 0));
        return said.states;
    };
    using Moves = std::vector<std::pair<std::string, State>>;
    CHECK(delegated(false) == Moves{{"s-1", State::Pending}, {"s-1", State::Failed}});
    CHECK(delegated(true) ==
          Moves{{"s-1", State::Pending}, {"s-1", State::Running}, {"s-1", State::Done}});
}

TEST_CASE("the parent's cancellation stops the delegation, and reaches the parent",
          "[tools][delegate][cancel]") {
    World world{{MockTurn{.text = "never"}}};
    t::DelegateContext context = world.context();
    context.parent.cancellation = apogee::harness::CancellationToken::create();
    context.parent.cancellation.cancel();
    CHECK_THROWS_AS(t::run_delegate(context, R"({"brief": "x"})"), apogee::harness::CancelledError);
}

TEST_CASE("a role runs it on that role's model, and one billed per call is refused",
          "[tools][delegate][role]") {
    World world{{}, {MockTurn{.text = "helper did it"}}};
    const apogee::agent::ToolOutcome on_helper =
        t::run_delegate(world.context(), R"({"brief": "x", "role": "utility"})");
    CHECK_THAT(on_helper.content,
               ContainsSubstring("helper did it") && ContainsSubstring("[delegate on helper: "));
    CHECK(world.mocks["root"]->requests().empty());

    std::string paid{kConfig};
    paid.replace(paid.find("default_utility: helper"),
                 std::string_view{"default_utility: helper"}.size(), "default_utility: paid");
    World billed{{}, {}, paid};
    const apogee::agent::ToolOutcome refused =
        t::run_delegate(billed.context(), R"({"brief": "x", "role": "utility"})");
    CHECK(refused.is_error);
    CHECK_THAT(refused.content,
               ContainsSubstring("the utility role is 'paid', billed per call -- a delegation "
                                 "the model starts never spends"));

    const apogee::agent::ToolOutcome unknown =
        t::run_delegate(world.context(), R"({"brief": "x", "role": "boss"})");
    CHECK_THAT(unknown.content, ContainsSubstring("'boss' is not a role -- one of "));
    CHECK_THAT(t::run_delegate(world.context(), R"({"brief": "  "})").content,
               ContainsSubstring("brief is required"));
}

TEST_CASE("with no turn current, the tool says there is no session to delegate from",
          "[tools][delegate]") {
    World world{{}};
    const apogee::agent::ToolOutcome outcome =
        world.registry.find("delegate")->run(R"({"brief": "x"})");
    CHECK(outcome.is_error);
    CHECK_THAT(outcome.content, ContainsSubstring("there is no session here to delegate from"));
    // Current, it runs.
    const t::DelegateScope scope{world.context()};
    CHECK_FALSE(world.registry.find("delegate")->run(R"({"brief": "x"})").is_error);
}

TEST_CASE("the environment note says when to delegate only where delegate is registered",
          "[tools][delegate]") {
    CHECK_THAT(t::delegate_policy(), ContainsSubstring("the brief is all it gets"));
}
