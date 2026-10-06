#include "symphony/runner.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <utility>

#include "agentloop/structured.h"
#include "symphony/definition.h"

namespace apogee::symphony {
namespace {

std::string stage_at(std::size_t index, std::size_t count, const harness::SymphonyStage& stage) {
    return "stage " + std::to_string(index + 1) + "/" + std::to_string(count) + " " + stage.name +
           " (" + stage.role + ")";
}

/// Whether any stage's template reads `{{input}}`.
bool reads_input(const harness::SymphonySpec& spec) {
    for (const harness::SymphonyStage& stage : spec.stages) {
        for (const std::string& name : template_variables(stage.prompt)) {
            if (name == kInputVariable) {
                return true;
            }
        }
    }
    return false;
}

/// The answer's JSON as the member wrote it when it parses as it stands, so
/// the properties keep the order the grammar wrote them in; the object
/// inside a fence or prose otherwise.
std::string json_text(const std::string& answer, const nlohmann::json& parsed) {
    const nlohmann::json direct = nlohmann::json::parse(answer, nullptr, false);
    return direct.is_discarded() ? parsed.dump() : answer;
}

}  // namespace

std::string stage_label(std::size_t index, std::size_t count, const harness::SymphonyStage& stage) {
    return "stage " + std::to_string(index + 1) + "/" + std::to_string(count) + " " + stage.name;
}

agentloop::MemberCall stage_call(const harness::SymphonySpec& spec, std::size_t index,
                                 const std::map<std::string, std::string>& values,
                                 const PlayInput& input, const PlayOptions& options) {
    const harness::SymphonyStage& stage = spec.stages.at(index);
    agentloop::MemberCall call;
    // The parser admits roles only.
    call.role = agentloop::role_named(stage.role).value_or(harness::ModelRole::Utility);
    call.brief = render_template(stage.prompt, values);
    call.conversation = options.conversation;
    call.brief_tokens = stage.brief_tokens.value_or(kStageBriefTokens);
    call.answer_tokens = stage.answer_tokens.value_or(kStageAnswerTokens);
    call.label = stage_label(index, spec.stages.size(), stage);
    call.local_only = options.local_only;
    call.schema = stage.schema;
    if (stage.image) {
        call.images = input.image;
    }
    return call;
}

std::string refusal(const harness::SymphonySpec& spec, const PlayInput& input) {
    if (const std::vector<std::string> problems = validate(spec); !problems.empty()) {
        std::string out = "'" + spec.name + "' cannot be played: " + problems.front();
        if (problems.size() > 1) {
            out += " (and " + std::to_string(problems.size() - 1) + " more -- " +
                   "'apogee symphonies show " + spec.name + "' lists them)";
        }
        return out;
    }
    if (reads_input(spec) && input.text.find_first_not_of(" \t\r\n") == std::string::npos) {
        return "'" + spec.name + "' reads its input" +
               (spec.input.description.empty() ? std::string{}
                                               : " (" + spec.input.description + ")") +
               ", and none was given";
    }
    if (spec.input.image && input.image.empty()) {
        return "'" + spec.name + "' takes an image with its input, and none was given";
    }
    if (!spec.input.image && !input.image.empty()) {
        return "'" + spec.name + "' takes no image -- its input is text alone";
    }
    return {};
}

PlayResult play(const harness::SymphonySpec& spec, const PlayInput& input,
                agentloop::MemberCalls& calls, const PlayOptions& options) {
    PlayResult out;
    out.failure = refusal(spec, input);
    if (!out.failure.empty()) {
        return out;
    }
    const std::size_t count = spec.stages.size();
    const std::int64_t budget =
        options.per_turn > 0 ? options.per_turn : static_cast<std::int64_t>(count);
    std::map<std::string, std::string> values{{std::string{kInputVariable}, input.text}};

    for (std::size_t index = 0; index < count; ++index) {
        const harness::SymphonyStage& stage = spec.stages[index];
        const agentloop::MemberCall call = stage_call(spec, index, values, input, options);
        const auto began = std::chrono::steady_clock::now();
        const agentloop::MemberAnswer answer = calls.call(call, budget);
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
        const auto stop = [&](std::string why, bool member) {
            out.failure = stage_at(index, count, stage) + ": " + std::move(why);
            out.failed_stage = index + 1;
            out.member_failure = member;
            out.output.clear();
        };
        if (!answer.refused.empty()) {
            stop(answer.refused, false);
            return out;
        }
        if (!answer.failed.empty()) {
            stop(answer.failed, true);
            return out;
        }
        StageResult result;
        result.name = stage.name;
        result.role = stage.role;
        result.backend = answer.backend;
        result.answer = answer.text;
        result.cut = answer.cut;
        result.tokens = answer.tokens;
        result.seconds = seconds;
        if (!stage.schema.empty()) {
            // Held by a grammar where the member has one (26f); checked here
            // on every member alike, and a miss stops the walk rather than
            // threading a broken document on.
            const nlohmann::json schema = nlohmann::json::parse(stage.schema);
            const std::optional<nlohmann::json> parsed = agentloop::extract_json(answer.text);
            if (!parsed.has_value()) {
                stop("'" + answer.backend + "' answered with no JSON, and the stage holds its " +
                         "answer to a schema" +
                         (answer.cut ? " (the answer was cut at its cap)" : ""),
                     true);
                return out;
            }
            if (const agentloop::ValidationResult checked =
                    agentloop::validate_against(schema, *parsed);
                !checked.ok) {
                stop("'" + answer.backend + "' answered outside the stage's schema -- " +
                         (checked.errors.empty() ? std::string{} : checked.errors.front()),
                     true);
                return out;
            }
            result.answer = json_text(answer.text, *parsed);
        }
        values[stage.name] = result.answer;
        out.stages.push_back(std::move(result));
    }
    out.output = out.stages.back().answer;
    return out;
}

}  // namespace apogee::symphony
