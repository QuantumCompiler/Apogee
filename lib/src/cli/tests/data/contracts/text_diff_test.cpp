#include "contracts/text_diff.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <random>
#include <string>
#include <string_view>
#include <vector>

/// A file change as a permission question carries it (52a): a unified diff
/// that patch(1) would apply, cut at its cap with the whole change's counts
/// said, and those counts read back off the text a prompt prints.
namespace {

namespace h = apogee::harness;

std::vector<std::string> lines_with_endings(std::string_view text) {
    std::vector<std::string> out;
    std::size_t from = 0;
    while (from < text.size()) {
        const std::size_t newline = text.find('\n', from);
        const std::size_t end = newline == std::string_view::npos ? text.size() : newline + 1;
        out.emplace_back(text.substr(from, end - from));
        from = end;
    }
    return out;
}

/// `diff` applied to `before`, as patch(1) applies a unified diff: each
/// hunk's context and removals checked against the file, its additions put
/// in their place.
std::string patched(std::string_view before, std::string_view diff) {
    const std::vector<std::string> source = lines_with_endings(before);
    const std::vector<std::string> patch = lines_with_endings(diff);
    std::string out;
    std::size_t next = 0;  // the first source line not yet copied
    std::size_t p = 2;     // past the --- and +++ lines
    while (p < patch.size()) {
        const std::string& header = patch[p];
        REQUIRE(header.starts_with("@@ -"));
        const std::size_t comma = header.find_first_of(", ", 4);
        std::size_t start = std::stoul(header.substr(4, comma - 4));
        // A side that holds no lines names the line before it.
        const bool empty_old =
            header.find(",0 ") != std::string::npos && header.find(",0 ") < header.find(" +");
        if (!empty_old) {
            start -= 1;
        }
        while (next < start) {
            out += source.at(next++);
        }
        ++p;
        while (p < patch.size() && !patch[p].starts_with("@@ ")) {
            std::string line = patch[p].substr(1);
            const char sign = patch[p][0];
            ++p;
            if (p < patch.size() && patch[p].starts_with("\\ No newline")) {
                line.pop_back();  // the '\n' the marker line says was not there
                ++p;
            }
            if (sign == ' ' || sign == '-') {
                REQUIRE(source.at(next) == line);
                ++next;
            }
            if (sign == ' ' || sign == '+') {
                out += line;
            }
        }
    }
    while (next < source.size()) {
        out += source.at(next++);
    }
    return out;
}

}  // namespace

TEST_CASE("an edit's diff is its hunk, with three lines of context", "[contracts][diff]") {
    const std::string before = "one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\n";
    const std::string after = "one\ntwo\nthree\nfour\nFIVE\nsix\nseven\neight\n";
    CHECK(h::unified_diff("src/a.txt", before, after) ==
          "--- a/src/a.txt\n"
          "+++ b/src/a.txt\n"
          "@@ -2,7 +2,7 @@\n"
          " two\n"
          " three\n"
          " four\n"
          "-five\n"
          "+FIVE\n"
          " six\n"
          " seven\n"
          " eight\n");
}

TEST_CASE("a new file is all additions, from /dev/null", "[contracts][diff]") {
    CHECK(h::unified_diff("notes.md", "", "a\nb\n", false) ==
          "--- /dev/null\n+++ b/notes.md\n@@ -0,0 +1,2 @@\n+a\n+b\n");
    // An empty new file is still a change: there was none before.
    CHECK(h::unified_diff("empty", "", "", false) == "--- /dev/null\n+++ b/empty\n");
}

TEST_CASE("nothing changed is no diff", "[contracts][diff]") {
    CHECK(h::unified_diff("a", "same\n", "same\n").empty());
}

TEST_CASE("an unterminated last line is marked, as diff(1) marks it", "[contracts][diff]") {
    CHECK(h::unified_diff("a", "x\ny", "x\nz") ==
          "--- a/a\n+++ b/a\n@@ -1,2 +1,2 @@\n x\n-y\n\\ No newline at end of file\n+z\n"
          "\\ No newline at end of file\n");
    // Adding the newline is a change of the last line.
    CHECK(h::unified_diff("a", "x", "x\n") ==
          "--- a/a\n+++ b/a\n@@ -1 +1 @@\n-x\n\\ No newline at end of file\n+x\n");
}

TEST_CASE("distant changes are separate hunks, near ones share one", "[contracts][diff]") {
    std::string before;
    for (int i = 1; i <= 30; ++i) {
        before += std::to_string(i) + "\n";
    }
    std::string far = before;
    far.replace(far.find("2\n"), 2, "two\n");
    far.replace(far.find("\n25\n") + 1, 3, "twenty-five\n");
    const std::string diff = h::unified_diff("n", before, far);
    CHECK(diff.find("@@ -1,5 +1,5 @@") != std::string::npos);
    CHECK(diff.find("@@ -22,7 +22,7 @@") != std::string::npos);
    CHECK(patched(before, diff) == far);

    std::string near = before;
    near.replace(near.find("\n10\n") + 1, 3, "ten\n");
    near.replace(near.find("\n16\n") + 1, 3, "sixteen\n");
    const std::string one = h::unified_diff("n", before, near);
    CHECK(one.find("@@ -7,13 +7,13 @@") != std::string::npos);
    CHECK(h::diff_counts(one).added == 2);
}

TEST_CASE("a binary side is said, never shown", "[contracts][diff]") {
    const std::string binary{"a\0b", 3};
    CHECK(h::unified_diff("blob", "text\n", binary) == "Binary files a/blob and b/blob differ\n");
    const h::DiffCounts counts = h::diff_counts(h::unified_diff("blob", "", binary, false));
    CHECK(counts.binary);
    CHECK(h::describe_diff_counts(counts) == "binary");
}

TEST_CASE("past its cap a diff stops at a line and says the whole change", "[contracts][diff]") {
    std::string before;
    std::string after;
    for (int i = 0; i < 400; ++i) {
        before += "old line " + std::to_string(i) + "\n";
        after += "new line " + std::to_string(i) + "\n";
    }
    const std::string diff = h::unified_diff("big", before, after, true, 2048);
    CHECK(diff.size() < 2048 + 64);
    CHECK(diff.ends_with("[diff cut at 2 KB: +400 -400 lines in all]\n"));
    const h::DiffCounts counts = h::diff_counts(diff);
    CHECK(counts.cut);
    CHECK(counts.added == 400);
    CHECK(counts.removed == 400);
    CHECK(h::describe_diff_counts(counts) == "+400 -400 (cut)");
    // What it kept is still a diff: its one hunk's header counts the lines
    // it holds.
    const std::vector<std::string> lines = lines_with_endings(diff);
    REQUIRE(lines.at(2).starts_with("@@ -1,"));
    std::size_t old_side = 0;
    std::size_t new_side = 0;
    for (std::size_t i = 3; i + 1 < lines.size(); ++i) {
        old_side += lines[i].starts_with('-') || lines[i].starts_with(' ') ? 1 : 0;
        new_side += lines[i].starts_with('+') || lines[i].starts_with(' ') ? 1 : 0;
    }
    const auto side = [](std::size_t count) {
        return count == 1 ? std::string{"1"} : "1," + std::to_string(count);
    };
    const auto added_side = [](std::size_t count) {
        return count == 0 ? std::string{"0,0"}
                          : (count == 1 ? std::string{"1"} : "1," + std::to_string(count));
    };
    CHECK(lines.at(2) == "@@ -" + side(old_side) + " +" + added_side(new_side) + " @@\n");
}

TEST_CASE("the counts a prompt prints are read off the diff's own text", "[contracts][diff]") {
    const std::string diff = h::unified_diff("c", "a\nb\nc\n", "a\nB\nc\nd\n");
    const h::DiffCounts counts = h::diff_counts(diff);
    CHECK(counts.added == 2);
    CHECK(counts.removed == 1);
    CHECK_FALSE(counts.cut);
    CHECK(h::describe_diff_counts(counts) == "+2 -1");
    // A `---`/`+++` line inside a hunk is a changed line, not a header.
    const h::DiffCounts dashes = h::diff_counts(h::unified_diff("d", "--- x\n", "+++ y\n"));
    CHECK(dashes.added == 1);
    CHECK(dashes.removed == 1);
}

TEST_CASE("every diff applies back to the text it was made for", "[contracts][diff]") {
    // Random edits over a small alphabet, so lines repeat and the search has
    // choices to make; the diff must still be one patch(1) applies exactly.
    std::mt19937 random{20261020};
    const std::vector<std::string> words{"a\n", "b\n", "c\n", "d\n", "e\n"};
    for (int round = 0; round < 300; ++round) {
        std::string before;
        std::string after;
        const int length = static_cast<int>(random() % 40);
        for (int i = 0; i < length; ++i) {
            const std::string& word = words[random() % words.size()];
            before += word;
            switch (random() % 4) {
                case 0:
                    break;  // removed
                case 1:
                    after += words[random() % words.size()];  // changed
                    break;
                default:
                    after += word;
            }
            if (random() % 6 == 0) {
                after += words[random() % words.size()];  // inserted
            }
        }
        if (random() % 5 == 0 && !after.empty()) {
            after.pop_back();  // an unterminated last line
        }
        const std::string diff = h::unified_diff("r", before, after);
        if (before == after) {
            CHECK(diff.empty());
            continue;
        }
        CHECK(patched(before, diff) == after);
    }
}
