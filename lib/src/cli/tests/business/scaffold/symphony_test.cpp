#include "scaffold/symphony.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "contracts/config.h"
#include "contracts/config_edit.h"
#include "support/env_guard.h"

/// The symphony scaffold core (27q): the one writer the CLI's create and
/// edit and the admin plane's POST and PUT share -- the entry, the refusals
/// that leave the config as it was, and a replacement in place; since 27r a
/// chain, walked as it will stand among every source before it is written.
namespace {

using apogee::scaffold::create_symphony;

struct Tree {
    apogee::testing::TempDir home{"scaffold-symphony-" + std::to_string(std::random_device{}())};
    std::filesystem::path config = home.path() / "config" / "config.yaml";

    Tree() {
        std::filesystem::create_directories(config.parent_path());
        // Binary: a text-mode stream writes CRLF on Windows, the editor keeps
        // a CRLF file CRLF, and the cases below look for LF bytes.
        std::ofstream{config, std::ios::binary}
            << "# my config\nmodels:\n  default: l3b  # kept\n\nbackends:\n"
               "  l3b:\n    type: mock\n";
    }

    [[nodiscard]] std::string bytes() const {
        const std::ifstream in{config, std::ios::binary};
        std::ostringstream out;
        out << in.rdbuf();
        return out.str();
    }
};

apogee::harness::SymphonySpec duo() {
    return apogee::harness::parse_symphony_spec(R"YAML(name: duo
description: Two.
stages:
  - {name: one, role: utility, prompt: "Do {{input}}"}
  - {name: two, role: chat, prompt: "Check {{one}}"}
)YAML",
                                                "<test>");
}

std::string refusal(const Tree& tree, const apogee::harness::SymphonySpec& spec) {
    try {
        (void)create_symphony(tree.config, spec, false);
    } catch (const std::runtime_error& e) {
        return e.what();
    }
    return {};
}

}  // namespace

TEST_CASE("create appends exactly the entry the editor renders", "[scaffold][symphony]") {
    const Tree tree;
    const std::string before = tree.bytes();
    const apogee::scaffold::SymphonyResult result = create_symphony(tree.config, duo(), false);
    CHECK(result.name == "duo");
    CHECK_FALSE(result.replaced);
    CHECK(tree.bytes() == apogee::harness::append_symphony(before, "duo", duo(), false));
    const apogee::harness::Config config = apogee::harness::load_config(tree.config);
    REQUIRE(config.find_symphony("duo") != nullptr);
    CHECK(*config.find_symphony("duo") == duo());
}

TEST_CASE("every refusal names its reason and writes nothing", "[scaffold][symphony]") {
    const Tree tree;
    const std::string before = tree.bytes();
    apogee::harness::SymphonySpec spec = duo();
    spec.name = "two words";
    CHECK_THAT(refusal(tree, spec), Catch::Matchers::ContainsSubstring("is not a symphony name"));
    spec = duo();
    spec.stages[0].role = "l3b";
    CHECK_THAT(refusal(tree, spec), Catch::Matchers::ContainsSubstring(
                                        "'l3b' is a backend -- a stage names the role it plays"));
    spec = duo();
    spec.stages[1].prompt = "{{three}}";
    CHECK_THAT(refusal(tree, spec),
               Catch::Matchers::ContainsSubstring("symphony 'duo': stage 2 (two): {{three}}"));
    spec = duo();
    spec.stages[0].schema = "not json";
    CHECK_THAT(refusal(tree, spec), Catch::Matchers::ContainsSubstring("not a JSON object"));
    CHECK(tree.bytes() == before);

    (void)create_symphony(tree.config, duo(), false);
    CHECK_THAT(refusal(tree, duo()), Catch::Matchers::ContainsSubstring("already exists"));
}

TEST_CASE("force replaces an entry in place, the comment above it kept", "[scaffold][symphony]") {
    const Tree tree;
    (void)create_symphony(tree.config, duo(), false);
    // A comment the user wrote above the entry, and an entry after it.
    std::string text = tree.bytes();
    text.insert(text.find("  duo:"), "  # mine, keep me\n");
    std::ofstream{tree.config, std::ios::binary | std::ios::trunc} << text;
    apogee::harness::SymphonySpec other = duo();
    other.name = "zeta";
    (void)create_symphony(tree.config, other, false);

    apogee::harness::SymphonySpec changed = duo();
    changed.description = "Changed.";
    const apogee::scaffold::SymphonyResult result = create_symphony(tree.config, changed, true);
    CHECK(result.replaced);
    const std::string after = tree.bytes();
    CHECK(after.find("  # mine, keep me\n  duo:\n    description: Changed.") != std::string::npos);
    CHECK(after.find("  duo:") < after.find("  zeta:"));
    CHECK(apogee::harness::load_config(tree.config).find_symphony("duo")->description ==
          "Changed.");
}

TEST_CASE("a bare create writes a one-stage skeleton that plays", "[scaffold][symphony]") {
    const Tree tree;
    const apogee::harness::SymphonySpec starter =
        apogee::scaffold::starter_symphony("mine", "Mine.");
    REQUIRE(starter.stages.size() == 1);
    CHECK(starter.stages[0].role == "utility");
    CHECK(starter.stages[0].prompt == "{{input}}\n");
    CHECK(create_symphony(tree.config, starter, false).name == "mine");
}

TEST_CASE(
    "a chain is created like any symphony; one that loops, nests too deep or plays nothing "
    "is refused, unwritten",
    "[scaffold][symphony]") {
    const Tree tree;
    const auto spec = [](std::string_view text) {
        return apogee::harness::parse_symphony_spec(text, "<test>");
    };
    // Two shipped starters, chained (27r).
    CHECK(create_symphony(tree.config,
                          spec("name: digest\nstages:\n  - {name: summary, play: "
                               "summarize-verify}\n  - {name: facts, play: extract-facts}\n"),
                          false)
              .name == "digest");

    std::string before = tree.bytes();
    CHECK(refusal(tree, spec("name: a\nstages:\n  - {name: one, play: b}\n")) ==
          "symphony 'a': stage 1 (one): plays 'b', and no symphony is named 'b'");
    CHECK(tree.bytes() == before);

    // A loop through another entry, made by replacing that entry.
    (void)create_symphony(
        tree.config,
        spec("name: b\nstages:\n  - {name: one, role: utility, prompt: '{{input}}'}\n"), false);
    (void)create_symphony(tree.config, spec("name: a\nstages:\n  - {name: one, play: b}\n"), false);
    before = tree.bytes();
    try {
        (void)create_symphony(tree.config, spec("name: b\nstages:\n  - {name: one, play: a}\n"),
                              true);
        FAIL("a loop through another entry was written");
    } catch (const std::runtime_error& e) {
        CHECK_THAT(e.what(), Catch::Matchers::ContainsSubstring("a loop, b → a → b"));
    }
    CHECK(tree.bytes() == before);

    // A loop through a spec file the config never sees.
    const std::filesystem::path dir = tree.home.path() / "symphonies";
    std::filesystem::create_directories(dir);
    std::ofstream{dir / "filed.yaml"} << "stages:\n  - {name: one, play: c}\n";
    CHECK_THAT(refusal(tree, spec("name: c\nstages:\n  - {name: one, play: filed}\n")),
               Catch::Matchers::ContainsSubstring("a loop, c → filed → c"));
    CHECK(tree.bytes() == before);

    // Four deep is written; a fifth level is refused naming the path.
    (void)create_symphony(
        tree.config,
        spec("name: l4\nstages:\n  - {name: one, role: utility, prompt: '{{input}}'}\n"), false);
    for (const std::string level : {"3", "2", "1"}) {
        (void)create_symphony(tree.config,
                              spec("name: l" + level + "\nstages:\n  - {name: one, play: l" +
                                   std::to_string(std::stoi(level) + 1) + "}\n"),
                              false);
    }
    before = tree.bytes();
    CHECK_THAT(refusal(tree, spec("name: l0\nstages:\n  - {name: one, play: l1}\n")),
               Catch::Matchers::ContainsSubstring("which nests 5 deep -- l0 → l1 → l2 → l3 → l4"));
    CHECK(tree.bytes() == before);
}
