#include "symphony/tools.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include "agentloop/side_call.h"
#include "contracts/symphony_walk.h"
#include "harness/roles.h"
#include "symphony/runner.h"

namespace apogee::symphony {
namespace {

/// The framing a registry holding a symphony's tool adds to its note (27t).
/// Under 512 bytes, as 26p's policy paragraph is: the note heads every
/// request, and the budget never trims it.
constexpr std::string_view kOrchestratorFraming =
    "You are the orchestrator of a suite of models. Each play_ tool plays a symphony: a staged "
    "process in which your suite's models work through an input in turn. When a request is the "
    "work a symphony is for, play it rather than doing that work yourself -- give it the whole "
    "text it needs as its input, since it sees nothing else -- then answer the user from its "
    "output. Anything else, answer yourself, without a tool.";

bool blank(std::string_view text) {
    return text.find_first_not_of(" \t\r\n") == std::string_view::npos;
}

/// Whether `registry` holds any symphony's tool.
bool holds_play_tool(const agent::ToolRegistry& registry) {
    return std::ranges::any_of(registry.names(), [](const std::string& name) {
        return name.starts_with(kPlayToolPrefix);
    });
}

/// `1 member call`, `2 member calls`.
std::string member_calls(std::int64_t count) {
    return std::to_string(count) + (count == 1 ? " member call" : " member calls");
}

/// Why `member` cannot answer a play the model starts here, or empty: nothing
/// answers for its role, a backend the config does not have or this session
/// could not build, or one billed per call -- the model's initiative never
/// spends. The stage call refuses the same at play time (`local_only`); this
/// is the offer's word, said before the model is told the symphony exists.
std::string cannot_answer(const harness::Harness& harness, std::string_view root,
                          const ReachedMember& member, const std::vector<std::string>& built) {
    const std::string at = reached_at(root, member);
    if (member.backend.empty()) {
        return at + " has no backend to answer for it";
    }
    const auto entry = harness.config().backends.find(member.backend);
    if (entry == harness.config().backends.end()) {
        return at + " names '" + member.backend + "', which is not a configured backend";
    }
    if (std::ranges::find(built, entry->first) == built.end()) {
        return at + " is '" + entry->first + "', which could not be built in this session";
    }
    if (harness.generation_is_metered(entry->first)) {
        return at + " is '" + entry->first +
               "', billed per call -- a play the model starts runs on its initiative, which "
               "never spends";
    }
    return {};
}

/// The tokens a play's stages produced, when every member said; nullopt when
/// any did not -- a side call's figure is never an estimate (26n).
// NOLINTNEXTLINE(misc-no-recursion): bounded by the nesting the walk was checked to.
std::optional<std::int64_t> reported_tokens(const std::vector<StageResult>& stages) {
    std::int64_t total = 0;
    for (const StageResult& stage : stages) {
        if (!stage.play.empty()) {
            const std::optional<std::int64_t> inner = reported_tokens(stage.stages);
            if (!inner.has_value()) {
                return std::nullopt;
            }
            total += *inner;
        } else if (stage.tokens.has_value()) {
            total += *stage.tokens;
        } else {
            return std::nullopt;
        }
    }
    return total;
}

/// What a play tool holds: its definition, the catalog its plays resolve
/// against, and what it plays with.
struct Held {
    harness::SymphonySpec spec;
    Catalog catalog;
    PlayToolContext context;
};

/// One play the model asked for: refused, said and answered without when the
/// turn cannot afford it; played on local members otherwise, the play its
/// labeled line in the turn's narration and each stage a line under it.
agent::ToolOutcome play_for_model(const harness::Harness& harness,
                                  const harness::SymphonySpec& spec, const Catalog& catalog,
                                  const PlayToolContext& context, std::string_view arguments) {
    const nlohmann::json args = nlohmann::json::parse(arguments, nullptr, false);
    const auto input = args.is_object() ? args.find("input") : args.end();
    if (!args.is_object() || input == args.end() || !input->is_string()) {
        return agent::ToolOutcome{.content = "Error: input is required -- the whole text " +
                                             spec.name +
                                             R"( works on, as a string: {"input": "..."})",
                                  .is_error = true};
    }
    if (blank(input->get_ref<const std::string&>())) {
        return agent::ToolOutcome{.content = "Error: input is empty -- give " + spec.name +
                                             " the whole text it works on, since it sees "
                                             "nothing else",
                                  .is_error = true};
    }
    agentloop::MemberCalls& calls = *context.calls;
    // A stated limit, not a failure: the model reads why and answers without
    // the play, and the surface keeps the line -- never silent.
    const auto refuse = [&](const std::string& why) {
        calls.notice("orchestrate: " + spec.name + " not played -- " + why);
        return agent::ToolOutcome{.content = "Not played: " + why + ". Answer without it.",
                                  .is_error = false};
    };
    if (!calls.in_turn()) {
        return refuse("no turn is open to play it in");
    }

    // One budget family (27f, 27g): the turn's member calls, which consults
    // and checks spend too -- and a whole play's own caps (27r). A play the
    // budget cannot finish is refused before its first call: every stage is
    // called unless one fails, so its static count is what it spends.
    const harness::Config& config = harness.config();
    const harness::SuiteConfig* suite = harness::active_suite(config);
    const harness::ConsultLimits limits =
        suite == nullptr ? harness::ConsultLimits{} : harness::consult_limits(suite->consult_caps);
    const std::int64_t needed = walk(spec, catalog).stage_calls;
    if (calls.used() + needed > limits.per_turn) {
        return refuse("this turn's member-call budget has " +
                      std::to_string(std::max<std::int64_t>(limits.per_turn - calls.used(), 0)) +
                      " of " + std::to_string(limits.per_turn) +
                      " calls left -- plays, consults and checks share it -- and a play of " +
                      spec.name + " makes " + std::to_string(needed));
    }
    std::int64_t budget = limits.per_turn;
    if (config.symphony_caps.stage_calls.has_value()) {
        if (needed > *config.symphony_caps.stage_calls) {
            return refuse("a play of it makes " + member_calls(needed) +
                          ", and symphony_caps.stage_calls allows " +
                          std::to_string(*config.symphony_caps.stage_calls));
        }
        budget = std::min(budget, calls.used() + *config.symphony_caps.stage_calls);
    }

    PlayOptions options;
    options.conversation = context.conversation ? context.conversation() : std::string{};
    // The model's initiative never spends: a member billed per call is
    // refused at the stage, whatever the offer said.
    options.local_only = true;
    options.per_turn = budget;
    options.answer_tokens = config.symphony_caps.answer_tokens.value_or(0);
    PlayResult result;
    {
        // The labeled line (26n): the model's choice and what it costs,
        // closed with the time and tokens, its stages said beneath it.
        agentloop::SideCallScope said{
            calls.narration(), "play",
            "the model chose " + spec.name + ": " + member_calls(needed) + ", " + role_chain(spec)};
        result = play(spec, catalog, PlayInput{.text = input->get<std::string>(), .image = {}},
                      calls, options);
        if (const std::optional<std::int64_t> tokens = reported_tokens(result.stages);
            result.ok() && tokens.has_value()) {
            said.tokens(*tokens);
        }
    }
    if (!result.ok()) {
        calls.notice("orchestrate: " + spec.name + " stopped -- " + result.failure);
        if (result.member_failure) {
            return agent::ToolOutcome{
                .content = "Error: " + spec.name + " stopped with no output -- " + result.failure,
                .is_error = true};
        }
        return agent::ToolOutcome{
            .content = "Not played: " + result.failure + ". Answer without it.", .is_error = false};
    }
    std::string out = spec.name + " answered:\n" + result.output;
    if (!result.stages.empty() && result.stages.back().cut) {
        out += "\n[the output reached its last stage's token cap and was cut there]";
    }
    return agent::ToolOutcome{.content = std::move(out), .is_error = false};
}

}  // namespace

std::string play_tool_name(std::string_view symphony) {
    return std::string{kPlayToolPrefix} + std::string{symphony};
}

bool orchestrating(const harness::Config& config, bool flag) {
    const harness::SuiteConfig* suite = harness::active_suite(config);
    return flag || (suite != nullptr && suite->orchestrate);
}

std::string_view orchestrator_framing() noexcept {
    return kOrchestratorFraming;
}

std::vector<ReachedMember> reached_members(const harness::Config& config,
                                           const harness::SymphonySpec& spec,
                                           const Catalog& catalog, std::string_view conversation) {
    std::vector<ReachedMember> out;
    const auto add = [&](const harness::SymphonySpec& owner) {
        for (const harness::SymphonyStage& stage : owner.stages) {
            if (stage.plays()) {
                continue;  // its symphony's stages are reached in their turn
            }
            ReachedMember member;
            member.symphony = owner.name;
            member.stage = stage.name;
            member.role = stage.role;
            if (const std::optional<harness::ModelRole> role = agentloop::role_named(stage.role);
                role.has_value()) {
                member.backend =
                    harness::resolve_backend(
                        config, harness::RoleRequest{.role = *role,
                                                     .conversation = std::string{conversation}})
                        .key;
            }
            out.push_back(std::move(member));
        }
    };
    add(spec);
    for (const std::string& name : walk(spec, catalog).reached) {
        if (const Definition* played = catalog.find(name); played != nullptr) {
            add(played->spec);
        }
    }
    return out;
}

std::string reached_at(std::string_view root, const ReachedMember& member) {
    const std::string stage = member.stage + " stage (" + member.role + ")";
    if (member.symphony == root) {
        return "its " + stage;
    }
    return member.symphony + "'s " + stage;
}

std::string unprojectable(const harness::SymphonySpec& spec, const Catalog& catalog) {
    if (spec.input.image) {
        return "it takes an image, which a tool call cannot give -- 'apogee symphonies play " +
               spec.name + " --image <file>' plays it";
    }
    if (const std::vector<std::string> problems = validate(spec, catalog); !problems.empty()) {
        return "it cannot be played: " + problems.front();
    }
    if (play_tool_name(spec.name).size() > kMaxToolName) {
        return "its name makes a tool name past " + std::to_string(kMaxToolName) +
               " characters, which some providers refuse -- a shorter name offers it";
    }
    return {};
}

std::string play_tool_description(const harness::SymphonySpec& spec, const Catalog& catalog) {
    std::string out = spec.description;
    if (!out.empty() && out.back() != '.') {
        out += '.';
    }
    if (!out.empty()) {
        out += ' ';
    }
    out += "Plays the " + spec.name +
           " symphony: " + member_calls(walk(spec, catalog).stage_calls) +
           " on your suite's models, one after another (" + role_chain(spec) +
           "). It sees only the input you give it -- not this conversation, not any file -- so "
           "put the whole text it needs there.";
    return out;
}

std::string play_tool_schema(const harness::SymphonySpec& spec) {
    const std::string described = spec.input.description.empty()
                                      ? std::string{"The text the symphony works on"}
                                      : spec.input.description;
    const nlohmann::json schema{
        {"type", "object"},
        {"properties", {{"input", {{"type", "string"}, {"description", described}}}}},
        {"required", {"input"}}};
    return schema.dump();
}

agent::Tool make_play_tool(const harness::Harness& harness, const Definition& definition,
                           const Catalog& catalog, PlayToolContext context) {
    agent::Tool tool;
    tool.name = play_tool_name(definition.spec.name);
    tool.description = play_tool_description(definition.spec, catalog);
    tool.parameters_schema = play_tool_schema(definition.spec);
    // A symphony's stages call no tools (27q): a play reads no file, writes
    // nothing and reaches no website, so the gate has nothing to ask.
    tool.writes = false;
    tool.outbound = false;
    // What a play needs, held once and shared by every copy of the tool.
    const auto held = std::make_shared<const Held>(
        Held{.spec = definition.spec, .catalog = catalog, .context = std::move(context)});
    tool.run = [&harness, held](std::string_view arguments) {
        return play_for_model(harness, held->spec, held->catalog, held->context, arguments);
    };
    return tool;
}

OrchestraOffer orchestra_offer(const harness::Harness& harness, const Catalog& catalog,
                               std::string_view conversation) {
    OrchestraOffer out;
    const harness::Config& config = harness.config();
    const harness::SuiteConfig* suite = harness::active_suite(config);
    if (suite == nullptr) {
        return out;
    }
    const harness::ConsultLimits limits = harness::consult_limits(suite->consult_caps);
    const std::vector<std::string> built = harness.provider_names();
    for (const Definition& definition : catalog.definitions) {
        const harness::SymphonySpec& spec = definition.spec;
        if (const std::string why = unprojectable(spec, catalog); !why.empty()) {
            out.withheld.push_back(
                Withheld{.symphony = spec.name, .reason = why, .structural = spec.input.image});
            continue;
        }
        const std::int64_t needed = walk(spec, catalog).stage_calls;
        std::string why;
        if (needed > limits.per_turn) {
            why = "a play of it makes " + member_calls(needed) + ", more than a turn's budget of " +
                  std::to_string(limits.per_turn) + " (the suite's consult_caps.per_turn)";
        } else if (config.symphony_caps.stage_calls.has_value() &&
                   needed > *config.symphony_caps.stage_calls) {
            why = "a play of it makes " + member_calls(needed) +
                  ", more than symphony_caps.stage_calls allows (" +
                  std::to_string(*config.symphony_caps.stage_calls) + ")";
        }
        for (const ReachedMember& member : reached_members(config, spec, catalog, conversation)) {
            if (!why.empty()) {
                break;
            }
            why = cannot_answer(harness, spec.name, member, built);
        }
        if (!why.empty()) {
            out.withheld.push_back(Withheld{.symphony = spec.name, .reason = why});
            continue;
        }
        out.offered.push_back(spec.name);
    }
    return out;
}

OrchestraOffer register_play_tools(agent::ToolRegistry& registry, const harness::Harness& harness,
                                   const Catalog& catalog, const PlayToolContext& context) {
    const OrchestraOffer offer = orchestra_offer(
        harness, catalog, context.conversation ? context.conversation() : std::string{});
    const bool framed = holds_play_tool(registry);
    for (const std::string& name : offer.offered) {
        const Definition* definition = catalog.find(name);
        if (definition == nullptr || registry.find(play_tool_name(name)) != nullptr) {
            continue;
        }
        registry.add(make_play_tool(harness, *definition, catalog, context));
    }
    if (!framed) {
        // The root is the Orchestrator (26p's mechanism): the note says so
        // while the registry asking holds a symphony's tool, and a copy
        // narrowed to none says nothing of it.
        registry.set_environment(
            [inner = registry.environment_source()](const agent::ToolRegistry& tools) {
                std::string note = inner ? inner(tools) : std::string{};
                if (holds_play_tool(tools)) {
                    note += (note.empty() ? "" : "\n\n") + std::string{kOrchestratorFraming};
                }
                return note;
            });
    }
    return offer;
}

}  // namespace apogee::symphony
