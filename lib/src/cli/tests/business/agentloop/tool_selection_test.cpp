#include "agentloop/tool_selection.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "agent/tool.h"
#include "agentloop/loop.h"
#include "backends/mock.h"
#include "contracts/config.h"
#include "contracts/errors.h"

/// Tool selection by relevance (26g): the lexical ranking as a table, the
/// vector ranking over scripted embeddings and its cache, the selection's
/// rules, and the loop offering the chosen few -- with everything else
/// still dispatched and gated when named.
namespace {

using apogee::agent::Permission;
using apogee::agent::Tool;
using apogee::agent::ToolOutcome;
using apogee::agent::ToolRegistry;
using apogee::agentloop::RankedTool;
using apogee::agentloop::ToolEmbedding;
using apogee::agentloop::ToolOffer;
using apogee::agentloop::ToolRanker;
using apogee::agentloop::ToolSelection;
using apogee::agentloop::ToolVectors;
using apogee::backends::MockProvider;
using apogee::backends::MockTurn;
using apogee::harness::ChatMessage;
using apogee::harness::ChatRequest;
using apogee::harness::ToolCall;

struct Spec {
    std::string name;
    std::string description;
    bool writes = false;
};

/// The native toolsets' shape, and two MCP servers' tools beside them.
const std::vector<Spec>& specs() {
    static const std::vector<Spec> all{
        {"collection_info", "Describe a document collection: its size and model"},
        {"delete_file", "Delete a file from the working folder", true},
        {"delete_note", "Delete a saved note", true},
        {"edit_file", "Replace exact text in a file", true},
        {"fetch_url", "Fetch a web page and read it as text"},
        {"git_diff", "Show the changes between two commits or the working tree"},
        {"git_log", "Show the commit history of the repository"},
        {"git_show", "Show one commit and its changes"},
        {"git_status", "Show the repository's working tree status"},
        {"grep_files", "Search the contents of files for a pattern"},
        {"list_collections", "List the document collections"},
        {"list_directory", "List the entries of a folder"},
        {"list_notes", "List the saved notes"},
        {"mcp__calendar__book_meeting", "Book a meeting room for a time slot"},
        {"mcp__tickets__create_ticket", "Open a new ticket in the issue tracker"},
        {"read_file", "Read the lines of a text file"},
        {"read_note", "Read a saved note"},
        {"run_command", "Run a shell command", true},
        {"search_documents", "Search a document collection for passages"},
        {"search_files", "Find files by their name"},
        {"write_file", "Write text to a file", true},
        {"write_note", "Save a note for later", true},
        {"mcp__calendar__list_events", "List the calendar events between two dates"},
        {"mcp__tickets__get_ticket", "Get one ticket by its key"},
    };
    return all;
}

ToolRegistry registry_of(std::size_t count) {
    ToolRegistry registry;
    for (std::size_t index = 0; index < count && index < specs().size(); ++index) {
        Tool tool;
        tool.name = specs()[index].name;
        tool.description = specs()[index].description;
        tool.writes = specs()[index].writes;
        tool.run = [name = tool.name](std::string_view) { return ToolOutcome{"ran " + name}; };
        registry.add(std::move(tool));
    }
    return registry;
}

ToolRegistry full_registry() {
    return registry_of(specs().size());
}

std::shared_ptr<const ToolRanker> words_ranker(const ToolRegistry& registry) {
    return std::make_shared<const ToolRanker>(registry.definitions(), registry.definition_hashes(),
                                              std::nullopt);
}

std::vector<std::string> names_of(const std::vector<RankedTool>& ranking, std::size_t count) {
    std::vector<std::string> out;
    for (std::size_t index = 0; index < count && index < ranking.size(); ++index) {
        out.push_back(ranking[index].name);
    }
    return out;
}

std::vector<std::string> tool_names(const ChatRequest& request) {
    std::vector<std::string> out;
    for (const auto& tool : request.tools) {
        out.push_back(tool.name);
    }
    return out;
}

bool offers(const ChatRequest& request, std::string_view name) {
    return std::ranges::any_of(
        request.tools, [name](const apogee::harness::Tool& tool) { return tool.name == name; });
}

/// A scripted embedder: one dimension per concept, set when the text
/// mentions it -- so the vectors' meaning is the test's to choose.
struct ScriptedEmbedder {
    std::vector<std::vector<std::string>> concepts{
        {"meeting", "room", "slot", "schedule"}, {"ticket", "tracker", "bug", "issue"},
        {"commit", "history", "repository"},     {"note", "remember"},
        {"web", "page", "url", "site"},          {"file", "folder"},
    };
    int calls = 0;
    std::size_t texts = 0;

    apogee::agentloop::EmbedFunc func() {
        return [this](const std::vector<std::string>& batch,
                      const apogee::harness::CancellationToken&) {
            ++calls;
            texts += batch.size();
            std::vector<std::vector<float>> out;
            for (const std::string& text : batch) {
                std::vector<float> vector(concepts.size(), 0.0F);
                for (std::size_t dimension = 0; dimension < concepts.size(); ++dimension) {
                    for (const std::string& word : concepts[dimension]) {
                        if (text.find(word) != std::string::npos) {
                            vector[dimension] += 1.0F;
                        }
                    }
                }
                out.push_back(std::move(vector));
            }
            return out;
        };
    }
};

struct LoopFixture {
    std::shared_ptr<MockProvider> provider;
    std::unique_ptr<apogee::harness::Harness> harness;
    std::vector<ChatMessage> history;
};

LoopFixture loop_fixture(std::vector<MockTurn> turns, std::string question) {
    MockProvider::Options options;
    options.backend_name = "mock";
    options.turns = std::move(turns);
    LoopFixture fixture;
    fixture.provider = std::make_shared<MockProvider>(std::move(options));
    fixture.harness = std::make_unique<apogee::harness::Harness>(apogee::harness::Config{});
    fixture.harness->register_provider("mock", fixture.provider);
    fixture.harness->use_default_router();
    fixture.history = {ChatMessage::user(std::move(question))};
    return fixture;
}

MockTurn answer(std::string text) {
    return MockTurn{std::move(text), {}, apogee::harness::FinishReason::Stop, {}};
}

MockTurn calls(std::vector<ToolCall> made) {
    return MockTurn{"", std::move(made), apogee::harness::FinishReason::ToolCalls, {}};
}

ToolCall call(std::string id, std::string name, std::string arguments = "{}") {
    ToolCall made;
    made.id = std::move(id);
    made.name = std::move(name);
    made.arguments = std::move(arguments);
    return made;
}

}  // namespace

TEST_CASE("the words ranking puts the tool that shares the question's words first",
          "[agentloop][tool_selection]") {
    const ToolRegistry registry = full_registry();
    const std::vector<apogee::harness::Tool> tools = registry.definitions();

    struct Row {
        std::string question;
        std::string first;
    };

    const std::vector<Row> table{
        {"show me the commit history", "git_log"},
        {"open a ticket about the crash", "mcp__tickets__create_ticket"},
        {"book a room for the 3pm slot", "mcp__calendar__book_meeting"},
        {"fetch this web page", "fetch_url"},
        {"list what is in this folder", "list_directory"},
        {"run the build command in the shell", "run_command"},
    };
    for (const Row& row : table) {
        INFO(row.question);
        const auto ranking = apogee::agentloop::rank_by_words(tools, row.question);
        REQUIRE_FALSE(ranking.empty());
        CHECK(ranking.front().name == row.first);
    }
    // Plurals meet their singular, and a name's own words count.
    CHECK(apogee::agentloop::rank_by_words(tools, "my saved notes").front().name.ends_with("note"));
    CHECK(apogee::agentloop::rank_by_words(tools, "create tickets").front().name ==
          "mcp__tickets__create_ticket");
    // A question sharing nothing scores nothing, and the order is the names'.
    const auto nothing = apogee::agentloop::rank_by_words(tools, "zzz qqq");
    CHECK(nothing.front().score == 0.0);
    CHECK(nothing.front().name == "collection_info");
}

TEST_CASE("a tool's words are its split name and its description", "[agentloop][tool_selection]") {
    apogee::harness::Tool tool;
    tool.name = "mcp__tickets__create_ticket";
    tool.description = "Open a ticket";
    CHECK(apogee::agentloop::tool_text(tool) == "mcp tickets create ticket: Open a ticket");
}

TEST_CASE("vectors rank by meaning, made once and cached by definition",
          "[agentloop][tool_selection][cache]") {
    const ToolRegistry registry = full_registry();
    ScriptedEmbedder embedder;
    ToolVectors cache;
    int loads = 0;
    int stores = 0;
    const auto embedding = [&] {
        return ToolEmbedding{
            .embed = embedder.func(),
            .model = "local/embed",
            .load = [&](const std::string& key) -> std::optional<std::vector<float>> {
                ++loads;
                const auto found = cache.find(key);
                if (found == cache.end()) {
                    return std::nullopt;
                }
                return found->second;
            },
            .store =
                [&](const ToolVectors& made) {
                    ++stores;
                    for (const auto& [key, vector] : made) {
                        cache.insert_or_assign(key, vector);
                    }
                }};
    };

    const ToolRanker first{registry.definitions(), registry.definition_hashes(), embedding()};
    // "schedule" is no word of the tool's: only the vectors find it.
    const auto ranking = first.rank("schedule something for tomorrow", {});
    CHECK(first.semantic());
    CHECK(ranking.front().name == "mcp__calendar__book_meeting");
    CHECK(embedder.texts == registry.size() + 1);  // every tool, then the question
    CHECK(stores == 1);
    CHECK(cache.size() == registry.size());
    for (const auto& [key, vector] : cache) {
        CHECK(key.starts_with("local/embed:"));
    }

    // Asked again, only the question is embedded, and the cache is not read.
    const int loaded = loads;
    (void)first.rank("a bug report", {});
    CHECK(embedder.texts == registry.size() + 2);
    CHECK(loads == loaded);

    // A second ranker over the same definitions reads the cache.
    embedder.texts = 0;
    const ToolRanker second{registry.definitions(), registry.definition_hashes(), embedding()};
    CHECK(second.rank("a bug in the tracker", {}).front().name == "mcp__tickets__create_ticket");
    CHECK(embedder.texts == 1);

    // A changed definition is a new key: that one tool is embedded again.
    std::vector<apogee::harness::Tool> changed = registry.definitions();
    auto hashes = registry.definition_hashes();
    for (auto& tool : changed) {
        if (tool.name == "read_note") {
            tool.description = "Read a saved note aloud";
            hashes[tool.name] = apogee::agent::definition_hash(tool);
        }
    }
    embedder.texts = 0;
    const ToolRanker third{changed, hashes, embedding()};
    (void)third.rank("anything", {});
    CHECK(embedder.texts == 2);  // read_note, then the question
}

TEST_CASE("a definition's hash changes with its name, description or arguments",
          "[agentloop][tool_selection][cache]") {
    apogee::harness::Tool tool{"read_file", "Read a file", R"({"type":"object"})"};
    const std::string base = apogee::agent::definition_hash(tool);
    CHECK(base.size() == 64);
    auto renamed = tool;
    renamed.name = "read_files";
    auto described = tool;
    described.description = "Read a whole file";
    auto argued = tool;
    argued.parameters_schema = R"({"type":"object","properties":{}})";
    CHECK(apogee::agent::definition_hash(renamed) != base);
    CHECK(apogee::agent::definition_hash(described) != base);
    CHECK(apogee::agent::definition_hash(argued) != base);
    // Where one field ends is part of it.
    CHECK(apogee::agent::definition_hash({"ab", "c", "{}"}) !=
          apogee::agent::definition_hash({"a", "bc", "{}"}));
}

TEST_CASE("an embedder that fails ranks by words, and says why", "[agentloop][tool_selection]") {
    const ToolRegistry registry = full_registry();
    int attempts = 0;
    ToolEmbedding embedding;
    embedding.model = "local/embed";
    embedding.embed =
        [&attempts](const std::vector<std::string>&,
                    const apogee::harness::CancellationToken&) -> std::vector<std::vector<float>> {
        ++attempts;
        throw std::runtime_error("model not loaded");
    };
    const ToolRanker ranker{registry.definitions(), registry.definition_hashes(), embedding};
    const auto ranking = ranker.rank("show the commit history", {});
    CHECK_FALSE(ranker.semantic());
    CHECK(ranking.front().name == "git_log");
    CHECK(ranker.fallback_reason().find("model not loaded") != std::string::npos);
    // Tried once: every later turn ranks by words without asking again.
    (void)ranker.rank("a bug report", {});
    CHECK(attempts == 1);
}

TEST_CASE("selection is off at or below the threshold", "[agentloop][tool_selection]") {
    const ToolRegistry small = registry_of(apogee::agentloop::kToolSelectionThreshold);
    ToolSelection off{words_ranker(small), small.size()};
    CHECK_FALSE(off.active());
    const ToolOffer offer = off.begin_turn("show the commit history", {});
    CHECK_FALSE(offer.active);
    CHECK(off.offered("anything"));

    const ToolRegistry large = registry_of(apogee::agentloop::kToolSelectionThreshold + 1);
    CHECK(ToolSelection{words_ranker(large), large.size()}.active());
}

TEST_CASE("with twelve tools a request is exactly what it was", "[agentloop][tool_selection]") {
    const ToolRegistry registry = registry_of(12);
    const auto run_with = [&registry](bool selecting) {
        LoopFixture fixture = loop_fixture({answer("done")}, "show the commit history");
        ToolSelection selection{words_ranker(registry), registry.size()};
        apogee::agentloop::Options options;
        options.model = "mock";
        options.tools = &registry;
        options.tool_selection = selecting ? &selection : nullptr;
        apogee::agentloop::NullReporter reporter;
        (void)apogee::agentloop::run(*fixture.harness, fixture.history, options, reporter);
        return fixture.provider->requests().front();
    };
    const ChatRequest with = run_with(true);
    const ChatRequest without = run_with(false);
    CHECK(tool_names(with) == tool_names(without));
    CHECK(with.tools.size() == 12);
    REQUIRE(with.tools.size() == without.tools.size());
    for (std::size_t index = 0; index < with.tools.size(); ++index) {
        CHECK(with.tools[index].description == without.tools[index].description);
        CHECK(with.tools[index].parameters_schema == without.tools[index].parameters_schema);
    }
    CHECK(with.messages.size() == without.messages.size());
}

TEST_CASE("a turn offers the core, the question's top eight, and find_tools",
          "[agentloop][tool_selection]") {
    const ToolRegistry registry = full_registry();
    ToolSelection selection{words_ranker(registry), registry.size()};
    const ToolOffer offer = selection.begin_turn("show the commit history of the repository", {});
    REQUIRE(offer.active);
    CHECK(offer.ranked.size() == apogee::agentloop::kSelectedTools);
    CHECK(offer.ranked.front() == "git_log");
    for (const std::string& core : {"read_file", "list_directory", "run_command", "find_tools"}) {
        CHECK(offer.names.contains(core));
    }
    for (const std::string& ranked : offer.ranked) {
        CHECK(offer.names.contains(ranked));
    }
    CHECK(offer.names.size() <= apogee::agentloop::kSelectedTools + 4);
    CHECK_FALSE(offer.names.contains("mcp__calendar__book_meeting"));
}

TEST_CASE("a registered consult is in the core: offered whatever the question ranks",
          "[agentloop][tool_selection][consult]") {
    // 27f: a suite's `consultable:` is the user's opt-in, and a call ranked
    // out of the offer cannot be parsed by a local model's grammar at all.
    ToolRegistry registry = full_registry();
    Tool consult;
    consult.name = "consult";
    consult.description = "Ask another model in your suite a question";
    consult.run = [](std::string_view) { return ToolOutcome{"ok"}; };
    registry.add(std::move(consult));
    ToolSelection selection{words_ranker(registry), registry.size()};
    // A question the ranking fills its eight with other tools for.
    const ToolOffer offer = selection.begin_turn(
        "read write edit delete list search files directory git log diff show notes commit "
        "branch status",
        {});
    REQUIRE(offer.active);
    REQUIRE(std::ranges::find(offer.ranked, "consult") == offer.ranked.end());
    CHECK(offer.names.contains("consult"));
    // Unregistered, the core never brings it in.
    const ToolRegistry plain = full_registry();
    ToolSelection without{words_ranker(plain), plain.size()};
    CHECK_FALSE(without.begin_turn("show the commit history", {}).names.contains("consult"));
}

TEST_CASE("selection only narrows what the registry holds", "[agentloop][tool_selection]") {
    // A read-only policy's registry has no run_command: the core never
    // brings back what the policy dropped, and find_tools never finds it.
    ToolRegistry registry;
    for (const Spec& spec : specs()) {
        if (spec.writes) {
            continue;
        }
        Tool tool;
        tool.name = spec.name;
        tool.description = spec.description;
        tool.run = [](std::string_view) { return ToolOutcome{"ok"}; };
        registry.add(std::move(tool));
    }
    for (const std::string& name : {"mcp__wiki__read_page", "mcp__wiki__search_pages"}) {
        Tool tool;
        tool.name = name;
        tool.description = "Read the team wiki";
        tool.run = [](std::string_view) { return ToolOutcome{"ok"}; };
        registry.add(std::move(tool));
    }
    REQUIRE(registry.size() > apogee::agentloop::kToolSelectionThreshold);
    ToolSelection selection{words_ranker(registry), registry.size()};
    const ToolOffer offer = selection.begin_turn("run a shell command", {});
    CHECK_FALSE(offer.names.contains("run_command"));
    std::vector<std::string> added;
    (void)selection.find("shell command", {}, added);
    CHECK(std::ranges::find(added, "run_command") == added.end());
}

TEST_CASE("find_tools offers what it finds, and says when there is nothing",
          "[agentloop][tool_selection]") {
    const ToolRegistry registry = full_registry();
    ToolSelection selection{words_ranker(registry), registry.size()};
    (void)selection.begin_turn("show the commit history", {});
    REQUIRE_FALSE(selection.offered("mcp__calendar__book_meeting"));

    std::vector<std::string> added;
    const std::string found = selection.find("book a meeting room", {}, added);
    REQUIRE_FALSE(added.empty());
    CHECK(added.front() == "mcp__calendar__book_meeting");
    CHECK(selection.offered("mcp__calendar__book_meeting"));
    CHECK(found.find("mcp__calendar__book_meeting: Book a meeting room") != std::string::npos);
    CHECK(found.find("arguments: ") != std::string::npos);
    CHECK(added.size() <= apogee::agentloop::kFoundTools);

    // What is offered already is never found again.
    std::vector<std::string> again;
    (void)selection.find("book a meeting room", {}, again);
    CHECK(std::ranges::find(again, "mcp__calendar__book_meeting") == again.end());

    // At most five a call, however many match.
    ToolSelection fresh{words_ranker(registry), registry.size()};
    (void)fresh.begin_turn("book a meeting room", {});
    std::vector<std::string> many;
    (void)fresh.find("file note", {}, many);
    CHECK(many.size() == apogee::agentloop::kFoundTools);

    std::vector<std::string> none;
    CHECK(selection.find("zzz qqq", {}, none).starts_with("No other tool matches"));
    CHECK(none.empty());
    CHECK(selection.find("", {}, none).starts_with("Error: find_tools needs a query"));
    CHECK(apogee::agentloop::find_tools_query(R"({"query":"a room"})") == "a room");
    CHECK(apogee::agentloop::find_tools_query("not json").empty());
}

TEST_CASE("a new turn keeps the last turn's tools while they offer its top three",
          "[agentloop][tool_selection]") {
    const ToolRegistry registry = full_registry();
    ToolSelection selection{words_ranker(registry), registry.size()};
    const ToolOffer first = selection.begin_turn("show the commit history of the repository", {});
    CHECK_FALSE(first.kept);

    // A follow-up about the same things: the same set, the prompt's start kept.
    const ToolOffer related = selection.begin_turn("and the commit history before that", {});
    CHECK(related.kept);
    CHECK(related.names == first.names);

    // A question the set does not cover: chosen afresh.
    const ToolOffer other = selection.begin_turn("book a meeting room for the slot", {});
    CHECK_FALSE(other.kept);
    CHECK(other.names.contains("mcp__calendar__book_meeting"));
    CHECK(other.names != first.names);
}

TEST_CASE("an MCP tool is offered with the rest of its server", "[agentloop][tool_selection]") {
    // A server's tools refer to each other's ids -- an invite needs the
    // event list_events finds -- and a local model cannot call a tool it was
    // not offered: its name is outside the grammar (the user's call,
    // 2026-10-03).
    const ToolRegistry registry = full_registry();
    ToolSelection selection{words_ranker(registry), registry.size()};
    const ToolOffer offer = selection.begin_turn("book a meeting room for the slot", {});
    REQUIRE(offer.names.contains("mcp__calendar__book_meeting"));
    CHECK(offer.names.contains("mcp__calendar__list_events"));
    // Native tools are not grouped: the question's top eight, not a toolset.
    CHECK_FALSE(offer.names.contains("git_status"));

    // find_tools brings a server whole too.
    std::vector<std::string> added;
    const std::string found = selection.find("open a new ticket in the issue tracker", {}, added);
    REQUIRE(std::ranges::find(added, "mcp__tickets__create_ticket") != added.end());
    CHECK(selection.offered("mcp__tickets__get_ticket"));
    CHECK(
        found.starts_with("These tools are yours from your next step, with the rest of each "
                          "one's server:"));

    // And so does a tool named without being offered.
    ToolSelection named{words_ranker(registry), registry.size()};
    (void)named.begin_turn("show the commit history of the repository", {});
    REQUIRE_FALSE(named.offered("mcp__tickets__get_ticket"));
    CHECK(named.offer("mcp__tickets__create_ticket"));
    CHECK(named.offered("mcp__tickets__get_ticket"));
}

TEST_CASE("find_tools names the tools not shown, and how many past its limit",
          "[agentloop][tool_selection]") {
    CHECK(apogee::agentloop::find_tools_tool().description.find("not shown yet") ==
          std::string::npos);
    const auto two = apogee::agentloop::find_tools_tool({"a_tool", "b_tool"});
    CHECK(two.description.ends_with(" Tools not shown yet: a_tool, b_tool."));
    std::vector<std::string> hidden;
    for (std::size_t index = 0; index < apogee::agentloop::kHiddenNamesListed + 5; ++index) {
        hidden.push_back("t" + std::to_string(index));
    }
    const auto lots = apogee::agentloop::find_tools_tool(hidden);
    CHECK(lots.description.ends_with(", and 5 more."));
    CHECK(lots.description.find("t99,") != std::string::npos);
    CHECK(lots.description.find("t100") == std::string::npos);
}

TEST_CASE("a tool's server is its name's mcp__ prefix", "[agentloop][tool_selection]") {
    CHECK(apogee::agentloop::tool_server("mcp__tickets__create_ticket") == "mcp__tickets__");
    CHECK(apogee::agentloop::tool_server("mcp__a__b__c") == "mcp__a__");
    CHECK(apogee::agentloop::tool_server("read_file").empty());
    CHECK(apogee::agentloop::tool_server("mcp__").empty());
    CHECK(apogee::agentloop::tool_server("mcp____x").empty());
}

TEST_CASE("a question is ranked whole and by each of its clauses", "[agentloop][tool_selection]") {
    using apogee::agentloop::ranking_queries;
    CHECK(ranking_queries("show the commit history") ==
          std::vector<std::string>{"show the commit history"});
    CHECK(ranking_queries("The fix needs review: add Sam to the meeting.") ==
          std::vector<std::string>{"The fix needs review: add Sam to the meeting.",
                                   "The fix needs review", " add Sam to the meeting"});
    // A clause too short to rank on is not one.
    CHECK(ranking_queries("Thanks! Open a ticket for the crash please.").size() == 1);
}

TEST_CASE("a two-part question finds each part's tools by meaning", "[agentloop][tool_selection]") {
    // Vectors over the whole question blend its two intents; each clause's
    // own vector still finds its tool.
    const ToolRegistry registry = full_registry();
    ScriptedEmbedder embedder;
    const ToolRanker ranker{registry.definitions(), registry.definition_hashes(),
                            ToolEmbedding{.embed = embedder.func(), .model = "m"}};
    const auto ranking =
        ranker.rank("the bug ticket in the issue tracker; schedule a meeting room", {});
    REQUIRE(ranker.semantic());
    // Whole, the question leans to tickets; each clause matches its tool
    // fully, so both lead.
    const auto top = names_of(ranking, 2);
    CHECK(std::ranges::find(top, "mcp__calendar__book_meeting") != top.end());
    CHECK(std::ranges::find(top, "mcp__tickets__create_ticket") != top.end());
}

TEST_CASE("the offer's line says how many, how ranked, and for what",
          "[agentloop][tool_selection]") {
    ToolOffer offer;
    offer.active = true;
    offer.names = {"read_file", "git_log", "find_tools"};
    offer.ranked = {"git_log", "read_file"};
    offer.semantic = true;
    CHECK(apogee::agentloop::describe_offer(offer, 22) ==
          "tools: 2 of 22 offered, and find_tools, ranked by meaning -- for this question: "
          "git_log, read_file");
    offer.kept = true;
    offer.semantic = false;
    CHECK(apogee::agentloop::describe_offer(offer, 22).ends_with(
        "ranked by words -- for this question: git_log, read_file (the last turn's tools kept)"));
}

// ---------------------------------------------------------------------------
// The loop
// ---------------------------------------------------------------------------

TEST_CASE("the loop offers the turn's few, in the registry's order, the same on every step",
          "[agentloop][tool_selection][loop]") {
    const ToolRegistry registry = full_registry();
    LoopFixture fixture =
        loop_fixture({calls({call("1", "git_log")}),
                      calls({call("2", "read_file", R"({"path":"a"})")}), answer("done")},
                     "show the commit history of the repository");
    ToolSelection selection{words_ranker(registry), registry.size()};
    apogee::agentloop::Options options;
    options.model = "mock";
    options.tools = &registry;
    options.tool_selection = &selection;
    options.ask = [](const apogee::agentloop::QuestionRequest&) {
        return apogee::agentloop::Answers{};
    };
    apogee::agentloop::NullReporter reporter;
    (void)apogee::agentloop::run(*fixture.harness, fixture.history, options, reporter);

    const auto& requests = fixture.provider->requests();
    REQUIRE(requests.size() == 3);
    const std::vector<std::string> first = tool_names(requests[0]);
    CHECK(first.size() < registry.size());
    // Registry order (by name), then find_tools, then ask_user.
    REQUIRE(first.size() >= 3);
    CHECK(first[first.size() - 2] == "find_tools");
    CHECK(first.back() == "ask_user");
    CHECK(std::is_sorted(first.begin(), first.end() - 2));
    CHECK(std::ranges::find(first, "git_log") != first.end());
    // find_tools names what is not shown.
    const auto& find = requests[0].tools[requests[0].tools.size() - 2];
    CHECK(find.description.find("mcp__calendar__book_meeting") != std::string::npos);
    CHECK(find.description.find("git_log,") == std::string::npos);
    // Nothing was added: every step of the turn is the same list.
    CHECK(tool_names(requests[1]) == first);
    CHECK(tool_names(requests[2]) == first);
}

TEST_CASE("a tool called without being offered is dispatched, gated, and offered next step",
          "[agentloop][tool_selection][loop]") {
    const ToolRegistry registry = full_registry();
    LoopFixture fixture =
        loop_fixture({calls({call("1", "write_note", R"({"text":"x"})")}), answer("done")},
                     "show the commit history of the repository");
    ToolSelection selection{words_ranker(registry), registry.size()};
    std::vector<std::string> gated;
    apogee::agentloop::Options options;
    options.model = "mock";
    options.tools = &registry;
    options.tool_selection = &selection;
    options.permission = [&gated](const apogee::agent::GateRequest& request) {
        gated.emplace_back(request.tool);
        return Permission::Allow;
    };
    apogee::agentloop::NullReporter reporter;
    (void)apogee::agentloop::run(*fixture.harness, fixture.history, options, reporter);

    const auto& requests = fixture.provider->requests();
    REQUIRE(requests.size() == 2);
    CHECK(!offers(requests[0], "write_note"));
    // Gated, and its result is in history.
    CHECK(gated == std::vector<std::string>{"write_note"});
    const bool ran = std::ranges::any_of(fixture.history, [](const ChatMessage& message) {
        return message.content.plain_text() == "ran write_note";
    });
    CHECK(ran);
    CHECK(offers(requests[1], "write_note"));
}

TEST_CASE("find_tools widens the next step", "[agentloop][tool_selection][loop]") {
    const ToolRegistry registry = full_registry();
    LoopFixture fixture = loop_fixture(
        {calls({call("1", "find_tools", R"({"query":"book a meeting room"})")}),
         calls({call("2", "mcp__calendar__book_meeting", R"({"slot":"3pm"})")}), answer("booked")},
        "show the commit history of the repository");
    ToolSelection selection{words_ranker(registry), registry.size()};
    apogee::agentloop::Options options;
    options.model = "mock";
    options.tools = &registry;
    options.tool_selection = &selection;
    apogee::agentloop::NullReporter reporter;
    const auto result =
        apogee::agentloop::run(*fixture.harness, fixture.history, options, reporter);

    const auto& requests = fixture.provider->requests();
    REQUIRE(requests.size() == 3);
    CHECK(!offers(requests[0], "mcp__calendar__book_meeting"));
    CHECK(offers(requests[1], "mcp__calendar__book_meeting"));
    // find_tools answered in the loop -- the registry has no such tool -- and
    // the meeting was booked through the registry.
    const bool listed = std::ranges::any_of(fixture.history, [](const ChatMessage& message) {
        return message.content.plain_text().starts_with(
            "These tools are yours from your next step");
    });
    CHECK(listed);
    const bool booked = std::ranges::any_of(fixture.history, [](const ChatMessage& message) {
        return message.content.plain_text() == "ran mcp__calendar__book_meeting";
    });
    CHECK(booked);
    CHECK(result.answer == "booked");
}

TEST_CASE("without selection, find_tools is no tool at all", "[agentloop][tool_selection][loop]") {
    const ToolRegistry registry = registry_of(12);
    LoopFixture fixture =
        loop_fixture({calls({call("1", "find_tools", R"({"query":"x"})")}), answer("done")}, "hi");
    ToolSelection selection{words_ranker(registry), registry.size()};
    apogee::agentloop::Options options;
    options.model = "mock";
    options.tools = &registry;
    options.tool_selection = &selection;
    apogee::agentloop::NullReporter reporter;
    (void)apogee::agentloop::run(*fixture.harness, fixture.history, options, reporter);
    CHECK_FALSE(offers(fixture.provider->requests().front(), "find_tools"));
    const bool unknown = std::ranges::any_of(fixture.history, [](const ChatMessage& message) {
        return message.content.plain_text().find("no tool named 'find_tools'") !=
                   std::string::npos ||
               message.content.plain_text().find("unknown tool") != std::string::npos ||
               message.content.plain_text().find("find_tools") != std::string::npos;
    });
    CHECK(unknown);
}

TEST_CASE("the loop ranks for the surface's restated question when it has one",
          "[agentloop][tool_selection][loop]") {
    const ToolRegistry registry = full_registry();
    LoopFixture fixture = loop_fixture({answer("ok")}, "and the other one?");
    ToolSelection selection{words_ranker(registry), registry.size()};
    apogee::agentloop::Options options;
    options.model = "mock";
    options.tools = &registry;
    options.tool_selection = &selection;
    options.selection_query = "show the commit history";

    struct Progress final : apogee::agentloop::Reporter {
        std::vector<std::string> lines;

        void on_progress(std::string_view line) override {
            lines.emplace_back(line);
        }
    } reporter;

    (void)apogee::agentloop::run(*fixture.harness, fixture.history, options, reporter);
    // Ranked for the restatement, not for the words the user typed.
    REQUIRE_FALSE(reporter.lines.empty());
    CHECK(reporter.lines.front().find("for this question: git_log") != std::string::npos);
    const std::vector<std::string> offered = tool_names(fixture.provider->requests().front());
    CHECK(std::ranges::find(offered, "git_log") != offered.end());
}

TEST_CASE("the final pass still withdraws every tool", "[agentloop][tool_selection][loop]") {
    const ToolRegistry registry = full_registry();
    LoopFixture fixture =
        loop_fixture({calls({call("1", "git_log")}), calls({call("2", "git_log", R"({"n":2})")}),
                      answer("enough")},
                     "show the commit history");
    ToolSelection selection{words_ranker(registry), registry.size()};
    apogee::agentloop::Options options;
    options.model = "mock";
    options.tools = &registry;
    options.tool_selection = &selection;
    options.max_iterations = 1;
    apogee::agentloop::NullReporter reporter;
    (void)apogee::agentloop::run(*fixture.harness, fixture.history, options, reporter);
    REQUIRE(fixture.provider->requests().size() == 2);
    CHECK(fixture.provider->requests().back().tools.empty());
}

TEST_CASE("with --verbose, the turn says what it offers", "[agentloop][tool_selection][loop]") {
    const ToolRegistry registry = full_registry();
    LoopFixture fixture = loop_fixture({answer("ok")}, "show the commit history");
    ToolSelection selection{words_ranker(registry), registry.size()};
    apogee::agentloop::Options options;
    options.model = "mock";
    options.tools = &registry;
    options.tool_selection = &selection;

    struct Progress final : apogee::agentloop::Reporter {
        std::vector<std::string> lines;

        void on_progress(std::string_view line) override {
            lines.emplace_back(line);
        }
    } reporter;

    (void)apogee::agentloop::run(*fixture.harness, fixture.history, options, reporter);
    REQUIRE_FALSE(reporter.lines.empty());
    CHECK(reporter.lines.front().starts_with("tools: "));
    CHECK(reporter.lines.front().find("of 24 offered") != std::string::npos);
    CHECK(reporter.lines.front().find("git_log") != std::string::npos);
}
