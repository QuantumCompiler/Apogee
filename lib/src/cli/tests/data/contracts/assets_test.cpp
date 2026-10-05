#include "contracts/assets.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <random>
#include <span>
#include <sstream>
#include <string>

#include "contracts/config.h"
#include "contracts/layout.h"
#include "contracts/sha256.h"
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
    // Six agent files, four kits, three training drivers, the two mlx
    // drivers, the vendored converter.
    CHECK(first.created.size() == apogee::harness::bundled_files().size());
    CHECK(first.created.size() ==
          6 + 4 + 3 + 2 + apogee::harness::bundled_converter_files().size());
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
          6 + 4 + 3 + 2 + apogee::harness::bundled_converter_files().size());
}

TEST_CASE("the compiled-in mlx drivers byte-match the shipped files and seed beside the trainers",
          "[harness][assets][mlx]") {
    // The mlx backend's driver (27a) and the converter's (27b), held to the
    // training drivers' rule: the file is the source of truth, and the
    // literal never drifts from it.
    const std::filesystem::path assets{APOGEE_ASSETS_DIR};
    REQUIRE(apogee::harness::bundled_mlx_scripts().size() == 2);
    const apogee::harness::BundledScript& converter = apogee::harness::bundled_mlx_scripts()[0];
    CHECK(converter.name == "mlx_convert.py");
    CHECK(read(assets / "mlx" / "mlx_convert.py") == converter.text);
    CHECK(apogee::harness::bundled_mlx_converter_relative_path() ==
          "training/scripts/mlx_convert.py");
    const apogee::harness::BundledScript& driver = apogee::harness::bundled_mlx_scripts()[1];
    CHECK(driver.name == "mlx_generate.py");
    CHECK(read(assets / "mlx" / "mlx_generate.py") == driver.text);
    CHECK(apogee::harness::bundled_mlx_driver_relative_path() ==
          "training/scripts/mlx_generate.py");

    // Seeded skip-if-present by the one seeding path, and Apogee's until edited.
    const apogee::testing::TempDir root{"assets-mlx-" + std::to_string(std::random_device{}())};
    REQUIRE(apogee::harness::seed_data_directory(root.path()).ok());
    const std::filesystem::path seeded =
        root.path() / apogee::harness::bundled_mlx_driver_relative_path();
    REQUIRE(std::filesystem::exists(seeded));
    CHECK(read(seeded) == driver.text);
    CHECK(apogee::harness::is_unmodified_bundled_asset(root.path(), seeded));
    std::ofstream{seeded, std::ios::binary} << "# my edit\n";
    CHECK_FALSE(apogee::harness::is_unmodified_bundled_asset(root.path(), seeded));
    REQUIRE(apogee::harness::seed_data_directory(root.path()).ok());
    CHECK(read(seeded) == "# my edit\n");
}

TEST_CASE("the compiled-in converter byte-matches the vendored llama.cpp tree, file for file",
          "[harness][assets][training][converter]") {
    const std::filesystem::path vendored =
        std::filesystem::path{APOGEE_THIRD_PARTY_DIR} / "llama.cpp-convert";
    const std::span<const apogee::harness::BundledScript> files =
        apogee::harness::bundled_converter_files();
    // The entry script, the conversion package, the `gguf` package it
    // imports, the three templates.
    REQUIRE(files.size() > 70);
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
    bool gguf_seen = false;
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
        // Where the script's own `sys.path` tweak looks, so the pinned copy
        // shadows the PyPI one (which lags the pin at the same version).
        gguf_seen = gguf_seen || file.name == "convert/gguf-py/gguf/__init__.py";
        template_seen =
            template_seen || file.name == "convert/models/templates/llama-cpp-rwkv-world.jinja";
    }
    CHECK(entry_seen);
    CHECK(package_seen);
    CHECK(gguf_seen);
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

TEST_CASE("an earlier Apogee's converter is brought up to this build's; an edit is kept",
          "[harness][assets][converter][refresh]") {
    // Seeding is skip-if-present, so without this a pin bump never reaches an
    // existing install: its converter stays the old llama.cpp's, and refuses
    // the models the bump was for (Gemma 4 "unified", 2026-09-23).
    const apogee::testing::TempDir root{"assets-refresh-" + std::to_string(std::random_device{}())};
    REQUIRE(apogee::harness::seed_data_directory(root.path()).ok());
    const std::filesystem::path tree =
        root.path() / apogee::harness::bundled_converter_relative_dir();
    CHECK(apogee::harness::inspect_converter_tree(tree).current());

    const auto write = [](const std::filesystem::path& path, const std::string& bytes) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream{path, std::ios::binary} << bytes;
    };
    // Three files an earlier Apogee shipped: one this build ships too, one it
    // no longer does (in a directory of its own), and one the user edited.
    write(tree / "conversion" / "llama.py", "# llama, as the old pin had it\n");
    write(tree / "conversion" / "gone" / "retired.py", "# upstream deleted this\n");
    write(tree / "conversion" / "qwen.py", "# the user's own change\n");
    // Python's cache is nobody's edit.
    write(tree / "conversion" / "__pycache__" / "llama.cpython-314.pyc", "bytecode");
    std::vector<std::string> retired{
        "convert/conversion/gone/retired.py " +
            apogee::models::sha256_hex("# upstream deleted this\n"),
        "convert/conversion/llama.py " +
            apogee::models::sha256_hex("# llama, as the old pin had it\n"),
    };
    std::ranges::sort(retired);
    const std::vector<std::string_view> list{retired.begin(), retired.end()};

    const apogee::harness::ConverterTreeState before =
        apogee::harness::inspect_converter_tree(tree, list);
    CHECK(before.stale == 2);
    CHECK(before.edited == 1);
    CHECK(before.missing == 0);

    apogee::harness::AssetSeedResult result;
    apogee::harness::refresh_converter_tree(root.path(), tree, result, list);
    REQUIRE(result.ok());
    CHECK(result.updated ==
          std::vector<std::string>{"training/scripts/convert/conversion/llama.py"});
    CHECK(result.removed ==
          std::vector<std::string>{"training/scripts/convert/conversion/gone/retired.py"});
    // The stale file is this build's now, the retired one and its emptied
    // directory are gone, and the edit is exactly as the user left it.
    const apogee::harness::BundledScript* llama = nullptr;
    for (const apogee::harness::BundledScript& file : apogee::harness::bundled_converter_files()) {
        if (file.name == "convert/conversion/llama.py") {
            llama = &file;
        }
    }
    REQUIRE(llama != nullptr);
    CHECK(read(tree / "conversion" / "llama.py") == llama->text);
    CHECK_FALSE(std::filesystem::exists(tree / "conversion" / "gone"));
    CHECK(read(tree / "conversion" / "qwen.py") == "# the user's own change\n");

    const apogee::harness::ConverterTreeState after =
        apogee::harness::inspect_converter_tree(tree, list);
    CHECK(after.stale == 0);
    CHECK(after.edited == 1);
}

TEST_CASE("the compiled-in retired list names earlier versions, never this build's",
          "[harness][assets][converter][refresh]") {
    const std::span<const std::string_view> retired = apogee::harness::bundled_converter_retired();
    CHECK(std::ranges::is_sorted(retired));
    for (const std::string_view entry : retired) {
        INFO(entry);
        const std::size_t space = entry.rfind(' ');
        REQUIRE(space != std::string_view::npos);
        CHECK(entry.starts_with("convert/"));
        CHECK(entry.size() - space - 1 == 64);
    }
    // A file this build ships unchanged is not "retired": the refresh would
    // rewrite it for nothing and check would call a current tree stale.
    for (const apogee::harness::BundledScript& file : apogee::harness::bundled_converter_files()) {
        const std::string current =
            std::string{file.name} + " " + apogee::models::sha256_hex(file.text);
        CHECK_FALSE(std::ranges::binary_search(retired, std::string_view{current}));
    }
}

// ---------------------------------------------------------------------------
// The drivers an earlier Apogee seeded (27c)
// ---------------------------------------------------------------------------

namespace {

/// The seeded driver `name` under `root`.
std::filesystem::path seeded_script(const std::filesystem::path& root, std::string_view name) {
    return root / apogee::harness::bundled_script_relative_path(name);
}

/// This build's text of the seeded driver `name`.
std::string bundled_text(std::string_view name) {
    for (const std::span<const apogee::harness::BundledScript> scripts :
         {apogee::harness::bundled_training_scripts(), apogee::harness::bundled_mlx_scripts()}) {
        for (const apogee::harness::BundledScript& script : scripts) {
            if (script.name == name) {
                return std::string{script.text};
            }
        }
    }
    FAIL("no bundled driver " << name);
    return {};
}

}  // namespace

TEST_CASE("an earlier Apogee's drivers are brought up to this build's; an edit is kept",
          "[harness][assets][scripts][refresh]") {
    // Seeding is skip-if-present, so without this an install keeps the
    // driver an earlier Apogee seeded: 27a's mlx_generate.py, which never
    // reads a picture, on the install that upgraded to read them.
    using apogee::harness::SeededScript;
    const apogee::testing::TempDir root{"scripts-refresh-" +
                                        std::to_string(std::random_device{}())};
    REQUIRE(apogee::harness::seed_data_directory(root.path()).ok());
    const auto write = [&root](std::string_view name, const std::string& bytes) {
        std::ofstream{seeded_script(root.path(), name), std::ios::binary} << bytes;
    };
    // Two an earlier Apogee shipped, one the user edited, one never seeded.
    write("mlx_generate.py", "# 27a's driver\n");
    write("train_mlx.py", "# v0.1.0's trainer\n");
    write("train_peft.py", "# the user's own change\n");
    std::filesystem::remove(seeded_script(root.path(), "prepare_dataset.py"));
    std::vector<std::string> retired{
        "mlx_generate.py " + apogee::models::sha256_hex("# 27a's driver\n"),
        "train_mlx.py " + apogee::models::sha256_hex("# v0.1.0's trainer\n"),
    };
    std::ranges::sort(retired);
    const std::vector<std::string_view> list{retired.begin(), retired.end()};

    const auto state = [&root, &list](std::string_view name) {
        return apogee::harness::inspect_seeded_script(root.path(), name, list);
    };
    CHECK(state("mlx_generate.py") == SeededScript::Stale);
    CHECK(state("train_mlx.py") == SeededScript::Stale);
    CHECK(state("train_peft.py") == SeededScript::Edited);
    CHECK(state("prepare_dataset.py") == SeededScript::Missing);
    CHECK(state("mlx_convert.py") == SeededScript::Current);
    // Under another list, the same bytes are an edit: never guessed.
    CHECK(apogee::harness::inspect_seeded_script(root.path(), "mlx_generate.py", {}) ==
          SeededScript::Edited);

    apogee::harness::AssetSeedResult result;
    apogee::harness::refresh_seeded_scripts(root.path(), result, list);
    REQUIRE(result.ok());
    CHECK(result.updated == std::vector<std::string>{"training/scripts/train_mlx.py",
                                                     "training/scripts/mlx_generate.py"});
    CHECK(result.created.empty());
    CHECK(read(seeded_script(root.path(), "mlx_generate.py")) == bundled_text("mlx_generate.py"));
    CHECK(read(seeded_script(root.path(), "train_mlx.py")) == bundled_text("train_mlx.py"));
    // The edit is exactly as the user left it; the missing one is seeding's.
    CHECK(read(seeded_script(root.path(), "train_peft.py")) == "# the user's own change\n");
    CHECK_FALSE(std::filesystem::exists(seeded_script(root.path(), "prepare_dataset.py")));
    CHECK(state("mlx_generate.py") == SeededScript::Current);
    CHECK(state("train_peft.py") == SeededScript::Edited);

    // Run again: nothing left to update.
    apogee::harness::AssetSeedResult again;
    apogee::harness::refresh_seeded_scripts(root.path(), again, list);
    CHECK(again.updated.empty());
}

TEST_CASE("27a's own mlx_generate.py is refreshed by the one seeding path, as check --fix runs it",
          "[harness][assets][scripts][refresh]") {
    // The real file an install that ran 27a holds (its bytes pinned as a
    // fixture, from commit ad5124c), through the compiled-in list.
    const apogee::testing::TempDir root{"scripts-27a-" + std::to_string(std::random_device{}())};
    REQUIRE(apogee::harness::seed_data_directory(root.path()).ok());
    const std::string earlier =
        read(std::filesystem::path{APOGEE_TEST_FIXTURES} / "mlx" / "mlx_generate-27a.py");
    REQUIRE(earlier.find("PROTOCOL = 1") != std::string::npos);
    REQUIRE(earlier != bundled_text("mlx_generate.py"));
    std::ofstream{seeded_script(root.path(), "mlx_generate.py"), std::ios::binary} << earlier;
    CHECK(apogee::harness::inspect_seeded_script(root.path(), "mlx_generate.py") ==
          apogee::harness::SeededScript::Stale);
    // Still Apogee's, not the user's: `uninstall` takes it as seeded.
    CHECK(apogee::harness::is_unmodified_bundled_asset(
        root.path(), seeded_script(root.path(), "mlx_generate.py")));

    const apogee::harness::SeedResult seeded = apogee::harness::seed_data_directory(root.path());
    REQUIRE(seeded.ok());
    CHECK(seeded.updated == std::vector<std::string>{"training/scripts/mlx_generate.py"});
    CHECK(read(seeded_script(root.path(), "mlx_generate.py")) == bundled_text("mlx_generate.py"));
    CHECK(apogee::harness::inspect_seeded_script(root.path(), "mlx_generate.py") ==
          apogee::harness::SeededScript::Current);
    // An edit of it is never "an earlier Apogee's".
    std::ofstream{seeded_script(root.path(), "mlx_generate.py"), std::ios::binary} << earlier
                                                                                   << "# mine\n";
    CHECK_FALSE(apogee::harness::is_unmodified_bundled_asset(
        root.path(), seeded_script(root.path(), "mlx_generate.py")));
    CHECK(apogee::harness::seed_data_directory(root.path()).updated.empty());
    CHECK(read(seeded_script(root.path(), "mlx_generate.py")) == earlier + "# mine\n");
}

TEST_CASE("the compiled-in driver list names earlier versions, never this build's",
          "[harness][assets][scripts][refresh]") {
    const std::span<const std::string_view> retired = apogee::harness::bundled_scripts_retired();
    CHECK(std::ranges::is_sorted(retired));
    std::vector<std::string> names;
    std::vector<std::string> currents;
    for (const std::span<const apogee::harness::BundledScript> scripts :
         {apogee::harness::bundled_training_scripts(), apogee::harness::bundled_mlx_scripts()}) {
        for (const apogee::harness::BundledScript& script : scripts) {
            names.emplace_back(script.name);
            // A driver this build ships unchanged is not "retired": the
            // refresh would rewrite it for nothing and check would call a
            // current install stale.
            const std::string current =
                std::string{script.name} + " " + apogee::models::sha256_hex(script.text);
            CHECK_FALSE(std::ranges::binary_search(retired, std::string_view{current}));
            currents.push_back(current);
        }
    }
    for (const std::string_view entry : retired) {
        INFO(entry);
        const std::size_t space = entry.rfind(' ');
        REQUIRE(space != std::string_view::npos);
        CHECK(entry.size() - space - 1 == 64);
        CHECK(std::ranges::find(names, entry.substr(0, space)) != names.end());
    }
    // The list is the file's, regenerated: an entry added to
    // assets/retired-scripts.txt and never compiled in would refresh nothing.
    std::vector<std::string> recorded;
    std::istringstream file{read(std::filesystem::path{APOGEE_ASSETS_DIR} / "retired-scripts.txt")};
    for (std::string line; std::getline(file, line);) {
        // Less this build's own bytes, as the generator leaves them out.
        if (!line.empty() && !line.starts_with("#") &&
            std::ranges::find(currents, line) == currents.end()) {
            recorded.push_back(line);
        }
    }
    std::ranges::sort(recorded);
    CHECK(std::vector<std::string>{retired.begin(), retired.end()} == recorded);
    // Every version an earlier Apogee seeded: 27a's driver, and v0.1.0's
    // trainers and preparer before their 2026-10-04 edits.
    for (const std::string_view earlier :
         {"mlx_generate.py 4676e348595cdbfdb4a23191f7b008e718c29b097da45b5f563c75091e8c2335",
          "prepare_dataset.py 868db898b9bb043399a4f57b2ee5f1ad4bf6e614022b0d0249f1b118ec0de41f",
          "train_mlx.py 7f5b206c455094c5d63b72dd804f9ba49a0647d84cf87b723be6192db4ddc478",
          "train_peft.py 8663c7508fb6630ed0d63bbbb856c55ca817196e05178ea71a1621d77199f508"}) {
        INFO(earlier);
        CHECK(std::ranges::binary_search(retired, earlier));
    }
}
