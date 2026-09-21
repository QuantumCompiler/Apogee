#include "commands/ask_prompt.h"

#include <charconv>
#include <iostream>
#include <string>

#include "platform/platform.h"

namespace apogee::commands {
namespace {

/// Reads one line from stdin. Returns false on EOF.
bool read_line(std::string& line) {
    return static_cast<bool>(std::getline(std::cin, line));
}

std::string trim(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t')) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin &&
           (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r')) {
        --end;
    }
    return std::string{text.substr(begin, end - begin)};
}

/// Presents one question and returns the answer.
std::string ask_one(StatusLine& status, const ansi::Style& style,
                    const agentloop::Question& question, std::size_t index, std::size_t total) {
    // Through the status line, not raw stderr: the spinner may be mid-frame,
    // and print_line bumps the generation counter so an in-flight repaint
    // cannot land on top of the question.
    std::string header = style.tag(ansi::Role::Permission) + " ";
    if (total > 1) {
        header += "(" + std::to_string(index + 1) + "/" + std::to_string(total) + ") ";
    }
    status.print_line(header + question.question);

    for (std::size_t i = 0; i < question.options.size(); ++i) {
        std::string line = "  " + std::to_string(i + 1) + ") " + question.options[i].label;
        if (!question.options[i].description.empty()) {
            line += style.dim(" -- " + question.options[i].description);
        }
        status.print_line(line);
    }
    // Free text is ALWAYS accepted. The model's options are a convenience, not
    // a constraint on what the user is allowed to say -- and the right answer
    // is often none of the four.
    std::cerr << style.dim("Choose a number, or type your own answer: ") << std::flush;

    std::string line;
    if (!read_line(line)) {
        return {};
    }
    const std::string answer = trim(line);
    if (answer.empty()) {
        return {};
    }

    // A bare number selects an option; anything else is taken verbatim.
    std::size_t choice = 0;
    const auto* begin = answer.data();
    const auto* end = begin + answer.size();
    if (std::from_chars(begin, end, choice).ec == std::errc{} && choice >= 1 &&
        choice <= question.options.size()) {
        return question.options[choice - 1].label;
    }
    return answer;
}

}  // namespace

agentloop::AskFn terminal_ask_fn(StatusLine& status, ansi::Style style) {
    // Both ends must be a terminal: stdin because the answer is read from it,
    // stderr because the question is written there. A run reading its prompt
    // from a pipe has no interactive stdin left, and gets no ask_user.
    if (!platform::is_terminal(platform::StandardStream::In) ||
        !platform::is_terminal(platform::StandardStream::Err)) {
        return nullptr;
    }

    return [&status, style](const agentloop::QuestionRequest& request) {
        agentloop::Answers answers;
        answers.values.reserve(request.questions.size());
        for (std::size_t i = 0; i < request.questions.size(); ++i) {
            answers.values.push_back(
                ask_one(status, style, request.questions[i], i, request.questions.size()));
        }
        std::cerr << "\n";
        return answers;
    };
}

}  // namespace apogee::commands
