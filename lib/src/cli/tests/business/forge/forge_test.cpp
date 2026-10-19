#include "forge/forge.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <optional>
#include <string>
#include <vector>

/// The reference grammar (48a), pinned as a table: every URL and short form
/// a merge request is typed as, each read to the one spelling, and every
/// near miss refused with the forms named.
namespace {

using apogee::forge::Forge;
using apogee::forge::MergeRequestRef;
using Catch::Matchers::ContainsSubstring;

[[nodiscard]] std::optional<MergeRequestRef> parse(std::string_view text) {
    std::string error;
    std::optional<MergeRequestRef> ref = apogee::forge::parse_ref(text, error);
    if (!ref.has_value()) {
        INFO(error);
    }
    return ref;
}

[[nodiscard]] std::string refusal(std::string_view text) {
    std::string error;
    CHECK_FALSE(apogee::forge::parse_ref(text, error).has_value());
    return error;
}

}  // namespace

TEST_CASE("every way a merge request is typed reads to one spelling", "[forge][ref]") {
    struct Row {
        std::string_view typed;
        Forge forge;
        std::string_view host;
        std::string_view project;
        std::uint64_t number;
        std::string_view canonical;
    };

    const std::vector<Row> rows{
        {"https://github.com/acme/widget/pull/12", Forge::GitHub, "github.com", "acme/widget", 12,
         "github.com/acme/widget#12"},
        {"https://github.com/acme/widget/pull/12/files?diff=split#r1", Forge::GitHub, "github.com",
         "acme/widget", 12, "github.com/acme/widget#12"},
        {"http://GitHub.com/acme/widget/pull/7", Forge::GitHub, "github.com", "acme/widget", 7,
         "github.com/acme/widget#7"},
        {"https://ghe.example.com/team/svc/pull/3", Forge::GitHub, "ghe.example.com", "team/svc", 3,
         "ghe.example.com/team/svc#3"},
        {"https://gitlab.com/group/project/-/merge_requests/4", Forge::GitLab, "gitlab.com",
         "group/project", 4, "gitlab.com/group/project!4"},
        {"https://gitlab.com/g/sub/deep/proj/-/merge_requests/45/diffs", Forge::GitLab,
         "gitlab.com", "g/sub/deep/proj", 45, "gitlab.com/g/sub/deep/proj!45"},
        {"https://git.corp.example/infra/tools/-/merge_requests/9", Forge::GitLab,
         "git.corp.example", "infra/tools", 9, "git.corp.example/infra/tools!9"},
        {"acme/widget#12", Forge::GitHub, "github.com", "acme/widget", 12,
         "github.com/acme/widget#12"},
        {"group/sub/project!4", Forge::GitLab, "gitlab.com", "group/sub/project", 4,
         "gitlab.com/group/sub/project!4"},
        {"github:acme/widget#12", Forge::GitHub, "github.com", "acme/widget", 12,
         "github.com/acme/widget#12"},
        {"gitlab:group/project!4", Forge::GitLab, "gitlab.com", "group/project", 4,
         "gitlab.com/group/project!4"},
        // The prefix chooses, whichever separator was typed.
        {"gitlab:group/project#4", Forge::GitLab, "gitlab.com", "group/project", 4,
         "gitlab.com/group/project!4"},
        {"  acme/my.repo_name-2#1  ", Forge::GitHub, "github.com", "acme/my.repo_name-2", 1,
         "github.com/acme/my.repo_name-2#1"},
    };
    for (const Row& row : rows) {
        INFO(row.typed);
        const std::optional<MergeRequestRef> ref = parse(row.typed);
        REQUIRE(ref.has_value());
        CHECK(ref->forge == row.forge);
        CHECK(ref->host == row.host);
        CHECK(ref->project == row.project);
        CHECK(ref->number == row.number);
        CHECK(apogee::forge::to_string(*ref) == row.canonical);
    }
}

TEST_CASE("a near miss is refused with the forms named", "[forge][ref]") {
    for (const std::string_view typed :
         {"", "acme/widget", "acme/widget#", "acme/widget#0", "acme/widget#-3", "acme/widget#1x",
          "widget#12", "a/b/c#12", "acme/../widget#1", "acme/wid get#1", "group!4",
          "https://github.com/acme/widget/issues/12", "https://github.com/acme/widget",
          "https://github.com", "https://gitlab.com/group/-/merge_requests/4",
          "https://gitlab.com/group/project/-/merge_requests/abc",
          "acme/widget#99999999999999999999"}) {
        INFO(typed);
        CHECK_THAT(refusal(typed),
                   ContainsSubstring("is not a merge request") &&
                       ContainsSubstring("owner/repo#12 (GitHub), group/project!4 (GitLab)"));
    }
    CHECK_THAT(refusal("a/b/c#12"), ContainsSubstring("a GitHub repository is owner/repo"));
}

TEST_CASE("a refusal carries its kind and the fix", "[forge]") {
    const apogee::forge::ForgeError error{apogee::forge::ForgeError::Kind::LoggedOut,
                                          "gh is not logged in", "gh auth login"};
    CHECK(error.kind() == apogee::forge::ForgeError::Kind::LoggedOut);
    CHECK(error.remedy() == "gh auth login");
    CHECK(std::string{error.what()} == "gh is not logged in");
    CHECK(apogee::forge::to_string(Forge::GitLab) == "gitlab");
    CHECK(apogee::forge::default_host(Forge::GitHub) == "github.com");
}
