#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "agent/tool.h"
#include "contracts/config.h"

/// A task's autonomy policy (27i): the authority the user hands a task
/// before it runs, since nobody is present while it does.
///
/// **Declared up front, scoped to the task, recorded per use** -- never an
/// answer the machine gives live on the user's behalf, which is the rubber
/// stamp the permission gate exists to prevent. Two halves:
///
///  - **Grants** (`task run --allow <tool>`, repeatable): a tool that writes,
///    named one at a time -- there is no `--allow-all`, and its absence is
///    permanent -- resolved from `ask` to `allow` for this task's life. The
///    gate's semantics do not change: a grant is chat's own `session` answer,
///    given at launch, through chat's own composition (`cli/permissions`).
///  - **The question policy** (`--on-question fail|answer:<text>`): `fail`,
///    the default, ends the task on a question nobody answers; `answer:` is
///    one declared answer, served to every question the run asks.
///
/// **The ceiling.** A task is never a way past the config's `permissions:`
/// or the per-agent tool policy of Milestone X: a grant either names a tool
/// both leave askable, or the run is refused naming the rule.
///
/// A declared answer is stored in the ledger like any other field, and shown
/// by `task status` -- a credential does not belong in one.
namespace apogee::tasks {

enum class OnQuestion : std::uint8_t {
    /// A question nobody answers ends the task, naming it.
    Fail,
    /// Every question gets the one declared answer.
    Answer,
};

/// `fail` and `answer`, as the ledger keeps them.
[[nodiscard]] std::string_view to_string(OnQuestion policy) noexcept;

/// What a task was handed before it ran.
struct AutonomyPolicy {
    OnQuestion on_question = OnQuestion::Fail;
    /// The declared answer, when `on_question` is `Answer`.
    std::string answer;
    /// The tools granted, sorted, each once.
    std::vector<std::string> grants;
    /// The agent whose policy the task runs under (`--agent`), or empty.
    std::string agent;

    [[nodiscard]] bool grants_tool(std::string_view tool) const noexcept;
};

/// The forms `--on-question` takes -- `fail`, and `answer:` with the text
/// after it -- as `parse_on_question` reads them and completion offers them.
[[nodiscard]] std::span<const std::string_view> on_question_forms() noexcept;

/// Reads `--on-question`: `fail`, or `answer:<text>` -- the text after the
/// colon taken as it is, one pair of matching quotes around it dropped (the
/// shell already took any a person typed). Fills `policy`; the refusal, or
/// empty.
[[nodiscard]] std::string parse_on_question(std::string_view text, AutonomyPolicy& policy);

/// `grants` sorted and each named once -- `--allow write_file --allow
/// write_file` is one grant.
[[nodiscard]] std::vector<std::string> normalized_grants(std::vector<std::string> grants);

/// What a grant is held to. Every registry is the task's own, built by the
/// composition root.
struct GrantScope {
    /// The task's tools as the config and the suite leave them --
    /// `tools.disabled`, the suite's toolset -- before any agent's policy.
    const agent::ToolRegistry* available = nullptr;
    /// The same after the agent's policy -- Milestone X's filter over the
    /// registry, applied by the composition root, never re-derived here:
    /// what the model is offered.
    const agent::ToolRegistry* offered = nullptr;
    /// The config's `permissions:`.
    const harness::PermissionsConfig* permissions = nullptr;
    /// The agent the task runs under, and its policy word, for the refusal;
    /// empty for none.
    std::string agent;
    std::string agent_tools;
};

/// **The ceiling**: why `grants` would take a task past what the config and
/// its agent allow, naming the rule -- or empty. Each grant must name a tool
/// the task has that writes (the only kind the gate asks about per tool --
/// an outbound tool is asked about per website, a read-only one never), that
/// the agent's policy keeps, and that the config does not deny. A grant of a
/// tool the config already allows is no wider than the config, and stands.
[[nodiscard]] std::string grant_refusal(const std::vector<std::string>& grants,
                                        const GrantScope& scope);

[[nodiscard]] nlohmann::json policy_to_json(const AutonomyPolicy& policy);
/// A ledger written before the policy existed reads as the default: no
/// grants, fail on a question. Throws std::runtime_error on a wrong shape.
[[nodiscard]] AutonomyPolicy policy_from_json(const nlohmann::json& json);

}  // namespace apogee::tasks
