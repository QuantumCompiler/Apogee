#include "cli/chat_play.h"

#include <string>
#include <utility>
#include <vector>

#include "agentloop/side_call.h"
#include "cli/chat_recall.h"
#include "contracts/errors.h"
#include "contracts/types.h"
#include "contracts/utf8.h"
#include "logger/operational.h"

namespace apogee::commands {
namespace {

bool is_space(char c) {
    return c == ' ' || c == '\t';
}

/// What a refusal adds to say how to get past it in a session: an input to
/// type after the name, or -- for an image, which `/play` cannot give -- the
/// command that can.
std::string way_out(const harness::SymphonySpec& spec, const symphony::PlayInput& input) {
    if (spec.input.image) {
        return " -- /play gives a symphony text alone; 'apogee symphonies play " + spec.name +
               " --image <file>' plays it with one";
    }
    if (input.text.find_first_not_of(" \t\r\n") == std::string::npos) {
        return " -- " + std::string{kPlayShape};
    }
    return {};
}

}  // namespace

PlayArgument parse_play_argument(std::string_view argument) {
    PlayArgument out;
    std::size_t at = 0;
    while (at < argument.size() && is_space(argument[at])) {
        ++at;
    }
    std::size_t end = at;
    while (end < argument.size() && !is_space(argument[end])) {
        ++end;
    }
    if (end == at) {
        out.error = "/play takes a symphony, then its input -- " + std::string{kPlayShape} +
                    " (/symphonies lists them)";
        return out;
    }
    out.symphony = std::string{argument.substr(at, end - at)};
    while (end < argument.size() && is_space(argument[end])) {
        ++end;
    }
    out.input = std::string{argument.substr(end)};
    return out;
}

symphony::Catalog session_catalog(const harness::Config& config,
                                  const std::filesystem::path& config_path) {
    return symphony::catalog(config, symphony::directory_for(config_path));
}

PreparedPlay prepare_play(const harness::Config& config, const std::filesystem::path& config_path,
                          const PlayArgument& asked) {
    PreparedPlay out;
    if (!asked.error.empty()) {
        out.refusal = asked.error;
        return out;
    }
    symphony::Found found =
        symphony::find_definition(config, symphony::directory_for(config_path), asked.symphony);
    if (!found.definition.has_value()) {
        out.refusal = found.error;
        return out;
    }
    out.definition = std::move(*found.definition);
    out.catalog = std::move(found.catalog);
    out.input.text = asked.input;
    // What the definition refuses is said before anything is sent -- the
    // whole walk checked, every symphony it plays found (27r).
    if (const std::string refused = symphony::refusal(out.definition.spec, out.catalog, out.input);
        !refused.empty()) {
        out.refusal = refused + way_out(out.definition.spec, out.input);
    }
    return out;
}

PlayTurnResult run_play_turn(const harness::Harness& harness, logger::Session& session,
                             const std::string& line, const PreparedPlay& play,
                             agentloop::MemberCalls& member_calls, agentloop::Reporter& reporter,
                             ChatRecall* recall, const harness::CancellationToken& cancellation) {
    PlayTurnResult outcome;
    if (!play.refusal.empty()) {
        outcome.failure = play.refusal;
        return outcome;
    }
    // Each stage said where the surface says a turn's other model calls:
    // the terminal's thinking block, machine mode's `tool_status` (26n).
    agentloop::SideCallSink narrate = [&reporter](const agentloop::SideCall& call) {
        reporter.on_side_call(call);
    };
    // The user's play, as `symphonies play` plays one: may reach a billed
    // member (the user's initiative spends), the whole walk under the
    // config's budget when it sets one, a helper with no member falling back
    // to the conversation's backend as the one chain says.
    const harness::Config& config = harness.config();
    symphony::PlayOptions options;
    options.conversation = session.backend;
    options.per_turn = config.symphony_caps.stage_calls.value_or(0);
    options.answer_tokens = config.symphony_caps.answer_tokens.value_or(0);
    symphony::PlayResult result;
    try {
        // A turn of the session's own member calls: its stages are counted
        // there, and its cancellation stops the walk.
        const agentloop::MemberCalls::Turn turn =
            member_calls.begin_turn(std::move(narrate), cancellation);
        result =
            symphony::play(play.definition.spec, play.catalog, play.input, member_calls, options);
    } catch (const harness::CancelledError&) {
        reporter.on_clear_status();
        outcome.cancelled = true;
        outcome.failure = "cancelled";
        return outcome;
    } catch (const harness::HarnessError& e) {
        reporter.on_clear_status();
        logger::log(logger::Level::Error, "execute", e.what());
        outcome.failure = e.what();
        outcome.member_failure = true;
        return outcome;
    }
    if (!result.ok()) {
        reporter.on_clear_status();
        outcome.failure = result.failure;
        outcome.member_failure = result.member_failure;
        return outcome;
    }

    // The output is the session's answer: said as one, and kept as one --
    // the line as typed, then the output, an ordinary exchange.
    reporter.on_answer_start();
    reporter.on_answer_token(result.output);
    reporter.on_answer_end();
    // The line as typed, kept as text: it never passed `build_messages`, and
    // the session file is a strict JSON dump.
    session.messages.push_back(harness::ChatMessage::user(harness::valid_utf8(line)));
    session.messages.push_back(harness::ChatMessage::assistant(result.output));
    ++session.turns;
    if (recall != nullptr) {
        recall->after_turn();
    }
    // Persisted at once, as every turn is: a kill after a play keeps it.
    logger::save(session);
    outcome.completed = true;
    return outcome;
}

std::vector<NamedChoice> symphony_choices(const symphony::Catalog& catalog) {
    // The walk's own refusal, asked of an input as /play gives one -- typed
    // text, never an image: what it refuses then, /play refuses whatever is
    // typed after the name.
    const symphony::PlayInput typed{.text = "input", .image = {}};
    std::vector<NamedChoice> out;
    for (const symphony::Definition& definition : catalog.definitions) {
        if (symphony::refusal(definition.spec, catalog, typed).empty()) {
            out.push_back(
                {.name = definition.spec.name, .description = definition.spec.description});
        }
    }
    return out;
}

std::string symphony_count(std::size_t count) {
    return std::to_string(count) + (count == 1 ? " symphony" : " symphonies");
}

}  // namespace apogee::commands
