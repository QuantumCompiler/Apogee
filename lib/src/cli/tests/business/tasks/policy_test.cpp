#include "tasks/policy.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

/// A task's autonomy policy (27i): the question policy read from its flag,
/// the grants normalized, the policy kept in the ledger -- and **the
/// ceiling**, exhaustively: every tool kind under every agent policy and
/// every config level, granted, against what the config and the agent allow
/// together. No combination lets a grant through that either would refuse.
namespace {

namespace t = apogee::tasks;
using apogee::agent::Tool;
using apogee::agent::ToolOutcome;
using apogee::agent::ToolRegistry;
using apogee::harness::AgentToolPolicy;
using apogee::harness::PermissionLevel;
using apogee::harness::PermissionsConfig;

bool has(const std::string& text, std::string_view part) {
    return text.find(part) != std::string::npos;
}

/// A tool that writes, one that only reads, and one that reaches a website.
ToolRegistry tools() {
    ToolRegistry out;
    const auto run = [](std::string_view) { return ToolOutcome{"ok", false}; };
    Tool write;
    write.name = "write_file";
    write.writes = true;
    write.run = run;
    out.add(write);
    Tool read;
    read.name = "read_file";
    read.run = run;
    out.add(read);
    Tool fetch;
    fetch.name = "fetch_url";
    fetch.outbound = true;
    fetch.describe_target = [](std::string_view) { return std::string{"example.org"}; };
    fetch.run = run;
    out.add(fetch);
    return out;
}

/// `registry` as an agent with `policy` is offered it -- the shape of
/// Milestone X's filter, which the composition root applies; the ceiling is
/// handed its result.
ToolRegistry offered(const ToolRegistry& registry, std::optional<AgentToolPolicy> policy) {
    if (!policy.has_value() || *policy == AgentToolPolicy::All) {
        return registry;
    }
    ToolRegistry out;
    if (*policy == AgentToolPolicy::None) {
        return out;
    }
    for (const std::string& name : registry.names()) {
        if (const Tool* tool = registry.find(name); tool != nullptr && !tool->writes) {
            out.add(*tool);
        }
    }
    return out;
}

std::string_view policy_word(std::optional<AgentToolPolicy> policy) {
    return policy.has_value() ? apogee::harness::to_string(*policy) : std::string_view{};
}

}  // namespace

TEST_CASE("the question policy reads fail, or one declared answer", "[tasks][policy]") {
    struct Row {
        std::string_view text;
        bool accepted;
        t::OnQuestion on_question;
        std::string_view answer;
    };

    const std::array<Row, 11> rows{{
        {"fail", true, t::OnQuestion::Fail, ""},
        {"answer:blue", true, t::OnQuestion::Answer, "blue"},
        {R"(answer:"blue")", true, t::OnQuestion::Answer, "blue"},
        {"answer:'dark blue'", true, t::OnQuestion::Answer, "dark blue"},
        {"answer:a:b", true, t::OnQuestion::Answer, "a:b"},
        {R"(answer:"blue)", true, t::OnQuestion::Answer, R"("blue)"},
        {"answer:", false, t::OnQuestion::Fail, ""},
        {"answer:   ", false, t::OnQuestion::Fail, ""},
        {R"(answer:"")", false, t::OnQuestion::Fail, ""},
        {"Fail", false, t::OnQuestion::Fail, ""},
        {"yes", false, t::OnQuestion::Fail, ""},
    }};
    for (const Row& row : rows) {
        INFO(row.text);
        t::AutonomyPolicy policy;
        const std::string refused = t::parse_on_question(row.text, policy);
        CHECK(refused.empty() == row.accepted);
        CHECK(policy.on_question == row.on_question);
        CHECK(policy.answer == row.answer);
        if (!row.accepted) {
            CHECK(has(refused, "--on-question"));
        }
    }
    // `fail` after an answer clears it.
    t::AutonomyPolicy policy;
    REQUIRE(t::parse_on_question("answer:blue", policy).empty());
    REQUIRE(t::parse_on_question("fail", policy).empty());
    CHECK(policy.on_question == t::OnQuestion::Fail);
    CHECK(policy.answer.empty());
    // Completion offers exactly the forms the parser reads.
    REQUIRE(t::on_question_forms().size() == 2);
    CHECK(t::on_question_forms()[0] == "fail");
    CHECK(t::on_question_forms()[1] == "answer:");
}

TEST_CASE("grants are each a tool named once, in order", "[tasks][policy]") {
    CHECK(t::normalized_grants({"write_file", "edit_file", "write_file"}) ==
          std::vector<std::string>{"edit_file", "write_file"});
    CHECK(t::normalized_grants({}).empty());
    t::AutonomyPolicy policy;
    policy.grants = {"edit_file", "write_file"};
    CHECK(policy.grants_tool("write_file"));
    CHECK_FALSE(policy.grants_tool("write"));
    CHECK_FALSE(policy.grants_tool("run_command"));
}

TEST_CASE("the ceiling, exhaustively: no grant is wider than config x agent policy",
          "[tasks][policy][ceiling]") {
    const ToolRegistry available = tools();
    const std::array<std::optional<AgentToolPolicy>, 4> agents{
        std::nullopt, AgentToolPolicy::All, AgentToolPolicy::ReadOnly, AgentToolPolicy::None};
    const std::array<std::optional<PermissionLevel>, 4> levels{
        std::nullopt, PermissionLevel::Ask, PermissionLevel::Allow, PermissionLevel::Deny};
    const std::array<std::string_view, 4> grants{"write_file", "read_file", "fetch_url",
                                                 "wrte_file"};
    int rows = 0;
    int stood = 0;
    for (const std::optional<AgentToolPolicy>& agent : agents) {
        const ToolRegistry offer = offered(available, agent);
        for (const std::optional<PermissionLevel>& level : levels) {
            PermissionsConfig permissions;
            if (level.has_value()) {
                for (const std::string& name : available.names()) {
                    permissions.levels[name] = *level;
                }
            }
            for (const std::string_view grant : grants) {
                ++rows;
                const t::GrantScope scope{.available = &available,
                                          .offered = &offer,
                                          .permissions = &permissions,
                                          .agent = agent.has_value() ? "helper" : "",
                                          .agent_tools = std::string{policy_word(agent)}};
                const std::string refused = t::grant_refusal({std::string{grant}}, scope);
                INFO("agent " << policy_word(agent) << ", level "
                              << (level.has_value() ? apogee::harness::to_string(*level) : "unset")
                              << ", grant " << grant << ": " << refused);

                // What config x agent allow together: a tool the agent
                // keeps, that writes, and that the config does not deny.
                const Tool* tool = available.find(grant);
                const bool exists = tool != nullptr;
                const bool writes = exists && tool->writes;
                const bool agent_keeps = offer.find(grant) != nullptr;
                const bool config_denies = level == PermissionLevel::Deny;
                const bool within = writes && agent_keeps && !config_denies;
                CHECK(refused.empty() == within);
                stood += within ? 1 : 0;

                // Each refusal names its rule.
                if (!exists) {
                    CHECK(has(refused, "no tool named 'wrte_file' in this task"));
                } else if (tool->outbound) {
                    CHECK(has(refused, "asked about per website, never per tool"));
                } else if (!writes) {
                    CHECK(has(refused, "never asks before it runs"));
                } else if (!agent_keeps) {
                    CHECK(has(refused, "the agent 'helper' runs with tools: " +
                                           std::string{policy_word(agent)}));
                    CHECK(has(refused, "a task's grant is never wider than its agent's policy"));
                } else if (config_denies) {
                    CHECK(has(refused, "the config says permissions.write_file: deny"));
                    CHECK(has(refused, "a task's grant is never wider than the config"));
                }
                if (!refused.empty()) {
                    CHECK(refused.starts_with("--allow " + std::string{grant} + ": "));
                }
            }
        }
    }
    CHECK(rows == 64);
    // write_file under no agent or an `all` one, at every level but deny.
    CHECK(stood == 6);
}

TEST_CASE("the ceiling names the first grant that exceeds it, and stands for none",
          "[tasks][policy][ceiling]") {
    const ToolRegistry available = tools();
    PermissionsConfig permissions;
    const t::GrantScope scope{
        .available = &available, .offered = &available, .permissions = &permissions};
    CHECK(t::grant_refusal({}, scope).empty());
    CHECK(t::grant_refusal({"write_file"}, scope).empty());
    const std::string refused = t::grant_refusal({"read_file", "write_file", "wrte_file"}, scope);
    CHECK(refused.starts_with("--allow read_file: "));
    // Every grant is held, not the first alone.
    CHECK(t::grant_refusal({"write_file", "wrte_file"}, scope).starts_with("--allow wrte_file: "));
    // An unknown name lists what could have been granted.
    CHECK(has(t::grant_refusal({"wrte_file"}, scope),
              "the ones that ask before they run: "
              "write_file"));
    // No tools at all: nothing may be named.
    CHECK(has(t::grant_refusal({"write_file"}, t::GrantScope{}), "this task has no tools"));
    const ToolRegistry none;
    CHECK(has(t::grant_refusal({"write_file"}, t::GrantScope{.available = &none, .offered = &none}),
              "none of its tools asks before it runs"));
}

TEST_CASE("the policy is kept in the ledger, and an older ledger reads as the default",
          "[tasks][policy]") {
    t::AutonomyPolicy policy;
    policy.on_question = t::OnQuestion::Answer;
    policy.answer = "blue";
    policy.grants = {"edit_file", "write_file"};
    policy.agent = "helper";
    const nlohmann::json json = t::policy_to_json(policy);
    const t::AutonomyPolicy back = t::policy_from_json(json);
    CHECK(back.on_question == t::OnQuestion::Answer);
    CHECK(back.answer == "blue");
    CHECK(back.grants == policy.grants);
    CHECK(back.agent == "helper");

    const t::AutonomyPolicy plain = t::policy_from_json(t::policy_to_json(t::AutonomyPolicy{}));
    CHECK(plain.on_question == t::OnQuestion::Fail);
    CHECK(plain.grants.empty());
    CHECK_FALSE(t::policy_to_json(t::AutonomyPolicy{}).contains("answer"));

    const t::AutonomyPolicy older = t::policy_from_json(nlohmann::json{});
    CHECK(older.on_question == t::OnQuestion::Fail);
    CHECK(older.grants.empty());
    CHECK(older.agent.empty());

    CHECK(t::policy_from_json(nlohmann::json{{"grants", nlohmann::json::array({"b", "a", "b"})}})
              .grants == std::vector<std::string>{"a", "b"});
    CHECK_THROWS_AS(t::policy_from_json(nlohmann::json::array()), std::runtime_error);
    CHECK_THROWS_AS(t::policy_from_json(nlohmann::json{{"on_question", "maybe"}}),
                    std::runtime_error);
    CHECK_THROWS_AS(t::policy_from_json(nlohmann::json{{"grants", "write_file"}}),
                    std::runtime_error);
    CHECK_THROWS_AS(t::policy_from_json(nlohmann::json{{"grants", nlohmann::json::array({1})}}),
                    std::runtime_error);
}
