#include "agentloop/question.h"

#include <nlohmann/json.hpp>

#include <set>
#include <stdexcept>

namespace apogee::agentloop {
namespace {

std::string ordinal(std::size_t index) {
    return std::to_string(index + 1);
}

}  // namespace

harness::Tool question_tool() {
    harness::Tool tool;
    tool.name = std::string{kQuestionToolName};
    tool.description =
        "Ask the user up to 4 multiple-choice questions when you need a decision only they "
        "can make. Use it when the request is genuinely ambiguous and different readings "
        "would lead to materially different work -- not for questions you can answer "
        "yourself from the context. The user may always type their own answer instead of "
        "picking an option.";
    tool.parameters_schema = nlohmann::json{
        {"type", "object"},
        {"properties",
         {{"questions",
           {{"type", "array"},
            {"minItems", 1},
            {"maxItems", kMaxQuestions},
            {"description", "The questions to ask, at most 4."},
            {"items",
             {{"type", "object"},
              {"properties",
               {{"header", {{"type", "string"}, {"description", "Very short label, <= 12 chars."}}},
                {"question", {{"type", "string"}, {"description", "The full question."}}},
                {"multiSelect",
                 {{"type", "boolean"}, {"description", "Whether several options may be chosen."}}},
                {"options",
                 {{"type", "array"},
                  {"minItems", kMinQuestionOptions},
                  {"maxItems", kMaxQuestionOptions},
                  {"items",
                   {{"type", "object"},
                    {"properties",
                     {{"label", {{"type", "string"}}}, {"description", {{"type", "string"}}}}},
                    {"required", nlohmann::json::array({"label"})}}}}}}},
              {"required", nlohmann::json::array({"question", "options"})}}}}}}},
        {"required",
         nlohmann::json::array(
             {"questions"})}}.dump();
    return tool;
}

QuestionRequest parse_question_request(std::string_view arguments) {
    if (arguments.empty()) {
        throw std::invalid_argument(R"(missing arguments: expected {"questions": [...]})");
    }
    const nlohmann::json parsed = nlohmann::json::parse(arguments, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        throw std::invalid_argument("arguments are not a valid JSON object");
    }

    const auto questions = parsed.find("questions");
    if (questions == parsed.end() || !questions->is_array()) {
        throw std::invalid_argument(R"(missing "questions": expected an array of questions)");
    }

    QuestionRequest request;
    for (const auto& entry : *questions) {
        if (!entry.is_object()) {
            throw std::invalid_argument("each question must be an object");
        }
        Question question;
        question.header = entry.value("header", std::string{});
        question.question = entry.value("question", std::string{});
        question.multi_select = entry.value("multiSelect", false);

        if (const auto options = entry.find("options"); options != entry.end()) {
            if (!options->is_array()) {
                throw std::invalid_argument(R"("options" must be an array)");
            }
            for (const auto& option : *options) {
                if (!option.is_object()) {
                    throw std::invalid_argument("each option must be an object");
                }
                question.options.push_back(
                    QuestionOption{option.value("label", std::string{}),
                                   option.value("description", std::string{})});
            }
        }
        request.questions.push_back(std::move(question));
    }

    validate(request);
    return request;
}

void validate(const QuestionRequest& request) {
    if (request.questions.empty()) {
        throw std::invalid_argument("no questions given: provide 1 to " +
                                    std::to_string(kMaxQuestions) + " questions");
    }
    if (request.questions.size() > kMaxQuestions) {
        throw std::invalid_argument("too many questions (" +
                                    std::to_string(request.questions.size()) + "): ask at most " +
                                    std::to_string(kMaxQuestions) + " per call");
    }

    std::set<std::string> seen;
    for (std::size_t i = 0; i < request.questions.size(); ++i) {
        const Question& question = request.questions[i];
        if (question.question.empty()) {
            throw std::invalid_argument(
                "question " + ordinal(i) +
                R"( has no text: every question needs a "question" string)");
        }
        if (!seen.insert(question.question).second) {
            throw std::invalid_argument("question " + ordinal(i) +
                                        " repeats an earlier question (\"" + question.question +
                                        "\"): question texts must be distinct");
        }
        if (question.options.size() < kMinQuestionOptions ||
            question.options.size() > kMaxQuestionOptions) {
            throw std::invalid_argument("question " + ordinal(i) + " (\"" + question.question +
                                        "\") has " + std::to_string(question.options.size()) +
                                        " option(s): give " + std::to_string(kMinQuestionOptions) +
                                        " to " + std::to_string(kMaxQuestionOptions) + " options");
        }
        for (std::size_t j = 0; j < question.options.size(); ++j) {
            if (question.options[j].label.empty()) {
                throw std::invalid_argument("question " + ordinal(i) + " (\"" + question.question +
                                            "\") option " + ordinal(j) +
                                            R"( has no label: every option needs a "label")");
            }
        }
    }
}

std::string encode_answers(const QuestionRequest& request, const Answers& answers) {
    // Rendered as question/answer pairs rather than bare values: a model reading
    // "Postgres" with no question attached has to guess which of four it
    // answers, and it guesses wrong when the answers are short.
    std::string out = "The user answered:\n";
    for (std::size_t i = 0; i < request.questions.size(); ++i) {
        out += "\n";
        out += request.questions[i].question;
        out += "\n  ";
        out += i < answers.values.size() && !answers.values[i].empty() ? answers.values[i]
                                                                       : "(no answer given)";
        out += "\n";
    }
    return out;
}

}  // namespace apogee::agentloop
