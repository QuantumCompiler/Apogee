#include "agentloop/thinking.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>

namespace apogee::agentloop {
namespace {

/// The judge's whole instruction: one word, nothing to reason about.
constexpr std::string_view kJudgePrompt =
    "Decide whether answering the question below well needs careful step-by-step "
    "reasoning first -- working, calculation, analysis, or a judgement with several "
    "parts -- or whether it can be answered directly. Reply with one word: yes or no.";

/// Enough for "yes" or "no" and a stray full stop.
constexpr std::int64_t kJudgeTokens = 8;

/// The punctuation of a language rather than of prose, a fence or an inline
/// span among it.
constexpr std::array<std::string_view, 10> kCodeSigns = {"`",  "{",  "}",        "()",   "=>",
                                                         "->", "::", "#include", "def ", "#!/"};

/// Words that ask for working.
constexpr std::array<std::string_view, 8> kMathsWords = {
    "calculate", "compute", "solve", "prove", "equation", "integral", "derivative", "probability"};

[[nodiscard]] std::string lowercased(std::string_view text) {
    std::string out{text};
    std::ranges::transform(out, out.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

[[nodiscard]] bool is_word_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

/// Whether `word` appears in `text` on word boundaries.
[[nodiscard]] bool has_word(std::string_view text, std::string_view word) {
    for (std::size_t at = text.find(word); at != std::string_view::npos;
         at = text.find(word, at + 1)) {
        const bool starts = at == 0 || !is_word_char(text[at - 1]);
        const std::size_t end = at + word.size();
        const bool ends = end >= text.size() || !is_word_char(text[end]);
        if (starts && ends) {
            return true;
        }
    }
    return false;
}

/// Code in a question: a fence or an inline span, or the punctuation of a
/// language rather than of prose.
[[nodiscard]] bool has_code(std::string_view text) {
    return std::ranges::any_of(kCodeSigns, [text](std::string_view sign) {
        return text.find(sign) != std::string_view::npos;
    });
}

/// Arithmetic: a number, an operator, a number -- "17 * 23", "2^10" -- or a
/// word that asks for working.
[[nodiscard]] bool has_maths(std::string_view lower) {
    for (std::size_t index = 0; index < lower.size(); ++index) {
        if (std::string_view{"+-*/^=%"}.find(lower[index]) == std::string_view::npos) {
            continue;
        }
        std::size_t before = index;
        while (before > 0 && lower[before - 1] == ' ') {
            --before;
        }
        std::size_t after = index + 1;
        while (after < lower.size() && lower[after] == ' ') {
            ++after;
        }
        if (before > 0 && after < lower.size() &&
            std::isdigit(static_cast<unsigned char>(lower[before - 1])) != 0 &&
            std::isdigit(static_cast<unsigned char>(lower[after])) != 0) {
            return true;
        }
    }
    return std::ranges::any_of(kMathsWords,
                               [lower](std::string_view word) { return has_word(lower, word); });
}

}  // namespace

bool question_needs_thinking(std::string_view question) {
    if (question.size() > kThinkingQuestionBytes) {
        return true;
    }
    const std::string lower = lowercased(question);
    return has_code(question) || has_maths(lower) || has_word(lower, "why") ||
           has_word(lower, "how");
}

harness::ChatRequest thinking_judge_request(std::string_view judge, std::string_view question) {
    harness::ChatRequest request;
    request.model = std::string{judge};
    request.messages = {harness::ChatMessage::system(std::string{kJudgePrompt}),
                        harness::ChatMessage::user(std::string{question})};
    request.max_tokens = kJudgeTokens;
    request.temperature = 0.0;
    // Not a turn of the conversation, and nothing to reason about: the
    // judge's own thinking would cost more than the one it saves.
    request.transient.side_request = true;
    request.thinking.mode = harness::ThinkingMode::Off;
    return request;
}

ThinkingDecision decide_thinking(const harness::Harness& harness, std::string_view judge,
                                 std::string_view question,
                                 const harness::CancellationToken& cancellation) {
    const auto by_rule = [question] {
        const bool think = question_needs_thinking(question);
        return ThinkingDecision{
            .think = think,
            .by = think ? "a long question, or one with code, maths, a why or a how"
                        : "a short question with no code, maths, why or how"};
    };
    if (judge.empty()) {
        return by_rule();
    }
    try {
        const harness::ChatResponse response =
            harness.chat(thinking_judge_request(judge, question), cancellation);
        std::string answer = lowercased(response.message.content.plain_text());
        std::erase_if(answer, [](char c) { return !std::isalpha(static_cast<unsigned char>(c)); });
        if (answer.starts_with("yes")) {
            return {.think = true, .by = "the utility model"};
        }
        if (answer.starts_with("no")) {
            return {.think = false, .by = "the utility model"};
        }
    } catch (const harness::CancelledError&) {
        throw;
    } catch (const std::exception&) {
        // A judge that cannot answer is no reason to fail the turn: the rule
        // still can.
        return by_rule();
    }
    return by_rule();
}

harness::Thinking resolve_turn_thinking(const harness::Harness& harness,
                                        const harness::Thinking& asked, std::string_view judge,
                                        std::string_view question,
                                        const harness::CancellationToken& cancellation,
                                        ThinkingDecision* decision) {
    if (asked.mode != harness::ThinkingMode::Auto) {
        return asked;
    }
    const ThinkingDecision decided = decide_thinking(harness, judge, question, cancellation);
    if (decision != nullptr) {
        *decision = decided;
    }
    return harness::Thinking{
        .mode = decided.think ? harness::ThinkingMode::On : harness::ThinkingMode::Off,
        .budget = asked.budget};
}

}  // namespace apogee::agentloop
