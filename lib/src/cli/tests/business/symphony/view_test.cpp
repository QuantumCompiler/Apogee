#include "symphony/view.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <string>

#include "contracts/config.h"

/// The one view of a symphony (27q): what `symphonies list|show
/// --output-format json` print and the admin plane serves, and the play's
/// document -- each field pinned; since 27r a play stage's, and a chain's
/// play with its played symphony's stages inside the stage that played it.
namespace {

using apogee::symphony::Definition;

Definition sample() {
    Definition definition;
    definition.spec = apogee::harness::parse_symphony_spec(R"YAML(name: duo
description: Two steps.
input:
  description: A passage.
stages:
  - name: one
    role: utility
    answer_tokens: 200
    prompt: "Do {{input}}"
  - name: two
    role: extraction
    prompt: "Read {{one}}"
    schema: '{"type": "object"}'
)YAML",
                                                           "<test>");
    definition.source = apogee::symphony::Source::File;
    definition.path = "/somewhere/duo.yaml";
    return definition;
}

}  // namespace

TEST_CASE("a definition's document carries every field, a schema as its text", "[symphony][view]") {
    const nlohmann::json document = apogee::symphony::definition_document(sample(), {});
    CHECK(document == nlohmann::json::parse(R"({
        "object": "symphony",
        "name": "duo",
        "description": "Two steps.",
        "source": "file",
        "path": "/somewhere/duo.yaml",
        "overrides": false,
        "input": {"description": "A passage.", "image": false},
        "stages": [
            {"name": "one", "role": "utility", "prompt": "Do {{input}}", "image": false,
             "answer_tokens": 200},
            {"name": "two", "role": "extraction", "prompt": "Read {{one}}", "image": false,
             "schema": "{\"type\": \"object\"}"}
        ],
        "problems": []
    })"));
    Definition entry = sample();
    entry.path.clear();
    entry.source = apogee::symphony::Source::Config;
    entry.spec.stages[1].prompt = "{{nope}}";
    const nlohmann::json broken = apogee::symphony::definition_document(entry, {});
    CHECK_FALSE(broken.contains("path"));
    CHECK(broken["source"] == "config");
    REQUIRE(broken["problems"].size() == 1);
}

TEST_CASE("the list document wraps the definitions and the files that could not be read",
          "[symphony][view]") {
    apogee::symphony::Catalog catalog;
    catalog.definitions = {sample()};
    catalog.problems = {"/x/broken.yaml: not valid YAML"};
    const nlohmann::json document = apogee::symphony::list_document(catalog);
    CHECK(document["object"] == "list");
    REQUIRE(document["data"].size() == 1);
    CHECK(document["data"][0] == apogee::symphony::definition_document(sample(), {}));
    CHECK(document["problems"] == nlohmann::json::array({"/x/broken.yaml: not valid YAML"}));
}

TEST_CASE("a play's document: the output, each stage's answer, the suite", "[symphony][view]") {
    apogee::symphony::PlayResult result;
    result.stages.push_back({.name = "one",
                             .role = "utility",
                             .backend = "l3b",
                             .answer = "first",
                             .cut = false,
                             .tokens = 12,
                             .seconds = 1.26});
    result.stages.push_back({.name = "two",
                             .role = "chat",
                             .backend = "root",
                             .answer = "second",
                             .cut = true,
                             .tokens = std::nullopt,
                             .seconds = 0.04});
    result.output = "second";
    CHECK(apogee::symphony::play_document("duo", "research", result) == nlohmann::json::parse(R"({
        "object": "symphony.play",
        "symphony": "duo",
        "suite": "research",
        "output": "second",
        "stages": [
            {"name": "one", "role": "utility", "backend": "l3b", "answer": "first", "cut": false,
             "tokens": 12, "seconds": 1.3},
            {"name": "two", "role": "chat", "backend": "root", "answer": "second", "cut": true,
             "seconds": 0.0}
        ]
    })"));
    CHECK(apogee::symphony::play_document("duo", "", result)["suite"].is_null());
}

TEST_CASE("a play stage's view: what it plays and what it is given; problems from the catalog",
          "[symphony][view]") {
    Definition chain;
    chain.spec = apogee::harness::parse_symphony_spec(R"YAML(name: chain
stages:
  - name: first
    play: duo
  - name: second
    play: duo
    input: "Again: {{first}}"
)YAML",
                                                      "<test>");
    chain.source = apogee::symphony::Source::Config;
    const apogee::symphony::Catalog catalog{.definitions = {chain, sample()}};
    const nlohmann::json document = apogee::symphony::definition_document(chain, catalog);
    CHECK(document["stages"] == nlohmann::json::parse(R"([
        {"name": "first", "play": "duo", "image": false},
        {"name": "second", "play": "duo", "input": "Again: {{first}}", "image": false}
    ])"));
    CHECK(document["problems"].empty());
    // Without the symphony it plays, the chain cannot be played -- said.
    const nlohmann::json alone = apogee::symphony::definition_document(chain, {});
    REQUIRE(alone["problems"].size() == 1);
    CHECK(alone["problems"][0] == "stage 1 (first): plays 'duo', and no symphony is named 'duo'");
}

TEST_CASE("a chain's play document nests each played symphony's stages in the stage that played it",
          "[symphony][view]") {
    apogee::symphony::StageResult inner{.name = "one",
                                        .role = "utility",
                                        .backend = "l3b",
                                        .answer = "first",
                                        .cut = false,
                                        .tokens = 12,
                                        .seconds = 1.26};
    const apogee::symphony::StageResult played{.name = "summary",
                                               .play = "duo",
                                               .answer = "first",
                                               .cut = false,
                                               .tokens = 12,
                                               .seconds = 1.26,
                                               .stages = {inner}};
    apogee::symphony::PlayResult result;
    result.stages = {played};
    result.output = "first";
    CHECK(apogee::symphony::play_document("chain", "", result)["stages"] ==
          nlohmann::json::parse(R"([
        {"name": "summary", "play": "duo", "answer": "first", "cut": false, "tokens": 12,
         "seconds": 1.3,
         "stages": [
            {"name": "one", "role": "utility", "backend": "l3b", "answer": "first",
             "cut": false, "tokens": 12, "seconds": 1.3}
         ]}
    ])"));
}
