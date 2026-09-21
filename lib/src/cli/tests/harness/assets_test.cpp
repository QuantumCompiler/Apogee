#include "harness/assets.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <span>
#include <sstream>
#include <string>

#include "harness/config.h"
#include "harness/layout.h"
#include "support/env_guard.h"

/// The bundled agents: compiled-in texts byte-equal to the shipped files,
/// seeding that never overwrites, and the lookup precedence.
namespace {

using apogee::harness::all_agents;
using apogee::harness::bundled_agents;
using apogee::harness::find_bundled_agent;
using apogee::harness::resolve_agent;
using apogee::harness::seed_bundled_assets;

std::string read(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    REQUIRE(in.good());
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

}  // namespace

TEST_CASE("the compiled-in prompts and schemas byte-match the shipped files",
          "[harness][assets][template]") {
    // The config template's drift test, for six more files: the literal in
    // the binary and the file under assets/ are the same bytes, or the build
    // fails -- so an edit to one cannot silently leave the other behind.
    const std::filesystem::path assets{APOGEE_ASSETS_DIR};
    REQUIRE(bundled_agents().size() == 3);
    for (const apogee::harness::BundledAgent& agent : bundled_agents()) {
        INFO(agent.name);
        CHECK(read(assets / "prompts" / (std::string{agent.name} + ".txt")) == agent.prompt);
        CHECK(read(assets / "schemas" / (std::string{agent.name} + "-output.json")) ==
              agent.schema);
        CHECK_FALSE(agent.description.empty());
    }
    CHECK(find_bundled_agent("security-review") != nullptr);
    CHECK(find_bundled_agent("release-notes") != nullptr);
    CHECK(find_bundled_agent("merge-request") != nullptr);
    CHECK(find_bundled_agent("nope") == nullptr);
}

TEST_CASE("seeding writes absent files only: an edit survives a re-seed",
          "[harness][assets][seed]") {
    const apogee::testing::TempDir root{"assets-seed-" + std::to_string(std::random_device{}())};
    const apogee::harness::AssetSeedResult first = seed_bundled_assets(root.path());
    REQUIRE(first.ok());
    // Six agent files, four kits, three drivers, the vendored converter.
    CHECK(first.created.size() == apogee::harness::bundled_files().size());
    CHECK(first.created.size() == 6 + 4 + 3 + apogee::harness::bundled_converter_files().size());
    const std::filesystem::path prompt = root.path() / "prompts" / "security-review.txt";
    REQUIRE(std::filesystem::exists(prompt));
    CHECK(read(prompt) == find_bundled_agent("security-review")->prompt);

    // The user edits the prompt; a second seed reads the edit back.
    std::ofstream{prompt, std::ios::binary} << "MY EDITED PROMPT\n";
    const apogee::harness::AssetSeedResult second = seed_bundled_assets(root.path());
    REQUIRE(second.ok());
    CHECK(second.created.empty());
    CHECK(read(prompt) == "MY EDITED PROMPT\n");

    // A deleted file comes back on the next seed, and nothing else moves.
    std::filesystem::remove(root.path() / "schemas" / "release-notes-output.json");
    const apogee::harness::AssetSeedResult third = seed_bundled_assets(root.path());
    REQUIRE(third.ok());
    CHECK(third.created == std::vector<std::string>{"schemas/release-notes-output.json"});
    CHECK(read(prompt) == "MY EDITED PROMPT\n");
}

TEST_CASE("the one seeding path materialises the bundled files with the directories",
          "[harness][assets][seed][layout]") {
    const apogee::testing::TempDir root{"assets-layout-" + std::to_string(std::random_device{}())};
    const apogee::harness::SeedResult seeded = apogee::harness::seed_data_directory(root.path());
    REQUIRE(seeded.ok());
    CHECK(std::filesystem::exists(root.path() / "prompts" / "merge-request.txt"));
    CHECK(std::filesystem::exists(root.path() / "schemas" / "merge-request-output.json"));
    CHECK(std::filesystem::is_directory(root.path() / "analyses"));
    bool listed = false;
    for (const std::string& created : seeded.created) {
        listed = listed || created == "prompts/merge-request.txt";
    }
    CHECK(listed);
}

TEST_CASE("a config entry of the same name overrides a bundled agent; unknown names are nothing",
          "[harness][assets][resolve]") {
    apogee::harness::Config config;
    const apogee::harness::BundledAgent* bundled = nullptr;
    const std::optional<apogee::harness::AgentConfig> plain =
        resolve_agent(config, "security-review", &bundled);
    REQUIRE(plain.has_value());
    REQUIRE(bundled != nullptr);
    CHECK(plain->prompts == std::vector<std::string>{"prompts/security-review.txt"});
    CHECK(plain->schemas == std::vector<std::string>{"schemas/security-review-output.json"});
    CHECK(plain->tools == apogee::harness::AgentToolPolicy::ReadOnly);
    CHECK(plain->save_subdir == "security-review");
    CHECK_FALSE(resolve_agent(config, "nothing").has_value());

    apogee::harness::AgentConfig mine;
    mine.model = "local";
    mine.tools = apogee::harness::AgentToolPolicy::All;
    config.agents.emplace("security-review", mine);
    apogee::harness::AgentConfig extra;
    extra.description = "custom";
    config.agents.emplace("zeta", extra);
    const std::optional<apogee::harness::AgentConfig> overridden =
        resolve_agent(config, "Security-Review", &bundled);
    REQUIRE(overridden.has_value());
    CHECK(overridden->model == "local");
    CHECK(overridden->tools == apogee::harness::AgentToolPolicy::All);
    CHECK(bundled != nullptr);  // still reported, for the compiled-in fallback

    const std::vector<apogee::harness::NamedAgent> agents = all_agents(config);
    REQUIRE(agents.size() == 4);
    CHECK(agents[0].name == "security-review");
    CHECK(agents[0].overrides_bundled);
    CHECK_FALSE(agents[0].bundled);
    CHECK(agents[1].name == "release-notes");
    CHECK(agents[1].bundled);
    CHECK(agents[2].name == "merge-request");
    CHECK(agents[3].name == "zeta");
    CHECK_FALSE(agents[3].bundled);
}

TEST_CASE("agent paths resolve against the data directory, absolute paths as they are",
          "[harness][assets][resolve]") {
    const std::filesystem::path home{"/srv/apogee"};
    CHECK(apogee::harness::resolve_agent_path(home, "prompts/x.txt") ==
          std::filesystem::path{"/srv/apogee/prompts/x.txt"});
    CHECK(apogee::harness::resolve_agent_path(home, "/abs/x.txt") ==
          std::filesystem::path{"/abs/x.txt"});
    const apogee::testing::EnvGuard guard{"APOGEE_ASSETS_TEST_DIR", "/from/env"};
    CHECK(apogee::harness::resolve_agent_path(home, "${APOGEE_ASSETS_TEST_DIR}/y.txt") ==
          std::filesystem::path{"/from/env/y.txt"});
}

TEST_CASE("an unedited bundled file is Apogee's, an edited one is the user's",
          "[harness][assets][uninstall]") {
    const apogee::testing::TempDir root{"assets-owned-" + std::to_string(std::random_device{}())};
    REQUIRE(seed_bundled_assets(root.path()).ok());
    const std::filesystem::path prompt = root.path() / "prompts" / "security-review.txt";
    CHECK(apogee::harness::is_unmodified_bundled_asset(root.path(), prompt));
    std::ofstream{prompt, std::ios::binary} << "edited\n";
    CHECK_FALSE(apogee::harness::is_unmodified_bundled_asset(root.path(), prompt));
    // A file that is not bundled at all is the user's whatever it holds.
    std::ofstream{root.path() / "prompts" / "mine.txt", std::ios::binary} << "x";
    CHECK_FALSE(apogee::harness::is_unmodified_bundled_asset(root.path(),
                                                             root.path() / "prompts" / "mine.txt"));
    CHECK_FALSE(apogee::harness::is_unmodified_bundled_asset(root.path(), root.path() / "nope"));
}

TEST_CASE("the compiled-in kits and scripts byte-match the shipped files",
          "[harness][assets][training]") {
    const std::filesystem::path assets{APOGEE_ASSETS_DIR};
    REQUIRE(apogee::harness::bundled_kits().size() == 4);
    for (const apogee::harness::BundledKit& kit : apogee::harness::bundled_kits()) {
        INFO(kit.name);
        CHECK(read(assets / "training" / "kits" / (std::string{kit.name} + ".yaml")) == kit.text);
        CHECK(apogee::harness::bundled_kit_relative_path(kit.name) ==
              "training/kits/" + std::string{kit.name} + ".yaml");
    }
    REQUIRE(apogee::harness::bundled_training_scripts().size() == 3);
    const apogee::harness::BundledScript& script = apogee::harness::bundled_training_scripts()[0];
    CHECK(script.name == "prepare_dataset.py");
    CHECK(apogee::harness::bundled_training_scripts()[1].name == "train_mlx.py");
    CHECK(apogee::harness::bundled_training_scripts()[2].name == "train_peft.py");
    for (const apogee::harness::BundledScript& driver :
         apogee::harness::bundled_training_scripts()) {
        INFO(driver.name);
        CHECK(read(assets / "training" / driver.name) == driver.text);
    }
    CHECK(apogee::harness::bundled_script_relative_path(script.name) ==
          "training/scripts/prepare_dataset.py");
    CHECK(apogee::harness::bundled_files().size() ==
          6 + 4 + 3 + apogee::harness::bundled_converter_files().size());
}

TEST_CASE("the compiled-in converter byte-matches the vendored llama.cpp tree, file for file",
          "[harness][assets][training][converter]") {
    const std::filesystem::path vendored =
        std::filesystem::path{APOGEE_THIRD_PARTY_DIR} / "llama.cpp-convert";
    const std::span<const apogee::harness::BundledScript> files =
        apogee::harness::bundled_converter_files();
    // The entry script, the conversion package, the three templates.
    REQUIRE(files.size() > 60);
    std::size_t on_disk = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(vendored)) {
        if (entry.is_regular_file() &&
            (entry.path().extension() == ".py" || entry.path().extension() == ".jinja")) {
            ++on_disk;
        }
    }
    CHECK(on_disk == files.size());
    bool entry_seen = false;
    bool package_seen = false;
    bool template_seen = false;
    for (const apogee::harness::BundledScript& file : files) {
        INFO(file.name);
        REQUIRE(file.name.starts_with("convert/"));
        const std::filesystem::path path = vendored / std::string{file.name.substr(8)};
        CHECK(read(path) == file.text);
        CHECK(apogee::harness::bundled_script_relative_path(file.name) ==
              "training/scripts/" + std::string{file.name});
        entry_seen = entry_seen || file.name == "convert/convert_hf_to_gguf.py";
        package_seen = package_seen || file.name == "convert/conversion/__init__.py";
        template_seen =
            template_seen || file.name == "convert/models/templates/llama-cpp-rwkv-world.jinja";
    }
    CHECK(entry_seen);
    CHECK(package_seen);
    CHECK(template_seen);
    CHECK(apogee::harness::bundled_converter_relative_dir() == "training/scripts/convert");
    // Not a single literal: chunked under MSVC's limit, joined at first use.
    CHECK(read(vendored / "conversion" / "base.py").size() > 16380);
}

TEST_CASE(
    "seeding materialises the kits and the script, and a seeded directory is Apogee's "
    "until something in it is edited",
    "[harness][assets][training][seed]") {
    const apogee::testing::TempDir root{"assets-kits-" + std::to_string(std::random_device{}())};
    REQUIRE(apogee::harness::seed_data_directory(root.path()).ok());
    const std::filesystem::path kits = root.path() / "training" / "kits";
    const std::filesystem::path script =
        root.path() / "training" / "scripts" / "prepare_dataset.py";
    REQUIRE(std::filesystem::exists(kits / "reasoning.yaml"));
    REQUIRE(std::filesystem::exists(script));
    CHECK(apogee::harness::is_unmodified_bundled_asset(root.path(), script));
    CHECK(apogee::harness::is_unmodified_bundled_asset(root.path(), kits));
    CHECK(apogee::harness::is_unmodified_bundled_asset(root.path(),
                                                       root.path() / "training" / "scripts"));
    // An empty directory is nobody's, so it is not "Apogee's".
    std::filesystem::create_directories(root.path() / "training" / "datasets");
    CHECK_FALSE(apogee::harness::is_unmodified_bundled_asset(
        root.path(), root.path() / "training" / "datasets"));
    // One edited kit makes the directory the user's.
    std::ofstream{kits / "reasoning.yaml", std::ios::binary} << "name: mine\n";
    CHECK_FALSE(apogee::harness::is_unmodified_bundled_asset(root.path(), kits));
    CHECK_FALSE(apogee::harness::is_unmodified_bundled_asset(root.path(), kits / "reasoning.yaml"));
    CHECK(apogee::harness::is_unmodified_bundled_asset(root.path(), kits / "summarization.yaml"));
}
