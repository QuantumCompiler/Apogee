#include "agent/tool.h"

#include <stdexcept>
#include <string>
#include <utility>

#include "contracts/sha256.h"
#include "contracts/utf8.h"

namespace apogee::agent {

void ToolRegistry::add(Tool tool) {
    if (tool.name.empty()) {
        throw std::invalid_argument("ToolRegistry::add: a tool needs a name");
    }
    if (!tool.run && !tool.run_gated) {
        throw std::invalid_argument("ToolRegistry::add: tool '" + tool.name +
                                    "' has no implementation");
    }
    if (tool.outbound && !tool.describe_target) {
        // The gate decides an outbound call by where it goes. A tool that
        // cannot say would be refused on every call; better to fail here.
        throw std::invalid_argument("ToolRegistry::add: outbound tool '" + tool.name +
                                    "' names no target");
    }
    const std::string name = tool.name;
    const auto [unused, inserted] = tools_.emplace(name, std::move(tool));
    if (!inserted) {
        throw std::invalid_argument("ToolRegistry::add: duplicate tool name '" + name + "'");
    }
}

const Tool* ToolRegistry::find(std::string_view name) const noexcept {
    const auto it = tools_.find(name);
    return it == tools_.end() ? nullptr : &it->second;
}

std::vector<std::string> ToolRegistry::names() const {
    std::vector<std::string> out;
    out.reserve(tools_.size());
    for (const auto& [name, unused] : tools_) {
        out.push_back(name);
    }
    return out;
}

void ToolRegistry::set_environment(EnvironmentRender render) {
    environment_ = std::move(render);
}

std::string ToolRegistry::environment() const {
    return environment_ ? environment_(*this) : std::string{};
}

std::vector<harness::Tool> ToolRegistry::definitions() const {
    std::vector<harness::Tool> out;
    out.reserve(tools_.size());
    for (const auto& [name, tool] : tools_) {
        out.push_back(harness::Tool{tool.name, tool.description, tool.parameters_schema});
    }
    return out;
}

std::map<std::string, std::string, std::less<>> ToolRegistry::definition_hashes() const {
    std::map<std::string, std::string, std::less<>> out;
    for (const harness::Tool& definition : definitions()) {
        out.emplace(definition.name, definition_hash(definition));
    }
    return out;
}

std::string_view to_string(ToolCallState state) noexcept {
    switch (state) {
        case ToolCallState::Running:
            return "running";
        case ToolCallState::Done:
            return "done";
        case ToolCallState::Failed:
            return "failed";
        case ToolCallState::Pending:
            break;
    }
    return "pending";
}

bool gated(const Tool& tool) noexcept {
    return tool.writes || tool.outbound;
}

std::string definition_hash(const harness::Tool& definition) {
    // NUL-separated: no field can carry one, so two definitions that differ
    // only in where one field ends cannot hash alike.
    std::string bytes = definition.name;
    bytes += '\0';
    bytes += definition.description;
    bytes += '\0';
    bytes += definition.parameters_schema;
    return models::sha256_hex(bytes);
}

namespace {

/// The gate's resolution of one target. `late_detail`, when set, is what the
/// person asked is shown beside it, worked out only once someone is about to
/// be asked (52a): a file writer's diff reads the file, and a call the
/// checker settles -- allowed by the config, accept-edits, an `always` --
/// never shows it.
bool resolve(const Tool& tool, std::string_view target, std::string_view detail,
             const std::function<std::string()>& late_detail, const DispatchContext& context,
             std::string_view call_id) {
    // An address the gate cannot read is not one it can allow. The tool's own
    // parser is what named the target, so a call it could not name one for is
    // a call it would refuse anyway; denying here keeps that true even for a
    // tool that forgot to.
    if (tool.outbound && target.empty()) {
        return false;
    }

    const GateRequest request{tool.name, target, detail, tool.outbound, tool.edits, call_id};
    const Permission decision = context.permission ? context.permission(request) : Permission::Ask;

    switch (decision) {
        case Permission::Allow:
            return true;
        case Permission::Deny:
            return false;
        case Permission::Ask:
            break;
    }

    // Ask with nobody to ask resolves to DENY. A pipe, a cron job, or `serve`
    // has no user present, and allowing a destructive operation because no one
    // was around to object is the wrong direction to fail.
    if (!context.confirm) {
        return false;
    }
    if (!late_detail) {
        return context.confirm(request);
    }
    const std::string shown = late_detail();
    return context.confirm(
        GateRequest{tool.name, target, shown, tool.outbound, tool.edits, call_id});
}

}  // namespace

bool permitted_target(const Tool& tool, std::string_view target, std::string_view detail,
                      const DispatchContext& context, std::string_view call_id) {
    return resolve(tool, target, detail, {}, context, call_id);
}

namespace {

/// The surface's reason for a refusal, as a sentence ending in a space, or
/// empty (39b).
std::string refusal_reason(const Tool& tool, std::string_view target, std::string_view detail,
                           const DispatchContext& context) {
    if (!context.refusal) {
        return {};
    }
    std::string reason =
        context.refusal(GateRequest{tool.name, target, detail, tool.outbound, tool.edits});
    return reason.empty() ? reason : reason + " ";
}

}  // namespace

bool permitted(const Tool& tool, std::string_view arguments, const DispatchContext& context,
               std::string_view call_id) {
    // A read-only tool never prompts. Asking about every read trains the user
    // to approve without reading, which is worse than not asking.
    if (!gated(tool)) {
        return true;
    }
    const std::string target =
        tool.describe_target ? tool.describe_target(arguments) : std::string{};
    if (!tool.describe_detail) {
        return resolve(tool, target, {}, {}, context, call_id);
    }
    return resolve(
        tool, target, {}, [&tool, arguments] { return tool.describe_detail(arguments); }, context,
        call_id);
}

ToolOutcome dispatch(const ToolRegistry& registry, const harness::ToolCall& call,
                     const DispatchContext& context) {
    // Each call's state, said as it moves (56a): pending before its gate,
    // running where `[tool] <name>` is said, then done or failed.
    const auto report = [&context, &call](ToolCallState state, std::string_view target) {
        if (context.on_call) {
            context.on_call(ToolCallReport{call.id, call.name, state, target});
        }
    };
    const Tool* tool = registry.find(call.name);
    if (tool == nullptr) {
        report(ToolCallState::Pending, {});
        report(ToolCallState::Failed, {});
        // A hallucinated tool name is an ERROR RESULT, not an exception. The
        // model reads it and picks a real tool; aborting the turn would throw
        // away the conversation over a name.
        std::string known;
        for (const std::string& name : registry.names()) {
            known += known.empty() ? "" : ", ";
            known += name;
        }
        return ToolOutcome{"Error: no tool named '" + call.name + "'." +
                               (known.empty() ? "" : " Available tools: " + known + "."),
                           true};
    }

    // What the call acts on, as the gate will be told it.
    const std::string named =
        tool->describe_target ? tool->describe_target(call.arguments) : std::string{};
    report(ToolCallState::Pending, named);
    // A gate that throws ends the call `failed` before the turn unwinds: the
    // turn cancelled while its question was open (`CancelledError`), or a
    // driver gone mid-question. The call never ran, and no surface is left
    // drawing it as `pending` -- a cancelled question is a no, as the
    // question's own answers have it.
    const auto asked = [&report, &named](const auto& gate) {
        try {
            return gate();
        } catch (...) {
            report(ToolCallState::Failed, named);
            throw;
        }
    };

    if (tool->outbound) {
        const std::string& target = named;
        if (target.empty()) {
            report(ToolCallState::Failed, named);
            return ToolOutcome{"Error: could not tell which website '" + call.name +
                                   "' would reach from these arguments, so nothing was sent. "
                                   "Give an absolute http or https URL.",
                               true};
        }
        const std::string detail =
            tool->describe_detail ? tool->describe_detail(call.arguments) : std::string{};
        if (!asked([&] { return permitted_target(*tool, target, detail, context, call.id); })) {
            report(ToolCallState::Failed, named);
            return ToolOutcome{"Error: permission to reach " + target +
                                   " was not given, so nothing was sent to it. " +
                                   refusal_reason(*tool, target, detail, context) +
                                   "Do not retry it; continue without it or ask what to do "
                                   "instead.",
                               true};
        }
    } else if (!asked([&] { return permitted(*tool, call.arguments, context, call.id); })) {
        report(ToolCallState::Failed, named);
        const std::string reason = refusal_reason(*tool, named, {}, context);
        return ToolOutcome{
            (reason.empty() ? "Error: the user denied permission to run '" + call.name + "'. "
                            : "Error: '" + call.name + "' was not run: " + reason) +
                "Do not retry it; continue without it or ask what to do "
                "instead.",
            true};
    }

    if (context.on_status) {
        context.on_status("[tool] " + call.name);
    }
    report(ToolCallState::Running, named);

    ToolOutcome outcome;
    try {
        if (tool->run_gated) {
            const TargetGate gate = [tool, &context, &call](std::string_view target,
                                                            std::string_view detail) {
                return permitted_target(*tool, target, detail, context, call.id);
            };
            outcome = tool->run_gated(call.arguments, gate);
        } else {
            outcome = tool->run(call.arguments);
        }
    } catch (const std::exception& e) {
        // A tool that throws despite the contract must not take the turn down
        // with it.
        outcome = ToolOutcome{std::string{"Error: "} + e.what(), true};
    }
    // What the tool found, made text: history, the session file, the next
    // request and an MCP client's frame are each a strict JSON dump.
    outcome.content = harness::valid_utf8(outcome.content);
    report(outcome.is_error ? ToolCallState::Failed : ToolCallState::Done, named);

    if (context.on_status) {
        context.on_status("");
    }
    return outcome;
}

}  // namespace apogee::agent
