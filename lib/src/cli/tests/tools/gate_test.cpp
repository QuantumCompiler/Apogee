#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "agent/fetch_url.h"
#include "agent/tool.h"
#include "agent/web_search.h"
#include "agentloop/loop.h"
#include "agentloop/reporter.h"
#include "backends/mock.h"
#include "contracts/config.h"
#include "harness/harness.h"
#include "support/env_guard.h"
#include "tools/toolsets.h"

/// The gate's first real consumer, end to end: a scripted model calls a
/// destructive native tool through the shared loop, and what happens is
/// decided by the permission checker and the confirm function -- never by
/// the tool.
namespace {

using apogee::agent::Permission;
using apogee::agent::ToolRegistry;
using apogee::agentloop::Options;
using apogee::agentloop::RunResult;
using apogee::backends::MockProvider;
using apogee::backends::MockTurn;
using apogee::harness::ChatMessage;
using apogee::harness::Config;
using apogee::harness::FinishReason;
using apogee::harness::Harness;
using apogee::harness::ToolCall;

class QuietReporter final : public apogee::agentloop::Reporter {
public:
    void on_thinking() override {}

    void on_thinking_token(std::string_view) override {}

    void on_tool_status(std::string_view) override {}

    void on_clear_status() override {}

    void on_answer_start() override {}

    void on_answer_token(std::string_view) override {}

    void on_answer_end() override {}
};

struct World {
    apogee::testing::TempDir temp{"tools-gate-" + std::to_string(std::random_device{}())};
    std::filesystem::path root = temp.path() / "root";
    ToolRegistry registry;
    std::shared_ptr<MockProvider> provider;
    std::unique_ptr<Harness> harness;

    explicit World(std::vector<MockTurn> turns) {
        std::filesystem::create_directories(root);
        std::ofstream{root / "in.txt"} << "readable";
        apogee::tools::ToolsetOptions options;
        options.fs_root = root;
        options.working_directory = root;
        options.notes_dir = temp.path() / "notes";
        options.disabled = {"rag"};
        apogee::tools::register_native_toolsets(registry, options);

        MockProvider::Options mock;
        mock.backend_name = "mock";
        mock.turns = std::move(turns);
        provider = std::make_shared<MockProvider>(std::move(mock));
        harness = std::make_unique<Harness>(Config{});
        harness->register_provider("mock", provider);
        harness->use_default_router();
    }

    [[nodiscard]] std::vector<ChatMessage> run(Options options) {
        options.model = "mock";
        options.tools = &registry;
        std::vector<ChatMessage> history{ChatMessage::user("go")};
        QuietReporter reporter;
        (void)apogee::agentloop::run(*harness, history, options, reporter);
        return history;
    }
};

MockTurn calls(std::vector<ToolCall> tool_calls) {
    return MockTurn{"", std::move(tool_calls), FinishReason::ToolCalls, {}};
}

MockTurn says(std::string text) {
    return MockTurn{std::move(text), {}, FinishReason::Stop, {}};
}

std::string tool_result(const std::vector<ChatMessage>& history, std::string_view call_id) {
    for (const ChatMessage& message : history) {
        if (message.tool_call_id == call_id) {
            return message.content.plain_text();
        }
    }
    return {};
}

}  // namespace

TEST_CASE("a read runs with no gate at all; a write with nobody to ask is denied as a result",
          "[tools][gate][permission]") {
    World world{{calls({ToolCall{"r", "read_file", R"({"path":"in.txt"})"},
                        ToolCall{"w", "write_file", R"({"path":"out.txt","content":"x"})"}}),
                 says("done")}};
    const std::vector<ChatMessage> history = world.run(Options{});
    CHECK(tool_result(history, "r") == "readable");
    const std::string denied = tool_result(history, "w");
    CHECK(denied.find("denied permission") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(world.root / "out.txt"));
    // The turn finished: a denial is something the model reads, not an exception.
    CHECK(history.back().content.plain_text() == "done");
}

TEST_CASE("the checker's answer decides: allow runs, deny refuses, ask consults the prompt",
          "[tools][gate][permission]") {
    const auto script = [] {
        return std::vector<MockTurn>{
            calls({ToolCall{"w", "write_file", R"({"path":"out.txt","content":"x"})"}}),
            says("done")};
    };
    {
        World world{script()};
        Options allow;
        allow.permission = [](const apogee::agent::GateRequest&) { return Permission::Allow; };
        (void)world.run(allow);
        CHECK(std::filesystem::exists(world.root / "out.txt"));
    }
    {
        World world{script()};
        Options deny;
        deny.permission = [](const apogee::agent::GateRequest&) { return Permission::Deny; };
        deny.confirm = [](const apogee::agent::GateRequest&) { return true; };  // never asked
        (void)world.run(deny);
        CHECK_FALSE(std::filesystem::exists(world.root / "out.txt"));
    }
    {
        World world{script()};
        std::string asked_tool;
        std::string asked_target;
        Options ask;
        ask.permission = [](const apogee::agent::GateRequest&) { return Permission::Ask; };
        ask.confirm = [&](const apogee::agent::GateRequest& request) {
            asked_tool = request.tool;
            asked_target = request.target;
            return true;
        };
        (void)world.run(ask);
        CHECK(asked_tool == "write_file");
        CHECK(asked_target == "out.txt");
        CHECK(std::filesystem::exists(world.root / "out.txt"));
    }
}

TEST_CASE("every destructive native tool is gated and every other one is not",
          "[tools][gate][permission]") {
    World world{{says("")}};
    for (const std::string_view name : apogee::tools::destructive_tool_names()) {
        INFO(name);
        const apogee::agent::Tool* tool = world.registry.find(name);
        REQUIRE(tool != nullptr);
        CHECK(tool->writes);
    }
    for (const std::string& name : world.registry.names()) {
        const auto destructive = apogee::tools::destructive_tool_names();
        const bool listed =
            std::find(destructive.begin(), destructive.end(), name) != destructive.end();
        INFO(name);
        CHECK(world.registry.find(name)->writes == listed);
    }
    // `rag` was disabled: none of its tools exists; the others all do.
    CHECK(world.registry.find("search_documents") == nullptr);
    CHECK(world.registry.find("run_command") != nullptr);
    CHECK(world.registry.find("git_diff") != nullptr);
    CHECK(world.registry.find("write_note") != nullptr);
}

TEST_CASE("edit_file asks through the gate where someone can answer and is refused where not",
          "[tools][gate][permission][edit]") {
    const auto script = [] {
        return std::vector<MockTurn>{
            calls({ToolCall{"e", "edit_file",
                            R"({"path":"in.txt","old_string":"readable","new_string":"edited"})"}}),
            says("done")};
    };
    const auto contents = [](const World& world) {
        std::ifstream in{world.root / "in.txt"};
        return std::string{std::istreambuf_iterator<char>{in}, {}};
    };
    {
        // A terminal: the config says ask, the user is asked -- the tool and
        // the file it would change -- and says yes.
        World world{script()};
        std::string asked_tool;
        std::string asked_target;
        Options terminal;
        terminal.permission = [](const apogee::agent::GateRequest&) { return Permission::Ask; };
        terminal.confirm = [&](const apogee::agent::GateRequest& request) {
            asked_tool = request.tool;
            asked_target = request.target;
            return true;
        };
        (void)world.run(terminal);
        CHECK(asked_tool == "edit_file");
        CHECK(asked_target == "in.txt");
        CHECK(contents(world) == "edited");
    }
    {
        // A pipe: ask, and nobody to answer -- denied, the file untouched,
        // and the model told so.
        World world{script()};
        Options pipe;
        pipe.permission = [](const apogee::agent::GateRequest&) { return Permission::Ask; };
        const std::vector<ChatMessage> history = world.run(pipe);
        CHECK(tool_result(history, "e").find("denied permission") != std::string::npos);
        CHECK(contents(world) == "readable");
    }
}

TEST_CASE("every request with tools carries the environment note, and history never does",
          "[tools][environment][transient]") {
    World world{{calls({ToolCall{"r", "read_file", R"({"path":"in.txt"})"}}), says("done")}};
    const std::vector<ChatMessage> history = world.run(Options{});

    // Both steps of the turn: the same note, first, marked transient.
    REQUIRE(world.provider->requests().size() == 2);
    for (const apogee::harness::ChatRequest& request : world.provider->requests()) {
        REQUIRE_FALSE(request.messages.empty());
        const std::string note = request.messages.front().content.plain_text();
        CHECK(request.messages.front().role == apogee::harness::Role::System);
        CHECK(note.starts_with("Environment:\n- Today is "));
        CHECK(note.find("- Working directory: " + world.root.string()) != std::string::npos);
        CHECK(note.find("- The file tools reach " + world.root.string()) != std::string::npos);
        CHECK(request.is_transient(0));
    }
    CHECK(world.provider->requests()[0].messages.front().content.plain_text() ==
          world.provider->requests()[1].messages.front().content.plain_text());
    for (const ChatMessage& message : history) {
        CHECK(message.content.plain_text().find("Environment:") == std::string::npos);
    }
}

TEST_CASE("every built-in tool's parameters are a JSON Schema a template can read",
          "[tools][schema]") {
    // A schema that does not parse fails the whole tool list at render time:
    // a local model's template then answers without any tool (found on real
    // weights, 2026-09-28 -- a `\s` in an example inside a description).
    const apogee::testing::TempDir temp{"tools-schema-" + std::to_string(std::random_device{}())};
    apogee::tools::ToolsetOptions options;
    options.fs_root = temp.path();
    options.working_directory = temp.path();
    options.notes_dir = temp.path() / "notes";
    ToolRegistry registry;
    apogee::tools::register_native_toolsets(registry, options);
    registry.add(apogee::agent::make_fetch_url_tool(
        [](std::string_view) { return apogee::agent::FetchResult{}; }));
    registry.add(apogee::agent::make_web_search_tool(
        [](const apogee::agent::SearchRequest&) { return apogee::agent::SearchResponse{}; },
        "127.0.0.1", 5));
    REQUIRE(registry.size() >= 19);

    for (const apogee::harness::Tool& tool : registry.definitions()) {
        INFO(tool.name << ": " << tool.parameters_schema);
        const nlohmann::json schema = nlohmann::json::parse(tool.parameters_schema, nullptr, false);
        REQUIRE_FALSE(schema.is_discarded());
        CHECK(schema.value("type", "") == "object");
        REQUIRE(schema.contains("properties"));
        for (const nlohmann::json& required : schema.value("required", nlohmann::json::array())) {
            CHECK(schema["properties"].contains(required.get<std::string>()));
        }
        CHECK_FALSE(tool.description.empty());
    }
}
