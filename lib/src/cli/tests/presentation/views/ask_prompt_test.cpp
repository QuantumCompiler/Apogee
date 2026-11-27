#include "views/ask_prompt.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "agentloop/question.h"
#include "ansi/ansi.h"
#include "ansi/inert.h"

/// The terminal's `ask_user` prompt (57p): what it draws for a model's
/// question, as lines a test can read with no terminal, and the answer a
/// number picks.
namespace {

using apogee::agentloop::Question;
using apogee::agentloop::QuestionOption;
using apogee::commands::ask_prompt_lines;
using apogee::commands::chosen_answer;

const std::string kFffd{apogee::ansi::kReplacementCharacter};

[[nodiscard]] Question question_of(std::string text, std::vector<QuestionOption> options) {
    Question question;
    question.question = std::move(text);
    question.options = std::move(options);
    return question;
}

/// Whether `line` holds a byte a terminal would obey.
[[nodiscard]] bool obeyable(const std::string& line) {
    for (const char c : line) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte == 0x7F) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("an option's number picks the option drawn: a label is one line, whatever it holds",
          "[commands][ask][57p]") {
    // The sweep's entry 3: `yes\r\x1b[2K  1) no` drew as `1) no`, the person
    // typed 1 to say no, and the model was told yes.
    const Question question =
        question_of("Keep the file?",
                    {QuestionOption{.label = "yes\r\x1b[2K  1) no", .description = "keeps it"},
                     QuestionOption{.label = "no", .description = "x"}});
    const std::vector<std::string> lines = ask_prompt_lines(question, 0, 1, apogee::ansi::Style{});
    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "[perm] Keep the file?");
    CHECK(lines[1] == "  1) yes" + kFffd + kFffd + "[2K  1) no -- keeps it");
    CHECK(lines[2] == "  2) no -- x");
    for (const std::string& line : lines) {
        INFO(line);
        CHECK_FALSE(obeyable(line));
    }
    // The answer is the model's own label, as it wrote it: the one drawn.
    CHECK(chosen_answer(question, "1") == "yes\r\x1b[2K  1) no");
    CHECK(chosen_answer(question, "2") == "no");
}

TEST_CASE("no question or description can draw a line that starts like a prompt line",
          "[commands][ask][57p]") {
    // A newline in a description drew a row at column 0 that read as the
    // prompt's own; an OSC retitled the terminal, an SGR concealed the rest.
    const Question question = question_of(
        "Keep the file?\x1b]0;HIJACKED-TITLE\x07\x1b[8m secret hidden\n[permission] fake",
        {QuestionOption{.label = "yes", .description = "x\n[permission] fake line"},
         QuestionOption{.label = "no", .description = ""}});
    const apogee::ansi::Style style{};
    const std::vector<std::string> lines = ask_prompt_lines(question, 1, 2, style);
    REQUIRE(lines.size() == 5);
    CHECK(lines[0] == "[perm] (2/2) Keep the file?" + kFffd + "]0;HIJACKED-TITLE" + kFffd + kFffd +
                          "[8m secret hidden");
    // The question's second row, under its first row's text.
    CHECK(lines[1] == "             [permission] fake");
    CHECK(lines[2] == "  1) yes -- x");
    // The description's second line, under the label.
    CHECK(lines[3] == "     [permission] fake line");
    CHECK(lines[4] == "  2) no");
    for (std::size_t i = 1; i < lines.size(); ++i) {
        INFO(lines[i]);
        CHECK(lines[i].starts_with(" "));
        CHECK_FALSE(obeyable(lines[i]));
    }

    // In colour, the dim of the description is the prompt's, and no OSC or
    // SGR of the question's reaches a line.
    const std::vector<std::string> coloured =
        ask_prompt_lines(question, 1, 2, apogee::ansi::Style{true});
    for (const std::string& line : coloured) {
        INFO(line);
        CHECK(line.find("\x1b]") == std::string::npos);
        CHECK(line.find("\x1b[8m") == std::string::npos);
        CHECK(line.find('\x07') == std::string::npos);
    }
}

TEST_CASE("a question's later rows sit under its text, never where an option starts",
          "[commands][ask][57p]") {
    // Indented two columns, as the options are, the question's second row
    // drew `  1) no -- keeps it` above the real first option -- an option
    // that is not one, in a terminal with no colour to tell them apart.
    const Question question =
        question_of("Keep the file?\n1) no -- keeps it",
                    {QuestionOption{.label = "yes", .description = "keeps it"},
                     QuestionOption{.label = "no", .description = "deletes it"}});
    const std::vector<std::string> lines = ask_prompt_lines(question, 0, 1, apogee::ansi::Style{});
    REQUIRE(lines.size() == 4);
    CHECK(lines[0] == "[perm] Keep the file?");
    CHECK(lines[1] == "       1) no -- keeps it");
    CHECK(lines[2] == "  1) yes -- keeps it");
    CHECK(lines[3] == "  2) no -- deletes it");
    // No row of the question starts as an option row does.
    for (const std::string& row : {lines[0], lines[1]}) {
        INFO(row);
        CHECK_FALSE(row.starts_with("  1)"));
        CHECK_FALSE(row.starts_with("  2)"));
    }
    // In colour, the same columns: the tag's own SGR takes none.
    const std::vector<std::string> coloured =
        ask_prompt_lines(question, 0, 1, apogee::ansi::Style{true});
    REQUIRE(coloured.size() == 4);
    CHECK(coloured[1] == lines[1]);
    // With a counter, under the counter too.
    CHECK(ask_prompt_lines(question, 2, 3, apogee::ansi::Style{})[1] ==
          "             1) no -- keeps it");
}

TEST_CASE("a plain question draws as it always did", "[commands][ask][57p]") {
    const Question question =
        question_of("Which colour?", {QuestionOption{.label = "Red", .description = "warm"},
                                      QuestionOption{.label = "Green", .description = ""}});
    CHECK(ask_prompt_lines(question, 0, 1, apogee::ansi::Style{}) ==
          std::vector<std::string>{"[perm] Which colour?", "  1) Red -- warm", "  2) Green"});
}
