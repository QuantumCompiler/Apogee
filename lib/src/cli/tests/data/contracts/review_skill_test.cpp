#include "contracts/review_skill.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "contracts/assets.h"
#include "support/env_guard.h"

/// Review skills at the Data floor (48b): the parser's table -- every hint,
/// every refusal named -- the globs and the deterministic applies, the
/// ladder (user over seeded over shipped, a dropped file a new lens, a
/// broken one said), the seven shipped each carrying silence on its face,
/// and the one finding contract read from the code-review agent's schema.
namespace {

using apogee::harness::ReviewSkill;
using apogee::harness::ReviewSkillSource;
using Catch::Matchers::ContainsSubstring;

[[nodiscard]] ReviewSkill parsed(std::string_view text, std::string_view name = "lens") {
    return apogee::harness::parse_review_skill(text, "lens.md", name);
}

[[nodiscard]] std::string refusal(std::string_view text, std::string_view name = "lens") {
    try {
        (void)apogee::harness::parse_review_skill(text, "lens.md", name);
    } catch (const std::runtime_error& e) {
        return e.what();
    }
    return "parsed";
}

[[nodiscard]] ReviewSkill hinted(std::vector<std::string> extensions,
                                 std::vector<std::string> paths,
                                 std::vector<std::string> languages = {}) {
    ReviewSkill skill;
    skill.applies.extensions = std::move(extensions);
    skill.applies.paths = std::move(paths);
    skill.applies.languages = std::move(languages);
    return skill;
}

}  // namespace

TEST_CASE("a review skill is its front matter and its prompt", "[review][skills][contracts]") {
    const ReviewSkill skill = parsed(
        "---\n"
        "name: lens\n"
        "description: One lens\n"
        "applies:\n"
        "  extensions: [.PY, .pyi]\n"
        "  paths: [\"**/ml/**\"]\n"
        "  languages: [rust]\n"
        "---\n"
        "\n"
        "Hunt for things.\n\nSilence is correct.\n\n");
    CHECK(skill.name == "lens");
    CHECK(skill.description == "One lens");
    CHECK(skill.applies.extensions == std::vector<std::string>{".py", ".pyi"});
    CHECK(skill.applies.paths == std::vector<std::string>{"**/ml/**"});
    CHECK(skill.applies.languages == std::vector<std::string>{"rust"});
    CHECK(skill.prompt == "Hunt for things.\n\nSilence is correct.");
    CHECK_FALSE(skill.applies.everywhere());

    // No hints: every file. CRLF line ends read the same.
    const ReviewSkill crlf = parsed("---\r\ndescription: x\r\n---\r\nBody\r\n");
    CHECK(crlf.applies.everywhere());
    CHECK(crlf.prompt == "Body");
}

TEST_CASE("a malformed skill is refused naming its file and what is wrong",
          "[review][skills][contracts]") {
    const std::vector<std::pair<std::string_view, std::string_view>> rows{
        {"No front matter\n", "opens with a --- front matter"},
        {"---\ndescription: x\nBody with no close\n", "never closed with a --- line"},
        {"---\n[unclosed\n---\nBody\n", "the front matter is not YAML"},
        {"---\n- a list\n---\nBody\n", "the front matter is a map"},
        {"---\ndescription: x\nmodel: gpt\n---\nBody\n", "unknown key 'model'"},
        {"---\napplies:\n  paths: [x]\n---\nBody\n", "description: is missing"},
        {"---\ndescription: x\napplies: [a]\n---\nBody\n", "applies: is a map"},
        {"---\ndescription: x\napplies:\n  globs: [x]\n---\nBody\n", "unknown applies key 'globs'"},
        {"---\ndescription: x\napplies:\n  extensions: [py]\n---\nBody\n",
         "'py' is an extension with its dot"},
        {"---\ndescription: x\napplies:\n  paths: [/etc/**]\n---\nBody\n",
         "is a glob relative to the repository"},
        {"---\ndescription: x\napplies:\n  paths: [\"../up/**\"]\n---\nBody\n",
         "is a glob relative to the repository"},
        {"---\ndescription: x\napplies:\n  languages: [cobol]\n---\nBody\n",
         "'cobol' is not a language a skill can name (c, cpp"},
        {"---\ndescription: x\napplies:\n  extensions: .py\n---\nBody\n",
         "applies.extensions is a list of strings"},
        {"---\ndescription: x\n---\n\n   \n", "has no prompt below its front matter"},
        {"---\nname: other\ndescription: x\n---\nBody\n", "name: says 'other'"},
    };
    for (const auto& [text, why] : rows) {
        INFO(text);
        const std::string error = refusal(text);
        CHECK_THAT(error, ContainsSubstring("lens.md: ") && ContainsSubstring(std::string{why}));
    }
}

TEST_CASE("globs match repository paths: one star a segment, two stars any",
          "[review][skills][contracts]") {
    struct Row {
        std::string_view glob;
        std::string_view path;
        bool match;
    };

    const std::vector<Row> rows{
        {"**/components/**", "web/src/components/Button.tsx", true},
        {"**/components/**", "components/Button.tsx", true},
        {"**/components/**", "web/component/Button.tsx", false},
        {"*.py", "a.py", true},
        {"*.py", "dir/a.py", false},
        {"src/*.py", "src/a.py", true},
        {"src/*.py", "src/x/a.py", false},
        {"src/**/*.py", "src/a.py", true},
        {"src/**/*.py", "src/x/y/a.py", true},
        {"src/**/*.py", "lib/a.py", false},
        {"a?c.txt", "abc.txt", true},
        {"a?c.txt", "a/c.txt", false},
        {"**", "anything/at/all", true},
        {"docs/**", "docs/a/b.md", true},
        {"docs/**", "src/docs/a.md", false},
    };
    for (const Row& row : rows) {
        INFO(row.glob << " ~ " << row.path);
        CHECK(apogee::harness::review_glob_match(row.glob, row.path) == row.match);
    }
}

TEST_CASE("a skill applies by extension, language or path, and everywhere with no hints",
          "[review][skills][contracts]") {
    CHECK(apogee::harness::skill_applies(ReviewSkill{}, "any/file.txt"));
    const ReviewSkill web = hinted({".tsx"}, {"**/ui/**"});
    CHECK(apogee::harness::skill_applies(web, "src/App.TSX"));  // the extension's case folded
    CHECK(apogee::harness::skill_applies(web, "pkg/ui/theme.css"));
    CHECK_FALSE(apogee::harness::skill_applies(web, "server/main.go"));
    const ReviewSkill rust = hinted({}, {}, {"rust"});
    CHECK(apogee::harness::skill_applies(rust, "src/lib.rs"));
    CHECK_FALSE(apogee::harness::skill_applies(rust, "Makefile"));
    CHECK_FALSE(apogee::harness::skill_applies(rust, ".rs"));  // a dot file has no extension
}

TEST_CASE("the seven shipped skills parse, and each carries silence on its face",
          "[review][skills][contracts]") {
    const std::vector<std::string_view> expected{"ai-ml",    "back-end",  "concurrency",
                                                 "embedded", "front-end", "general-correctness",
                                                 "security"};
    std::vector<std::string_view> names;
    for (const apogee::harness::BundledReviewSkill& bundled :
         apogee::harness::bundled_review_skills()) {
        names.push_back(bundled.name);
        INFO(bundled.name);
        const ReviewSkill skill =
            apogee::harness::parse_review_skill(bundled.text, "shipped", bundled.name);
        CHECK_FALSE(skill.description.empty());
        // The prime directive, word for word, in every lens.
        CHECK_THAT(skill.prompt, ContainsSubstring("Silence is correct. When the change carries "
                                                   "no such defect, the answer is `{\"findings\": "
                                                   "[]}`: a complete, valid review."));
        CHECK_THAT(skill.prompt, ContainsSubstring("`kind` is `correctness`"));
        // Always-on lenses have no hints; every other one has some.
        const bool always = bundled.name == "general-correctness" || bundled.name == "security";
        CHECK(skill.applies.everywhere() == always);
    }
    std::ranges::sort(names);
    CHECK(names == expected);
}

TEST_CASE("the ladder: a file over the shipped text, a dropped file a new lens, a broken one said",
          "[review][skills][contracts][ladder]") {
    const apogee::testing::TempDir dir{"review-skills-" + std::to_string(std::random_device{}())};
    // Nothing on disk: the seven from their compiled-in text.
    apogee::harness::ReviewSkillBook book = apogee::harness::review_skill_book(dir.path());
    REQUIRE(book.skills.size() == 7);
    CHECK(book.problems.empty());
    for (const apogee::harness::ReviewSkillEntry& entry : book.skills) {
        CHECK(entry.source == ReviewSkillSource::Shipped);
        CHECK(entry.path.empty());
    }

    // A seeded copy is `seeded`; an edited one is the user's and is what runs.
    const apogee::harness::BundledReviewSkill* front =
        apogee::harness::find_bundled_review_skill("front-end");
    REQUIRE(front != nullptr);
    std::ofstream{dir.path() / "front-end.md", std::ios::binary} << front->text;
    std::ofstream{dir.path() / "security.md", std::ios::binary}
        << "---\ndescription: Our own security lens\n---\nHunt for our house's bugs.\n";
    // A dropped file is a new lens; a broken one is said and left out.
    std::ofstream{dir.path() / "house-style.md", std::ios::binary}
        << "---\ndescription: Our conventions\napplies:\n  extensions: [.go]\n---\nHunt.\n";
    std::ofstream{dir.path() / "broken.md", std::ios::binary} << "no front matter\n";
    std::ofstream{dir.path() / "notes.txt", std::ios::binary} << "not a skill\n";
    book = apogee::harness::review_skill_book(dir.path());
    REQUIRE(book.skills.size() == 8);
    CHECK(book.find("front-end")->source == ReviewSkillSource::Seeded);
    CHECK(book.find("security")->source == ReviewSkillSource::User);
    CHECK(book.find("security")->skill.prompt == "Hunt for our house's bugs.");
    CHECK(book.find("house-style")->source == ReviewSkillSource::User);
    CHECK(book.skills.back().skill.name == "house-style");  // after the shipped ones
    REQUIRE(book.problems.size() == 1);
    CHECK_THAT(book.problems.front(), ContainsSubstring("broken.md"));
    CHECK(book.find("broken") == nullptr);
    CHECK(book.find("notes") == nullptr);

    // Which lenses a change's files bring.
    std::vector<std::string> applicable;
    for (const apogee::harness::ReviewSkillEntry* entry :
         book.applicable({"web/components/Comment.tsx", "server/main.go"})) {
        applicable.push_back(entry->skill.name);
    }
    CHECK(applicable == std::vector<std::string>{"back-end", "concurrency", "front-end",
                                                 "general-correctness", "security", "house-style"});
}

TEST_CASE("the one finding contract is the code-review agent's findings, read from its schema",
          "[review][skills][contracts][schema]") {
    const nlohmann::json schema =
        nlohmann::json::parse(apogee::harness::review_finding_schema_text());
    CHECK(schema["type"] == "object");
    CHECK(schema["required"] == nlohmann::json::array({"findings"}));
    CHECK(schema["additionalProperties"] == false);
    const nlohmann::json agent =
        nlohmann::json::parse(apogee::harness::find_bundled_agent("code-review")->schema);
    // The finding itself is the agent's, field for field: owned there once.
    CHECK(schema["properties"]["findings"]["items"] == agent["properties"]["findings"]["items"]);
    CHECK(schema["properties"]["findings"]["items"]["required"] ==
          nlohmann::json::array(
              {"severity", "kind", "file", "location", "claim", "suggested_fix", "confidence"}));
}
