#include "scaffold/symphony.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>

#include "contracts/config.h"
#include "contracts/config_edit.h"
#include "support/env_guard.h"

/// The symphony scaffold core (27q): the one writer the CLI's create and
/// edit and the admin plane's POST and PUT share -- the entry, the refusals
/// that leave the config as it was, and a replacement in place.
namespace {

using apogee::scaffold::create_symphony;

struct Tree {
    apogee::testing::TempDir home{"scaffold-symphony-" + std::to_string(std::random_device{}())};
    std::filesystem::path config = home.path() / "config" / "config.yaml";

    Tree() {
        std::filesystem::create_directories(config.parent_path());
        std::ofstream{config} << "# my config\nmodels:\n  default: l3b  # kept\n\nbackends:\n"
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
