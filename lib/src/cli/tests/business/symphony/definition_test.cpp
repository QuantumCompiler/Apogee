#include "symphony/definition.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "contracts/assets.h"
#include "contracts/config.h"
#include "support/env_guard.h"

/// A symphony's definition (27q): the template grammar, the validation a
/// definition passes before any model is asked anything, and the sources --
/// a config entry over a starter over a spec file -- table-tested.
namespace {

using apogee::harness::SymphonySpec;
using apogee::harness::SymphonyStage;
using apogee::symphony::catalog;
using apogee::symphony::Definition;
using apogee::symphony::find_definition;
using apogee::symphony::parse_template;
using apogee::symphony::render_template;
using apogee::symphony::Segment;
using apogee::symphony::Source;
using apogee::symphony::validate;

SymphonyStage stage(std::string name, std::string role, std::string prompt) {
    SymphonyStage out;
    out.name = std::move(name);
    out.role = std::move(role);
    out.prompt = std::move(prompt);
    return out;
}

SymphonySpec spec_of(std::vector<SymphonyStage> stages) {
    SymphonySpec spec;
    spec.name = "s";
    spec.stages = std::move(stages);
    return spec;
}

std::string first_problem(const SymphonySpec& spec) {
    const std::vector<std::string> problems = validate(spec);
    return problems.empty() ? std::string{} : problems.front();
}

void write(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << text;
}

}  // namespace

TEST_CASE("a template reads into text and variables", "[symphony][definition]") {
    std::string error;
    CHECK(parse_template("Summarize {{input}} now", error) ==
          std::vector<Segment>{{false, "Summarize "}, {true, "input"}, {false, " now"}});
    CHECK(error.empty());
    CHECK(parse_template("{{ input }}{{a-b_c}}", error) ==
          std::vector<Segment>{{true, "input"}, {true, "a-b_c"}});
    CHECK(parse_template("no variables, {braces} and }} alone", error) ==
          std::vector<Segment>{{false, "no variables, {braces} and }} alone"}});
    CHECK(parse_template("", error).empty());
    CHECK(error.empty());

    for (const std::string bad : {"{{input", "{{}}", "{{ }}", "{{two words}}", "{{a.b}}"}) {
        INFO(bad);
        std::string why;
        CHECK(parse_template("x " + bad, why).empty());
        CHECK_FALSE(why.empty());
    }
}

TEST_CASE("rendering replaces variables and touches nothing else", "[symphony][definition]") {
    const std::map<std::string, std::string> values{{"input", "THE TEXT"},
                                                    {"summarize", "A {{summary}}"}};
    CHECK(render_template("P:\n{{input}}\nS: {{ summarize }}.", values) ==
          "P:\nTHE TEXT\nS: A {{summary}}.");
    // An answer holding braces is threaded as text, never re-read as a template.
    CHECK(render_template("{{summarize}}", values) == "A {{summary}}");
    CHECK(render_template("${HOME} $$ {single}", values) == "${HOME} $$ {single}");
    CHECK(render_template("{{unknown}}", values) == "{{unknown}}");
    CHECK(apogee::symphony::template_variables("{{a}} {{input}} {{a}}") ==
          std::vector<std::string>{"a", "input"});
}

TEST_CASE("validation: every reason a definition cannot be played, before any call",
          "[symphony][definition]") {
    CHECK(validate(spec_of({stage("a", "utility", "{{input}}"),
                            stage("b", "chat", "{{a}} and {{input}}")}))
              .empty());
    CHECK_THAT(first_problem(spec_of({stage("a", "utility", "{{nope}} {{input}}")})),
               Catch::Matchers::ContainsSubstring("stage 1 (a): {{nope}} names no stage"));
    CHECK_THAT(first_problem(spec_of(
                   {stage("a", "utility", "{{b}} {{input}}"), stage("b", "chat", "{{a}}")})),
               Catch::Matchers::ContainsSubstring(
                   "stage 1 (a): {{b}} is stage 2's answer, not yet played when stage 1 runs"));
    CHECK_THAT(first_problem(spec_of({stage("a", "utility", "{{a}} {{input}}")})),
               Catch::Matchers::ContainsSubstring("is this stage's own answer"));
    CHECK_THAT(first_problem(spec_of({stage("a", "utility", "{{input")})),
               Catch::Matchers::ContainsSubstring("an unclosed '{{'"));
    CHECK_THAT(first_problem(spec_of({stage("a", "utility", "nothing read")})),
               Catch::Matchers::ContainsSubstring("no stage reads the input"));

    SymphonySpec schema = spec_of({stage("a", "extraction", "{{input}}")});
    schema.stages[0].schema = R"({"type": "object", "properties": {"x": {"type": 7}}})";
    CHECK_THAT(first_problem(schema),
               Catch::Matchers::ContainsSubstring("its schema is not a valid JSON Schema"));
    schema.stages[0].schema = R"({"type": "object", "required": ["x"]})";
    CHECK(validate(schema).empty());

    SymphonySpec image = spec_of({stage("a", "vision", "Describe it.")});
    image.input.image = true;
    const std::vector<std::string> unread = validate(image);
    REQUIRE(unread.size() == 2);
    CHECK_THAT(unread[0], Catch::Matchers::ContainsSubstring("no stage reads the input"));
    CHECK_THAT(unread[1], Catch::Matchers::ContainsSubstring("no stage is given it"));
    image.stages[0].image = true;
    // A symphony whose one stage reads the image alone needs no {{input}}.
    CHECK(validate(image).empty());
}

TEST_CASE("every shipped starter is sound", "[symphony][definition]") {
    for (const apogee::harness::BundledSymphony& starter : apogee::harness::bundled_symphonies()) {
        INFO(starter.name);
        const SymphonySpec spec =
            apogee::harness::parse_symphony_spec(starter.text, "starter", starter.name);
        CHECK(validate(spec).empty());
    }
}

TEST_CASE("the sources: an entry over a starter over a file, each name once, in order",
          "[symphony][definition]") {
    const apogee::testing::TempDir home{"symphony-catalog-" +
                                        std::to_string(std::random_device{}())};
    const std::filesystem::path dir = home.path() / "symphonies";
    const std::string mine = "stages:\n  - {name: a, role: utility, prompt: '{{input}}'}\n";
    write(dir / "mine.yaml", mine);
    write(dir / "zeta.yml", "description: last\n" + mine);
    write(dir / "broken.yaml", "stages: [\n");
    write(dir / "liar.yaml", "name: other\n" + mine);
    write(dir / "notes.txt", "not a spec");
    // A seeded starter edited by its user is what plays.
    write(dir / "summarize-verify.yaml", "name: summarize-verify\ndescription: my edit\n" + mine);

    const apogee::harness::Config config = apogee::harness::parse_config(R"YAML(symphonies:
  extract-facts:
    description: my override
    stages:
      - {name: a, role: extraction, prompt: '{{input}}'}
  zeta:
    description: the entry wins
    stages:
      - {name: a, role: utility, prompt: '{{input}}'}
  alpha:
    stages:
      - {name: a, role: utility, prompt: '{{input}}'}
)YAML",
                                                                         "<test>");
    const apogee::symphony::Catalog all = catalog(config, dir);
    std::vector<std::string> names;
    for (const Definition& definition : all.definitions) {
        names.push_back(definition.spec.name + ":" +
                        std::string{apogee::symphony::to_string(definition.source)} +
                        (definition.overrides ? "+" : ""));
    }
    CHECK(names == std::vector<std::string>{"describe-answer:shipped", "extract-facts:config+",
                                            "summarize-verify:shipped", "alpha:config",
                                            "zeta:config+", "mine:file"});
    CHECK(all.definitions[2].spec.description == "my edit");
    CHECK(all.definitions[2].path == dir / "summarize-verify.yaml");
    // The compiled-in text stands in for a starter whose file is absent.
    CHECK(all.definitions[0].path.empty());
    CHECK(all.definitions[0].spec.input.image);
    REQUIRE(all.problems.size() == 2);
    CHECK_THAT(all.problems[0], Catch::Matchers::ContainsSubstring("broken.yaml"));
    CHECK_THAT(all.problems[1],
               Catch::Matchers::ContainsSubstring("names itself 'other' -- a file under "
                                                  "symphonies/ plays by its file's name"));

    SECTION("by name, as the catalog orders them") {
        const apogee::symphony::Found entry = find_definition(config, dir, "ZETA");
        REQUIRE(entry.definition.has_value());
        CHECK(entry.definition->spec.description == "the entry wins");
        const apogee::symphony::Found mine_found = find_definition(config, dir, "mine");
        REQUIRE(mine_found.definition.has_value());
        CHECK(mine_found.definition->source == Source::File);
        const apogee::symphony::Found none = find_definition(config, dir, "nope");
        CHECK_FALSE(none.definition.has_value());
        CHECK_THAT(none.error, Catch::Matchers::ContainsSubstring(
                                   "no symphony named 'nope' (there are: describe-answer"));
        CHECK_THAT(find_definition(config, dir, "broken").error,
                   Catch::Matchers::ContainsSubstring("not valid YAML"));
    }
    SECTION("by path, read through the one parser with the config's backends named") {
        const std::filesystem::path elsewhere = home.path() / "elsewhere" / "spec.yaml";
        write(elsewhere, mine);
        const apogee::symphony::Found found = find_definition(config, dir, elsewhere.string());
        REQUIRE(found.definition.has_value());
        CHECK(found.definition->spec.name == "spec");
        CHECK(found.definition->source == Source::File);
        CHECK(found.definition->path == elsewhere);
        CHECK_THAT(find_definition(config, dir, (home.path() / "missing.yaml").string()).error,
                   Catch::Matchers::ContainsSubstring("no spec file at"));
        const apogee::harness::Config with_backend =
            apogee::harness::parse_config("backends:\n  l3b:\n    type: mock\n", "<test>");
        write(elsewhere, "stages:\n  - {name: a, role: l3b, prompt: '{{input}}'}\n");
        CHECK_THAT(find_definition(with_backend, dir, elsewhere.string()).error,
                   Catch::Matchers::ContainsSubstring("'l3b' is a backend"));
    }
    CHECK(apogee::symphony::role_chain(all.definitions[0].spec) == "vision → chat");
}

TEST_CASE("with no directory and no entries, the starters alone", "[symphony][definition]") {
    const apogee::symphony::Catalog all =
        catalog(apogee::harness::Config{}, "/nonexistent/apogee/symphonies");
    REQUIRE(all.definitions.size() == apogee::harness::bundled_symphonies().size());
    CHECK(all.problems.empty());
    for (const Definition& definition : all.definitions) {
        CHECK(definition.source == Source::Shipped);
    }
}
