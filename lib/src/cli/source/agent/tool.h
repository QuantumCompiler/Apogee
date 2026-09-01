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

/// The permission decision for a destructive operation.
enum class Permission : std::uint8_t { Allow, Deny, Ask };

/// Consulted before a tool that declares `writes` runs.
///
/// Returns the decision for one operation on one target. Injected rather than
/// read from config here: the config schema for permissions lands with the
/// native filesystem toolsets, which are the gate's first real consumer.
using PermissionChecker = std::function<Permission(std::string_view tool, std::string_view target)>;

/// Prompts the user to confirm a destructive operation. Non-null only on an
/// interactive surface.
using ConfirmFn = std::function<bool(std::string_view tool, std::string_view target)>;

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

    /// The implementation. Receives the raw JSON argument string.
    ///
    /// **Must not throw for a bad argument** -- return an error outcome so the
    /// model can correct itself.
    std::function<ToolOutcome(std::string_view arguments)> run;

    /// Extracts the operation's target (a path, a URL) for the permission
    /// prompt. Optional; without it the prompt names only the tool.
    std::function<std::string(std::string_view arguments)> describe_target;
};

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
/// wrong direction to fail.
[[nodiscard]] bool permitted(const Tool& tool, std::string_view arguments,
                             const DispatchContext& context);

}  // namespace apogee::agent
