#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "agentloop/structured.h"
#include "contracts/assets.h"
#include "contracts/review_skill.h"
#include "support/cli_home.h"

/// `review skills list|show` (48b) through the real command tree -- the
/// listing and its document, a skill shown whole, a dropped file listed as
/// the user's, an edited shipped one shadowing it, names completed -- and
/// the goldens over the planted fixtures (`tests/fixtures/review/`): each
/// shipped skill applies to its fixture's files, its canned answer on the
/// planted defect is held to the one finding contract and points at a line
/// the change added, and silence on the clean twin is itself a valid answer.
namespace {

using Catch::Matchers::ContainsSubstring;

constexpr std::string_view kConfig = "backends:\n  mock:\n    type: mock\n";

[[nodiscard]] std::filesystem::path fixtures() {
    return std::filesystem::path{APOGEE_TEST_FIXTURES} / "review";
}

[[nodiscard]] std::string slurp(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    REQUIRE(in);
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// The new-side line numbers a unified diff adds, by file.
[[nodiscard]] std::map<std::string, std::set<int>> added_lines(const std::string& diff) {
    std::map<std::string, std::set<int>> out;
    std::istringstream in{diff};
    std::string line;
    std::string file;
    int next = 0;
    const std::regex hunk{R"(^@@ -\d+(?:,\d+)? \+(\d+)(?:,\d+)? @@)"};
    while (std::getline(in, line)) {
        std::smatch match;
        if (line.starts_with("+++ b/")) {
            file = line.substr(6);
        } else if (std::regex_search(line, match, hunk)) {
            next = std::stoi(match[1]);
        } else if (line.starts_with("+")) {
            out[file].insert(next++);
        } else if (!line.starts_with("-") && !line.starts_with("diff ") &&
                   !line.starts_with("--- ")) {
            ++next;
        }
    }
    return out;
}

}  // namespace

TEST_CASE("review skills lists the seven, then the user's, with its document",
          "[cli][review][skills]") {
    const apogee::testing::CliHome home{std::string{kConfig}};
    std::string out;
    std::string err;
    REQUIRE(home.run({"review", "skills", "list"}, &out, &err) == 0);
    CHECK(out.starts_with("NAME"));
    for (const std::string_view name : {"general-correctness", "front-end", "back-end", "embedded",
                                        "ai-ml", "concurrency", "security"}) {
        CHECK_THAT(out, ContainsSubstring(std::string{name}));
    }
    CHECK_THAT(out, ContainsSubstring("general-correctness  shipped  every file"));

    // A dropped lens and an edited shipped one: the user's, and listed so.
    const std::filesystem::path dir = home.home() / "review-skills";
    std::filesystem::create_directories(dir);
    std::ofstream{dir / "house-style.md", std::ios::binary}
        << "---\ndescription: Our conventions\napplies:\n  extensions: [.go]\n---\nHunt.\n";
    std::ofstream{dir / "security.md", std::ios::binary}
        << "---\ndescription: Our own security lens\n---\nHunt for ours.\n";
    REQUIRE(home.run({"review", "skills", "list", "--output-format", "json"}, &out, &err) == 0);
    const nlohmann::json document = nlohmann::json::parse(out);
    CHECK(document["object"] == "list");
    REQUIRE(document["data"].size() == 8);
    CHECK(document["data"][7]["name"] == "house-style");
    CHECK(document["data"][7]["source"] == "user");
    CHECK(document["data"][7]["applies"]["extensions"] == nlohmann::json::array({".go"}));
    CHECK(document["data"][6]["name"] == "security");
    CHECK(document["data"][6]["source"] == "user");
    CHECK(document["data"][6]["everywhere"] == true);
    CHECK_FALSE(document["data"][0].contains("prompt"));
    CHECK(document["problems"].empty());
}

TEST_CASE("review skills show prints one whole, or its document, and names the rest",
          "[cli][review][skills]") {
    const apogee::testing::CliHome home{std::string{kConfig}};
    std::string out;
    std::string err;
    REQUIRE(home.run({"review", "skills", "show", "front-end"}, &out, &err) == 0);
    CHECK(out.starts_with("front-end  shipped\nBrowser code"));
    CHECK_THAT(out, ContainsSubstring("extensions: .js, .jsx"));
    CHECK_THAT(out, ContainsSubstring("Silence is correct."));
    REQUIRE(home.run({"review", "skills", "show", "security", "--output-format", "json"}, &out,
                     &err) == 0);
    const nlohmann::json skill = nlohmann::json::parse(out);
    CHECK(skill["object"] == "review.skill");
    CHECK(skill["name"] == "security");
    CHECK(skill["everywhere"] == true);
    CHECK_THAT(skill["prompt"].get<std::string>(), ContainsSubstring("Hunt for:"));
    CHECK(home.run({"review", "skills", "show", "nope"}, &out, &err) != 0);
    CHECK_THAT(err, ContainsSubstring("no review skill named 'nope' (there are: ai-ml, back-end"));
    REQUIRE(home.run({"__complete", "review", "skills", "show", ""}, &out, &err) == 0);
    CHECK(out ==
          "ai-ml\nback-end\nconcurrency\nembedded\nfront-end\ngeneral-correctness\n"
          "security\n");
}

TEST_CASE("each shipped skill's planted defect and clean twin hold to the one finding contract",
          "[cli][review][skills][goldens]") {
    // The precision golden, both directions, as far as a scripted model
    // takes it: the canned answer on the planted defect conforms and points
    // at a line the change added, in a file the skill applies to; silence
    // on the clean twin conforms too. What a real model says over the same
    // fixtures is the real-weights check's.
    const nlohmann::json schema =
        nlohmann::json::parse(apogee::harness::review_finding_schema_text());
    REQUIRE(apogee::agentloop::validate_schema(schema).ok);
    for (const apogee::harness::BundledReviewSkill& bundled :
         apogee::harness::bundled_review_skills()) {
        const std::string name{bundled.name};
        INFO(name);
        const apogee::harness::ReviewSkill skill =
            apogee::harness::parse_review_skill(bundled.text, "shipped", name);
        const std::string defect = slurp(fixtures() / (name + ".defect.diff"));
        const std::string clean = slurp(fixtures() / (name + ".clean.diff"));
        const std::map<std::string, std::set<int>> added = added_lines(defect);
        REQUIRE(added.size() == 1);
        // The lens applies to the change it is planted in.
        CHECK(apogee::harness::skill_applies(skill, added.begin()->first));
        CHECK(added_lines(clean).begin()->first == added.begin()->first);

        const nlohmann::json found =
            nlohmann::json::parse(slurp(fixtures() / (name + ".defect.json")));
        const apogee::agentloop::ValidationResult valid =
            apogee::agentloop::validate_against(schema, found);
        INFO((valid.errors.empty() ? std::string{} : valid.errors.front()));
        CHECK(valid.ok);
        REQUIRE(found["findings"].size() == 1);
        const nlohmann::json& finding = found["findings"][0];
        CHECK(finding["kind"] == "correctness");
        CHECK(added.contains(finding["file"].get<std::string>()));
        const int line = std::stoi(finding["location"].get<std::string>());
        CHECK(added.at(finding["file"].get<std::string>()).contains(line));

        // Silence on the clean twin is a complete answer; a finding missing
        // a field is not.
        CHECK(apogee::agentloop::validate_against(schema,
                                                  nlohmann::json::parse(R"({"findings": []})"))
                  .ok);
        nlohmann::json partial = found;
        partial["findings"][0].erase("confidence");
        CHECK_FALSE(apogee::agentloop::validate_against(schema, partial).ok);
    }
}

TEST_CASE("review targets adds, lists and deletes the sweep's targets through the one editor",
          "[cli][review][targets]") {
    const apogee::testing::CliHome home{std::string{kConfig}};
    std::string out;
    std::string err;
    REQUIRE(home.run({"review", "targets"}, &out, &err) == 0);
    CHECK_THAT(out, ContainsSubstring("no review targets -- add one"));
    REQUIRE(home.run({"review", "targets", "add", "widget", "--repo", "acme/widget", "--skills",
                      "security,back-end", "--post"},
                     &out, &err) == 0);
    CHECK_THAT(out, ContainsSubstring("added review target 'widget'"));
    REQUIRE(home.run({"review", "targets", "add", "infra", "--owner", "platform", "--forge",
                      "gitlab", "--host", "git.corp.example", "--drafts"},
                     &out, &err) == 0);
    CHECK_THAT(out, ContainsSubstring("it posts only under --allow-post"));
    REQUIRE(home.run({"review", "targets", "--output-format", "json"}, &out, &err) == 0);
    const nlohmann::json document = nlohmann::json::parse(out);
    REQUIRE(document["data"].size() == 2);
    CHECK(document["data"][0]["name"] == "infra");
    CHECK(document["data"][0]["owner"] == "platform");
    CHECK(document["data"][0]["forge"] == "gitlab");
    CHECK(document["data"][0]["drafts"] == true);
    CHECK(document["data"][1]["repo"] == "acme/widget");
    CHECK(document["data"][1]["skills"] == nlohmann::json::array({"security", "back-end"}));
    CHECK(document["data"][1]["post"] == true);

    // Refusals, each named, the file untouched.
    const std::string before = slurp(home.config_path());
    CHECK(home.run({"review", "targets", "add", "bad", "--repo", "widget"}, &out, &err) != 0);
    CHECK_THAT(err, ContainsSubstring("'widget' is not owner/repo"));
    CHECK(home.run({"review", "targets", "add", "both", "--repo", "a/b", "--owner", "a"}, &out,
                   &err) != 0);
    CHECK(home.run({"review", "targets", "add", "widget", "--repo", "acme/other"}, &out, &err) !=
          0);
    CHECK(slurp(home.config_path()) == before);

    REQUIRE(home.run({"__complete", "review", "targets", "delete", ""}, &out, &err) == 0);
    CHECK(out == "infra\nwidget\n");
    REQUIRE(home.run({"__complete", "review", "sweep", "--target", ""}, &out, &err) == 0);
    CHECK(out == "infra\nwidget\n");
    REQUIRE(home.run({"review", "targets", "delete", "infra"}, &out, &err) == 0);
    REQUIRE(home.run({"review", "targets", "delete", "widget"}, &out, &err) == 0);
    REQUIRE(home.run({"review", "targets", "--output-format", "json"}, &out, &err) == 0);
    CHECK(nlohmann::json::parse(out)["data"].empty());
    CHECK(home.run({"review", "sweep"}, &out, &err) != 0);
    CHECK_THAT(err, ContainsSubstring("no review targets to sweep"));
}
