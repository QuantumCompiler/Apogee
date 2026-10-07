#include "tools/consult.h"

#include <nlohmann/json.hpp>

#include <utility>

#include "tools/args.h"

namespace apogee::tools {
namespace {

/// The members that can be consulted here, of `members`.
std::vector<agentloop::ConsultableMember> offered_of(
    const std::vector<agentloop::ConsultableMember>& members) {
    std::vector<agentloop::ConsultableMember> out;
    for (const agentloop::ConsultableMember& member : members) {
        if (member.unavailable.empty()) {
            out.push_back(member);
        }
    }
    return out;
}

/// The active suite's caps, or the defaults with none.
harness::ConsultLimits active_limits(const harness::Harness& harness) {
    const harness::SuiteConfig* suite = harness::active_suite(harness.config());
    return suite == nullptr ? harness::ConsultLimits{}
                            : harness::consult_limits(suite->consult_caps);
}

}  // namespace

std::string consult_description(const std::vector<agentloop::ConsultableMember>& offered,
                                const harness::ConsultLimits& limits) {
    std::string members;
    for (const agentloop::ConsultableMember& member : offered) {
        members += members.empty() ? "" : ", ";
        members += member.role + " (" + member.backend + ")";
    }
    return "Ask another model in your suite a question and get its answer back. Members you can "
           "consult: " +
           members +
           ". The member sees only the question you write -- not this conversation, not any "
           "file, not your tools -- so put every fact it needs into the question. It answers "
           "in at most " +
           std::to_string(limits.answer_tokens) + " tokens; a question may run to about " +
           std::to_string(limits.brief_tokens) + " tokens; at most " +
           std::to_string(limits.per_turn) + " consults per turn.";
}

agent::Tool make_consult_tool(std::shared_ptr<agentloop::MemberCalls> calls,
                              const std::vector<agentloop::ConsultableMember>& offered,
                              const harness::ConsultLimits& limits) {
    agent::Tool tool;
    tool.name = std::string{kConsultToolName};
    tool.description = consult_description(offered, limits);
    nlohmann::json roles = nlohmann::json::array();
    for (const agentloop::ConsultableMember& member : offered) {
        roles.push_back(member.role);
    }
    const nlohmann::json schema{
        {"type", "object"},
        {"properties",
         {{"member",
           {{"type", "string"}, {"enum", roles}, {"description", "The member to ask, by role"}}},
          {"question",
           {{"type", "string"},
            {"description",
             "The whole brief: everything the member needs, since it sees nothing else"}}}}},
        {"required", {"member", "question"}}};
    tool.parameters_schema = schema.dump();
    // Read-only, local and side-effect-free: no gate (27f, default taken).
    tool.writes = false;
    tool.outbound = false;
    tool.run = [calls = std::move(calls)](std::string_view arguments) -> agent::ToolOutcome {
        agent::ToolOutcome failure;
        const std::optional<Arguments> args =
            parse_arguments(arguments, R"({"member": "utility", "question": "..."})", failure);
        if (!args.has_value()) {
            return failure;
        }
        const std::string member = args->string("member");
        if (member.empty()) {
            return error("member is required: the role of the member to ask");
        }
        if (!args->has("question")) {
            return error("question is required: the whole brief the member needs");
        }
        const agentloop::MemberAnswer answer = calls->consult(member, args->string("question"));
        if (!answer.refused.empty()) {
            // A stated limit, not a failure: the model reads why and goes on.
            return agent::ToolOutcome{.content = "Not consulted: " + answer.refused + ".",
                                      .is_error = false};
        }
        if (!answer.failed.empty()) {
            return error(member + " (" + answer.backend + ") could not answer: " + answer.failed);
        }
        std::string out = member + " (" + answer.backend + ") answered:\n" + answer.text;
        if (answer.cut) {
            out += "\n[the answer reached its token cap and was cut there]";
        }
        return ok(std::move(out));
    };
    return tool;
}

ConsultOffer consult_offer(const harness::Harness& harness) {
    ConsultOffer offer;
    const std::vector<agentloop::ConsultableMember> members =
        agentloop::consultable_members(harness);
    for (const agentloop::ConsultableMember& member : members) {
        if (!member.unavailable.empty()) {
            offer.notes.push_back("consult: not offering " + member.role + " -- " +
                                  member.unavailable);
        }
    }
    const std::vector<agentloop::ConsultableMember> offered = offered_of(members);
    if (!offered.empty()) {
        offer.description = consult_description(offered, active_limits(harness));
    }
    return offer;
}

ConsultOffer register_consult_tool(agent::ToolRegistry& registry, const harness::Harness& harness,
                                   const std::shared_ptr<agentloop::MemberCalls>& calls) {
    ConsultOffer offer = consult_offer(harness);
    if (offer.description.empty() || registry.find(kConsultToolName) != nullptr) {
        return offer;
    }
    registry.add(make_consult_tool(calls, offered_of(agentloop::consultable_members(harness)),
                                   active_limits(harness)));
    return offer;
}

}  // namespace apogee::tools
