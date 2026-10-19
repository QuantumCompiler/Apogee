#include "cli/review_mr.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "forge/gh_transport.h"
#include "review/identity.h"
#include "support/env_guard.h"
#include "support/scripted_forge_cli.h"

/// `review mr` end to end in process (48c): the real `gh` transport over a
/// scripted `gh`, scripted model passes, and the gate as a seam -- a first
/// review posting one batch, argv and body byte for byte; `--dry-run`
/// posting nothing; the gate's refusal posting nothing and saying why; a
/// clean change silent with no comment at all; and a re-review reading its
/// own markers back (another login's ignored), comparing from the head it
/// read, and posting nothing old.
namespace {

using apogee::commands::ReviewMrOptions;
using apogee::commands::ReviewMrResult;
using apogee::commands::ReviewMrSeams;
using apogee::review::PassAnswer;
using apogee::review::PassRequest;
using apogee::testing::CliReply;
using apogee::testing::ScriptedCli;
using Catch::Matchers::ContainsSubstring;

[[nodiscard]] std::string fixture(std::string_view name) {
    std::ifstream in{std::filesystem::path{APOGEE_TEST_FIXTURES} / "review" / name,
                     std::ios::binary};
    REQUIRE(in);
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

constexpr std::string_view kPull = R"({"title": "Thumbnails", "body": "Adds them.",
  "user": {"login": "dev"}, "state": "open", "draft": false, "merged_at": null,
  "head": {"ref": "thumbs", "sha": "cafe2222cafe2222"},
  "base": {"ref": "main", "sha": "base1111base1111"},
  "html_url": "https://github.com/acme/widget/pull/12", "updated_at": "2026-10-18T09:00:00Z"})";

/// A review over a scripted `gh` and a scripted model.
struct Review {
    apogee::testing::TempDir dir{"review-mr-" + std::to_string(std::random_device{}())};
    ScriptedCli cli;
    std::vector<PassRequest> passes;
    std::vector<std::string> said;
    bool allow = true;
    std::string refusal = "nobody to ask -- run with --yes to post, or --dry-run";
    /// What the security hunt answers.
    nlohmann::json hunt = nlohmann::json::parse(fixture("security.defect.json"));

    [[nodiscard]] ReviewMrSeams seams() {
        ReviewMrSeams seams;
        seams.forge = [this](apogee::forge::Forge) -> std::unique_ptr<apogee::forge::ForgeClient> {
            return std::make_unique<apogee::forge::GhTransport>(cli.options());
        };
        seams.pass = [this](const PassRequest& request) {
            passes.push_back(request);
            if (request.label == "hunt security") {
                return PassAnswer{.json = hunt, .conforms = true, .error = {}};
            }
            if (request.label == "confirm") {
                return PassAnswer{.json = nlohmann::json{{"verdicts", nlohmann::json::array()}},
                                  .conforms = true,
                                  .error = {}};
            }
            return PassAnswer{.json = nlohmann::json{{"findings", nlohmann::json::array()}},
                              .conforms = true,
                              .error = {}};
        };
        seams.gate = [this](std::string_view, std::size_t, std::string& why_not) {
            if (!allow) {
                why_not = refusal;
            }
            return allow;
        };
        seams.progress = [this](std::string_view line) { said.emplace_back(line); };
        seams.skills_dir = dir.path();
        return seams;
    }

    [[nodiscard]] ReviewMrResult run(bool dry_run = false) {
        ReviewMrOptions options;
        options.ref = "acme/widget#12";
        options.skills = {"security"};
        options.dry_run = dry_run;
        options.model = "local";
        return apogee::commands::review_merge_request(options, seams());
    }
};

/// The replies a first review reads: the pull, its diff, the login, no
/// comments, no reviews -- then whatever posting answers.
[[nodiscard]] std::vector<CliReply> first_review(const std::string& diff) {
    return {{std::string{kPull}, "", 0},
            {diff, "", 0},
            {R"({"login": "apogee-bot"})", "", 0},
            {"[]", "", 0},
            {"[]", "", 0},
            {"{}", "", 0}};
}

}  // namespace

TEST_CASE("review mr posts its findings as one batch, argv and body byte for byte",
          "[cli][review][mr]") {
    Review review;
    review.cli.replies = first_review(fixture("security.defect.diff"));
    const ReviewMrResult result = review.run();
    REQUIRE(review.cli.commands.size() == 6);
    CHECK(review.cli.argv(0) ==
          std::vector<std::string>{"/usr/local/bin/cli", "api", "repos/acme/widget/pulls/12"});
    CHECK(review.cli.argv(2) == std::vector<std::string>{"/usr/local/bin/cli", "api", "user"});
    CHECK(review.cli.argv(5) ==
          std::vector<std::string>{"/usr/local/bin/cli", "api", "--method", "POST", "-H",
                                   "Content-Type: application/json",
                                   "repos/acme/widget/pulls/12/reviews", "--input", "-"});
    const nlohmann::json batch = nlohmann::json::parse(review.cli.input(5));
    CHECK(batch["commit_id"] == "cafe2222cafe2222");
    CHECK(batch["event"] == "COMMENT");
    REQUIRE(batch["comments"].size() == 1);
    CHECK(batch["comments"][0]["path"] == "app/thumbnails.py");
    CHECK(batch["comments"][0]["line"] == 8);
    CHECK(batch["comments"][0]["side"] == "RIGHT");
    const std::string body = batch["comments"][0]["body"];
    CHECK_THAT(body, ContainsSubstring("**high · correctness** — The file name comes from the "
                                       "request"));
    CHECK(apogee::review::read_markers(body).findings.size() == 1);
    CHECK(apogee::review::read_markers(batch["body"].get<std::string>()).head ==
          "cafe2222cafe2222");

    const nlohmann::json& document = result.document;
    CHECK(document["object"] == "review.run");
    CHECK(document["merge_request"] == "github.com/acme/widget#12");
    CHECK(document["mode"] == "full");
    CHECK(document["skills"] == nlohmann::json::array({"security"}));
    CHECK(document["plan"] == "named");
    REQUIRE(document["findings"].size() == 1);
    CHECK(document["findings"][0]["inline"] == true);
    CHECK(document["posted"]["done"] == true);
    CHECK(document["posted"]["comments"] == 1);
    CHECK(document["posted"]["batched"] == true);
    CHECK(result.lines.back() ==
          "posted 1 comment(s) and a summary to github.com/acme/widget#12 as one notification");
    // The hunt read the numbered change and the merge request's text as data.
    REQUIRE_FALSE(review.passes.empty());
    CHECK_THAT(review.passes.front().user, ContainsSubstring("Title (data):\n<<<\nThumbnails"));
}

TEST_CASE("a dry run and a refusing gate post nothing, and say why", "[cli][review][mr]") {
    {
        Review review;
        review.cli.replies = first_review(fixture("security.defect.diff"));
        const ReviewMrResult result = review.run(/*dry_run=*/true);
        CHECK(review.cli.commands.size() == 5);  // never the batch
        CHECK(result.document["posted"]["done"] == false);
        CHECK(result.document["posted"]["reason"] == "dry run: nothing posted");
        CHECK(result.document["dry_run"] == true);
        CHECK(result.document["findings"].size() == 1);
    }
    {
        Review review;
        review.allow = false;
        review.cli.replies = first_review(fixture("security.defect.diff"));
        const ReviewMrResult result = review.run();
        CHECK(review.cli.commands.size() == 5);
        CHECK(result.document["posted"]["reason"] ==
              "nobody to ask -- run with --yes to post, or --dry-run");
        CHECK_THAT(result.lines.back(), ContainsSubstring("posted nothing: nobody to ask"));
    }
}

TEST_CASE("a clean change is silence: no comment, no summary, no confirm", "[cli][review][mr]") {
    Review review;
    review.hunt = nlohmann::json{{"findings", nlohmann::json::array()}};
    review.cli.replies = first_review(fixture("security.clean.diff"));
    const ReviewMrResult result = review.run();
    CHECK(review.cli.commands.size() == 5);
    CHECK(result.document["findings"].empty());
    CHECK(result.document["posted"]["done"] == false);
    CHECK(result.document["posted"]["reason"] == "nothing to say: silence is a complete review");
    REQUIRE(review.passes.size() == 1);
    CHECK(review.passes.front().label == "hunt security");
    CHECK_THAT(result.lines[2], ContainsSubstring("no findings -- silence is a complete review"));
}

TEST_CASE("a re-review reads its own markers, compares from the head it read, posts nothing old",
          "[cli][review][mr]") {
    Review review;
    const std::string identity =
        apogee::review::finding_identity("app/thumbnails.py", "8", "correctness");
    // Its own summary names the head it read and its own finding is there; a
    // marker another login wrote is ignored, head and identity alike.
    const nlohmann::json comments = nlohmann::json::array(
        {{{"body", "old\n\n" + apogee::review::finding_marker(identity)},
          {"user", {{"login", "apogee-bot"}}},
          {"path", "app/thumbnails.py"},
          {"line", 8}},
         {{"body", apogee::review::finding_marker(
                       apogee::review::finding_identity("app/thumbnails.py", "9", "correctness"))},
          {"user", {{"login", "dev"}}},
          {"path", "app/thumbnails.py"},
          {"line", 9}}});
    const nlohmann::json reviews = nlohmann::json::array(
        {{{"body", "Found one.\n\n" + apogee::review::summary_marker("aaaa1111aaaa1111")},
          {"user", {{"login", "apogee-bot"}}}},
         {{"body", apogee::review::summary_marker("ffff0000ffff0000")},
          {"user", {{"login", "dev"}}}}});
    // The new push touched line 10 only.
    const std::string since =
        "diff --git a/app/thumbnails.py b/app/thumbnails.py\n--- a/app/thumbnails.py\n"
        "+++ b/app/thumbnails.py\n@@ -9,2 +9,2 @@\n         f\"convert\n-        check=True)\n"
        "+        shell=True, check=True)\n";
    review.cli.replies = {{std::string{kPull}, "", 0},
                          {fixture("security.defect.diff"), "", 0},
                          {R"({"login": "apogee-bot"})", "", 0},
                          {comments.dump(), "", 0},
                          {reviews.dump(), "", 0},
                          {since, "", 0},
                          {"{}", "", 0}};
    // The model says line 8 again, and line 10.
    review.hunt = nlohmann::json{{"findings",
                                  {{{"severity", "high"},
                                    {"kind", "correctness"},
                                    {"file", "app/thumbnails.py"},
                                    {"location", "8"},
                                    {"claim", "Again."},
                                    {"suggested_fix", "x"},
                                    {"confidence", "high"}},
                                   {{"severity", "high"},
                                    {"kind", "correctness"},
                                    {"file", "app/thumbnails.py"},
                                    {"location", "10"},
                                    {"claim", "shell=True is back."},
                                    {"suggested_fix", "x"},
                                    {"confidence", "high"}}}}};
    const ReviewMrResult result = review.run();
    CHECK(review.cli.argv(5) ==
          std::vector<std::string>{
              "/usr/local/bin/cli", "api", "-H", "Accept: application/vnd.github.diff",
              "repos/acme/widget/compare/aaaa1111aaaa1111...cafe2222cafe2222"});
    CHECK(result.document["mode"] == "incremental");
    CHECK(result.document["since"] == "aaaa1111aaaa1111");
    CHECK(result.document["counts"]["not_new"] == 1);
    REQUIRE(result.document["findings"].size() == 1);
    CHECK(result.document["findings"][0]["line"] == 10);
    // The hunt was told what it already said -- its own, not the other login's.
    CHECK_THAT(review.passes.front().user,
               ContainsSubstring("known, do not report these again:\n- app/thumbnails.py:8: old"));
    CHECK(review.passes.front().user.find("app/thumbnails.py:9") == std::string::npos);
    const nlohmann::json batch = nlohmann::json::parse(review.cli.input(6));
    REQUIRE(batch["comments"].size() == 1);
    CHECK(batch["comments"][0]["line"] == 10);

    // The same head again: nothing new, no pass at all.
    Review twice;
    const nlohmann::json head_reviews =
        nlohmann::json::array({{{"body", apogee::review::summary_marker("cafe2222cafe2222")},
                                {"user", {{"login", "apogee-bot"}}}}});
    twice.cli.replies = {{std::string{kPull}, "", 0},
                         {fixture("security.defect.diff"), "", 0},
                         {R"({"login": "apogee-bot"})", "", 0},
                         {"[]", "", 0},
                         {head_reviews.dump(), "", 0}};
    const ReviewMrResult again = twice.run();
    CHECK(again.document["mode"] == "already reviewed");
    CHECK(twice.passes.empty());
    CHECK(twice.cli.commands.size() == 5);
}
