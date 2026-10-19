#include "forge/glab_transport.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

#include "support/scripted_forge_cli.h"

/// GitLab through a scripted `glab` (48a): the project path encoded, every
/// verb's argv and body pinned, the per-file diffs joined into one unified
/// diff across pages, a comment anchored by the three commits, the batch as
/// draft notes published in bulk -- and a failure part-way said, the drafts
/// left the login's -- with the refusal ladder in glab's own words.
namespace {

using apogee::forge::ForgeError;
using apogee::forge::GlabTransport;
using apogee::forge::InlineComment;
using apogee::forge::MergeRequest;
using apogee::forge::MergeRequestRef;
using apogee::testing::ScriptedCli;
using Catch::Matchers::ContainsSubstring;

const MergeRequestRef kRef{.forge = apogee::forge::Forge::GitLab,
                           .host = "gitlab.com",
                           .project = "group/sub/project",
                           .number = 4};

constexpr std::string_view kBase = "projects/group%2Fsub%2Fproject/merge_requests/4";

constexpr std::string_view kMergeRequest = R"({
  "iid": 4, "title": "Tighten the loader", "description": "Refuses bad keys.",
  "author": {"username": "dev"}, "state": "opened", "draft": false,
  "work_in_progress": true, "source_branch": "tighten", "target_branch": "main",
  "sha": "headsha2",
  "diff_refs": {"base_sha": "base2", "start_sha": "start2", "head_sha": "headsha2"},
  "web_url": "https://gitlab.com/group/sub/project/-/merge_requests/4",
  "updated_at": "2026-10-18T10:00:00.000Z"
})";

[[nodiscard]] MergeRequest request_of(ScriptedCli& cli) {
    cli.replies = {{std::string{kMergeRequest}, "", 0}};
    GlabTransport glab{cli.options()};
    return glab.merge_request(kRef);
}

[[nodiscard]] std::vector<std::string> argv(std::initializer_list<std::string_view> words) {
    std::vector<std::string> out{"/usr/local/bin/cli"};
    for (const std::string_view word : words) {
        out.emplace_back(word);
    }
    return out;
}

}  // namespace

TEST_CASE("glab reads a merge request, its project path encoded", "[forge][glab]") {
    CHECK(apogee::forge::encode_project("group/sub/project") == "group%2Fsub%2Fproject");
    CHECK(apogee::forge::encode_project("a b+c") == "a%20b%2Bc");
    ScriptedCli cli;
    const MergeRequest request = request_of(cli);
    CHECK(cli.argv(0) == argv({"api", kBase}));
    CHECK(request.title == "Tighten the loader");
    CHECK(request.description == "Refuses bad keys.");
    CHECK(request.author == "dev");
    CHECK(request.state == "open");  // GitLab's "opened"
    CHECK(request.draft);            // work_in_progress, the older spelling
    CHECK(request.source_branch == "tighten");
    CHECK(request.base_sha == "base2");
    CHECK(request.start_sha == "start2");
    CHECK(request.head_sha == "headsha2");
    CHECK(request.web_url == "https://gitlab.com/group/sub/project/-/merge_requests/4");

    // A self-hosted host adds --hostname.
    cli.replies = {{std::string{kMergeRequest}, "", 0}};
    GlabTransport glab{cli.options()};
    MergeRequestRef hosted = kRef;
    hosted.host = "git.corp.example";
    (void)glab.merge_request(hosted);
    CHECK(cli.argv(1) == argv({"api", "--hostname", "git.corp.example", kBase}));
}

TEST_CASE("glab's per-file diffs become one unified diff, across pages", "[forge][glab]") {
    ScriptedCli cli;
    // Two pages, as --paginate prints them: one array after another.
    cli.replies = {{R"([{"old_path": "src/a.cpp", "new_path": "src/a.cpp", "a_mode": "100644",
                        "b_mode": "100644", "new_file": false, "renamed_file": false,
                        "deleted_file": false, "diff": "@@ -1,2 +1,2 @@\n-old\n+new\n ctx\n"}]
                     [{"old_path": "docs/new.md", "new_path": "docs/new.md", "a_mode": "0",
                       "b_mode": "100644", "new_file": true, "renamed_file": false,
                       "deleted_file": false, "diff": "@@ -0,0 +1 @@\n+hello café"},
                      {"old_path": "gone.txt", "new_path": "gone.txt", "a_mode": "100755",
                       "b_mode": "0", "new_file": false, "renamed_file": false,
                       "deleted_file": true, "diff": "@@ -1 +0,0 @@\n-bye\n"},
                      {"old_path": "old/name.h", "new_path": "new/name.h", "a_mode": "100644",
                       "b_mode": "100644", "new_file": false, "renamed_file": true,
                       "deleted_file": false, "diff": ""}])",
                    "", 0}};
    GlabTransport glab{cli.options()};
    const std::string diff = glab.diff(kRef);
    CHECK(cli.argv(0) == argv({"api", "--paginate", std::string{kBase} + "/diffs?per_page=100"}));
    CHECK(diff ==
          "diff --git a/src/a.cpp b/src/a.cpp\n"
          "--- a/src/a.cpp\n+++ b/src/a.cpp\n"
          "@@ -1,2 +1,2 @@\n-old\n+new\n ctx\n"
          "diff --git a/docs/new.md b/docs/new.md\n"
          "new file mode 100644\n"
          "--- /dev/null\n+++ b/docs/new.md\n"
          "@@ -0,0 +1 @@\n+hello caf\xc3\xa9\n"
          "diff --git a/gone.txt b/gone.txt\n"
          "deleted file mode 100755\n"
          "--- a/gone.txt\n+++ /dev/null\n"
          "@@ -1 +0,0 @@\n-bye\n"
          "diff --git a/old/name.h b/new/name.h\n"
          "rename from old/name.h\nrename to new/name.h\n");

    cli.replies = {{"[{\"old_path\": 1}] not json", "", 0}};
    CHECK_THROWS_AS(glab.diff(kRef), ForgeError);
}

TEST_CASE("glab posts a comment anchored by the three commits, and a batch as published drafts",
          "[forge][glab]") {
    ScriptedCli cli;
    const MergeRequest request = request_of(cli);
    cli.replies = {{"{}", "", 0}};
    GlabTransport glab{cli.options()};
    glab.create_comment(
        request,
        InlineComment{.path = "src/a.cpp", .line = 7, .body = "Off by one.", .old_line = 6});
    CHECK(cli.argv(1) == argv({"api", "--method", "POST", "-H", "Content-Type: application/json",
                               std::string{kBase} + "/discussions", "--input", "-"}));
    CHECK(nlohmann::json::parse(cli.input(1)) == nlohmann::json::parse(R"({
        "body": "Off by one.",
        "position": {"position_type": "text", "base_sha": "base2", "start_sha": "start2",
                     "head_sha": "headsha2", "old_path": "src/a.cpp", "new_path": "src/a.cpp",
                     "new_line": 7, "old_line": 6}})"));

    const apogee::forge::PublishOutcome outcome = glab.publish_batch(
        request, {InlineComment{.path = "src/b.cpp", .line = 2, .body = "Unchecked."}},
        "One finding.");
    CHECK(outcome.posted == 1);
    CHECK(outcome.batched);
    const std::string drafts = std::string{kBase} + "/draft_notes";
    CHECK(cli.argv(2) == argv({"api", "--method", "POST", "-H", "Content-Type: application/json",
                               drafts, "--input", "-"}));
    CHECK(nlohmann::json::parse(cli.input(2))["note"] == "Unchecked.");
    CHECK(nlohmann::json::parse(cli.input(2))["position"]["new_line"] == 2);
    CHECK_FALSE(nlohmann::json::parse(cli.input(2))["position"].contains("old_line"));
    CHECK(nlohmann::json::parse(cli.input(3)) ==
          nlohmann::json::parse(R"({"note": "One finding."})"));
    CHECK(cli.argv(4) == argv({"api", "--method", "POST", drafts + "/bulk_publish"}));
    CHECK(cli.commands.size() == 5);
}

TEST_CASE("a batch that fails part-way says the drafts it left", "[forge][glab][refusals]") {
    ScriptedCli cli;
    const MergeRequest request = request_of(cli);
    cli.replies.clear();
    cli.replies.push_back({std::string{kMergeRequest}, "", 0});  // the metadata, already read
    cli.replies.push_back({"{}", "", 0});                        // the first draft
    cli.replies.push_back({"", "glab: 400 Bad Request: line_code is invalid", 1});
    GlabTransport glab{cli.options()};
    try {
        (void)glab.publish_batch(request,
                                 {InlineComment{.path = "a", .line = 1, .body = "x"},
                                  InlineComment{.path = "b", .line = 2, .body = "y"}},
                                 "");
        FAIL("the failed draft was not refused");
    } catch (const ForgeError& e) {
        CHECK(e.kind() == ForgeError::Kind::Api);
        CHECK_THAT(e.what(), ContainsSubstring("400 Bad Request: line_code is invalid"));
        CHECK_THAT(e.what(), ContainsSubstring("(1 draft note(s) were made and not published: "
                                               "they stay as this login's drafts on the merge "
                                               "request)"));
    }
    // Never published.
    for (std::size_t i = 0; i < cli.commands.size(); ++i) {
        CHECK(cli.argv(i).back().find("bulk_publish") == std::string::npos);
    }
}

TEST_CASE("glab's refusals are its own words, each with the fix", "[forge][glab][refusals]") {
    const auto refused = [](ScriptedCli& cli) {
        GlabTransport glab{cli.options()};
        try {
            (void)glab.merge_request(kRef);
        } catch (const ForgeError& e) {
            return e;
        }
        FAIL("no refusal");
        return ForgeError{ForgeError::Kind::Api, "", ""};
    };
    {
        ScriptedCli cli;
        cli.on_path.clear();
        const ForgeError error = refused(cli);
        CHECK(error.kind() == ForgeError::Kind::Missing);
        CHECK(error.remedy() ==
              "install the GitLab CLI (https://gitlab.com/gitlab-org/cli), then: glab auth login");
    }
    {
        ScriptedCli cli;
        cli.replies = {{"", "glab: 401 Unauthorized", 1}};
        const ForgeError error = refused(cli);
        CHECK(error.kind() == ForgeError::Kind::LoggedOut);
        CHECK_THAT(
            error.what(),
            ContainsSubstring("glab is not logged in to gitlab.com: glab: 401 Unauthorized"));
        CHECK(error.remedy() == "glab auth login");
    }
    {
        ScriptedCli cli;
        cli.replies = {{"", "glab: 404 Project Not Found", 1}};
        const ForgeError error = refused(cli);
        CHECK(error.kind() == ForgeError::Kind::NotFound);
        CHECK(error.remedy() == "glab auth status");
    }
    {
        // A self-hosted login is named with its host.
        ScriptedCli cli;
        cli.replies = {{"", "glab: 401 Unauthorized", 1}};
        GlabTransport glab{cli.options()};
        MergeRequestRef hosted = kRef;
        hosted.host = "git.corp.example";
        try {
            (void)glab.merge_request(hosted);
            FAIL("no refusal");
        } catch (const ForgeError& e) {
            CHECK(e.remedy() == "glab auth login --hostname git.corp.example");
        }
    }
}

TEST_CASE("glab reads the login, the notes already there, and a comparison", "[forge][glab]") {
    ScriptedCli cli;
    cli.replies = {
        {R"({"username": "apogee-bot"})", "", 0},
        {R"([{"body": "added 2 commits", "system": true, "author": {"username": "dev"}},
             {"body": "Off by one.", "system": false, "author": {"username": "apogee-bot"},
              "position": {"new_path": "a.py", "new_line": 7}},
             {"body": "One finding.", "system": false, "author": {"username": "apogee-bot"}}])",
         "", 0},
        {R"({"commits": [], "diffs": [{"old_path": "a.py", "new_path": "a.py",
             "new_file": false, "renamed_file": false, "deleted_file": false,
             "diff": "@@ -1 +1 @@\n-a\n+b\n"}]})",
         "", 0},
    };
    GlabTransport glab{cli.options()};
    CHECK(glab.current_user(kRef) == "apogee-bot");
    CHECK(cli.argv(0) == argv({"api", "user"}));
    const std::vector<apogee::forge::PostedComment> notes = glab.comments(kRef);
    CHECK(cli.argv(1) == argv({"api", "--paginate", std::string{kBase} + "/notes?per_page=100"}));
    REQUIRE(notes.size() == 2);  // the system note left out
    CHECK(notes[0].path == "a.py");
    CHECK(notes[0].line == 7);
    CHECK(notes[1].path.empty());
    CHECK(glab.compare(kRef, "abc1234", "def5678") ==
          "diff --git a/a.py b/a.py\n--- a/a.py\n+++ b/a.py\n@@ -1 +1 @@\n-a\n+b\n");
    CHECK(cli.argv(2) == argv({"api",
                               "projects/group%2Fsub%2Fproject/repository/"
                               "compare?from=abc1234&to=def5678"}));
    CHECK_THROWS_AS(glab.compare(kRef, "abc1234", "main&x=1"), ForgeError);
    CHECK(cli.commands.size() == 3);
}
