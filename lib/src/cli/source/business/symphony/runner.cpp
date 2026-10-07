#include "symphony/runner.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "agentloop/content.h"
#include "agentloop/structured.h"
#include "contracts/symphony_walk.h"

namespace apogee::symphony {
namespace {

/// Whether any stage's template reads `{{input}}`.
bool reads_input(const harness::SymphonySpec& spec) {
    for (std::size_t index = 0; index < spec.stages.size(); ++index) {
        for (const std::string& name : template_variables(stage_template(spec, index))) {
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

/// Whether the definition plays another symphony anywhere.
bool chains(const harness::SymphonySpec& spec) {
    return std::ranges::any_of(spec.stages,
                               [](const harness::SymphonyStage& stage) { return stage.plays(); });
}

/// One walk: the root and every symphony it plays, through the one turn's
/// calls, under one budget, each position said.
class Walk {
public:
    Walk(const Catalog& catalog, agentloop::MemberCalls& calls, const PlayOptions& options,
         bool chained, std::int64_t call_budget, PlayResult& out)
        : catalog_{catalog},
          calls_{calls},
          options_{options},
          chained_{chained},
          call_budget_{call_budget},
          out_{out} {}

    /// Plays `spec` -- the end of the path once pushed -- with `input`, its
    /// stages' outcomes into `results`: its output, or nothing once the walk
    /// has stopped (`out_` says why).
    // NOLINTNEXTLINE(misc-no-recursion): bounded by the nesting the walk was checked to.
    std::optional<std::string> run(const harness::SymphonySpec& spec, const PlayInput& input,
                                   std::vector<StageResult>& results) {
        path_.push_back(spec.name);
        std::optional<std::string> output = stages(spec, input, results);
        path_.pop_back();
        return output;
    }

private:
    // NOLINTNEXTLINE(misc-no-recursion): bounded by the nesting the walk was checked to.
    std::optional<std::string> stages(const harness::SymphonySpec& spec, const PlayInput& input,
                                      std::vector<StageResult>& results) {
        std::map<std::string, std::string> values{{std::string{kInputVariable}, input.text}};
        for (std::size_t index = 0; index < spec.stages.size(); ++index) {
            if (path_.size() == 1) {
                root_index_ = index;
            }
            const harness::SymphonyStage& stage = spec.stages[index];
            std::optional<StageResult> result = stage.plays()
                                                    ? play_stage(spec, index, values, input)
                                                    : role_stage(spec, index, values, input);
            if (!result.has_value()) {
                return std::nullopt;
            }
            values[stage.name] = result->answer;
            results.push_back(std::move(*result));
        }
        return results.back().answer;
    }

    /// Where stage `index` of `spec` (at the end of the path) is, with what
    /// it plays: `outer → inner, stage 2/3 verify (chat)`.
    [[nodiscard]] std::string at(const harness::SymphonySpec& spec, std::size_t index) const {
        const harness::SymphonyStage& stage = spec.stages[index];
        return stage_position(path_, chained_, index, spec.stages.size(), stage) + " (" +
               (stage.plays() ? "plays " + stage.play : stage.role) + ")";
    }

    /// Ends the walk at stage `index` of `spec`: named, with no output.
    void stop(const harness::SymphonySpec& spec, std::size_t index, const std::string& why,
              bool member) {
        out_.failure = at(spec, index) + ": " + why;
        out_.failed_stage = root_index_ + 1;
        out_.member_failure = member;
        out_.output.clear();
    }

    /// The budget's word before a call, or empty while there is room.
    [[nodiscard]] std::string spent_before_call() const {
        if (calls_.used() >= call_budget_) {
            return "the play's budget is spent -- " + std::to_string(calls_.used()) + " of " +
                   std::to_string(call_budget_) + " member calls made, " +
                   std::to_string(out_.tokens) +
                   " answer tokens; the play stops here, with no answer";
        }
        if (options_.answer_tokens > 0 && out_.tokens >= options_.answer_tokens) {
            return answer_tokens_spent();
        }
        return {};
    }

    [[nodiscard]] std::string answer_tokens_spent() const {
        return "the play's budget is spent -- " + std::to_string(out_.tokens) + " of " +
               std::to_string(options_.answer_tokens) + " answer tokens used, in " +
               std::to_string(out_.calls) + (out_.calls == 1 ? " member call" : " member calls") +
               "; the play stops here, with no answer";
    }

    std::optional<StageResult> role_stage(const harness::SymphonySpec& spec, std::size_t index,
                                          const std::map<std::string, std::string>& values,
                                          const PlayInput& input) {
        const harness::SymphonyStage& stage = spec.stages[index];
        if (const std::string spent = spent_before_call(); !spent.empty()) {
            out_.budget_spent = true;
            stop(spec, index, spent, false);
            return std::nullopt;
        }
        agentloop::MemberCall call = stage_call(spec, index, values, input, options_);
        call.label = stage_position(path_, chained_, index, spec.stages.size(), stage);
        const auto began = std::chrono::steady_clock::now();
        const agentloop::MemberAnswer answer = calls_.call(call, call_budget_);
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
        if (!answer.refused.empty()) {
            stop(spec, index, answer.refused, false);
            return std::nullopt;
        }
        // Made: answered or failed, it spent a call -- and what it said, its
        // tokens, as its provider counts them or as its text estimates.
        ++out_.calls;
        out_.tokens += answer.tokens.value_or(agentloop::estimate_tokens(answer.text).tokens);
        if (!answer.failed.empty()) {
            stop(spec, index, answer.failed, true);
            return std::nullopt;
        }
        if (options_.answer_tokens > 0 && out_.tokens > options_.answer_tokens) {
            out_.budget_spent = true;
            stop(spec, index, answer_tokens_spent(), false);
            return std::nullopt;
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
                stop(spec, index,
                     "'" + answer.backend + "' answered with no JSON, and the stage holds its " +
                         "answer to a schema" +
                         (answer.cut ? " (the answer was cut at its cap)" : ""),
                     true);
                return std::nullopt;
            }
            if (const agentloop::ValidationResult checked =
                    agentloop::validate_against(schema, *parsed);
                !checked.ok) {
                stop(spec, index,
                     "'" + answer.backend + "' answered outside the stage's schema -- " +
                         (checked.errors.empty() ? std::string{} : checked.errors.front()),
                     true);
                return std::nullopt;
            }
            result.answer = json_text(answer.text, *parsed);
        }
        return result;
    }

    /// A stage that plays a symphony: that symphony's walk, here, on its
    /// rendered input -- the image passed on when the stage is marked for it.
    // NOLINTNEXTLINE(misc-no-recursion): bounded by the nesting the walk was checked to.
    std::optional<StageResult> play_stage(const harness::SymphonySpec& spec, std::size_t index,
                                          const std::map<std::string, std::string>& values,
                                          const PlayInput& input) {
        const harness::SymphonyStage& stage = spec.stages[index];
        const Definition* played = catalog_.find(stage.play);
        if (played == nullptr) {
            // The walk was checked before the first call; a catalog that
            // changed under it is said, never played around.
            stop(spec, index, "no symphony is named '" + stage.play + "'", false);
            return std::nullopt;
        }
        PlayInput given;
        given.text = render_template(stage_template(spec, index), values);
        if (stage.image) {
            given.image = input.image;
        }
        if (reads_input(played->spec) &&
            given.text.find_first_not_of(" \t\r\n") == std::string::npos) {
            stop(spec, index,
                 "'" + played->spec.name + "' reads its input, and the stage gave it nothing",
                 false);
            return std::nullopt;
        }
        StageResult result;
        result.name = stage.name;
        result.play = played->spec.name;
        const std::optional<std::string> output = run(played->spec, given, result.stages);
        if (!output.has_value()) {
            return std::nullopt;
        }
        result.answer = *output;
        result.cut = !result.stages.empty() && result.stages.back().cut;
        for (const StageResult& inner : result.stages) {
            result.seconds += inner.seconds;
            if (inner.tokens.has_value()) {
                result.tokens = result.tokens.value_or(0) + *inner.tokens;
            }
        }
        return result;
    }

    const Catalog& catalog_;
    agentloop::MemberCalls& calls_;
    const PlayOptions& options_;
    bool chained_;
    std::int64_t call_budget_;
    PlayResult& out_;
    std::vector<std::string> path_;
    std::size_t root_index_ = 0;
};

}  // namespace

std::string stage_label(std::size_t index, std::size_t count, const harness::SymphonyStage& stage) {
    return "stage " + std::to_string(index + 1) + "/" + std::to_string(count) + " " + stage.name;
}

std::string stage_position(const std::vector<std::string>& path, bool chained, std::size_t index,
                           std::size_t count, const harness::SymphonyStage& stage) {
    std::string label = stage_label(index, count, stage);
    if (!chained || path.empty()) {
        return label;
    }
    return harness::symphony_path(path) + ", " + label;
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

std::string refusal(const harness::SymphonySpec& spec, const Catalog& catalog,
                    const PlayInput& input) {
    if (const std::vector<std::string> problems = validate(spec, catalog); !problems.empty()) {
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

std::string refusal(const harness::SymphonySpec& spec, const PlayInput& input) {
    return refusal(spec, Catalog{}, input);
}

PlayResult play(const harness::SymphonySpec& spec, const Catalog& catalog, const PlayInput& input,
                agentloop::MemberCalls& calls, const PlayOptions& options) {
    PlayResult out;
    // The whole walk is checked before the first call: a late-bound spec
    // file that loops or nests too deep is refused with nothing sent.
    out.failure = refusal(spec, catalog, input);
    if (!out.failure.empty()) {
        return out;
    }
    // The walk's own budget is its count: every stage of every symphony it
    // reaches, once -- one turn's count, whatever the nesting.
    const std::int64_t budget =
        options.per_turn > 0 ? options.per_turn : calls.used() + walk(spec, catalog).stage_calls;
    Walk walker{catalog, calls, options, chains(spec), budget, out};
    std::optional<std::string> output = walker.run(spec, input, out.stages);
    if (output.has_value()) {
        out.output = std::move(*output);
    }
    return out;
}

PlayResult play(const harness::SymphonySpec& spec, const PlayInput& input,
                agentloop::MemberCalls& calls, const PlayOptions& options) {
    return play(spec, Catalog{}, input, calls, options);
}

}  // namespace apogee::symphony
