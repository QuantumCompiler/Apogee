#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "agent/tool.h"
#include "agentloop/loop.h"
#include "agentloop/member_call.h"
#include "agentloop/reporter.h"
#include "agentloop/validate.h"
#include "backends/mock.h"
#include "contracts/config.h"
#include "harness/harness.h"
#include "support/env_guard.h"
#include "tools/consult.h"
#include "tools/fs.h"

/// The tool-argument seam (27g) on the real filesystem tools: a bad
/// `delete_file` path objected to as the call's round-one result, the revised
/// call checked and run -- and the permission gate asked exactly what it
/// would have been asked with no validation at all.
namespace {

using apogee::agent::GateRequest;
using apogee::agent::Permission;
using apogee::agentloop::MemberCalls;
using apogee::agentloop::SideCall;
using apogee::backends::MockProvider;
using apogee::backends::MockTurn;
using apogee::harness::ChatMessage;
using apogee::harness::ToolCall;

constexpr std::string_view kConfig = R"YAML(models:
  default: root
  default_suite: checked
backends:
  root:
    type: mock
  helper:
    type: mock
suites:
  checked:
    members:
      chat: root
      utility: helper
    validate:
      tool_args: on
  plain:
    members:
      chat: root
      utility: helper
  tight:
    members:
      chat: root
      utility: helper
    consultable: [utility]
    consult_caps:
      per_turn: 1
    validate:
      tool_args: on
)YAML";

/// What a gate request carried, as plain data to compare.
struct Asked {
    std::string tool;
    std::string target;
    std::string detail;
    bool outbound = false;

    bool operator==(const Asked&) const = default;
};

/// A root and a verifier over mocks, a workspace with two notes, and the
/// filesystem tools sandboxed to it.
struct World {
    apogee::testing::TempDir temp{"validate-tools-" + std::to_string(std::random_device{}())};
    apogee::harness::Harness harness{apogee::harness::parse_config(kConfig, "<test>")};
    std::map<std::string, std::shared_ptr<MockProvider>> mocks;
    MemberCalls calls{harness};
    apogee::agent::ToolRegistry registry;
    std::vector<Asked> asked;

    World(std::vector<MockTurn> root, std::vector<MockTurn> helper) {
        std::filesystem::create_directories(temp.path() / "notes");
        std::ofstream{temp.path() / "notes" / "draft.md"} << "draft\n";
        std::ofstream{temp.path() / "notes" / "final.md"} << "final\n";
        apogee::tools::register_fs_tools(registry, temp.path());
        for (auto& [name, turns] : std::map<std::string, std::vector<MockTurn>>{
                 {"root", std::move(root)}, {"helper", std::move(helper)}}) {
            MockProvider::Options options;
            options.backend_name = name;
            options.turns = std::move(turns);
            mocks[name] = std::make_shared<MockProvider>(std::move(options));
            harness.register_provider(name, mocks[name]);
        }
        harness.use_default_router();
    }

    [[nodiscard]] apogee::agentloop::Options options(bool member_calls = true) {
        apogee::agentloop::Options out;
        out.model = "root";
        out.tools = &registry;
        out.member_calls = member_calls ? &calls : nullptr;
        out.permission = [](const GateRequest&) { return Permission::Ask; };
        out.confirm = [this](const GateRequest& request) {
            asked.push_back(Asked{.tool = std::string{request.tool},
                                  .target = std::string{request.target},
                                  .detail = std::string{request.detail},
                                  .outbound = request.outbound});
            return true;
        };
        return out;
    }

    [[nodiscard]] bool exists(const std::string& name) const {
        return std::filesystem::exists(temp.path() / "notes" / name);
    }
};

ToolCall deleting(std::string id, std::string path) {
    return ToolCall{.id = std::move(id),
                    .name = "delete_file",
                    .arguments = nlohmann::json{{"path", std::move(path)}}.dump()};
}

MockTurn calling(ToolCall call) {
    return MockTurn{.text = "",
                    .tool_calls = {std::move(call)},
                    .finish_reason = apogee::harness::FinishReason::ToolCalls};
}

struct Said : apogee::agentloop::Reporter {
    std::vector<std::string> notices;
    std::vector<SideCall> side_calls;

    void on_notice(std::string_view text) override {
        notices.emplace_back(text);
    }

    void on_side_call(const SideCall& call) override {
        side_calls.push_back(call);
    }
};

std::vector<std::string> results(const std::vector<ChatMessage>& history) {
    std::vector<std::string> out;
    for (const ChatMessage& message : history) {
        if (message.role == apogee::harness::Role::Tool) {
            out.push_back(message.content.plain_text());
        }
    }
    return out;
}

/// `text` with the world's workspace written `<root>`, so two worlds' runs
/// compare.
std::string rooted(std::string text, const World& world) {
    const std::string root = std::filesystem::canonical(world.temp.path()).string();
    for (std::size_t at = text.find(root); at != std::string::npos; at = text.find(root, at)) {
        text.replace(at, root.size(), "<root>");
    }
    return text;
}

std::vector<std::string> rooted(std::vector<std::string> texts, const World& world) {
    for (std::string& text : texts) {
        text = rooted(std::move(text), world);
    }
    return texts;
}

constexpr std::string_view kObjection =
    "the user asked to delete the draft; notes/final.md is the final copy";

}  // namespace

TEST_CASE("a bad delete_file path is objected to as its round-one result; the revision runs",
          "[tools][validate][tool_args]") {
    World world{
        {calling(deleting("1", "notes/final.md")), calling(deleting("2", "notes/draft.md")),
         MockTurn{.text = "Deleted the draft."}},
        {MockTurn{.text = "OBJECT: " + std::string{kObjection}}, MockTurn{.text = "AGREE"}}};
    std::vector<ChatMessage> history{ChatMessage::user("Delete the draft notes.")};
    Said said;
    const apogee::agentloop::RunResult result =
        apogee::agentloop::run(world.harness, history, world.options(), said);
    CHECK(result.answer == "Deleted the draft.");

    // Round one: the objection is the call's result, and it did not run.
    const std::vector<std::string> shown = results(history);
    REQUIRE(shown.size() == 2);
    CHECK(shown[0] ==
          "Not run: before delete_file ran, utility (helper) checked it and objected -- " +
              std::string{kObjection} +
              "\nCorrect the call if the objection is right. If it is wrong, make the same call "
              "again: it will run, and the user is shown the objection.");
    CHECK(world.exists("final.md"));
    // The revision passed its check and ran.
    CHECK_FALSE(world.exists("draft.md"));
    CHECK(shown[1].starts_with("Deleted "));

    // The verifier: two member calls, each exactly a brief naming the call
    // and the request -- nothing of the conversation besides.
    const auto& briefs = world.mocks["helper"]->requests();
    REQUIRE(briefs.size() == 2);
    for (const apogee::harness::ChatRequest& brief : briefs) {
        REQUIRE(brief.messages.size() == 1);
        CHECK(brief.tools.empty());
        CHECK_THAT(brief.messages[0].content.plain_text(),
                   Catch::Matchers::ContainsSubstring("Delete the draft notes."));
    }
    CHECK(briefs[0].messages[0].content.plain_text().starts_with(
        "Check a tool call before it runs: delete_file {\"path\":\"notes/final.md\"}"));
    CHECK(briefs[1].messages[0].content.plain_text().starts_with(
        "Check a tool call before it runs: delete_file {\"path\":\"notes/draft.md\"}"));
    // Said: the objection kept on screen, and each check in the thinking
    // block.
    REQUIRE(said.notices.size() == 1);
    CHECK(said.notices[0] ==
          "validate: utility (helper) objected to delete_file "
          "{\"path\":\"notes/final.md\"} -- " +
              std::string{kObjection} + " -- returned to the model for one revision");
    CHECK(said.side_calls.size() == 4);

    // The gate: asked once, for the call that ran -- exactly what a run with
    // no validation asks when the model makes that call.
    World control{
        {calling(deleting("2", "notes/draft.md")), MockTurn{.text = "Deleted the draft."}},
        {MockTurn{.text = "AGREE"}}};
    std::vector<ChatMessage> plain{ChatMessage::user("Delete the draft notes.")};
    (void)apogee::agentloop::run(control.harness, plain, control.options(false));
    REQUIRE(world.asked.size() == 1);
    CHECK(world.asked == std::vector<Asked>{Asked{.tool = "delete_file",
                                                  .target = control.asked.at(0).target,
                                                  .detail = control.asked.at(0).detail,
                                                  .outbound = false}});
    CHECK(world.asked == control.asked);
    CHECK(control.mocks["helper"]->requests().empty());
}

TEST_CASE("a path that does not exist is caught by structure; no model is woken",
          "[tools][validate][tool_args]") {
    World world{{calling(deleting("1", "notes/drfat.md")), calling(deleting("2", "notes/draft.md")),
                 MockTurn{.text = "done"}},
                {MockTurn{.text = "AGREE"}}};
    std::vector<ChatMessage> history{ChatMessage::user("Delete the draft notes.")};
    Said said;
    (void)apogee::agentloop::run(world.harness, history, world.options(), said);
    const std::vector<std::string> shown = results(history);
    REQUIRE(shown.size() == 2);
    CHECK(shown[0].starts_with(
        "Not run: delete_file's arguments failed a check made before it runs -- No such file: "));
    CHECK_THAT(shown[0], Catch::Matchers::ContainsSubstring(
                             "drfat.md (delete_file deletes an existing file)"));
    // The structural catch woke nobody and asked nobody; the revision was
    // checked once and ran through the gate.
    CHECK(world.mocks["helper"]->requests().size() == 1);
    CHECK(world.asked.size() == 1);
    CHECK_FALSE(world.exists("draft.md"));
}

TEST_CASE("arguments that break the tool's own schema are caught by structure",
          "[tools][validate][tool_args]") {
    World world{
        {calling(ToolCall{.id = "1", .name = "delete_file", .arguments = R"({"file":"x"})"}),
         MockTurn{.text = "done"}},
        {MockTurn{.text = "AGREE"}}};
    std::vector<ChatMessage> history{ChatMessage::user("Delete x.")};
    (void)apogee::agentloop::run(world.harness, history, world.options());
    const std::vector<std::string> shown = results(history);
    REQUIRE(shown.size() == 1);
    CHECK_THAT(shown[0], Catch::Matchers::ContainsSubstring("they do not match its parameters"));
    CHECK(world.mocks["helper"]->requests().empty());
    CHECK(world.asked.empty());
}

TEST_CASE("the model standing by its call: it runs at the round limit, the dispute said first",
          "[tools][validate][tool_args]") {
    World world{{calling(deleting("1", "notes/final.md")), calling(deleting("2", "notes/final.md")),
                 MockTurn{.text = "done"}},
                {MockTurn{.text = "OBJECT: " + std::string{kObjection}}}};
    std::vector<ChatMessage> history{ChatMessage::user("Delete the draft notes.")};
    Said said;
    (void)apogee::agentloop::run(world.harness, history, world.options(), said);
    // The verifier was asked once; the same call again ran -- through the
    // gate, which asked as it always does.
    CHECK(world.mocks["helper"]->requests().size() == 1);
    CHECK_FALSE(world.exists("final.md"));
    REQUIRE(world.asked.size() == 1);
    REQUIRE(said.notices.size() == 2);
    CHECK(said.notices[1] ==
          "validate: the model made the same call again, and delete_file "
          "{\"path\":\"notes/final.md\"} runs over utility (helper)'s objection -- " +
              std::string{kObjection});
}

TEST_CASE("no path to a third round: a verifier that objects to everything is outlasted",
          "[tools][validate][tool_args][rounds]") {
    World world{{calling(deleting("1", "notes/final.md")), calling(deleting("2", "notes/draft.md")),
                 MockTurn{.text = "done"}},
                {MockTurn{.text = "OBJECT: no"}}};
    std::vector<ChatMessage> history{ChatMessage::user("Delete the draft notes.")};
    Said said;
    (void)apogee::agentloop::run(world.harness, history, world.options(), said);
    // Two checks, one revision, and the revision ran with the dispute said.
    CHECK(world.mocks["helper"]->requests().size() == 2);
    CHECK(world.mocks["root"]->requests().size() == 3);
    CHECK_FALSE(world.exists("draft.md"));
    REQUIRE(said.notices.size() == 2);
    CHECK(
        said.notices[1].starts_with("validate: delete_file {\"path\":\"notes/draft.md\"} runs "
                                    "as the model revised it, over utility (helper)'s "
                                    "objection"));
}

TEST_CASE("validation off: the run is exactly what it was", "[tools][validate][tool_args]") {
    const std::vector<MockTurn> root{calling(deleting("1", "notes/final.md")),
                                     MockTurn{.text = "done"}};
    // The suite with no validate: block, with and without the member calls;
    // and the checked suite with no member calls handed in.
    const auto run_under = [&root](const std::string& suite, bool member_calls) {
        auto world =
            std::make_unique<World>(root, std::vector<MockTurn>{MockTurn{.text = "AGREE"}});
        world->harness.set_active_suite(suite);
        std::vector<ChatMessage> history{ChatMessage::user("Delete the final notes.")};
        Said said;
        (void)apogee::agentloop::run(world->harness, history, world->options(member_calls), said);
        CHECK(world->mocks["helper"]->requests().empty());
        CHECK(said.notices.empty());
        CHECK_FALSE(world->exists("final.md"));
        std::vector<std::string> shown = rooted(results(history), *world);
        return std::make_pair(std::move(world), std::move(shown));
    };
    const auto [baseline, baseline_results] = run_under("plain", false);
    const auto [plain, plain_results] = run_under("plain", true);
    const auto [unbudgeted, unbudgeted_results] = run_under("checked", false);
    CHECK(plain_results == baseline_results);
    CHECK(unbudgeted_results == baseline_results);
    CHECK(plain->asked == baseline->asked);
    CHECK(unbudgeted->asked == baseline->asked);
    // What the root was sent, request for request.
    const auto& sent = baseline->mocks["root"]->requests();
    for (const auto* other :
         {&plain->mocks["root"]->requests(), &unbudgeted->mocks["root"]->requests()}) {
        REQUIRE(other->size() == sent.size());
        for (std::size_t i = 0; i < sent.size(); ++i) {
            REQUIRE((*other)[i].messages.size() == sent[i].messages.size());
            for (std::size_t m = 0; m < sent[i].messages.size(); ++m) {
                CHECK(rooted((*other)[i].messages[m].content.plain_text(),
                             other == &plain->mocks["root"]->requests() ? *plain : *unbudgeted) ==
                      rooted(sent[i].messages[m].content.plain_text(), *baseline));
            }
            CHECK((*other)[i].tools.size() == sent[i].tools.size());
        }
    }
}

TEST_CASE("a tool that neither writes nor reaches out is not checked",
          "[tools][validate][tool_args]") {
    World world{{calling(ToolCall{
                     .id = "1", .name = "read_file", .arguments = R"({"path":"notes/draft.md"})"}),
                 MockTurn{.text = "done"}},
                {MockTurn{.text = "OBJECT: no"}}};
    std::vector<ChatMessage> history{ChatMessage::user("Read the draft.")};
    (void)apogee::agentloop::run(world.harness, history, world.options());
    CHECK(world.mocks["helper"]->requests().empty());
    CHECK_THAT(results(history).at(0), Catch::Matchers::ContainsSubstring("draft"));
}

TEST_CASE("validation and consults share the turn's budget; spent, a check degrades to structure",
          "[tools][validate][tool_args][budget]") {
    World world{
        {calling(ToolCall{.id = "c",
                          .name = "consult",
                          .arguments = R"({"member":"utility","question":"Which notes?"})"}),
         calling(deleting("1", "notes/final.md")), MockTurn{.text = "done"}},
        {MockTurn{.text = "the draft"}}};
    world.harness.set_active_suite("tight");
    const auto consults = std::make_shared<MemberCalls>(world.harness);
    (void)apogee::tools::register_consult_tool(world.registry, world.harness, consults);
    std::vector<ChatMessage> history{ChatMessage::user("Delete the notes.")};
    Said said;
    apogee::agentloop::Options options = world.options();
    options.member_calls = consults.get();
    (void)apogee::agentloop::run(world.harness, history, options, said);
    // The consult spent the one call; the check was not made, and said so.
    CHECK(world.mocks["helper"]->requests().size() == 1);
    REQUIRE(said.notices.size() == 1);
    CHECK(said.notices[0] ==
          "validate: delete_file {\"path\":\"notes/final.md\"} not checked -- the turn's "
          "member-call budget is spent (1 of 1, plays, consults and checks together) -- checked "
          "by structure only; its structure passed");
    CHECK_FALSE(world.exists("final.md"));
    CHECK(world.asked.size() == 1);
}
