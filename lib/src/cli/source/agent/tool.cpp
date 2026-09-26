#include "agent/tool.h"

#include <stdexcept>
#include <utility>

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

std::vector<harness::Tool> ToolRegistry::definitions() const {
    std::vector<harness::Tool> out;
    out.reserve(tools_.size());
    for (const auto& [name, tool] : tools_) {
        out.push_back(harness::Tool{tool.name, tool.description, tool.parameters_schema});
    }
    return out;
}

bool gated(const Tool& tool) noexcept {
    return tool.writes || tool.outbound;
}

bool permitted_target(const Tool& tool, std::string_view target, std::string_view detail,
                      const DispatchContext& context) {
    // An address the gate cannot read is not one it can allow. The tool's own
    // parser is what named the target, so a call it could not name one for is
    // a call it would refuse anyway; denying here keeps that true even for a
    // tool that forgot to.
    if (tool.outbound && target.empty()) {
        return false;
    }

    const GateRequest request{tool.name, target, detail, tool.outbound};
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
    return context.confirm(request);
}

bool permitted(const Tool& tool, std::string_view arguments, const DispatchContext& context) {
    // A read-only tool never prompts. Asking about every read trains the user
    // to approve without reading, which is worse than not asking.
    if (!gated(tool)) {
        return true;
    }
    const std::string target =
        tool.describe_target ? tool.describe_target(arguments) : std::string{};
    const std::string detail =
        tool.describe_detail ? tool.describe_detail(arguments) : std::string{};
    return permitted_target(tool, target, detail, context);
}

ToolOutcome dispatch(const ToolRegistry& registry, const harness::ToolCall& call,
                     const DispatchContext& context) {
    const Tool* tool = registry.find(call.name);
    if (tool == nullptr) {
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

    if (tool->outbound) {
        const std::string target = tool->describe_target(call.arguments);
        if (target.empty()) {
            return ToolOutcome{"Error: could not tell which website '" + call.name +
                                   "' would reach from these arguments, so nothing was sent. "
                                   "Give an absolute http or https URL.",
                               true};
        }
        const std::string detail =
            tool->describe_detail ? tool->describe_detail(call.arguments) : std::string{};
        if (!permitted_target(*tool, target, detail, context)) {
            return ToolOutcome{"Error: permission to reach " + target +
                                   " was not given, so nothing was sent to it. Do not retry "
                                   "it; continue without it or ask what to do instead.",
                               true};
        }
    } else if (!permitted(*tool, call.arguments, context)) {
        return ToolOutcome{"Error: the user denied permission to run '" + call.name +
                               "'. Do not retry it; continue without it or ask what to do "
                               "instead.",
                           true};
    }

    if (context.on_status) {
        context.on_status("[tool] " + call.name);
    }

    ToolOutcome outcome;
    try {
        if (tool->run_gated) {
            const TargetGate gate = [tool, &context](std::string_view target,
                                                     std::string_view detail) {
                return permitted_target(*tool, target, detail, context);
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

    if (context.on_status) {
        context.on_status("");
    }
    return outcome;
}

}  // namespace apogee::agent
