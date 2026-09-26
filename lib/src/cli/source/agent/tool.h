#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "harness/types.h"

/// Tools the model can call, and the registry that dispatches them.
///
/// Separate from `agentloop/` on purpose: the loop needs a registry, but a
/// registry needs no loop. Native filesystem toolsets and the MCP client land
/// later and register into this same place without the loop changing at all.
namespace apogee::agent {

/// What a tool did.
struct ToolOutcome {
    /// Text handed back to the model as the tool result.
    std::string content;
    /// Whether this represents a failure. An error is still a RESULT, not an
    /// exception -- the model is told what went wrong and gets to react, which
    /// is the whole point of a tool loop. Aborting the turn instead would throw
    /// away the conversation over a bad argument.
    bool is_error = false;
};

/// The permission decision for a gated operation.
enum class Permission : std::uint8_t { Allow, Deny, Ask };

/// One question put to the gate: may this tool act on this target?
struct GateRequest {
    std::string_view tool;
    /// What the decision is keyed on: a path, a command -- or, for an
    /// outbound tool, a host.
    std::string_view target;
    /// Shown beside the target, never decided on: `fetch_url`'s whole URL,
    /// so the person asked can see what would leave the machine in it.
    std::string_view detail;
    /// The tool sends data off the machine (`Tool::outbound`). Its answers
    /// are per host rather than per tool: allowing one website is not
    /// allowing the next.
    bool outbound = false;
};

/// Consulted before a gated tool runs -- one that `writes`, or is `outbound`.
///
/// Returns the decision for one operation on one target. Injected rather than
/// read from config here: `agent/` knows nothing about config files, and each
/// surface makes its checker from the `permissions:` and `tools:` sections
/// (`commands/permissions.h`).
using PermissionChecker = std::function<Permission(const GateRequest& request)>;

/// Asks someone to confirm a gated operation. Non-null only on a surface with
/// someone to ask.
using ConfirmFn = std::function<bool(const GateRequest& request)>;

/// Puts one more target through the gate while a tool runs: `fetch_url`'s
/// redirect to a new host. The same checker and the same prompt the call's
/// own target went through; true means go ahead.
using TargetGate = std::function<bool(std::string_view target, std::string_view detail)>;

/// One callable tool.
struct Tool {
    std::string name;
    std::string description;
    /// JSON Schema for the arguments, verbatim.
    std::string parameters_schema = R"({"type":"object","properties":{}})";

    /// Whether this tool mutates the filesystem or anything else destructive.
    /// A `writes` tool goes through the permission gate; a read-only one does
    /// not, because prompting for every read trains the user to say yes.
    bool writes = false;

    /// Whether this tool sends data off the machine -- `fetch_url`. Outbound
    /// is its own risk, separate from `writes`: a read-only tool that can put
    /// a file's contents into a URL is an exfiltration path, so it is gated
    /// too, per target host, while a `read-only` agent policy (which drops
    /// `writes` tools) still keeps it (2026-09-25).
    bool outbound = false;

    /// The implementation. Receives the raw JSON argument string.
    ///
    /// **Must not throw for a bad argument** -- return an error outcome so the
    /// model can correct itself.
    std::function<ToolOutcome(std::string_view arguments)> run;

    /// The implementation for a tool that reaches further targets while it
    /// runs, which it puts through `gate` one at a time -- a redirect's next
    /// host. Set instead of `run`; dispatch hands it the gate.
    std::function<ToolOutcome(std::string_view arguments, const TargetGate& gate)> run_gated;

    /// Extracts the operation's target (a path, a command, a host) for the
    /// permission gate. Optional for a `writes` tool; without it the prompt
    /// names only the tool. **An outbound tool must have one**, and a call
    /// it names no target for is refused unrun: an address the gate cannot
    /// read is not one it can allow.
    std::function<std::string(std::string_view arguments)> describe_target;

    /// More for the prompt to show than the target it is decided on -- the
    /// URL beside the host. Optional.
    std::function<std::string(std::string_view arguments)> describe_detail;
};

/// Whether `tool` goes through the permission gate: it writes, or it reaches
/// off the machine.
[[nodiscard]] bool gated(const Tool& tool) noexcept;

/// The set of tools available to a run.
class ToolRegistry {
public:
    /// Registers `tool`. Throws std::invalid_argument on a duplicate name --
    /// a silently shadowed tool is a bug found much later, if ever.
    void add(Tool tool);

    [[nodiscard]] const Tool* find(std::string_view name) const noexcept;
    [[nodiscard]] std::vector<std::string> names() const;

    [[nodiscard]] std::size_t size() const noexcept {
        return tools_.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        return tools_.empty();
    }

    /// The registry rendered as IR tool definitions, for the outgoing request.
    [[nodiscard]] std::vector<harness::Tool> definitions() const;

private:
    std::map<std::string, Tool, std::less<>> tools_;
};

/// Everything dispatch needs beyond the registry itself.
struct DispatchContext {
    PermissionChecker permission;
    ConfirmFn confirm;
    /// Reports progress. Optional.
    std::function<void(std::string_view)> on_status;
};

/// Runs one tool call.
///
/// Never throws for a tool-level problem: an unknown tool, a bad argument, or a
/// tool that failed all come back as error outcomes the model can read and act
/// on. That is what lets a model recover from its own hallucinated tool name
/// instead of the turn dying.
[[nodiscard]] ToolOutcome dispatch(const ToolRegistry& registry, const harness::ToolCall& call,
                                   const DispatchContext& context);

/// Resolves the permission gate for one call.
///
/// `Ask` with no confirm function resolves to **deny**. A non-interactive
/// surface -- a pipe, a cron job, `serve` -- has nobody to ask, and silently
/// allowing a destructive operation because no one was around to object is the
/// wrong direction to fail. An outbound call with no target is denied too.
[[nodiscard]] bool permitted(const Tool& tool, std::string_view arguments,
                             const DispatchContext& context);

/// The same resolution for one named target -- what a `TargetGate` asks.
[[nodiscard]] bool permitted_target(const Tool& tool, std::string_view target,
                                    std::string_view detail, const DispatchContext& context);

}  // namespace apogee::agent
