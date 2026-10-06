#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "agent/tool.h"
#include "agentloop/member_call.h"
#include "contracts/config.h"
#include "harness/harness.h"
#include "symphony/definition.h"

/// Symphonies as tools (27t): the Orchestrator.
///
/// **Registration and policy, nothing more.** Each symphony's definition
/// projects an ordinary tool -- `play_<name>`, the definition's own
/// description, its input contract as the argument schema -- and an execute
/// session that orchestrates adds them to its root model's registry. The
/// ordinary agent loop is then the orchestration engine: the model choosing
/// among tools is shipped, the loop's constrained calls hold the choice to
/// real names and valid arguments, and a chosen play runs through the one
/// walk (`symphony/runner`), its stages the same bounded, brief-only member
/// calls a consult makes. No planner, no second loop, no new wire shape: a
/// play the model starts is a tool call, said as one.
///
/// **The model's initiative never spends.** A play the model starts runs with
/// `PlayOptions::local_only` on, so a member billed per call is refused at
/// the stage; and before that, a symphony that reaches a member which cannot
/// answer here -- billed per call, not built, nothing named -- is not offered
/// at all, and the session says why (`orchestra_offer`). The config-time
/// refusal of `orchestrate: true` asks the same question of every member a
/// symphony reaches (`reached_members`), by the provider's own word.
///
/// **One budget.** A play draws on the turn's member calls -- the suite's
/// `consult_caps.per_turn`, which consults and validation already share -- and
/// on `symphony_caps` as a whole play does (27r). A play the budget cannot
/// finish is refused before its first call, said, and the model answers
/// without it.
///
/// **Off means absent.** Nothing here runs unless the session orchestrates;
/// a registry built without it holds no `play_` tool at all.
namespace apogee::symphony {

/// What every symphony's tool is called after: `play_<name>`.
inline constexpr std::string_view kPlayToolPrefix = agentloop::kPlayToolPrefix;

/// The longest tool name every vendor takes.
inline constexpr std::size_t kMaxToolName = 64;

/// The tool `symphony` projects: `play_<symphony>`.
[[nodiscard]] std::string play_tool_name(std::string_view symphony);

/// Whether a session under `config` orchestrates: `--orchestrate` (`flag`),
/// or the active suite's `orchestrate: true`.
[[nodiscard]] bool orchestrating(const harness::Config& config, bool flag);

/// The paragraph the environment note gains while the registry asking holds a
/// symphony's tool (26p's composition): the root model is the Orchestrator --
/// what a play is, that a symphony sees only the input it is given, and to
/// answer itself when no symphony fits.
[[nodiscard]] std::string_view orchestrator_framing() noexcept;

/// One member a symphony reaches: a role stage of the symphony, or of one it
/// plays (27r), and the backend the one chain gives that role.
struct ReachedMember {
    /// Whose stage it is: the symphony itself, or one it plays.
    std::string symphony;
    std::string stage;
    std::string role;
    /// The backend the one chain resolves the role to, under `config`'s
    /// active suite with `conversation` as the conversation; empty when
    /// nothing answers for it.
    std::string backend;
};

/// Every member `spec` reaches -- its role stages, then those of each
/// symphony it plays, in the order the walk reaches them -- each role stage
/// once. `spec` must walk soundly through `catalog`.
[[nodiscard]] std::vector<ReachedMember> reached_members(const harness::Config& config,
                                                         const harness::SymphonySpec& spec,
                                                         const Catalog& catalog,
                                                         std::string_view conversation);

/// Where `member` sits, for a reason that names it: `its verify stage
/// (chat)`, or `summarize-verify's verify stage (chat)` for a stage of a
/// symphony `root` plays.
[[nodiscard]] std::string reached_at(std::string_view root, const ReachedMember& member);

/// Why `spec` cannot be offered as a tool in any session, or empty: it cannot
/// be played (its own problems, or its walk's through `catalog`), it takes an
/// image -- which a tool call cannot give -- or its name makes a tool name
/// past `kMaxToolName`.
[[nodiscard]] std::string unprojectable(const harness::SymphonySpec& spec, const Catalog& catalog);

/// The tool's description: the definition's own, then what playing it is --
/// how many member calls, on which roles, and that it sees only its input.
[[nodiscard]] std::string play_tool_description(const harness::SymphonySpec& spec,
                                                const Catalog& catalog);

/// The tool's argument schema: the input contract -- one `input` string,
/// described as the definition describes it.
[[nodiscard]] std::string play_tool_schema(const harness::SymphonySpec& spec);

/// What a play the model starts is played with.
struct PlayToolContext {
    /// The conversation's member calls: the turn the loop opens, its budget
    /// and its narration.
    std::shared_ptr<agentloop::MemberCalls> calls;
    /// The conversation's backend, asked at each play: a helper role the
    /// suite leaves out falls back to it, as the one chain says. Null for
    /// none.
    std::function<std::string()> conversation;
};

/// The tool `definition` projects, playing it -- every symphony it plays
/// found in `catalog` -- on `harness`'s active suite through `context`.
/// Ungated: a symphony's stages call no tools, so a play reads nothing and
/// writes nothing the gate would be asked about.
[[nodiscard]] agent::Tool make_play_tool(const harness::Harness& harness,
                                         const Definition& definition, const Catalog& catalog,
                                         PlayToolContext context);

/// A symphony not offered, and why.
struct Withheld {
    std::string symphony;
    std::string reason;
    /// A property of the definition -- it takes an image -- rather than of
    /// this session's members or caps: nothing to fix, so said only when
    /// asked for (`--verbose`).
    bool structural = false;

    bool operator==(const Withheld&) const = default;
};

/// What orchestration offers a session: the symphonies whose tools it adds,
/// in the catalog's order, and each one it does not with why.
struct OrchestraOffer {
    std::vector<std::string> offered;
    std::vector<Withheld> withheld;

    bool operator==(const OrchestraOffer&) const = default;
};

/// What `catalog` offers a session on `harness` under its active suite, the
/// conversation on `conversation`: each symphony that can be projected
/// (`unprojectable`), whose every member can answer here -- configured,
/// built, and local and unmetered by its provider's word -- and whose whole
/// play fits a turn's budget (`consult_caps.per_turn`) and a play's
/// (`symphony_caps.stage_calls`). Nothing with no suite active.
[[nodiscard]] OrchestraOffer orchestra_offer(const harness::Harness& harness,
                                             const Catalog& catalog, std::string_view conversation);

/// Adds the tool of each symphony `orchestra_offer` offers to `registry` --
/// one it already holds left as it is -- and has its environment note say
/// the framing while it holds any; returns the offer.
OrchestraOffer register_play_tools(agent::ToolRegistry& registry, const harness::Harness& harness,
                                   const Catalog& catalog, const PlayToolContext& context);

}  // namespace apogee::symphony
