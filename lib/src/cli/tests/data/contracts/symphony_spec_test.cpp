#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/assets.h"
#include "contracts/config.h"
#include "contracts/config_edit.h"

/// Symphonies at the Data floor (27q): the one parser a `symphonies:` entry
/// and a spec file share -- roles, never backends, refused by name -- the
/// editor that writes an entry back as exactly what it read, and the shipped
/// starters compiled in byte for byte. Since 27r a stage that plays another
/// symphony: its keys and refusals, its own name refused at parse, the
/// entries walked against each other on load, `symphony_caps:`.
namespace {

using apogee::harness::append_symphony;
using apogee::harness::ConfigEditError;
using apogee::harness::ConfigError;
using apogee::harness::delete_symphony;
using apogee::harness::parse_config;
using apogee::harness::parse_symphony_spec;
using apogee::harness::render_symphony_spec;
using apogee::harness::SymphonySpec;
using apogee::harness::SymphonyStage;

std::string read(const std::filesystem::path& path) {
    const std::ifstream in{path, std::ios::binary};
    REQUIRE(in.good());
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::string refusal(std::string_view text, const std::vector<std::string>& backends = {}) {
    try {
        (void)parse_symphony_spec(text, "spec.yaml", "fallback", backends);
    } catch (const ConfigError& e) {
        return e.what();
    }
    return {};
}

SymphonySpec two_stages() {
    SymphonySpec spec;
    spec.name = "duo";
    spec.description = "Summarize, then check.";
    spec.input.description = "The passage.";
    SymphonyStage first;
    first.name = "summarize";
    first.role = "utility";
    first.prompt = "Summarize this:\n\n{{input}}\n";
    first.answer_tokens = 400;
    SymphonyStage second;
    second.name = "verify";
    second.role = "chat";
    second.prompt = "Passage:\n{{input}}\n\nSummary:\n{{summarize}}";
    spec.stages = {first, second};
    return spec;
}

}  // namespace

TEST_CASE("a spec file parses into the definition, every field", "[contracts][symphony]") {
    const SymphonySpec spec = parse_symphony_spec(R"YAML(name: look
description: Describe, then answer.
input:
  description: A question.
  image: true
stages:
  - name: describe
    role: vision
    image: true
    brief_tokens: 900
    answer_tokens: 300
    prompt: |
      Describe it for: {{input}}
  - name: answer
    role: chat
    prompt: "From {{describe}}: {{input}}"
    schema: '{"type": "object"}'
)YAML",
                                                  "look.yaml");
    CHECK(spec.name == "look");
    CHECK(spec.description == "Describe, then answer.");
    CHECK(spec.input.description == "A question.");
    CHECK(spec.input.image);
    REQUIRE(spec.stages.size() == 2);
    CHECK(spec.stages[0].name == "describe");
    CHECK(spec.stages[0].role == "vision");
    CHECK(spec.stages[0].image);
    CHECK(spec.stages[0].brief_tokens == 900);
    CHECK(spec.stages[0].answer_tokens == 300);
    CHECK(spec.stages[0].prompt == "Describe it for: {{input}}\n");
    CHECK(spec.stages[1].schema == R"({"type": "object"})");
    CHECK_FALSE(spec.stages[1].brief_tokens.has_value());
    // The name falls back when the text carries none.
    CHECK(parse_symphony_spec("stages:\n  - {name: a, role: utility, prompt: '{{input}}'}\n", "x",
                              "from-stem")
              .name == "from-stem");
}

TEST_CASE("a prompt is the author's text: no environment expansion", "[contracts][symphony]") {
    const SymphonySpec spec = parse_symphony_spec(
        "stages:\n  - name: a\n    role: utility\n    prompt: 'cost ${HOME} $$ {{input}}'\n", "x",
        "x");
    CHECK(spec.stages.front().prompt == "cost ${HOME} $$ {{input}}");
}

TEST_CASE("a stage names a role, never a backend: refused at parse with the reason",
          "[contracts][symphony]") {
    const std::string stage = "stages:\n  - name: a\n    prompt: '{{input}}'\n";
    CHECK_THAT(refusal(stage + "    role: l3b\n", {"l3b", "root"}),
               Catch::Matchers::ContainsSubstring(
                   "stages[0].role: 'l3b' is a backend -- a stage names the role it plays"));
    CHECK_THAT(refusal(stage + "    role: L3B\n", {"l3b"}),
               Catch::Matchers::ContainsSubstring("'L3B' is a backend"));
    CHECK_THAT(refusal(stage + "    role: utility\n    backend: l3b\n"),
               Catch::Matchers::ContainsSubstring(
                   "stages[0].backend: a stage names the role it plays, never a backend"));
    CHECK_THAT(refusal(stage + "    role: utility\n    model: x\n"),
               Catch::Matchers::ContainsSubstring("stages[0].model: a stage names the role"));
    CHECK_THAT(refusal(stage + "    role: helper\n"),
               Catch::Matchers::ContainsSubstring(
                   "stages[0].role: 'helper' is not a role (accepted: chat, extraction, vision, "
                   "transcription, utility)"));
    CHECK_THAT(refusal(stage + "    role: embedding\n"),
               Catch::Matchers::ContainsSubstring("'embedding' answers no prompt"));
    CHECK_THAT(refusal(stage), Catch::Matchers::ContainsSubstring("a stage needs a role"));
    for (const std::string_view role : apogee::harness::symphony_role_names()) {
        CHECK(refusal(stage + "    role: " + std::string{role} + "\n").empty());
    }
}

TEST_CASE("what else the parser refuses, by name", "[contracts][symphony]") {
    const auto stage = [](std::string_view rest) {
        return "stages:\n  - name: a\n    role: utility\n    prompt: '{{input}}'\n" +
               std::string{rest};
    };
    CHECK_THAT(refusal("description: x\n"),
               Catch::Matchers::ContainsSubstring("needs at least one stage"));
    CHECK_THAT(refusal("stages: []\n"),
               Catch::Matchers::ContainsSubstring("needs at least one stage"));
    CHECK_THAT(refusal("stages:\n  - {role: utility, prompt: x}\n"),
               Catch::Matchers::ContainsSubstring("a stage needs a name"));
    CHECK_THAT(refusal("stages:\n  - {name: 'a b', role: utility, prompt: x}\n"),
               Catch::Matchers::ContainsSubstring("is not a stage name"));
    CHECK_THAT(refusal("stages:\n  - {name: input, role: utility, prompt: x}\n"),
               Catch::Matchers::ContainsSubstring("'input' is the symphony's input"));
    CHECK_THAT(refusal("stages:\n  - {name: a, role: utility}\n"),
               Catch::Matchers::ContainsSubstring("a stage needs a prompt"));
    CHECK_THAT(refusal("stages:\n  - {name: a, role: utility, prompt: '  '}\n"),
               Catch::Matchers::ContainsSubstring("a stage needs a prompt"));
    CHECK_THAT(refusal(stage("  - {name: A, role: chat, prompt: x}\n")),
               Catch::Matchers::ContainsSubstring("names an earlier stage too"));
    CHECK_THAT(refusal(stage("    promt: typo\n")),
               Catch::Matchers::ContainsSubstring("unknown key 'promt'"));
    CHECK_THAT(refusal("colour: red\n" + stage("")),
               Catch::Matchers::ContainsSubstring("the symphony: unknown key 'colour'"));
    CHECK_THAT(refusal("input: {kind: text}\n" + stage("")),
               Catch::Matchers::ContainsSubstring("input: unknown key 'kind'"));
    CHECK_THAT(refusal(stage("    schema: 'not json'\n")),
               Catch::Matchers::ContainsSubstring("stages[0].schema: not a JSON object"));
    CHECK_THAT(refusal(stage("    schema: '[1]'\n")),
               Catch::Matchers::ContainsSubstring("not a JSON object"));
    CHECK_THAT(refusal(stage("    answer_tokens: 0\n")),
               Catch::Matchers::ContainsSubstring("answer_tokens: must be at least 1"));
    CHECK_THAT(refusal(stage("    brief_tokens: -3\n")),
               Catch::Matchers::ContainsSubstring("brief_tokens: must be at least 1"));
    CHECK_THAT(refusal(stage("    image: true\n")),
               Catch::Matchers::ContainsSubstring("the input takes none"));
    CHECK_THAT(refusal("name: 'no way'\n" + stage("")),
               Catch::Matchers::ContainsSubstring("is not a symphony name"));
    CHECK_THAT(refusal("name: 'true'\n" + stage("")),
               Catch::Matchers::ContainsSubstring("is not a symphony name"));
    CHECK_THAT(refusal("stages: [\n"), Catch::Matchers::ContainsSubstring("not valid YAML"));
}

TEST_CASE("a stage may play a symphony instead of a role: its keys, and what it refuses",
          "[contracts][symphony]") {
    const SymphonySpec spec = parse_symphony_spec(R"YAML(name: chain
input:
  description: A passage.
  image: true
stages:
  - name: summary
    play: summarize-verify
  - name: look
    play: describe-answer
    image: true
    input: "About {{summary}}: {{input}}"
)YAML",
                                                  "chain.yaml");
    REQUIRE(spec.stages.size() == 2);
    CHECK(spec.stages[0].plays());
    CHECK(spec.stages[0].play == "summarize-verify");
    CHECK(spec.stages[0].role.empty());
    CHECK(spec.stages[0].prompt.empty());
    CHECK(spec.stages[0].input.empty());
    CHECK(spec.stages[1].play == "describe-answer");
    CHECK(spec.stages[1].image);
    // The input is a template, read exactly as written.
    CHECK(spec.stages[1].input == "About {{summary}}: {{input}}");

    const auto play_stage = [](std::string_view rest) {
        return "stages:\n  - name: a\n    play: other\n" + std::string{rest};
    };
    CHECK(refusal(play_stage("")).empty());
    CHECK_THAT(refusal(play_stage("    role: utility\n")),
               Catch::Matchers::ContainsSubstring(
                   "stages[0].role: a stage plays a role or a symphony, never both"));
    CHECK_THAT(refusal(play_stage("    prompt: x\n")),
               Catch::Matchers::ContainsSubstring("stages[0].prompt: a play stage has no prompt"));
    CHECK_THAT(refusal(play_stage("    schema: '{}'\n")),
               Catch::Matchers::ContainsSubstring("stages[0].schema: a play stage's answer is the "
                                                  "played symphony's output"));
    CHECK_THAT(refusal(play_stage("    answer_tokens: 9\n")),
               Catch::Matchers::ContainsSubstring(
                   "stages[0].answer_tokens: a play stage makes no call of its own"));
    CHECK_THAT(refusal(play_stage("    brief_tokens: 9\n")),
               Catch::Matchers::ContainsSubstring("a play stage makes no call of its own"));
    CHECK_THAT(refusal(play_stage("    input: '  '\n")),
               Catch::Matchers::ContainsSubstring("stages[0].input: empty"));
    CHECK_THAT(refusal(play_stage("    image: true\n")),
               Catch::Matchers::ContainsSubstring("the input takes none"));
    CHECK_THAT(refusal(play_stage("    backend: l3b\n")),
               Catch::Matchers::ContainsSubstring("never a backend"));
    CHECK_THAT(refusal("stages:\n  - {name: a, play: 'two words'}\n"),
               Catch::Matchers::ContainsSubstring("'two words' is not a symphony name"));
    CHECK_THAT(refusal("stages:\n  - {name: a, play: 'yes'}\n"),
               Catch::Matchers::ContainsSubstring("is not a symphony name"));
    // A role stage has a prompt, never an input.
    CHECK_THAT(refusal("stages:\n  - {name: a, role: utility, prompt: x, input: y}\n"),
               Catch::Matchers::ContainsSubstring(
                   "stages[0].input: a role stage's brief is its 'prompt'"));
    // Neither kind: the stage is told both ways out.
    CHECK_THAT(refusal("stages:\n  - {name: a, prompt: x}\n"),
               Catch::Matchers::ContainsSubstring("or a symphony to play (play: <name>)"));
}

TEST_CASE("a definition that plays its own name is refused at parse, the loop named",
          "[contracts][symphony]") {
    CHECK(refusal("name: again\nstages:\n  - {name: a, role: utility, prompt: '{{input}}'}\n"
                  "  - {name: b, play: AGAIN}\n") ==
          "spec.yaml: stage 2 (b): plays 'AGAIN', which is already playing -- a "
          "loop, again → AGAIN; a symphony may not reach itself, directly or through another");
    // The name falls back to the file's: a file that plays its own stem loops.
    CHECK_THAT(refusal("stages:\n  - {name: a, play: fallback}\n"),
               Catch::Matchers::ContainsSubstring("a loop, fallback → fallback"));
}

TEST_CASE(
    "a config's entries are walked against each other on load: a loop or a nesting past "
    "the cap fails it, named",
    "[contracts][symphony]") {
    const auto load_refusal = [](std::string_view text) {
        try {
            (void)parse_config(text, "<test>");
        } catch (const ConfigError& e) {
            return std::string{e.what()};
        }
        return std::string{};
    };
    // Mutual.
    CHECK(load_refusal(R"YAML(symphonies:
  a:
    stages:
      - {name: one, play: b}
  b:
    stages:
      - {name: one, role: utility, prompt: "{{input}}"}
      - {name: two, play: a}
)YAML") == "<test>: symphonies.a: a → b, stage 2 (two): plays 'a', which is already playing -- a "
           "loop, a → b → a; a symphony may not reach itself, directly or through another");
    // Its own name, as an entry.
    CHECK_THAT(load_refusal("symphonies:\n  me:\n    stages:\n      - {name: one, play: me}\n"),
               Catch::Matchers::ContainsSubstring("symphonies.me: stage 1 (one): plays 'me', "
                                                  "which is already playing -- a loop, me → me"));
    // Transitive, through an entry standing in for a starter.
    CHECK_THAT(
        load_refusal(R"YAML(symphonies:
  summarize-verify:
    stages:
      - {name: one, play: middle}
  middle:
    stages:
      - {name: one, play: front}
  front:
    stages:
      - {name: one, play: summarize-verify}
)YAML"),
        Catch::Matchers::ContainsSubstring("a loop, front → summarize-verify → middle → front"));

    // Five deep fails the load; four deep loads -- and a played name no entry
    // defines (a starter, a spec file) is late-bound, a leaf one level down.
    const auto nested = [](int levels, std::string_view caps = {}) {
        std::string text{caps};
        text += "symphonies:\n";
        for (int level = 1; level < levels; ++level) {
            text += "  l" + std::to_string(level) + ":\n    stages:\n      - {name: one, play: l" +
                    std::to_string(level + 1) + "}\n";
        }
        text += "  l" + std::to_string(levels) +
                ":\n    stages:\n      - {name: one, role: utility, prompt: \"{{input}}\"}\n";
        return text;
    };
    CHECK(load_refusal(nested(4)).empty());
    CHECK_THAT(load_refusal(nested(5)),
               Catch::Matchers::ContainsSubstring(
                   "symphonies.l1: l1 → l2 → l3 → l4, stage 1 (one): plays 'l5', which nests 5 "
                   "deep -- l1 → l2 → l3 → l4 → l5, and the cap is 4 (symphony_caps.depth)"));
    CHECK(load_refusal(nested(5, "symphony_caps:\n  depth: 5\n")).empty());
    CHECK_THAT(load_refusal(nested(3, "symphony_caps:\n  depth: 2\n")),
               Catch::Matchers::ContainsSubstring("and the cap is 2"));
    CHECK(load_refusal("symphonies:\n  chain:\n    stages:\n      - {name: one, play: "
                       "summarize-verify}\n      - {name: two, play: some-file}\n")
              .empty());
}

TEST_CASE("symphony_caps: the depth and the whole walk's budget, each a positive number",
          "[contracts][symphony]") {
    const apogee::harness::Config none = parse_config("models:\n  default: x\n", "<test>");
    CHECK(none.symphony_caps == apogee::harness::SymphonyCaps{});
    CHECK(none.symphony_caps.max_depth() == apogee::harness::kSymphonyDepth);
    CHECK(apogee::harness::kSymphonyDepth == 4);

    const apogee::harness::Config set = parse_config(
        "symphony_caps:\n  depth: 6\n  stage_calls: 12\n  answer_tokens: 4000\n", "<test>");
    CHECK(set.symphony_caps.max_depth() == 6);
    CHECK(set.symphony_caps.stage_calls == 12);
    CHECK(set.symphony_caps.answer_tokens == 4000);

    const auto load_refusal = [](std::string_view text) {
        try {
            (void)parse_config(text, "<test>");
        } catch (const ConfigError& e) {
            return std::string{e.what()};
        }
        return std::string{};
    };
    CHECK_THAT(load_refusal("symphony_caps:\n  depth: 0\n"),
               Catch::Matchers::ContainsSubstring("symphony_caps.depth: must be at least 1"));
    CHECK_THAT(load_refusal("symphony_caps:\n  stage_calls: -1\n"),
               Catch::Matchers::ContainsSubstring("symphony_caps.stage_calls: must be at least 1"));
    CHECK_THAT(load_refusal("symphony_caps:\n  calls: 3\n"),
               Catch::Matchers::ContainsSubstring("symphony_caps: unknown key 'calls'"));
    CHECK_THAT(load_refusal("symphony_caps: 4\n"),
               Catch::Matchers::ContainsSubstring("symphony_caps: expected a mapping"));
}

TEST_CASE("a symphonies: entry is read by the same parser, its key its name",
          "[contracts][symphony]") {
    const apogee::harness::Config config = parse_config(R"YAML(backends:
  l3b:
    type: mock
symphonies:
  duo:
    description: Two.
    stages:
      - name: one
        role: utility
        prompt: "{{input}}"
)YAML",
                                                        "<test>");
    const SymphonySpec* duo = config.find_symphony("DUO");
    REQUIRE(duo != nullptr);
    CHECK(duo->name == "duo");
    CHECK(duo->stages.front().role == "utility");

    const auto load_refusal = [](std::string_view text) {
        try {
            (void)parse_config(text, "<test>");
        } catch (const ConfigError& e) {
            return std::string{e.what()};
        }
        return std::string{};
    };
    // The config's backends are named to the parser.
    CHECK_THAT(
        load_refusal(R"YAML(backends:
  l3b:
    type: mock
symphonies:
  duo:
    stages:
      - {name: one, role: l3b, prompt: x}
)YAML"),
        Catch::Matchers::ContainsSubstring("symphonies.duo.stages[0].role: 'l3b' is a backend"));
    CHECK_THAT(load_refusal("symphonies:\n  duo:\n    name: other\n    stages:\n      - {name: "
                            "one, role: utility, prompt: x}\n"),
               Catch::Matchers::ContainsSubstring("an entry's name is its key"));
    CHECK_THAT(load_refusal("symphonies:\n  a:\n    stages:\n      - {name: x, role: utility, "
                            "prompt: x}\n  A:\n    stages:\n      - {name: x, role: utility, "
                            "prompt: x}\n"),
               Catch::Matchers::ContainsSubstring("compared case-insensitively"));
    CHECK_THAT(load_refusal("symphonies: [a]\n"),
               Catch::Matchers::ContainsSubstring("expected a mapping"));
}

TEST_CASE("an entry is written as exactly what it reads back, whatever its prompts hold",
          "[contracts][symphony][config_edit]") {
    // The table: text a block cannot carry is quoted, and every one reads back.
    const std::vector<std::string> prompts{
        "{{input}}",
        "plain words, ending.",
        "Summarize:\n\n{{input}}\n",
        "no final newline\n{{input}}",
        "  leading spaces\n{{input}}\n",
        "\nleading blank line {{input}}",
        "# A heading\n{{input}}\n# a last line that looks like a comment",
        "two final newlines {{input}}\n\n",
        "a line of blanks\n   \n{{input}}",
        "tab\tinside {{input}}\n",
        "carriage\r\nreturn {{input}}",
        "quotes \" and 'single' and \\ backslash {{input}}",
        "key: value -- {{input}}",
        "- a dash first {{input}}",
        "true",
        "~",
        "${HOME} {{input}}",
        "unicode — ✓ {{input}}\n",
    };
    for (const std::string& prompt : prompts) {
        INFO(prompt);
        SymphonySpec spec = two_stages();
        spec.stages[0].prompt = prompt;
        spec.description = prompt;
        // A stage that plays a symphony (27r), its input the same text.
        SymphonyStage played;
        played.name = "again";
        played.play = "other";
        played.input = prompt;
        spec.stages.push_back(played);
        const std::string written = append_symphony("models:\n  default: x\n", "duo", spec, false);
        const apogee::harness::Config config = parse_config(written, "<test>");
        REQUIRE(config.find_symphony("duo") != nullptr);
        CHECK(*config.find_symphony("duo") == spec);
        // And as a spec file.
        CHECK(parse_symphony_spec(render_symphony_spec(spec), "x") == spec);
    }
}

TEST_CASE("the entry's bytes, and delete is its exact inverse",
          "[contracts][symphony][config_edit]") {
    const std::string base = R"YAML(# My config -- every comment stays.
models:
  default: x   # the default

symphonies:
  # a comment above the first entry
  first:
    stages:
      - name: one
        role: utility
        prompt: "{{input}}"
)YAML";
    const SymphonySpec spec = two_stages();
    const std::string added = append_symphony(base, "duo", spec, false);
    CHECK(added == base + R"YAML(
  duo:
    description: Summarize, then check.
    input:
      description: The passage.
    stages:
      - name: summarize
        role: utility
        answer_tokens: 400
        prompt: |
          Summarize this:

          {{input}}
      - name: verify
        role: chat
        prompt: |-
          Passage:
          {{input}}

          Summary:
          {{summarize}}
)YAML");
    CHECK(delete_symphony(added, "duo") == base);
    CHECK_THROWS_WITH(append_symphony(added, "DUO", spec, false),
                      Catch::Matchers::ContainsSubstring("collides"));
    CHECK_THROWS_AS(delete_symphony(base, "nope"), ConfigEditError);
    CHECK_THROWS_AS(append_symphony(base, "bad name", spec, false), ConfigEditError);
}

TEST_CASE("a play stage's entry: its play and its input, nothing a role stage writes",
          "[contracts][symphony][config_edit]") {
    SymphonySpec spec;
    spec.name = "chain";
    SymphonyStage first;
    first.name = "summary";
    first.play = "summarize-verify";
    SymphonyStage second;
    second.name = "facts";
    second.play = "extract-facts";
    second.input = "Summary:\n{{summary}}\n";
    spec.stages = {first, second};
    const std::string added = append_symphony("models:\n  default: x\n", "chain", spec, false);
    CHECK(added == R"YAML(models:
  default: x

symphonies:
  chain:
    stages:
      - name: summary
        play: summarize-verify
      - name: facts
        play: extract-facts
        input: |
          Summary:
          {{summary}}
)YAML");
    CHECK(*parse_config(added, "<test>").find_symphony("chain") == spec);
    CHECK(delete_symphony(added, "chain").find("chain") == std::string::npos);
    // A stage holding both kinds is written as it is, so the parser -- not
    // the renderer -- refuses it.
    SymphonySpec both = spec;
    both.stages[0].role = "utility";
    CHECK_THROWS_WITH(parse_symphony_spec(render_symphony_spec(both), "x"),
                      Catch::Matchers::ContainsSubstring("never both"));
}

TEST_CASE("a replaced entry keeps its place, and a prompt's last line goes with it",
          "[contracts][symphony][config_edit]") {
    SymphonySpec spec = two_stages();
    spec.stages[1].prompt = "{{summarize}}\n# looks like a comment, is the prompt's last line";
    const std::string first = append_symphony("symphonies:\n", "duo", spec, false);
    const std::string next = append_symphony(first, "after", two_stages(), false);
    SymphonySpec changed = two_stages();
    changed.description = "Changed.";
    const std::string replaced = append_symphony(next, "duo", changed, true);
    const apogee::harness::Config config = parse_config(replaced, "<test>");
    CHECK(config.find_symphony("duo")->description == "Changed.");
    SymphonySpec after = two_stages();
    after.name = "after";
    CHECK(*config.find_symphony("after") == after);
    CHECK(replaced.find("looks like a comment") == std::string::npos);
    CHECK(replaced.find("  duo:") < replaced.find("  after:"));
    // Deleted whole: nothing of its prompt is left behind to misread.
    const std::string gone = delete_symphony(next, "duo");
    CHECK(gone.find("looks like a comment") == std::string::npos);
    CHECK(parse_config(gone, "<test>").find_symphony("after") != nullptr);
}

TEST_CASE("a comment inside a hand-written entry goes with the entry, not before it",
          "[contracts][symphony][config_edit]") {
    // A comment at the entry's own indent with the entry's fields after it is
    // the entry's: a delete or a replace that stopped at it would leave the
    // rest of the entry behind, read as the next one's.
    const std::string text = R"YAML(symphonies:
  duo:
    description: First.
  # the user's note, between the entry's lines
    stages:
      - {name: a, role: utility, prompt: "{{input}}"}

  after:
    stages:
      - {name: b, role: chat, prompt: "{{input}}"}
)YAML";
    const std::string gone = delete_symphony(text, "duo");
    CHECK(gone == R"YAML(symphonies:

  after:
    stages:
      - {name: b, role: chat, prompt: "{{input}}"}
)YAML");
    const apogee::harness::Config config = parse_config(gone, "<test>");
    CHECK(config.find_symphony("duo") == nullptr);
    CHECK(config.find_symphony("after")->stages.front().name == "b");

    SymphonySpec changed = two_stages();
    const std::string replaced = append_symphony(text, "duo", changed, true);
    const apogee::harness::Config again = parse_config(replaced, "<test>");
    changed.name = "duo";
    CHECK(*again.find_symphony("duo") == changed);
    CHECK(again.find_symphony("after")->stages.front().name == "b");
    CHECK(replaced.find("the user's note") == std::string::npos);
}

TEST_CASE("the shipped starters are compiled in byte for byte, and each parses",
          "[contracts][symphony][assets]") {
    const std::filesystem::path dir = std::filesystem::path{APOGEE_ASSETS_DIR} / "symphonies";
    std::set<std::string> files;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        files.insert(entry.path().filename().string());
    }
    std::set<std::string> compiled;
    REQUIRE(apogee::harness::bundled_symphonies().size() == 3);
    for (const apogee::harness::BundledSymphony& starter : apogee::harness::bundled_symphonies()) {
        INFO(starter.name);
        compiled.insert(std::string{starter.name} + ".yaml");
        CHECK(read(dir / (std::string{starter.name} + ".yaml")) == starter.text);
        const SymphonySpec spec =
            parse_symphony_spec(starter.text, std::string{starter.name}, starter.name);
        CHECK(spec.name == starter.name);
        CHECK_FALSE(spec.description.empty());
        CHECK(apogee::harness::find_bundled_symphony(starter.name) == &starter);
        CHECK(apogee::harness::bundled_symphony_relative_path(starter.name) ==
              "symphonies/" + std::string{starter.name} + ".yaml");
    }
    // Every file under assets/symphonies/ is compiled in, and nothing else.
    CHECK(files == compiled);
    // The three each show a distinct stage feature: threading, a schema, an
    // image.
    const auto spec_of = [](std::string_view name) {
        return parse_symphony_spec(apogee::harness::find_bundled_symphony(name)->text, "x", name);
    };
    CHECK(spec_of("summarize-verify").stages.size() == 2);
    CHECK_FALSE(spec_of("extract-facts").stages.front().schema.empty());
    CHECK(spec_of("describe-answer").input.image);
    CHECK(spec_of("describe-answer").stages.front().role == "vision");
    CHECK(apogee::harness::find_bundled_symphony("nope") == nullptr);
}
