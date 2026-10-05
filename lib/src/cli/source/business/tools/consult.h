#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "agent/tool.h"
#include "agentloop/member_call.h"
#include "contracts/config.h"
#include "harness/harness.h"

/// The `consult` tool (27f): the root model delegating a self-contained
/// brief to a member of its suite, and reading the answer back as an ordinary
/// tool result.
///
/// A thin tool over the shared member-call core (`agentloop/member_call`):
/// the brief is the member's whole context, the call is serial, capped and
/// narrated, and the member is resolved through the one chain under the
/// session's suite. It exists only while the active suite designates members
/// (`consultable:`), names them in its description so a model knows whom to
/// ask -- and once registered it is in 26g's core, offered on every step
/// rather than ranked -- and is ungated -- read-only, local, side-effect-free
/// -- but never unbounded: the per-turn budget and the token caps hold every
/// call.
namespace apogee::tools {

inline constexpr std::string_view kConsultToolName = agentloop::kConsultToolName;

/// The tool's description over the members it offers, naming each with its
/// backend, and the caps.
[[nodiscard]] std::string consult_description(
    const std::vector<agentloop::ConsultableMember>& offered, const harness::ConsultLimits& limits);

/// The tool, spending from `calls`, offering `offered` (each member that can
/// be consulted here) under `limits`.
[[nodiscard]] agent::Tool make_consult_tool(
    std::shared_ptr<agentloop::MemberCalls> calls,
    const std::vector<agentloop::ConsultableMember>& offered, const harness::ConsultLimits& limits);

/// What the active suite of `harness` offers: the description `consult`
/// would carry -- empty when no member can be consulted, and then no tool --
/// and a line for each consultable member that cannot be, saying why.
struct ConsultOffer {
    std::string description;
    std::vector<std::string> notes;
};

[[nodiscard]] ConsultOffer consult_offer(const harness::Harness& harness);

/// Adds `consult` to `registry` when the active suite of `harness`
/// designates a member that can be consulted here -- after any toolset pin is
/// applied, since a suite's `consultable:` is its own switch -- and returns
/// the offer it made. A registry that already holds one is left as it is.
ConsultOffer register_consult_tool(agent::ToolRegistry& registry, const harness::Harness& harness,
                                   const std::shared_ptr<agentloop::MemberCalls>& calls);

}  // namespace apogee::tools
