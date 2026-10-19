#include "forge/gh_transport.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

#include "support/scripted_forge_cli.h"

/// GitHub through a scripted `gh` (48a): every verb's argv and request body
/// pinned byte for byte, the answers read into plain structs and made valid
/// UTF-8, the batch one review, and the refusal ladder -- missing, logged
/// out, not found, any other failure, an answer that is not JSON -- each in
/// gh's own words with the fix named. Nothing but `gh api` is ever run.
namespace {

using apogee::forge::ForgeError;
using apogee::forge::GhTransport;
using apogee::forge::InlineComment;
using apogee::forge::MergeRequest;
using apogee::forge::MergeRequestRef;
using apogee::testing::ScriptedCli;
using Catch::Matchers::ContainsSubstring;

const MergeRequestRef kRef{.forge = apogee::forge::Forge::GitHub,
                           .host = "github.com",
                           .project = "acme/widget",
                           .number = 12};

constexpr std::string_view kPull = R"({
  "number": 12, "title": "Fix the parser", "body": "It dropped the last line.",
  "user": {"login": "octo"}, "state": "open", "draft": true, "merged_at": null,
  "head": {"ref": "fix-parser", "sha": "headsha1"},
  "base": {"ref": "main", "sha": "basesha1"},
  "html_url": "https://github.com/acme/widget/pull/12",
  "updated_at": "2026-10-18T09:00:00Z"
})";

[[nodiscard]] MergeRequest request_of(ScriptedCli& cli) {
    cli.replies = {{std::string{kPull}, "", 0}};
    GhTransport gh{cli.options()};
    return gh.merge_request(kRef);
}

[[nodiscard]] ForgeError refusal(ScriptedCli& cli) {
    GhTransport gh{cli.options()};
    try {
        (void)gh.merge_request(kRef);
    } catch (const ForgeError& e) {
        return e;
    }
    FAIL("no refusal");
    return ForgeError{ForgeError::Kind::Api, "", ""};
}

}  // namespace

TEST_CASE("gh reads a pull request's metadata into the plain struct", "[forge][gh]") {
    ScriptedCli cli;
    const MergeRequest request = request_of(cli);
    CHECK(cli.argv(0) ==
          std::vector<std::string>{"/usr/local/bin/cli", "api", "repos/acme/widget/pulls/12"});
    CHECK(cli.input(0).empty());
    CHECK(request.ref == kRef);
    CHECK(request.title == "Fix the parser");
    CHECK(request.description == "It dropped the last line.");
    CHECK(request.author == "octo");
    CHECK(request.state == "open");
    CHECK(request.draft);
    CHECK(request.source_branch == "fix-parser");
    CHECK(request.target_branch == "main");
    CHECK(request.head_sha == "headsha1");
    CHECK(request.base_sha == "basesha1");
    CHECK(request.start_sha == "basesha1");
    CHECK(request.web_url == "https://github.com/acme/widget/pull/12");
    CHECK(request.updated_at == "2026-10-18T09:00:00Z");

    // Merged reads as merged; an Enterprise host adds --hostname.
    cli.replies = {
        {R"({"state": "closed", "merged_at": "2026-10-17T00:00:00Z", "title": "x"})", "", 0}};
    GhTransport gh{cli.options()};
    MergeRequestRef enterprise = kRef;
    enterprise.host = "ghe.example.com";
    CHECK(gh.merge_request(enterprise).state == "merged");
    CHECK(cli.argv(1) == std::vector<std::string>{"/usr/local/bin/cli", "api", "--hostname",
                                                  "ghe.example.com", "repos/acme/widget/pulls/12"});
}

TEST_CASE("gh fetches the diff, made valid UTF-8", "[forge][gh]") {
    ScriptedCli cli;
    // A Latin-1 byte in a file the diff carries: mended at entry.
    cli.replies = {{"diff --git a/n.txt b/n.txt\n+caf\xe9\n", "", 0}};
    GhTransport gh{cli.options()};
    const std::string diff = gh.diff(kRef);
    CHECK(cli.argv(0) == std::vector<std::string>{"/usr/local/bin/cli", "api", "-H",
                                                  "Accept: application/vnd.github.diff",
                                                  "repos/acme/widget/pulls/12"});
    CHECK(diff == "diff --git a/n.txt b/n.txt\n+caf\xef\xbf\xbd\n");
}

TEST_CASE("gh posts one comment, and a batch as one review", "[forge][gh]") {
    ScriptedCli cli;
    const MergeRequest request = request_of(cli);
    cli.replies = {{"{}", "", 0}};
    GhTransport gh{cli.options()};
    gh.create_comment(request,
                      InlineComment{.path = "src/a.cpp", .line = 7, .body = "Off by one."});
    CHECK(cli.argv(1) == std::vector<std::string>{"/usr/local/bin/cli", "api", "--method", "POST",
                                                  "-H", "Content-Type: application/json",
                                                  "repos/acme/widget/pulls/12/comments", "--input",
                                                  "-"});
    CHECK(nlohmann::json::parse(cli.input(1)) ==
          nlohmann::json::parse(R"({"path": "src/a.cpp", "line": 7, "side": "RIGHT",
                                    "body": "Off by one.", "commit_id": "headsha1"})"));

    const apogee::forge::PublishOutcome outcome =
        gh.publish_batch(request,
                         {InlineComment{.path = "src/a.cpp", .line = 7, .body = "Off by one."},
                          InlineComment{.path = "src/b.cpp", .line = 2, .body = "Unchecked."}},
                         "Two findings.");
    CHECK(outcome.posted == 2);
    CHECK(outcome.batched);
    CHECK(outcome.note.empty());
    CHECK(cli.argv(2) == std::vector<std::string>{"/usr/local/bin/cli", "api", "--method", "POST",
                                                  "-H", "Content-Type: application/json",
                                                  "repos/acme/widget/pulls/12/reviews", "--input",
                                                  "-"});
    CHECK(nlohmann::json::parse(cli.input(2)) == nlohmann::json::parse(R"({
        "commit_id": "headsha1", "event": "COMMENT", "body": "Two findings.",
        "comments": [
          {"path": "src/a.cpp", "line": 7, "side": "RIGHT", "body": "Off by one."},
          {"path": "src/b.cpp", "line": 2, "side": "RIGHT", "body": "Unchecked."}]})"));

    // Nothing to say is nothing posted.
    const apogee::forge::PublishOutcome silent = gh.publish_batch(request, {}, "");
    CHECK(silent.posted == 0);
    CHECK(cli.commands.size() == 3);
    // Only ever `gh api`: never `gh auth token`, never a credential read.
    for (std::size_t i = 0; i < cli.commands.size(); ++i) {
        CHECK(cli.argv(i).at(1) == "api");
    }
}

TEST_CASE("gh's refusals are its own words, each with the fix", "[forge][gh][refusals]") {
    SECTION("not installed") {
        ScriptedCli cli;
        cli.on_path.clear();
        const ForgeError error = refusal(cli);
        CHECK(error.kind() == ForgeError::Kind::Missing);
        CHECK_THAT(error.what(), ContainsSubstring("gh is not installed, or not on PATH"));
        CHECK(error.remedy() ==
              "install the GitHub CLI (https://cli.github.com), then: gh auth login");
        CHECK(cli.commands.empty());
    }
    SECTION("logged out") {
        ScriptedCli cli;
        cli.replies = {{"", "To get started with GitHub CLI, please run:  gh auth login", 4}};
        const ForgeError error = refusal(cli);
        CHECK(error.kind() == ForgeError::Kind::LoggedOut);
        CHECK_THAT(error.what(), ContainsSubstring("gh is not logged in to github.com: To get "
                                                   "started with GitHub CLI, please run:  gh "
                                                   "auth login"));
        CHECK(error.remedy() == "gh auth login");
    }
    SECTION("not found") {
        ScriptedCli cli;
        cli.replies = {{R"({"message":"Not Found"})", "gh: Not Found (HTTP 404)", 1}};
        const ForgeError error = refusal(cli);
        CHECK(error.kind() == ForgeError::Kind::NotFound);
        CHECK_THAT(error.what(),
                   ContainsSubstring("github.com/acme/widget#12 was not found, or this gh login "
                                     "cannot see it: gh: Not Found (HTTP 404)"));
        CHECK(error.remedy() == "gh auth status");
    }
    SECTION("any other failure, its tail folded in") {
        ScriptedCli cli;
        cli.replies = {{"", "gh: Validation Failed (HTTP 422)\nline must be part of the diff", 1}};
        const ForgeError error = refusal(cli);
        CHECK(error.kind() == ForgeError::Kind::Api);
        CHECK_THAT(error.what(), ContainsSubstring("gh api failed for github.com/acme/widget#12 "
                                                   "(exit 1): gh: Validation Failed (HTTP 422)"));
    }
    SECTION("an answer that is not a pull request") {
        ScriptedCli cli;
        cli.replies = {{"<html>proxy login</html>", "", 0}};
        CHECK(refusal(cli).kind() == ForgeError::Kind::Malformed);
    }
    SECTION("a GitLab reference given to gh") {
        ScriptedCli cli;
        GhTransport gh{cli.options()};
        MergeRequestRef gitlab = kRef;
        gitlab.forge = apogee::forge::Forge::GitLab;
        CHECK_THROWS_AS(gh.diff(gitlab), ForgeError);
        CHECK(cli.commands.empty());
    }
}

TEST_CASE("gh reads the login, the comments already there, and a comparison", "[forge][gh]") {
    ScriptedCli cli;
    cli.replies = {
        {R"({"login": "apogee-bot"})", "", 0},
        // Two pages of inline comments, then the reviews: the one with a body
        // is a general comment, the empty one is not.
        {R"([{"body": "Off by one.", "user": {"login": "apogee-bot"}, "path": "a.py", "line": 7}])"
         R"([{"body": "Nit.", "user": {"login": "dev"}, "path": "b.py", "line": 2}])",
         "", 0},
        {R"([{"body": "Two findings.", "user": {"login": "apogee-bot"}},
             {"body": "", "user": {"login": "dev"}}])",
         "", 0},
        {"diff --git a/a.py b/a.py\n+x\n", "", 0},
    };
    GhTransport gh{cli.options()};
    CHECK(gh.current_user(kRef) == "apogee-bot");
    CHECK(cli.argv(0) == std::vector<std::string>{"/usr/local/bin/cli", "api", "user"});
    const std::vector<apogee::forge::PostedComment> comments = gh.comments(kRef);
    CHECK(cli.argv(1) == std::vector<std::string>{"/usr/local/bin/cli", "api", "--paginate",
                                                  "repos/acme/widget/pulls/12/comments"});
    CHECK(cli.argv(2) == std::vector<std::string>{"/usr/local/bin/cli", "api", "--paginate",
                                                  "repos/acme/widget/pulls/12/reviews"});
    REQUIRE(comments.size() == 3);
    CHECK(comments[0].path == "a.py");
    CHECK(comments[0].line == 7);
    CHECK(comments[0].author == "apogee-bot");
    CHECK(comments[1].author == "dev");
    CHECK(comments[2].body == "Two findings.");
    CHECK(comments[2].path.empty());

    CHECK(gh.compare(kRef, "abc1234", "def5678") == "diff --git a/a.py b/a.py\n+x\n");
    CHECK(cli.argv(3) == std::vector<std::string>{"/usr/local/bin/cli", "api", "-H",
                                                  "Accept: application/vnd.github.diff",
                                                  "repos/acme/widget/compare/abc1234...def5678"});
    // A commit id read from a comment is checked before it reaches a path.
    CHECK_THROWS_AS(gh.compare(kRef, "../../etc", "def5678"), ForgeError);
    CHECK(cli.commands.size() == 4);
}
