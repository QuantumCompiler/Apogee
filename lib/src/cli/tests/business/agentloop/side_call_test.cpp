#include "agentloop/side_call.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

/// A turn's other model calls, said when they start and when they are over
/// (26n): what the scope says, and the suffix the terminal completes with.
TEST_CASE("a side call is said when it starts and when it is over, with what it took",
          "[agentloop][side-call]") {
    std::vector<apogee::agentloop::SideCall> said;
    {
        apogee::agentloop::SideCallScope scope{
            [&said](const apogee::agentloop::SideCall& call) { said.push_back(call); }, "rerank",
            "judging 12 results with judge"};
        REQUIRE(said.size() == 1);
        CHECK_FALSE(said.front().done);
        CHECK(said.front().role == "rerank");
        CHECK_FALSE(said.front().seconds.has_value());
        scope.tokens(42);
    }
    REQUIRE(said.size() == 2);
    CHECK(said.back().done);
    CHECK(said.back().detail == "judging 12 results with judge");
    CHECK(said.back().seconds.has_value());
    CHECK(said.back().tokens == 42);
    // A null sink says nothing, and costs nothing.
    {
        const apogee::agentloop::SideCallScope quiet{{}, "utility", "x"};
    }
}

TEST_CASE("only what is known is said on completion", "[agentloop][side-call]") {
    apogee::agentloop::SideCall call;
    CHECK(apogee::agentloop::side_call_suffix(call).empty());
    call.seconds = 0.64;
    CHECK(apogee::agentloop::side_call_suffix(call) == " · 0.6 s");
    call.tokens = 120;
    CHECK(apogee::agentloop::side_call_suffix(call) == " · 0.6 s · 120 tokens");
    call.seconds.reset();
    CHECK(apogee::agentloop::side_call_suffix(call) == " · 120 tokens");
}
