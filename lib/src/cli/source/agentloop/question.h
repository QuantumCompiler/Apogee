#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "harness/types.h"

/// `ask_user` — the model asking the user a multiple-choice question mid-turn.
///
/// Unlike Ommi, this works uniformly on every provider, because Apogee owns the
/// loop everywhere rather than delegating it to a vendor CLI on one backend.
///
/// The availability rule is the important part: the tool is advertised **if and
/// only if there is someone to answer it**. A model told it can ask questions,
/// on a surface with no user attached, will eventually ask one and then hang or
/// fabricate an answer. So a null AskFn means the tool is never in the request
/// at all — not advertised-and-refused, absent.
namespace apogee::agentloop {

/// The tool name the model calls.
inline constexpr std::string_view kQuestionToolName = "ask_user";

inline constexpr std::size_t kMaxQuestions = 4;
inline constexpr std::size_t kMinQuestionOptions = 2;
inline constexpr std::size_t kMaxQuestionOptions = 4;

struct QuestionOption {
    std::string label;
    std::string description;
};

struct Question {
    /// Short label for the question, for a compact UI.
    std::string header;
    std::string question;
    std::vector<QuestionOption> options;
    /// Whether several options may be chosen.
    bool multi_select = false;
};

struct QuestionRequest {
    std::vector<Question> questions;
};

/// One answer per question, in order. Free text is always permitted regardless
/// of the offered options — the model's options are a convenience, not a
/// constraint on what the user is allowed to say.
struct Answers {
    std::vector<std::string> values;
};

/// Presents `request` and returns the user's answers.
///
/// Non-null only on a surface that actually has a user. Throwing from it aborts
/// the turn, and the loop rolls the half-turn out of history so nothing
/// dangling is persisted.
using AskFn = std::function<Answers(const QuestionRequest&)>;

/// The IR tool definition for `ask_user`.
[[nodiscard]] harness::Tool question_tool();

/// Parses the model's arguments.
/// Throws std::invalid_argument with a message written FOR THE MODEL — it
/// becomes the tool result, and the model is expected to fix its call and retry.
[[nodiscard]] QuestionRequest parse_question_request(std::string_view arguments);

/// Structural validation: 1..kMaxQuestions questions, each with text and
/// 2..4 labelled options, no duplicate question text.
/// Throws std::invalid_argument, message written for the model.
void validate(const QuestionRequest& request);

/// Renders the answers as the tool result the model reads back.
[[nodiscard]] std::string encode_answers(const QuestionRequest& request, const Answers& answers);

}  // namespace apogee::agentloop
