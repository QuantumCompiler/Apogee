#include "scaffold/agent.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "contracts/config.h"
#include "contracts/config_edit.h"
#include "contracts/layout.h"
#include "support/env_guard.h"
#include "support/text_edits.h"

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

/// Every file under the home but the config itself, sorted: what a refused
/// create must leave as it found it (CLI-M52).
std::vector<std::filesystem::path> files_besides_config(const Tree& tree) {
    std::vector<std::filesystem::path> files;
    std::error_code code;
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator{tree.home.path(), code}) {
        if (entry.is_regular_file() && entry.path() != tree.config) {
            files.push_back(entry.path());
        }
    }
    std::ranges::sort(files);
    return files;
}

/// The words `create_agent` refuses `spec` with; fails the case when it
/// creates instead.
std::string refusal(const Tree& tree, const AgentSpec& spec) {
    try {
        (void)create_agent(tree.config, spec);
    } catch (const std::runtime_error& e) {
        return e.what();
    }
    FAIL("'" + spec.name + "' was created, not refused");
    return {};
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
    // Everything the template said, kept in place around the one entry.
    CHECK(apogee::testing::inserted(before, tree.bytes()).has_value());
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

TEST_CASE("a collection named by a path is refused before any file is written",
          "[scaffold][agent][M45]") {
    // CLI-M45: an agent's collection is read by every run, so a path is
    // refused at add -- the one core `agents create` and the admin POST and
    // PUT share -- in the read rule's words, the config and the tree as they
    // were. Every plain name, odd or not, is kept.
    const Tree tree;
    const std::string before = tree.bytes();
    for (const std::string pathed : {"../x", "/tmp/x", "a/b", "a\\b", ".."}) {
        INFO(pathed);
        AgentSpec spec;
        spec.name = "reader";
        spec.collection = pathed;
        try {
            (void)create_agent(tree.config, spec);
            FAIL("a pathed collection must be refused");
        } catch (const std::runtime_error& e) {
            CHECK(std::string{e.what()} == "'" + pathed + "' is not a plain collection name");
        }
        CHECK(tree.bytes() == before);
        CHECK_FALSE(std::filesystem::exists(tree.home.path() / "prompts" / "reader.txt"));
        CHECK_FALSE(std::filesystem::exists(tree.home.path() / "schemas" / "reader.json"));
    }
    AgentSpec spec;
    spec.name = "reader";
    spec.collection = "my notes";
    const apogee::scaffold::AgentResult made = create_agent(tree.config, spec);
    CHECK(made.name == "reader");
}

TEST_CASE("a missing or broken config is refused before any file is written",
          "[scaffold][agent][M52]") {
    // Revised 2026-11-27 (CLI-M52): this case pinned "a config failure leaves
    // the files on disk and says so". A config the edit is certain to refuse
    // is now refused by the dry run, before the first file, as `models
    // pull`'s check_registrations refuses it; only trouble after the dry run
    // keeps files, and names them (the held-lock case below).
    const Tree tree;
    std::ofstream{tree.config, std::ios::binary} << "backends: [not, a, mapping]\n";
    const std::vector<std::filesystem::path> before = files_besides_config(tree);
    AgentSpec spec;
    spec.name = "orphan";
    const std::string broken = refusal(tree, spec);
    CHECK_FALSE(broken.starts_with("updating config:"));
    CHECK(broken.find("written") == std::string::npos);
    CHECK(files_besides_config(tree) == before);
    CHECK_FALSE(std::filesystem::exists(tree.home.path() / "prompts" / "orphan.txt"));

    std::filesystem::remove(tree.config);
    const std::string missing = refusal(tree, spec);
    CHECK(missing.find("config init") != std::string::npos);
    CHECK(files_besides_config(tree).empty());
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

TEST_CASE("a name the config cannot hold is refused before any file, run after run",
          "[scaffold][agent][M52]") {
    // CLI-M52: `agents create -- -x` wrote prompts/-x.txt and
    // schemas/-x-output.json, then the config refused the name; a rerun
    // looped on "prompt file already exists", and `--force` wrote again.
    // The add runs on the config's text first, so nothing is written, and
    // every rerun gives the same refusal.
    const Tree tree;
    const std::string bytes = tree.bytes();
    const std::vector<std::filesystem::path> before = files_besides_config(tree);
    for (const auto& [typed, said] : std::vector<std::pair<std::string, std::string>>{
             {"-x", "agent name '-x' is not one"},
             {".x", "agent name '.x' (as '-x') is not one"},
             {":y", "agent name ':y' (as '-y') is not one"},
             {"null", "agent name 'null' is not one"}}) {
        for (const bool force : {false, false, true}) {
            INFO(typed << (force ? " --force" : ""));
            AgentSpec spec;
            spec.name = typed;
            spec.force = force;
            const std::string words = refusal(tree, spec);
            CHECK(words.starts_with(said));
            CHECK(words.find("both config formats can edit") != std::string::npos);
            CHECK(words.find("file already exists") == std::string::npos);
            CHECK(words.find('\n') == std::string::npos);
            CHECK(files_besides_config(tree) == before);
            CHECK(tree.bytes() == bytes);
        }
    }
}

TEST_CASE("a case variant collides before any file, its neighbour's files there or not",
          "[scaffold][agent][M52]") {
    // On APFS the file check spoke first, and its `--force` remedy replaced
    // the other agent. The dry run says the collision on every disk.
    const Tree tree;
    AgentSpec original;
    original.name = "foo";
    original.description = "the original";
    (void)create_agent(tree.config, original);
    const std::string bytes = tree.bytes();
    const std::filesystem::path prompt = tree.home.path() / "prompts" / "foo.txt";
    const std::string prompt_text = read(prompt);
    const std::vector<std::filesystem::path> before = files_besides_config(tree);

    AgentSpec variant;
    variant.name = "FOO";
    const std::string present = refusal(tree, variant);
    CHECK(present.find("collides with existing 'foo'") != std::string::npos);
    CHECK(files_besides_config(tree) == before);
    CHECK(read(prompt) == prompt_text);
    CHECK(tree.bytes() == bytes);

    for (const std::filesystem::path& file : before) {
        std::filesystem::remove(file);
    }
    const std::string absent = refusal(tree, variant);
    CHECK(absent.find("collides with existing 'foo'") != std::string::npos);
    CHECK(files_besides_config(tree).empty());
    CHECK(tree.bytes() == bytes);
}

TEST_CASE("an entry whose files are gone is refused as existing, and nothing is written",
          "[scaffold][agent][M52]") {
    const Tree tree;
    AgentSpec spec;
    spec.name = "foo";
    (void)create_agent(tree.config, spec);
    for (const std::filesystem::path& file : files_besides_config(tree)) {
        std::filesystem::remove(file);
    }
    const std::string bytes = tree.bytes();
    CHECK(refusal(tree, spec).find("agent 'foo' already exists; pass --force to replace it") !=
          std::string::npos);
    CHECK(files_besides_config(tree).empty());
    CHECK(tree.bytes() == bytes);
    // --force still replaces it in place, and writes the files again.
    spec.force = true;
    CHECK_NOTHROW(create_agent(tree.config, spec));
    CHECK(std::filesystem::exists(tree.home.path() / "prompts" / "foo.txt"));
}

TEST_CASE("a new name past 200 bytes is refused before any file; 200 is created",
          "[scaffold][agent][M52]") {
    // `<name>-output.json.tmp.<10 digits>` must fit NAME_MAX's 255: a
    // 232-byte name once left its prompt behind when the schema's temporary
    // file could not be made.
    const Tree tree;
    const std::string bytes = tree.bytes();
    AgentSpec spec;
    spec.name = std::string(201, 'n');
    const std::string words = refusal(tree, spec);
    CHECK(words.find("is 201 bytes -- a new name is at most 200") != std::string::npos);
    CHECK(files_besides_config(tree).empty());
    CHECK(tree.bytes() == bytes);
    spec.name = std::string(200, 'n');
    CHECK(create_agent(tree.config, spec).name == spec.name);
}

TEST_CASE("a lock held past the wait keeps the files, and the words name every one",
          "[scaffold][agent][M52]") {
    // Trouble after the dry run -- here a config.json.lock another edit holds
    // -- is the one case that leaves files, and says each one.
    const Tree tree;
    const std::string bytes = tree.bytes();
    const apogee::platform::FileLock held = apogee::harness::lock_config_file(tree.config);
    AgentSpec spec;
    spec.name = "late";
    try {
        (void)create_agent(tree.config, spec, std::chrono::milliseconds{150});
        FAIL("the edit went through a held lock");
    } catch (const std::runtime_error& e) {
        const std::string words = e.what();
        CHECK(words.starts_with("updating config: another edit of "));
        CHECK(words.find((tree.home.path() / "prompts" / "late.txt").string()) !=
              std::string::npos);
        CHECK(words.find((tree.home.path() / "schemas" / "late-output.json").string()) !=
              std::string::npos);
        CHECK(words.ends_with("; run it again with --force to replace them)"));
    }
    CHECK(std::filesystem::exists(tree.home.path() / "prompts" / "late.txt"));
    CHECK(std::filesystem::exists(tree.home.path() / "schemas" / "late-output.json"));
    CHECK(tree.bytes() == bytes);
}

TEST_CASE("a config link that leads nowhere is refused before any file", "[scaffold][agent][M52]") {
    // The dry run reads the config as the edit will: through the link, so a
    // dangling one is refused with both ends named, and nothing is written.
    const Tree tree;
    std::filesystem::remove(tree.config);
    std::error_code code;
    std::filesystem::create_symlink(tree.home.path() / "nowhere" / "config.json", tree.config,
                                    code);
    if (code) {
        SKIP("no symbolic links here: " + code.message());
    }
    AgentSpec spec;
    spec.name = "linked";
    CHECK(refusal(tree, spec).find(" is a link to ") != std::string::npos);
    CHECK(files_besides_config(tree).empty());
}

TEST_CASE("a hand-written entry past 200 bytes is not new: it lists, and force replaces it",
          "[scaffold][agent][M52]") {
    // The cap is for new names only (ADR 0006): an existing entry with a
    // longer name keeps loading, and `--force` (or a PUT) still edits it.
    const Tree tree;
    const std::string name(210, 'h');
    apogee::harness::AgentConfig hand;
    hand.prompts = {"prompts/" + name + ".txt"};
    std::ofstream{tree.config, std::ios::binary}
        << apogee::harness::append_agent(tree.bytes(), name, hand, false);
    REQUIRE(apogee::harness::load_config(tree.config).find_agent(name) != nullptr);
    AgentSpec spec;
    spec.name = name;
    spec.no_schema = true;
    CHECK(refusal(tree, spec).find("already exists; pass --force") != std::string::npos);
    spec.force = true;
    CHECK(create_agent(tree.config, spec).name == name);
    CHECK(std::filesystem::exists(tree.home.path() / "prompts" / (name + ".txt")));
    CHECK(apogee::harness::load_config(tree.config).find_agent(name) != nullptr);
}

TEST_CASE("a layout the YAML editor refuses is said before any file", "[scaffold][agent][M52]") {
    // CLI-M44's refusals come out of `append_agent`, so the dry run says
    // them: a 4-space file is refused naming the indent and `config migrate`.
    const Tree tree;
    std::ofstream{tree.config, std::ios::binary}
        << "backends:\n    mock:\n        type: mock\nagents:\n    existing:\n"
           "        tools: none\n";
    const std::string bytes = tree.bytes();
    AgentSpec spec;
    spec.name = "fresh";
    const std::string words = refusal(tree, spec);
    CHECK(words.find("config migrate") != std::string::npos);
    CHECK(files_besides_config(tree).empty());
    CHECK(tree.bytes() == bytes);
}
