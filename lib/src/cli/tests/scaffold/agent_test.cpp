#include "scaffold/agent.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>

#include "harness/config.h"
#include "harness/layout.h"
#include "support/env_guard.h"

/// The agent scaffold core: files, the entry, defaults, and every refusal.
namespace {

using apogee::scaffold::agent_files;
using apogee::scaffold::AgentSpec;
using apogee::scaffold::create_agent;
using apogee::scaffold::sanitize_agent_name;

struct Tree {
    apogee::testing::TempDir home{"scaffold-agent-" + std::to_string(std::random_device{}())};
    std::filesystem::path config = home.path() / "config" / "config.yaml";

    Tree() {
        std::filesystem::create_directories(config.parent_path());
        std::ofstream{config} << apogee::harness::config_template();
    }

    [[nodiscard]] std::string bytes() const {
        std::ifstream in{config, std::ios::binary};
        std::ostringstream out;
        out << in.rdbuf();
        return out.str();
    }
};

std::string read(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

}  // namespace

TEST_CASE("create writes a prompt and a schema beside the config and appends the entry",
          "[scaffold][agent]") {
    const Tree tree;
    const std::string before = tree.bytes();
    AgentSpec spec;
    spec.name = "code.reviewer";
    spec.description = "Reviews code";
    const apogee::scaffold::AgentResult result = create_agent(tree.config, spec);
    CHECK(result.name == "code-reviewer");
    CHECK(result.prompt_path == tree.home.path() / "prompts" / "code-reviewer.txt");
    CHECK(result.schema_path == tree.home.path() / "schemas" / "code-reviewer-output.json");
    REQUIRE(std::filesystem::exists(result.prompt_path));
    REQUIRE(std::filesystem::exists(result.schema_path));
    CHECK(read(result.prompt_path).find("You are code-reviewer -- Reviews code.") !=
          std::string::npos);
    CHECK(read(result.prompt_path).find("complete, valid result") != std::string::npos);
    CHECK(read(result.schema_path).find("\"human_summary\"") != std::string::npos);

    // The template plus exactly one entry, with the defaults the item fixes:
    // relative paths, read-only, save_subdir = the name.
    CHECK(tree.bytes().starts_with(before));
    const apogee::harness::Config config = apogee::harness::load_config(tree.config);
    const apogee::harness::AgentConfig* entry = config.find_agent("code-reviewer");
    REQUIRE(entry != nullptr);
    CHECK(entry->prompts == std::vector<std::string>{"prompts/code-reviewer.txt"});
    CHECK(entry->schemas == std::vector<std::string>{"schemas/code-reviewer-output.json"});
    CHECK(entry->tools == apogee::harness::AgentToolPolicy::ReadOnly);
    CHECK(entry->save_subdir == "code-reviewer");
    CHECK(entry->description == "Reviews code");
    CHECK_FALSE(entry->questions);
    // The resolved files are exactly the two written.
    CHECK(agent_files(tree.config, *entry) ==
          std::vector<std::filesystem::path>{result.prompt_path, result.schema_path});

    // A second create is refused without force, and replaces with it.
    CHECK_THROWS_AS(create_agent(tree.config, spec), std::runtime_error);
    spec.force = true;
    spec.tools = "all";
    spec.questions = true;
    CHECK_NOTHROW(create_agent(tree.config, spec));
    const apogee::harness::Config again = apogee::harness::load_config(tree.config);
    CHECK(again.find_agent("code-reviewer")->tools == apogee::harness::AgentToolPolicy::All);
    CHECK(again.find_agent("code-reviewer")->questions);
    CHECK(again.agents.size() == 1);
}

TEST_CASE("custom bodies, a prose-only agent, and every knob round-trip", "[scaffold][agent]") {
    const Tree tree;
    AgentSpec spec;
    spec.name = "summarizer";
    spec.no_schema = true;
    spec.prompt_body = "custom persona\n";
    spec.model = "local";
    spec.output_format = "markdown";
    spec.collection = "notes";
    spec.save_dir = "~/reports";
    spec.save_filename = "sum";
    spec.save_subdir = "daily";
    spec.mcp = {"weather"};
    const apogee::scaffold::AgentResult result = create_agent(tree.config, spec);
    CHECK(result.schema_path.empty());
    CHECK_FALSE(std::filesystem::exists(tree.home.path() / "schemas" / "summarizer-output.json"));
    CHECK(read(result.prompt_path) == "custom persona\n");
    const apogee::harness::Config config = apogee::harness::load_config(tree.config);
    const apogee::harness::AgentConfig* entry = config.find_agent("summarizer");
    REQUIRE(entry != nullptr);
    CHECK(entry->schemas.empty());
    CHECK(entry->model == "local");
    CHECK(entry->output_format == apogee::harness::AgentOutputFormat::Markdown);
    CHECK(entry->collection == "notes");
    CHECK(entry->save_dir.ends_with("/reports"));
    CHECK(entry->save_filename == "sum");
    CHECK(entry->save_subdir == "daily");
    CHECK(entry->mcp == std::vector<std::string>{"weather"});
}

TEST_CASE("names are sanitised or refused, and bad enums are refused before any file is written",
          "[scaffold][agent]") {
    CHECK(sanitize_agent_name("a.b:c") == "a-b-c");
    CHECK(sanitize_agent_name("Ok_name-1") == "Ok_name-1");
    CHECK_THROWS_AS(sanitize_agent_name(""), std::runtime_error);
    CHECK_THROWS_AS(sanitize_agent_name("has space"), std::runtime_error);
    CHECK_THROWS_AS(sanitize_agent_name("../up"), std::runtime_error);

    const Tree tree;
    AgentSpec spec;
    spec.name = "x";
    spec.tools = "sometimes";
    CHECK_THROWS_AS(create_agent(tree.config, spec), std::runtime_error);
    spec.tools = "all";
    spec.output_format = "yaml";
    CHECK_THROWS_AS(create_agent(tree.config, spec), std::runtime_error);
    CHECK_FALSE(std::filesystem::exists(tree.home.path() / "prompts" / "x.txt"));
}

TEST_CASE("a config failure leaves the files on disk and says so", "[scaffold][agent]") {
    const Tree tree;
    std::ofstream{tree.config, std::ios::binary} << "backends: [not, a, mapping]\n";
    AgentSpec spec;
    spec.name = "orphan";
    try {
        (void)create_agent(tree.config, spec);
        FAIL("a broken config must refuse the entry");
    } catch (const std::runtime_error& e) {
        CHECK(std::string{e.what()}.starts_with("updating config:"));
        CHECK(std::string{e.what()}.find("the prompt was written to") != std::string::npos);
    }
    CHECK(std::filesystem::exists(tree.home.path() / "prompts" / "orphan.txt"));
}

TEST_CASE("prompts/, schemas/ and analyses/ are declared once, in the layout",
          "[scaffold][agent][layout]") {
    int seen = 0;
    for (const apogee::harness::LayoutEntry& entry : apogee::harness::data_directories()) {
        if (entry.relative_path == "prompts" || entry.relative_path == "schemas" ||
            entry.relative_path == "analyses") {
            ++seen;
            CHECK(entry.user_data);  // the user's own files: uninstall asks first
        }
    }
    CHECK(seen == 3);
    CHECK(apogee::harness::prompts_dir().filename() == "prompts");
    CHECK(apogee::harness::schemas_dir().filename() == "schemas");
    CHECK(apogee::harness::analyses_dir().filename() == "analyses");
}

TEST_CASE("an existing prompt file with no entry is never clobbered without force",
          "[scaffold][agent]") {
    // The bundled prompts are seeded with no config entry, and a user may have
    // edited one: `agents create security-review` must refuse to overwrite it
    // even though the config has nothing to collide with.
    const Tree tree;
    const std::filesystem::path prompt = tree.home.path() / "prompts" / "security-review.txt";
    std::filesystem::create_directories(prompt.parent_path());
    std::ofstream{prompt, std::ios::binary} << "MY EDITED PROMPT\n";
    AgentSpec spec;
    spec.name = "security-review";
    CHECK_THROWS_AS(create_agent(tree.config, spec), std::runtime_error);
    CHECK(read(prompt) == "MY EDITED PROMPT\n");
    CHECK(apogee::harness::load_config(tree.config).agents.empty());
    // The same for a schema file alone.
    const std::filesystem::path schema = tree.home.path() / "schemas" / "other-output.json";
    std::filesystem::create_directories(schema.parent_path());
    std::ofstream{schema, std::ios::binary} << "{}\n";
    AgentSpec other;
    other.name = "other";
    CHECK_THROWS_AS(create_agent(tree.config, other), std::runtime_error);
    CHECK(read(schema) == "{}\n");
    // Force replaces, and only then.
    spec.force = true;
    CHECK_NOTHROW(create_agent(tree.config, spec));
    CHECK(read(prompt) != "MY EDITED PROMPT\n");
}
