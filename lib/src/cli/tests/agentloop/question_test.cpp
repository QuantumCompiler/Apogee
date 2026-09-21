#include "agentloop/question.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <string>

using apogee::agentloop::Answers;
using apogee::agentloop::encode_answers;
using apogee::agentloop::parse_question_request;
using apogee::agentloop::QuestionRequest;
using apogee::agentloop::validate;

namespace {

constexpr std::string_view kValid = R"({"questions":[{
    "header":"DB","question":"Which database?","multiSelect":false,
    "options":[{"label":"Postgres","description":"relational"},{"label":"SQLite"}]}]})";

}  // namespace

TEST_CASE("a valid ask_user call parses", "[agentloop][question]") {
    const QuestionRequest request = parse_question_request(kValid);

    REQUIRE(request.questions.size() == 1);
    CHECK(request.questions[0].header == "DB");
    CHECK(request.questions[0].question == "Which database?");
    CHECK_FALSE(request.questions[0].multi_select);
    REQUIRE(request.questions[0].options.size() == 2);
    CHECK(request.questions[0].options[0].label == "Postgres");
    CHECK(request.questions[0].options[0].description == "relational");
}

TEST_CASE("the tool schema is valid JSON and requires questions", "[agentloop][question]") {
    const auto tool = apogee::agentloop::question_tool();
    CHECK(tool.name == "ask_user");

    const auto schema = nlohmann::json::parse(tool.parameters_schema);
    CHECK(schema.at("required")[0] == "questions");
    CHECK(schema.at("properties").at("questions").at("maxItems") ==
          apogee::agentloop::kMaxQuestions);
}

TEST_CASE("validation messages are written for the model to act on", "[agentloop][question]") {
    // These become the tool result. The model is expected to read them and fix
    // its call, so they name the field and the bound.
    auto message_for = [](std::string_view arguments) {
        try {
            (void)parse_question_request(arguments);
            return std::string{"(no error)"};
        } catch (const std::invalid_argument& e) {
            return std::string{e.what()};
        }
    };

    CHECK(message_for("").find("missing arguments") != std::string::npos);
    CHECK(message_for("not json").find("valid JSON") != std::string::npos);
    CHECK(message_for("{}").find("questions") != std::string::npos);
    CHECK(message_for(R"({"questions":[]})").find("provide 1 to 4") != std::string::npos);

    CHECK(message_for(R"({"questions":[{"question":"a","options":[{"label":"x"}]}]})")
              .find("give 2 to 4 options") != std::string::npos);

    CHECK(message_for(R"({"questions":[{"question":"","options":[{"label":"x"},{"label":"y"}]}]})")
              .find("no text") != std::string::npos);

    CHECK(message_for(R"({"questions":[{"question":"a","options":[{"label":""},{"label":"y"}]}]})")
              .find("no label") != std::string::npos);
}

TEST_CASE("too many questions is refused", "[agentloop][question]") {
    std::string arguments = R"({"questions":[)";
    for (int i = 0; i < 5; ++i) {
        arguments += std::string{i == 0 ? "" : ","} + R"({"question":"q)" + std::to_string(i) +
                     R"(","options":[{"label":"x"},{"label":"y"}]})";
    }
    arguments += "]}";

    try {
        (void)parse_question_request(arguments);
        FAIL("expected a rejection");
    } catch (const std::invalid_argument& e) {
        CHECK(std::string{e.what()}.find("at most 4") != std::string::npos);
    }
}

TEST_CASE("duplicate question text is refused", "[agentloop][question]") {
    // Two identical questions cannot be told apart in the encoded answers.
    constexpr std::string_view arguments = R"({"questions":[
        {"question":"same","options":[{"label":"a"},{"label":"b"}]},
        {"question":"same","options":[{"label":"c"},{"label":"d"}]}]})";
    try {
        (void)parse_question_request(arguments);
        FAIL("expected a rejection");
    } catch (const std::invalid_argument& e) {
        CHECK(std::string{e.what()}.find("repeats an earlier question") != std::string::npos);
    }
}

TEST_CASE("answers are encoded paired with their questions", "[agentloop][question]") {
    // A bare "Postgres" leaves a model with four questions guessing which one
    // it answers -- and it guesses wrong when the answers are short.
    const QuestionRequest request = parse_question_request(kValid);
    const std::string encoded = encode_answers(request, Answers{{"Postgres"}});

    CHECK(encoded.find("Which database?") != std::string::npos);
    CHECK(encoded.find("Postgres") != std::string::npos);
    CHECK(encoded.find("Which database?") < encoded.find("Postgres"));
}

TEST_CASE("a missing answer is stated, not silently blank", "[agentloop][question]") {
    const QuestionRequest request = parse_question_request(kValid);
    CHECK(encode_answers(request, Answers{}).find("(no answer given)") != std::string::npos);
}

TEST_CASE("free text is preserved verbatim", "[agentloop][question]") {
    // The model's options are a convenience, not a constraint on what the user
    // is allowed to say.
    const QuestionRequest request = parse_question_request(kValid);
    const std::string encoded = encode_answers(request, Answers{{"actually, DuckDB"}});
    CHECK(encoded.find("actually, DuckDB") != std::string::npos);
}
