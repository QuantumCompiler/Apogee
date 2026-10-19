#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>
#include <vector>

#include "contracts/config.h"
#include "contracts/config_edit.h"
#include "contracts/errors.h"

/// The `review:` config unit (48d): sweep targets keyed by name -- a
/// repository or an owner, its forge and host, its lenses, its post grant,
/// drafts -- read and refused at load, and written through the one editor
/// in both formats, the append's exact inverse a delete.
namespace {

using Catch::Matchers::ContainsSubstring;

[[nodiscard]] std::string load_failure(const std::string& text) {
    try {
        (void)apogee::harness::parse_config(text, "<test>");
    } catch (const apogee::harness::ConfigError& e) {
        return e.what();
    }
    return "loaded";
}

}  // namespace

TEST_CASE("review targets load: a repository or an owner, its lenses, its grant",
          "[review][config]") {
    const apogee::harness::Config config = apogee::harness::parse_config(
        "review:\n"
        "  widget:\n"
        "    repo: acme/widget\n"
        "    skills: [security, back-end]\n"
        "    post: true\n"
        "  infra:\n"
        "    forge: gitlab\n"
        "    host: git.corp.example\n"
        "    owner: platform/infra\n"
        "    drafts: true\n"
        "    model: local\n"
        "    cli: /opt/bin/glab\n",
        "<test>");
    const apogee::harness::ReviewTargetConfig* widget = config.find_review_target("WIDGET");
    REQUIRE(widget != nullptr);
    CHECK(widget->forge == "github");
    CHECK(widget->repo == "acme/widget");
    CHECK(widget->owner.empty());
    CHECK(widget->skills == std::vector<std::string>{"security", "back-end"});
    CHECK(widget->post);
    CHECK_FALSE(widget->drafts);
    const apogee::harness::ReviewTargetConfig* infra = config.find_review_target("infra");
    REQUIRE(infra != nullptr);
    CHECK(infra->forge == "gitlab");
    CHECK(infra->host == "git.corp.example");
    CHECK(infra->owner == "platform/infra");
    CHECK(infra->drafts);
    CHECK_FALSE(infra->post);
    CHECK(infra->model == "local");
    CHECK(infra->cli == "/opt/bin/glab");
    CHECK(apogee::harness::parse_config("backends: {}\n", "<test>").review_targets.empty());
}

TEST_CASE("a malformed review target is refused naming the target and what is wrong",
          "[review][config]") {
    const std::vector<std::pair<std::string, std::string>> rows{
        {"review: [a]\n", "review: expected a mapping of target name -> settings"},
        {"review:\n  w: acme/widget\n", "review.w: expected a mapping of settings"},
        {"review:\n  w:\n    forge: github\n", "set exactly one of repo"},
        {"review:\n  w:\n    repo: a/b\n    owner: a\n", "set exactly one of repo"},
        {"review:\n  w:\n    repo: widget\n", "review.w.repo: 'widget' is not owner/repo"},
        {"review:\n  w:\n    repo: a/b/c\n", "review.w.repo: 'a/b/c' is not owner/repo"},
        {"review:\n  w:\n    forge: gitlab\n    repo: solo\n", "is not group/project"},
        {"review:\n  w:\n    repo: a/../b\n", "is not owner/repo"},
        {"review:\n  w:\n    owner: a/b\n", "review.w.owner: 'a/b' is not an organization"},
        {"review:\n  w:\n    repo: a/b\n    forge: bitbucket\n",
         "'bitbucket' is not github or gitlab"},
        {"review:\n  w:\n    repo: a/b\n    post: yes\n",
         "review.w.post: 'yes' is not true or false"},
        {"review:\n  w:\n    repo: a/b\n    schedule: hourly\n", "unknown key 'schedule'"},
        {"review:\n  w:\n    repo: a/b\n  W:\n    repo: c/d\n", "'W' collides with 'w'"},
    };
    for (const auto& [text, why] : rows) {
        INFO(text);
        CHECK_THAT(load_failure(text), ContainsSubstring(why));
    }
}

TEST_CASE("the editor writes a review target in place, in both formats, and deletes it back",
          "[review][config][config_edit]") {
    constexpr std::string_view kYaml = "backends:\n  mock:\n    type: mock   # the reviewer\n";
    apogee::harness::ReviewTargetConfig target;
    target.repo = "acme/widget";
    target.skills = {"security"};
    target.post = true;
    const std::string added = apogee::harness::append_review_target(kYaml, "widget", target, false);
    CHECK(added == std::string{kYaml} +
                       "\nreview:\n  widget:\n    forge: github\n    post: true\n"
                       "    repo: acme/widget\n    skills: [security]\n");
    CHECK(apogee::harness::parse_config(added, "<test>").find_review_target("widget")->post);
    // A second of the same name is refused without --force.
    CHECK_THROWS(apogee::harness::append_review_target(added, "widget", target, false));
    CHECK(apogee::harness::delete_review_target(added, "widget") ==
          std::string{kYaml} + "\nreview:\n");

    const std::string jsonc =
        "{\n  // the reviewer\n  \"backends\": {\"mock\": {\"type\": \"mock\"}}\n}\n";
    apogee::harness::ReviewTargetConfig owner;
    owner.forge = "gitlab";
    owner.owner = "platform";
    const std::string edited = apogee::harness::append_review_target(jsonc, "infra", owner, false);
    CHECK_THAT(edited, ContainsSubstring("// the reviewer"));
    const apogee::harness::Config read = apogee::harness::parse_config(edited, "<test>");
    REQUIRE(read.find_review_target("infra") != nullptr);
    CHECK(read.find_review_target("infra")->owner == "platform");
    CHECK(read.find_review_target("infra")->forge == "gitlab");
}
