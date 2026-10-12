#include "cli/line_tokens.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

/// The one splitter a typed command line goes through (37f, 37h): the
/// quoting table -- what POSIX shells do with quotes and escapes, and nothing
/// they expand.
namespace {

[[nodiscard]] std::vector<std::string> words(const std::string& line) {
    const apogee::commands::LineTokens split = apogee::commands::line_tokens(line);
    REQUIRE(split.error.empty());
    return split.words;
}

}  // namespace

TEST_CASE("a line splits on whitespace, quotes keep their words whole", "[cli][tokens]") {
    using Words = std::vector<std::string>;
    CHECK(words("").empty());
    CHECK(words("   ").empty());
    CHECK(words("train run snap --dataset d") == Words{"train", "run", "snap", "--dataset", "d"});
    CHECK(words("  a\tb  ") == Words{"a", "b"});
    CHECK(words("say 'two words'") == Words{"say", "two words"});
    CHECK(words("say \"two words\"") == Words{"say", "two words"});
    CHECK(words("a'b c'd") == Words{"ab cd"});
    CHECK(words("'' \"\"") == Words{"", ""});
    CHECK(words("x ''") == Words{"x", ""});
    // Single quotes keep backslashes; double quotes unescape only their four.
    CHECK(words(R"('a\b')") == Words{R"(a\b)"});
    CHECK(words(R"("a\"b" "c\\d" "e\$f" "g\h")") == Words{"a\"b", R"(c\d)", "e$f", R"(g\h)"});
    // A backslash outside quotes takes the next character as it is.
    CHECK(words(R"(a\ b c\'d)") == Words{"a b", "c'd"});
    // Nothing expands.
    CHECK(words("$HOME ~ *.txt `x`") == Words{"$HOME", "~", "*.txt", "`x`"});
}

TEST_CASE("a trailing space is a new word begun; an open quote is refused", "[cli][tokens]") {
    CHECK(apogee::commands::line_tokens("models pull ").trailing_space);
    CHECK_FALSE(apogee::commands::line_tokens("models pull").trailing_space);
    CHECK(apogee::commands::line_tokens("say 'open").error == "a single quote is left open");
    CHECK(apogee::commands::line_tokens("say \"open").error == "a double quote is left open");
    CHECK(apogee::commands::line_tokens("end\\").error ==
          "a backslash ends the line with nothing to escape");
}
