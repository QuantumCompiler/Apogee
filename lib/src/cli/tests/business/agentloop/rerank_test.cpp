#include "agentloop/rerank.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "backends/mock.h"
#include "contracts/cancellation.h"
#include "contracts/config.h"
#include "contracts/errors.h"
#include "harness/harness.h"

/// The reranker's never-fail contract: every failure path returns raw order
/// with `applied = false`, an applied verdict is flagged, and a working judge
/// visibly reorders.
namespace {

using apogee::agentloop::kRerankMaxCandidates;
using apogee::agentloop::parse_rerank_ids;
using apogee::agentloop::rerank;
using apogee::agentloop::rerank_fetch_limit;
using apogee::agentloop::RerankChoice;
using apogee::agentloop::RerankOutcome;
using apogee::agentloop::resolve_turn_rerank;
using apogee::backends::MockProvider;
using apogee::backends::MockTurn;
using apogee::embedstore::SearchHit;

std::vector<SearchHit> five_hits() {
    std::vector<SearchHit> hits;
    for (int i = 1; i <= 5; ++i) {
        SearchHit hit;
        hit.chunk.id = i;
        hit.chunk.text = "passage number " + std::to_string(i);
        hit.score = 1.0 / i;
        hit.retriever = "lexical";
        hits.push_back(hit);
    }
    return hits;
}

/// A harness whose `judge` backend answers with `reply`.
struct Judge {
    apogee::harness::Harness harness{apogee::harness::Config{}};
    MockProvider* provider = nullptr;

    explicit Judge(std::string reply) {
        MockProvider::Options options;
        options.backend_name = "judge";
        options.turns = {MockTurn{.text = std::move(reply)}};
        auto owned = std::make_shared<MockProvider>(std::move(options));
        provider = owned.get();
        harness.register_provider("judge", owned);
        harness.use_default_router();
    }
};

apogee::harness::Config config_with(std::initializer_list<const char*> names) {
    apogee::harness::Config config;
    for (const char* name : names) {
        apogee::harness::BackendConfig backend;
        backend.type = apogee::harness::BackendType::Mock;
        config.backends.emplace(name, backend);
    }
    return config;
}

}  // namespace

// --- the parser ------------------------------------------------------------------

TEST_CASE("the ranking is read out of prose and fences, dropping junk ids",
          "[agentloop][rerank][parse]") {
    CHECK(parse_rerank_ids("[3, 1]", 5) == std::vector<int>{3, 1});
    CHECK(parse_rerank_ids("Sure! Here you go:\n```json\n[2, 5, 2, 9, 0, -1]\n```", 5) ==
          std::vector<int>{2, 5});
    // An empty array is a verdict, not a parse failure.
    const auto none = parse_rerank_ids("[]", 5);
    REQUIRE(none.has_value());
    CHECK(none->empty());
    // No array at all is the failure.
    CHECK_FALSE(parse_rerank_ids("I think passage 3 is best.", 5).has_value());
    CHECK_FALSE(parse_rerank_ids("[not json", 5).has_value());
    CHECK_FALSE(parse_rerank_ids(R"({"ranking": 1})", 5).has_value());
}

TEST_CASE("a reply that is not one usable ranking is no verdict, and only [] drops everything",
          "[agentloop][rerank][parse][57r]") {
    // The second sweep's probe (57r): `["2", "1"]` and a prose mention of a
    // passage the judge did not see were read as "none help", and every
    // chunk was dropped; a reply naming a passage before its ranking was
    // applied as the one-item ranking it named.
    using apogee::agentloop::read_rerank_reply;

    // A whole number written as a string is that id.
    CHECK(parse_rerank_ids(R"(["2","1"])", 2) == std::vector<int>{2, 1});
    CHECK(parse_rerank_ids(R"([" 2 ", 1])", 2) == std::vector<int>{2, 1});

    // Entries, none of them usable: not a ranking.
    for (const std::string_view reply :
         {"[3]", R"(["x"])", "[0, -1]", "[1.5]", R"(["-1", "2x", ""])",
          R"(["99999999999999999999"])", "[[1, 2]]"}) {
        INFO(reply);
        CHECK_FALSE(parse_rerank_ids(reply, 2).has_value());
        CHECK(read_rerank_reply(reply, 2).problem.find("no usable passage number") !=
              std::string::npos);
    }

    // More than one array: which one ranks is a guess, whatever the count.
    for (const std::size_t candidates : {std::size_t{2}, std::size_t{5}}) {
        INFO(candidates);
        const auto reading =
            read_rerank_reply("Passage [3] is off-topic; the ranking is [2, 1]", candidates);
        CHECK_FALSE(reading.ids.has_value());
        CHECK(reading.problem == "returned 2 bracketed lists, so which one ranks is a guess");
    }

    // The verdict is still a verdict, and a fence holds one array.
    const auto none = parse_rerank_ids("[]", 2);
    REQUIRE(none.has_value());
    CHECK(none->empty());
    CHECK(parse_rerank_ids("```json\n[2, 1]\n```", 2) == std::vector<int>{2, 1});
    // Bracketed prose is not an array: the one array after it ranks.
    CHECK(parse_rerank_ids("[see below] -- my ranking: [2, 1]", 2) == std::vector<int>{2, 1});
    // No array at all is said as before.
    CHECK(read_rerank_reply("passage two", 2).problem ==
          "returned something that was not a ranking");
}

// --- which judge ------------------------------------------------------------------

TEST_CASE("the rerank backend is the flag, else the pin, else none; off wins; unknown disables",
          "[agentloop][rerank][resolve]") {
    const apogee::harness::Config config = config_with({"haiku", "judge"});
    CHECK(resolve_turn_rerank("haiku", "judge", config).backend == "haiku");
    CHECK(resolve_turn_rerank("", "judge", config).backend == "judge");
    CHECK(resolve_turn_rerank("", "", config).backend.empty());
    CHECK(resolve_turn_rerank("off", "judge", config).backend.empty());
    CHECK(resolve_turn_rerank("off", "judge", config).note.empty());
    CHECK(resolve_turn_rerank("", "off", config).backend.empty());

    const RerankChoice missing = resolve_turn_rerank("ghost", "", config);
    CHECK(missing.backend.empty());
    CHECK(missing.note.find("'ghost'") != std::string::npos);
    CHECK(missing.note.find("not configured") != std::string::npos);
}

TEST_CASE("rerank on is the utility model, else the chat's own, through the one resolver",
          "[agentloop][rerank][resolve][helpers]") {
    apogee::harness::Config config = config_with({"chatty", "helper"});
    // No utility model: the conversation's backend judges.
    CHECK(resolve_turn_rerank("on", "", config, "chatty").backend == "chatty");
    CHECK(resolve_turn_rerank("", "on", config, "chatty").backend == "chatty");
    // A utility model judges wherever the chat is.
    config.models.default_utility = "helper";
    CHECK(resolve_turn_rerank("on", "", config, "chatty").backend == "helper");
    // Off still wins, and a named backend is still itself.
    CHECK(resolve_turn_rerank("off", "on", config, "chatty").backend.empty());
    CHECK(resolve_turn_rerank("chatty", "", config, "x").backend == "chatty");

    // Nothing to judge with: disabled, and said.
    const apogee::harness::Config none = config_with({"x"});
    const RerankChoice nothing = resolve_turn_rerank("on", "", none, "");
    CHECK(nothing.backend.empty());
    CHECK(nothing.note.find("no utility model or chat backend") != std::string::npos);
    // A pointer at nothing is caught like a named backend is.
    apogee::harness::Config ghost = config_with({"x"});
    ghost.models.default_utility = "ghost";
    CHECK(resolve_turn_rerank("on", "", ghost, "x").note.find("'ghost'") != std::string::npos);
}

TEST_CASE("one validator decides what a rerank setting may be", "[agentloop][rerank][helpers]") {
    using apogee::agentloop::valid_rerank;
    const apogee::harness::Config config = config_with({"judge"});
    CHECK(valid_rerank("", config));
    CHECK(valid_rerank("off", config));
    CHECK(valid_rerank("on", config));
    CHECK(valid_rerank("judge", config));
    CHECK_FALSE(valid_rerank("ghost", config));
    CHECK_FALSE(valid_rerank("On", config));
}

TEST_CASE("the candidate set is widened tenfold and capped", "[agentloop][rerank]") {
    CHECK(rerank_fetch_limit(4, false) == 4);
    CHECK(rerank_fetch_limit(4, true) == 40);
    CHECK(rerank_fetch_limit(8, true) == kRerankMaxCandidates);
    CHECK(rerank_fetch_limit(0, true) == 0);
}

// --- the contract, one failure path per case ------------------------------------------

TEST_CASE("a working judge visibly reorders and is flagged applied", "[agentloop][rerank]") {
    Judge judge{"[4, 2]"};
    const RerankOutcome out = rerank(judge.harness, "judge", "which passage?", five_hits(), 3, {});
    CHECK(out.applied);
    REQUIRE(out.hits.size() == 2);
    CHECK(out.hits[0].chunk.id == 4);
    CHECK(out.hits[1].chunk.id == 2);
    CHECK(out.note.empty());
    // The judge saw numbered candidates and the question.
    REQUIRE(judge.provider->requests().size() == 1);
    // Not a turn of the conversation: a chat judging with its own local
    // model keeps its cache (26b).
    CHECK(judge.provider->requests()[0].transient.side_request);
    const std::string prompt = judge.provider->requests()[0].messages.front().content.plain_text();
    CHECK(prompt.find("[1] passage number 1") != std::string::npos);
    CHECK(prompt.find("which passage?") != std::string::npos);
}

TEST_CASE("a judge that drops everything is a verdict: nothing injected, applied true",
          "[agentloop][rerank]") {
    // The relevance floor BM25 lacks. Not a failure, so it is NOT raw order.
    Judge judge{"[]"};
    const RerankOutcome out = rerank(judge.harness, "judge", "q", five_hits(), 3, {});
    CHECK(out.applied);
    CHECK(out.hits.empty());
}

TEST_CASE("garbage from the judge degrades to raw order with applied false",
          "[agentloop][rerank][degrade]") {
    Judge judge{"The most relevant passage is the third one."};
    const RerankOutcome out = rerank(judge.harness, "judge", "q", five_hits(), 3, {});
    CHECK_FALSE(out.applied);
    REQUIRE(out.hits.size() == 3);
    CHECK(out.hits[0].chunk.id == 1);  // raw order, cut to the limit
    CHECK(out.note.find("not a ranking") != std::string::npos);
}

TEST_CASE("a judge the harness cannot route degrades to raw order",
          "[agentloop][rerank][degrade]") {
    Judge judge{"[1]"};
    const RerankOutcome out = rerank(judge.harness, "no-such-backend", "q", five_hits(), 3, {});
    CHECK_FALSE(out.applied);
    CHECK(out.hits.size() == 3);
    CHECK(out.note.find("failed") != std::string::npos);
}

TEST_CASE("no judge at all degrades to raw order without a call", "[agentloop][rerank][degrade]") {
    Judge judge{"[5]"};
    const RerankOutcome out = rerank(judge.harness, "", "q", five_hits(), 2, {});
    CHECK_FALSE(out.applied);
    CHECK(out.hits.size() == 2);
    CHECK(out.hits[0].chunk.id == 1);
    CHECK(judge.provider->requests().empty());
}

TEST_CASE("one candidate is never judged", "[agentloop][rerank]") {
    Judge judge{"[1]"};
    const RerankOutcome out = rerank(judge.harness, "judge", "q", {five_hits().front()}, 3, {});
    CHECK_FALSE(out.applied);
    CHECK(out.hits.size() == 1);
    CHECK(judge.provider->requests().empty());
}

TEST_CASE("the judge never sees more than the cap", "[agentloop][rerank]") {
    Judge judge{"[1]"};
    std::vector<SearchHit> many;
    for (int i = 1; i <= 80; ++i) {
        SearchHit hit;
        hit.chunk.id = i;
        hit.chunk.text = "p" + std::to_string(i);
        many.push_back(hit);
    }
    (void)rerank(judge.harness, "judge", "q", many, 5, {});
    const std::string prompt = judge.provider->requests()[0].messages.front().content.plain_text();
    CHECK(prompt.find("[50] p50") != std::string::npos);
    CHECK(prompt.find("[51] p51") == std::string::npos);
}

TEST_CASE("a reply that is not one usable ranking keeps the chunks in raw order, and says why",
          "[agentloop][rerank][degrade][57r]") {
    struct Case {
        std::string reply{};
        std::size_t candidates = 0;
        std::string said{};
    };

    const std::vector<Case> cases{
        {R"(["x"])", 2, "no usable passage number"},
        {"[3]", 2, "no usable passage number"},
        {"[0, -1]", 2, "no usable passage number"},
        {"[1.5]", 2, "no usable passage number"},
        {"Passage [3] is off-topic; the ranking is [2, 1]", 2, "2 bracketed lists"},
        {"Passage [3] is off-topic; the ranking is [2, 1]", 5, "2 bracketed lists"},
    };
    for (const Case& c : cases) {
        INFO(c.reply << " over " << c.candidates);
        Judge judge{c.reply};
        std::vector<SearchHit> hits = five_hits();
        hits.resize(c.candidates);
        const RerankOutcome out = rerank(judge.harness, "judge", "q", hits, 2, {});
        CHECK_FALSE(out.applied);
        REQUIRE(out.hits.size() == 2);
        CHECK(out.hits[0].chunk.id == 1);  // raw order, cut to the limit
        CHECK(out.hits[1].chunk.id == 2);
        CHECK(out.note.find("rerank via 'judge' ") == 0);
        CHECK(out.note.find(c.said) != std::string::npos);
        CHECK(out.note.find("using raw retrieval order") != std::string::npos);
    }

    // A quoted ranking reorders, and is applied.
    Judge quoted{R"(["2", "1"])"};
    std::vector<SearchHit> two = five_hits();
    two.resize(2);
    const RerankOutcome reordered = rerank(quoted.harness, "judge", "q", two, 2, {});
    CHECK(reordered.applied);
    REQUIRE(reordered.hits.size() == 2);
    CHECK(reordered.hits[0].chunk.id == 2);
    CHECK(reordered.note.empty());
}

TEST_CASE("a cancelled judge is the turn's cancel, never raw order",
          "[agentloop][rerank][cancel][57r]") {
    // `rerank` caught every exception as a broken judge, the cancel too: a
    // stopped turn went on, said as "using raw retrieval order".
    Judge judge{"[2, 1]"};
    const apogee::harness::CancellationToken stop = apogee::harness::CancellationToken::create();
    stop.cancel();
    CHECK_THROWS_AS(rerank(judge.harness, "judge", "q", five_hits(), 3, stop),
                    apogee::harness::CancelledError);
}
