#include "agentloop/member_call.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "backends/mock.h"
#include "contracts/config.h"
#include "contracts/errors.h"
#include "harness/harness.h"

/// The bounded, brief-only member call (27f): the one model-calling path the
/// consult tool, validation (27g) and symphony stages (27q) share -- its wire
/// is exactly the brief, its caps refuse before anything is sent, it is
/// narrated, and no two member calls ever generate at once.
namespace {

using apogee::agentloop::call_member;
using apogee::agentloop::MemberAnswer;
using apogee::agentloop::MemberCall;
using apogee::agentloop::MemberCalls;
using apogee::agentloop::SideCall;
using apogee::backends::MockProvider;
using apogee::backends::MockTurn;
using apogee::harness::ChatRequest;
using apogee::harness::ModelRole;

constexpr std::string_view kSuites = R"YAML(models:
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
  small:
    type: mock
    context_size: 600
suites:
  research:
    members:
      chat: root
      utility: helper
      extraction: scribe
      vision: paid
    consultable: [utility, extraction, vision]
    consult_caps:
      per_turn: 2
  tiny:
    members:
      utility: small
    consultable: [utility]
  quiet:
    members:
      utility: helper
)YAML";

/// A harness over `kSuites` with a mock per backend; `paid` bills per call.
struct Suite {
    apogee::harness::Harness harness{apogee::harness::parse_config(kSuites, "<test>")};
    std::map<std::string, std::shared_ptr<MockProvider>> mocks;

    explicit Suite(const std::string& answer = "Paris.", bool register_scribe = true) {
        for (const char* name : {"root", "helper", "scribe", "paid", "small"}) {
            if (!register_scribe && std::string_view{name} == "scribe") {
                continue;
            }
            MockProvider::Options options;
            options.backend_name = name;
            options.turns = {MockTurn{.text = answer}};
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

MemberCall asking(ModelRole role, std::string brief) {
    MemberCall call;
    call.role = role;
    call.brief = std::move(brief);
    return call;
}

}  // namespace

TEST_CASE("a member's request is exactly the brief", "[agentloop][member_call]") {
    const ChatRequest request =
        apogee::agentloop::member_request("helper", "What is the capital of France?", 512);
    CHECK(request.model == "helper");
    REQUIRE(request.messages.size() == 1);
    CHECK(request.messages.front().role == apogee::harness::Role::User);
    CHECK(request.messages.front().content.plain_text() == "What is the capital of France?");
    CHECK(request.tools.empty());
    CHECK(request.max_tokens == 512);
    CHECK(request.thinking.off());
    CHECK(request.transient.side_request);
    CHECK(request.transient.length == 0);
    CHECK(request.transient.response_schema.empty());
}

TEST_CASE("a member call reaches the member through the one resolver, brief only",
          "[agentloop][member_call]") {
    Suite suite{"  Paris.  \n"};
    std::vector<SideCall> said;
    const MemberAnswer answer =
        call_member(suite.harness, asking(ModelRole::Utility, "What is the capital of France?"),
                    [&said](const SideCall& call) { said.push_back(call); }, {});
    REQUIRE(answer.ok());
    CHECK(answer.backend == "helper");
    CHECK(answer.text == "Paris.");
    CHECK_FALSE(answer.cut);
    // The wire: one user message, the brief, nothing else -- on the suite's
    // utility member, and on no one else.
    REQUIRE(suite.mocks["helper"]->requests().size() == 1);
    const ChatRequest& sent = suite.mocks["helper"]->requests().front();
    REQUIRE(sent.messages.size() == 1);
    CHECK(sent.messages.front().content.plain_text() == "What is the capital of France?");
    CHECK(sent.tools.empty());
    CHECK(suite.sent() == 1);
    // Narrated as 26n's side calls are: when it starts, and when it is over.
    REQUIRE(said.size() == 2);
    CHECK(said[0].role == "consult");
    CHECK(said[0].detail == "asking utility (helper): What is the capital of France?");
    CHECK_FALSE(said[0].done);
    CHECK(said[1].done);
    CHECK(said[1].seconds.has_value());
}

TEST_CASE("what a member call refuses, it refuses before anything is sent",
          "[agentloop][member_call]") {
    const Suite suite;
    SECTION("a member billed per call") {
        const MemberAnswer answer =
            call_member(suite.harness, asking(ModelRole::Vision, "hi"), {}, {});
        CHECK_THAT(answer.refused,
                   Catch::Matchers::ContainsSubstring("'paid' (vision) is billed per call"));
        CHECK(answer.backend == "paid");
    }
    SECTION("a brief over its cap") {
        MemberCall call = asking(ModelRole::Utility, std::string(std::size_t{4} * 40, 'w'));
        call.brief_tokens = 32;
        const MemberAnswer answer = call_member(suite.harness, call, {}, {});
        CHECK(answer.refused ==
              "the question is about 40 tokens, and a brief is at most 32 -- shorten it to what "
              "the member needs and ask again");
    }
    SECTION("an empty brief") {
        CHECK_THAT(call_member(suite.harness, asking(ModelRole::Utility, " \n"), {}, {}).refused,
                   Catch::Matchers::ContainsSubstring("the question is empty"));
    }
    SECTION("a brief and its answer that do not fit the member's window") {
        Suite tiny;
        tiny.harness.set_active_suite("tiny");
        MemberCall call = asking(ModelRole::Utility, std::string(std::size_t{4} * 100, 'w'));
        const MemberAnswer answer = call_member(tiny.harness, call, {}, {});
        CHECK_THAT(answer.refused, Catch::Matchers::ContainsSubstring(
                                       "'small' runs at a 600-token window, too small"));
        CHECK(tiny.sent() == 0);
        call.answer_tokens = 400;
        CHECK(call_member(tiny.harness, call, {}, {}).ok());
    }
    SECTION("a member whose backend was not built here") {
        const Suite partial{"x", /*register_scribe=*/false};
        const MemberAnswer answer =
            call_member(partial.harness, asking(ModelRole::Extraction, "hi"), {}, {});
        CHECK_THAT(answer.refused,
                   Catch::Matchers::ContainsSubstring("'scribe' (extraction) could not be built"));
        CHECK(partial.sent() == 0);
    }
    SECTION("a role nothing answers for") {
        const apogee::harness::Harness bare{apogee::harness::Config{}};
        CHECK_THAT(call_member(bare, asking(ModelRole::Utility, "hi"), {}, {}).refused,
                   Catch::Matchers::ContainsSubstring("no backend answers for utility"));
    }
    CHECK(suite.sent() == 0);
}

TEST_CASE("a member's answer says when it was cut, what it took, and when it failed",
          "[agentloop][member_call]") {
    apogee::harness::Harness harness{apogee::harness::parse_config(kSuites, "<test>")};
    MockProvider::Options options;
    options.backend_name = "helper";
    options.turns = {MockTurn{.text = "a long answer",
                              .finish_reason = apogee::harness::FinishReason::Length,
                              .usage = {.prompt_tokens = 9, .completion_tokens = 512}},
                     MockTurn{.text = "   "}};
    auto helper = std::make_shared<MockProvider>(std::move(options));
    harness.register_provider("helper", helper);
    harness.use_default_router();
    std::vector<SideCall> said;
    const MemberAnswer cut =
        call_member(harness, asking(ModelRole::Utility, "tell me everything"),
                    [&said](const SideCall& call) { said.push_back(call); }, {});
    REQUIRE(cut.ok());
    CHECK(cut.cut);
    CHECK(cut.tokens == 512);
    REQUIRE(said.size() == 2);
    CHECK(said[1].tokens == 512);
    // An answer of nothing is a failure, said, never an empty success.
    const MemberAnswer blank = call_member(harness, asking(ModelRole::Utility, "again"), {}, {});
    CHECK(blank.failed == "'helper' gave no answer");
    // Cancelled: thrown, never swallowed into a failure.
    const apogee::harness::CancellationToken cancelled =
        apogee::harness::CancellationToken::create();
    cancelled.cancel();
    CHECK_THROWS_AS(call_member(harness, asking(ModelRole::Utility, "x"), {}, cancelled),
                    apogee::harness::CancelledError);
}

TEST_CASE("no two member calls ever generate at once", "[agentloop][member_call]") {
    Suite suite;
    std::atomic<int> inflight{0};
    std::atomic<int> most{0};
    const auto slow = [&](const ChatRequest&) {
        const int now = ++inflight;
        int seen = most.load();
        while (now > seen && !most.compare_exchange_weak(seen, now)) {
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{60});
        --inflight;
    };
    // Two members, one thread each, so nothing but the gate keeps them apart.
    for (const char* name : {"helper", "scribe"}) {
        MockProvider::Options options;
        options.backend_name = name;
        options.turns = {MockTurn{.text = "ok"}};
        options.on_request = slow;
        suite.mocks[name] = std::make_shared<MockProvider>(std::move(options));
        suite.harness.register_provider(name, suite.mocks[name]);
    }
    suite.harness.use_default_router();
    std::vector<std::thread> threads;
    for (int round = 0; round < 3; ++round) {
        threads.emplace_back([&suite] {
            (void)call_member(suite.harness, asking(ModelRole::Utility, "a"), {}, {});
        });
        threads.emplace_back([&suite] {
            (void)call_member(suite.harness, asking(ModelRole::Extraction, "b"), {}, {});
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    CHECK(suite.mocks["helper"]->requests().size() == 3);
    CHECK(suite.mocks["scribe"]->requests().size() == 3);
    CHECK(most.load() == 1);
}

TEST_CASE("a turn's member calls are counted against the suite's cap, and refused past it",
          "[agentloop][member_call]") {
    Suite suite;
    MemberCalls calls{suite.harness};
    // No turn open: never run unbounded.
    CHECK(calls.consult("utility", "x").refused == "no turn is open to make a member call in");
    std::vector<SideCall> said;
    {
        const MemberCalls::Turn turn =
            calls.begin_turn([&said](const SideCall& call) { said.push_back(call); }, {});
        CHECK(calls.in_turn());
        CHECK_THROWS_AS((void)calls.begin_turn({}, {}), std::logic_error);
        CHECK(calls.consult("utility", "one").ok());
        // A refusal of the call's own -- an empty brief -- spends nothing.
        CHECK_FALSE(calls.consult("utility", "  ").refused.empty());
        CHECK(calls.used() == 1);
        CHECK(calls.consult("extraction", "two").ok());
        // The suite caps this at two (`consult_caps.per_turn`).
        const MemberAnswer third = calls.consult("utility", "three");
        CHECK(third.refused ==
              "consult budget spent this turn: 2 of 2 member calls made -- answer with what "
              "you have");
        CHECK(suite.sent() == 2);
        // Not a consultable role, or not a role at all.
        CHECK_THAT(calls.consult("chat", "x").refused,
                   Catch::Matchers::ContainsSubstring(
                       "'chat' is not a member you can consult (consultable: utility, "
                       "extraction, vision)"));
        CHECK_FALSE(calls.consult("root", "x").refused.empty());
        // The narration went where the turn said.
        CHECK(said.size() == 4);
    }
    CHECK_FALSE(calls.in_turn());
    // A new turn starts over.
    {
        const MemberCalls::Turn turn = calls.begin_turn({}, {});
        CHECK(calls.used() == 0);
        CHECK(calls.consult("utility", "again").ok());
    }
    // With the defaults where the suite sets none, and nothing where the
    // suite designates no one.
    suite.harness.set_active_suite("tiny");
    {
        const MemberCalls::Turn turn = calls.begin_turn({}, {});
        for (int i = 0; i < 4; ++i) {
            CHECK(calls.consult("utility", "q").ok());
        }
        CHECK_THAT(calls.consult("utility", "q").refused,
                   Catch::Matchers::ContainsSubstring("4 of 4 member calls made"));
    }
    suite.harness.set_active_suite("quiet");
    {
        const MemberCalls::Turn turn = calls.begin_turn({}, {});
        CHECK(calls.consult("utility", "q").refused ==
              "no member can be consulted: the active suite designates none");
    }
}

TEST_CASE("the active suite's consultable members are resolved and asked whether they can run",
          "[agentloop][member_call]") {
    Suite suite;
    const std::vector<apogee::agentloop::ConsultableMember> members =
        apogee::agentloop::consultable_members(suite.harness);
    REQUIRE(members.size() == 3);
    CHECK(members[0].role == "utility");
    CHECK(members[0].backend == "helper");
    CHECK(members[0].unavailable.empty());
    CHECK(members[1].role == "extraction");
    CHECK(members[1].backend == "scribe");
    CHECK(members[2].role == "vision");
    CHECK_THAT(members[2].unavailable, Catch::Matchers::ContainsSubstring("billed per call"));
    suite.harness.set_active_suite("quiet");
    CHECK(apogee::agentloop::consultable_members(suite.harness).empty());
    suite.harness.set_active_suite("");
    CHECK(apogee::agentloop::consultable_members(suite.harness).empty());
}

TEST_CASE("the narration names the member and shows the brief's first words on one line",
          "[agentloop][member_call]") {
    using apogee::agentloop::member_call_detail;
    CHECK(member_call_detail("utility", "l3b", "What is\n  the codeword?") ==
          "asking utility (l3b): What is the codeword?");
    const std::string long_brief =
        "Summarise the following design note in two sentences for a reviewer who has not read "
        "it yet";
    const std::string detail = member_call_detail("utility", "l3b", long_brief);
    CHECK(detail ==
          "asking utility (l3b): Summarise the following design note in two "
          "sentences for a…");
}
