#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "agent/tool.h"
#include "agentloop/loop.h"
#include "agentloop/reporter.h"
#include "backends/mock.h"
#include "harness/config.h"
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
