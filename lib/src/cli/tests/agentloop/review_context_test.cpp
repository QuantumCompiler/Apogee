#include "agentloop/review_context.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

/// The review context: `/branch` parsing, the note, and composition.
namespace {

using apogee::agentloop::compose_system_prompt;
using apogee::agentloop::parse_branch_arg;
using apogee::agentloop::review_note;
using apogee::agentloop::review_summary;
using apogee::agentloop::ReviewContext;
using apogee::agentloop::valid_fetch_mode;

}  // namespace

// Not "/branch parses ...": ctest hands the name to the binary as an argument,
// and on Windows Catch2 reads a leading '/' as an option prefix (2026-09-20).
TEST_CASE("the /branch argument parses off, a range, and a bare head", "[agentloop][review]") {
    ReviewContext current;
    current.base = "develop";

    const ReviewContext bare = parse_branch_arg("feature-x", current);
    CHECK(bare.head == "feature-x");
    CHECK(bare.base == "develop");  // a bare head keeps the base
    CHECK(bare.active());

    const ReviewContext two = parse_branch_arg("main..feature-y", current);
    CHECK(two.base == "main");
    CHECK(two.head == "feature-y");
    const ReviewContext three = parse_branch_arg(" release/2.0 ... feature/z ", current);
    CHECK(three.base == "release/2.0");
    CHECK(three.head == "feature/z");

    for (const char* word : {"off", "OFF", "clear", "none"}) {
        const ReviewContext off = parse_branch_arg(word, bare);
        CHECK_FALSE(off.active());
        CHECK(off.remote == "origin");  // the remote and fetch mode survive
    }
}

TEST_CASE(
    "the note names both refs, defaults the missing one, and tells the model the tools "
    "already default",
    "[agentloop][review]") {
    CHECK(review_note(ReviewContext{}).empty());
    ReviewContext head_only;
    head_only.head = "feature-x";
    const std::string note = review_note(head_only);
    CHECK(note.find("review feature-x compared to the repository's default branch") !=
          std::string::npos);
    CHECK(note.find("WITHOUT checking it out") != std::string::npos);
    CHECK(note.find("git diff <default-branch>...feature-x") != std::string::npos);
    CHECK(note.find("call git_diff and git_log with no refs") != std::string::npos);
    CHECK(note.find("Remote: origin.") != std::string::npos);

    ReviewContext both;
    both.head = "feature-x";
    both.base = "main";
    both.remote = "upstream";
    CHECK(review_note(both).find("git diff main...feature-x") != std::string::npos);
    CHECK(review_note(both).find("Remote: upstream.") != std::string::npos);
    CHECK(review_summary(both) == "feature-x vs main (remote upstream)");
    CHECK(review_summary(ReviewContext{}) == "current branch vs default branch (remote origin)");
}

TEST_CASE("compose joins with a blank line and tolerates either side empty",
          "[agentloop][review]") {
    CHECK(compose_system_prompt("persona", "note") == "persona\n\nnote");
    CHECK(compose_system_prompt("persona\n", "") == "persona");
    CHECK(compose_system_prompt("", "  note ") == "note");
    CHECK(compose_system_prompt("", "").empty());
    CHECK(valid_fetch_mode("auto"));
    CHECK(valid_fetch_mode("never"));
    CHECK_FALSE(valid_fetch_mode("sometimes"));
}
