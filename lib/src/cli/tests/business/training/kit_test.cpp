#include "training/kit.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "support/env_guard.h"

/// Training kits: the four bundled ones parse with their numbers, defaults
/// fill in what a kit omits, validation refuses what cannot gate, listing
/// sorts and reports a broken file by name, and the inline eval suite
/// materialises one object per line.
namespace {

using apogee::training::eval_suite_text;
using apogee::training::find_kit;
using apogee::training::kDefaultPerSeed;
using apogee::training::kDefaultSynthCount;
using apogee::training::Kit;
using apogee::training::list_kits;
using apogee::training::load_kit;
using apogee::training::parse_kit;
using apogee::training::validate_kit;

const std::filesystem::path kKits = std::filesystem::path{APOGEE_ASSETS_DIR} / "training" / "kits";

constexpr std::string_view kMinimal = R"(name: tiny
description: A tiny kit.
synth:
  system: |
    Teach a thing.
  seeds: ["a", "b"]
eval:
  - prompt: "Say hi"
    expected: "hi"
)";

}  // namespace

TEST_CASE("the four bundled kits load with their numbers and every eval item gates",
          "[training][kits][assets]") {
    const std::vector<apogee::training::KitSummary> kits = list_kits(kKits);
    REQUIRE(kits.size() == 4);
    CHECK(kits[0].name == "instruction-following");
    CHECK(kits[1].name == "reasoning");
    CHECK(kits[2].name == "structured-output");
    CHECK(kits[3].name == "summarization");
    for (const apogee::training::KitSummary& summary : kits) {
        INFO(summary.name);
        CHECK(summary.error.empty());
        CHECK(summary.eval_items == 8);
        const Kit kit = load_kit(summary.path);
        CHECK(kit.name == summary.name);
        CHECK(kit.skill == summary.name);
        CHECK(kit.synth.seeds.size() == 8);
        CHECK(kit.synth.count == 200);
        CHECK(kit.train.num_layers == 16);
        for (const apogee::training::EvalItem& item : kit.eval) {
            // Every bundled item carries an expected substring, so the gate
            // works with no judge configured.
            CHECK_FALSE(item.expected.empty());
        }
    }
    const Kit reasoning = load_kit(kKits / "reasoning.yaml");
    CHECK(reasoning.synth.per_seed == 8);
    CHECK(reasoning.synth.temperature == 0.7);
    CHECK(reasoning.train.iters == 500);
    const Kit summarization = load_kit(kKits / "summarization.yaml");
    CHECK(summarization.synth.per_seed == 6);
    CHECK(summarization.train.iters == 400);
}

TEST_CASE("defaults fill in what a kit omits", "[training][kits]") {
    const Kit kit = parse_kit(kMinimal, "<test>");
    CHECK(kit.name == "tiny");
    CHECK(kit.synth.count == kDefaultSynthCount);
    CHECK(kit.synth.per_seed == kDefaultPerSeed);
    CHECK(kit.synth.temperature == 0.9);
    CHECK(kit.train.iters == 0);
    REQUIRE(kit.synth.seeds.size() == 2);
    REQUIRE(kit.eval.size() == 1);
    CHECK(kit.eval[0].expected == "hi");
    CHECK(validate_kit(kit).empty());
}

TEST_CASE("validation refuses what cannot gate or synthesise", "[training][kits]") {
    Kit kit = parse_kit(kMinimal, "<test>");
    kit.synth.system = "   ";
    CHECK(validate_kit(kit).find("synth.system") != std::string::npos);

    kit = parse_kit(kMinimal, "<test>");
    kit.eval.clear();
    CHECK(validate_kit(kit).find("eval item") != std::string::npos);

    kit = parse_kit(kMinimal, "<test>");
    kit.eval[0].prompt = "";
    CHECK(validate_kit(kit).find("empty prompt") != std::string::npos);

    kit = parse_kit(kMinimal, "<test>");
    kit.synth.count = 0;
    CHECK(validate_kit(kit).find("synth.count") != std::string::npos);

    kit = parse_kit(kMinimal, "<test>");
    kit.name.clear();
    CHECK(validate_kit(kit).find("no name") != std::string::npos);
}

TEST_CASE("a malformed kit names what is wrong", "[training][kits]") {
    CHECK_THROWS_WITH(parse_kit("synth: [not a map]\n", "<x>"),
                      Catch::Matchers::ContainsSubstring("synth: expected a mapping"));
    CHECK_THROWS_WITH(parse_kit("eval: notalist\n", "<x>"),
                      Catch::Matchers::ContainsSubstring("eval: expected a list"));
    CHECK_THROWS_WITH(parse_kit("synth:\n  count: many\n", "<x>"),
                      Catch::Matchers::ContainsSubstring("synth.count: expected an integer"));
    CHECK_THROWS_WITH(parse_kit("- just\n- a list\n", "<x>"),
                      Catch::Matchers::ContainsSubstring("expected a mapping"));
}

TEST_CASE("listing sorts, names a broken file, and finds by name or path", "[training][kits]") {
    const apogee::testing::TempDir dir{"kits-" + std::to_string(std::random_device{}())};
    std::ofstream{dir.path() / "zeta.yaml"} << kMinimal;
    std::ofstream{dir.path() / "alpha.yaml"} << "name: alpha\nsynth:\n  system: x\neval: []\n";
    std::ofstream{dir.path() / "broken.yaml"} << "synth: [oops\n";
    std::ofstream{dir.path() / "notes.txt"} << "ignored";

    const std::vector<apogee::training::KitSummary> kits = list_kits(dir.path());
    REQUIRE(kits.size() == 3);
    CHECK(kits[0].name == "alpha");
    CHECK(kits[0].error.find("eval item") != std::string::npos);
    CHECK(kits[1].name == "broken");
    CHECK_FALSE(kits[1].error.empty());
    CHECK(kits[2].name == "zeta");
    CHECK(kits[2].error.empty());
    CHECK(kits[2].eval_items == 1);

    CHECK(find_kit(dir.path(), "zeta") == dir.path() / "zeta.yaml");
    CHECK(find_kit(dir.path(), (dir.path() / "alpha.yaml").string()) == dir.path() / "alpha.yaml");
    CHECK_FALSE(find_kit(dir.path(), "missing").has_value());
    CHECK(list_kits(dir.path() / "nowhere").empty());

    // A kit with no name takes the file's stem.
    std::ofstream{dir.path() / "stemmed.yaml"} << "synth:\n  system: x\neval:\n  - prompt: p\n";
    CHECK(load_kit(dir.path() / "stemmed.yaml").name == "stemmed");
}

TEST_CASE("the inline eval suite is one object per line, prompt before expected",
          "[training][kits]") {
    const Kit kit = parse_kit(kMinimal, "<test>");
    CHECK(eval_suite_text(kit) == "{\"prompt\":\"Say hi\",\"expected\":\"hi\"}\n");
}
