#include "review/diff.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "review/identity.h"

/// The review's diff and identity (48c): the parser's table -- every file
/// shape a unified diff carries, the added lines numbered on the new side --
/// the exclusions, the chunks, and what makes two findings one, with the
/// markers a review reads itself back by, a forged one refused.
namespace {

using apogee::review::ParsedDiff;
using Catch::Matchers::ContainsSubstring;

[[nodiscard]] std::string fixture(std::string_view name) {
    std::ifstream in{std::filesystem::path{APOGEE_TEST_FIXTURES} / "review" / name,
                     std::ios::binary};
    REQUIRE(in);
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

}  // namespace

TEST_CASE("a unified diff reads to its files and the lines each adds, numbered", "[review][diff]") {
    const ParsedDiff diff =
        apogee::review::parse_unified_diff(fixture("general-correctness.defect.diff"));
    REQUIRE(diff.files.size() == 1);
    const apogee::review::DiffFile& file = diff.files.front();
    CHECK(file.path == "src/stats.py");
    CHECK(file.old_path == "src/stats.py");
    CHECK(file.added.size() == 8);
    CHECK(file.added.at(11) == "    for i in range(max(0, len(items) - n), len(items) - 1):");
    CHECK(diff.is_added("src/stats.py", 11));
    CHECK_FALSE(diff.is_added("src/stats.py", 4));  // context: `def mean`
    CHECK_FALSE(diff.is_added("other.py", 11));
    CHECK_THAT(file.numbered, ContainsSubstring("  11 +    for i in range("));
    CHECK_THAT(file.numbered, ContainsSubstring("   4  def mean(items):"));
    CHECK(file.hunks.size() == 1);

    // A removed line is shown, never numbered; the hunk start is honoured.
    const ParsedDiff back = apogee::review::parse_unified_diff(fixture("back-end.defect.diff"));
    CHECK(back.files.front().added.begin()->first == 18);
    const ParsedDiff front = apogee::review::parse_unified_diff(fixture("front-end.defect.diff"));
    CHECK_THAT(front.files.front().numbered,
               ContainsSubstring("     -  return <p className=\"comment\">"));
}

TEST_CASE("added, deleted, renamed and binary files, and a CRLF diff, read as git writes them",
          "[review][diff]") {
    const std::string text =
        "diff --git a/new.txt b/new.txt\r\n"
        "new file mode 100644\r\n"
        "--- /dev/null\r\n"
        "+++ b/new.txt\r\n"
        "@@ -0,0 +1,2 @@\r\n"
        "+one\r\n"
        "+two\r\n"
        "\\ No newline at end of file\r\n"
        "diff --git a/gone.txt b/gone.txt\n"
        "deleted file mode 100644\n"
        "--- a/gone.txt\n"
        "+++ /dev/null\n"
        "@@ -1 +0,0 @@\n"
        "-bye\n"
        "diff --git a/old/name.h b/new/name.h\n"
        "similarity index 100%\n"
        "rename from old/name.h\n"
        "rename to new/name.h\n"
        "diff --git a/logo.png b/logo.png\n"
        "Binary files a/logo.png and b/logo.png differ\n";
    const ParsedDiff diff = apogee::review::parse_unified_diff(text);
    REQUIRE(diff.files.size() == 4);
    CHECK(diff.files[0].path == "new.txt");
    CHECK(diff.files[0].added_file);
    CHECK(diff.files[0].added.size() == 2);
    CHECK(diff.files[0].added.at(2) == "two");
    CHECK(diff.files[1].path == "gone.txt");
    CHECK(diff.files[1].deleted_file);
    CHECK(diff.files[1].added.empty());
    CHECK(diff.files[2].path == "new/name.h");
    CHECK(diff.files[2].old_path == "old/name.h");
    CHECK(diff.files[3].binary);

    std::vector<std::string> excluded;
    const ParsedDiff kept =
        apogee::review::exclude_files(diff, {"**/*.txt", "**/vendor/**"}, excluded);
    CHECK(excluded == std::vector<std::string>{"new.txt", "gone.txt", "logo.png"});
    REQUIRE(kept.files.size() == 1);
    CHECK(kept.files.front().path == "new/name.h");
}

TEST_CASE("chunks hold whole files where they fit, a large file split at its hunks",
          "[review][diff]") {
    std::string text;
    for (int f = 0; f < 3; ++f) {
        text += "diff --git a/f" + std::to_string(f) + ".py b/f" + std::to_string(f) + ".py\n";
        text += "--- a/f" + std::to_string(f) + ".py\n+++ b/f" + std::to_string(f) + ".py\n";
        for (int h = 0; h < 3; ++h) {
            text += "@@ -" + std::to_string(h * 100 + 1) + ",1 +" + std::to_string(h * 100 + 1) +
                    ",2 @@\n context\n+" + std::string(400, 'x') + "\n";
        }
    }
    const ParsedDiff diff = apogee::review::parse_unified_diff(text);
    // Room for everything: one chunk, every file.
    const std::vector<apogee::review::DiffChunk> one = apogee::review::chunk_diff(diff, 100000);
    REQUIRE(one.size() == 1);
    CHECK(one.front().paths == std::vector<std::string>{"f0.py", "f1.py", "f2.py"});
    // Room for a file each.
    const std::vector<apogee::review::DiffChunk> files = apogee::review::chunk_diff(diff, 400);
    CHECK(files.size() == 3);
    // Room for a hunk each: every chunk under its file's header.
    const std::vector<apogee::review::DiffChunk> hunks = apogee::review::chunk_diff(diff, 120);
    CHECK(hunks.size() == 9);
    for (const apogee::review::DiffChunk& chunk : hunks) {
        CHECK(chunk.text.starts_with("diff --git a/f"));
        CHECK(chunk.paths.size() == 1);
    }
    CHECK(apogee::review::estimate_tokens("12345678") == 2);
    CHECK(apogee::review::estimate_tokens("123456789") == 3);
}

TEST_CASE(
    "a finding's identity is its file, first line and kind; markers read back only as written",
    "[review][identity]") {
    using namespace apogee::review;
    CHECK(location_line("12") == 12U);
    CHECK(location_line("12-14") == 12U);
    CHECK(location_line("line 7") == 7U);
    CHECK_FALSE(location_line("the retry loop").has_value());
    CHECK_FALSE(location_line("0").has_value());
    CHECK(normalized_location("lines 12-14") == "L12");
    CHECK(normalized_location("  The   Retry Loop ") == "the retry loop");
    // The same finding from two lenses is one identity; another line, kind
    // or file is another.
    CHECK(finding_identity("a.py", "12", "correctness") ==
          finding_identity("a.py", "line 12-13", "correctness"));
    CHECK(finding_identity("a.py", "12", "correctness") !=
          finding_identity("a.py", "13", "correctness"));
    CHECK(finding_identity("a.py", "12", "correctness") !=
          finding_identity("b.py", "12", "correctness"));
    CHECK(finding_identity("a.py", "12", "correctness").size() == 16);

    const std::string id = finding_identity("a.py", "12", "correctness");
    const Markers read = read_markers("**high** -- x\n\n" + finding_marker(id) + "\n" +
                                      summary_marker("0123abcd0123abcd"));
    CHECK(read.findings == std::vector<std::string>{id});
    CHECK(read.head == "0123abcd0123abcd");
    // A head that is not a commit id, or an identity of the wrong shape, is
    // not read: a marker is a hint, never a path.
    const Markers forged = read_markers(
        "<!-- apogee-review:summary head=../../etc -->"
        "<!-- apogee-review:finding not-hex-at-all! -->");
    CHECK_FALSE(forged.head.has_value());
    CHECK(forged.findings.empty());
}
