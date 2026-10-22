#include "contracts/assets.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

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
    // The config template's drift test, for fourteen more files: the literal
    // in the binary and the file under assets/ are the same bytes, or the build
    // fails -- so an edit to one cannot silently leave the other behind.
    // scripts/generate_agent_assets.py writes the literals (40a).
    const std::filesystem::path assets{APOGEE_ASSETS_DIR};
    REQUIRE(bundled_agents().size() == 7);
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
    CHECK(find_bundled_agent("code-review") != nullptr);
    CHECK(find_bundled_agent("test-gaps") != nullptr);
    CHECK(find_bundled_agent("doc-drift") != nullptr);
    CHECK(find_bundled_agent("meeting-minutes") != nullptr);
    CHECK(find_bundled_agent("nope") == nullptr);
    // Every prompt and schema file shipped is a bundled agent's: a file the
    // generator was never told about would be seeded by nothing.
    for (const char* const dir : {"prompts", "schemas"}) {
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator{assets / dir}) {
            std::string name = entry.path().stem().string();
            if (name.ends_with("-output")) {
                name.resize(name.size() - std::string_view{"-output"}.size());
            }
            INFO(entry.path().string());
            CHECK(find_bundled_agent(name) != nullptr);
        }
    }
}

TEST_CASE("seeding writes absent files only: an edit survives a re-seed",
          "[harness][assets][seed]") {
    const apogee::testing::TempDir root{"assets-seed-" + std::to_string(std::random_device{}())};
    const apogee::harness::AssetSeedResult first = seed_bundled_assets(root.path());
    REQUIRE(first.ok());
    // Fourteen agent files (40a), eight kits (42b), three training regimes
    // (42a, 42b), seven symphony starters (27q, 40b), three task recipes
    // (40c), three training drivers, the three mlx drivers (27a, 27b, 53h),
    // the code-execution server (47b), the video, MLX video, voice and MLX
    // voice drivers (49b, 53l, 50a, 53m), seven review skills (48b), the
    // vendored converter.
    CHECK(first.created.size() == apogee::harness::bundled_files().size());
    CHECK(first.created.size() == 14 + 8 + 3 + 7 + 3 + 3 + 3 + 1 + 4 + 7 + 1 +
                                      apogee::harness::bundled_converter_files().size());
    for (const apogee::harness::BundledSymphony& starter : apogee::harness::bundled_symphonies()) {
        CHECK(read(root.path() / "symphonies" / (std::string{starter.name} + ".yaml")) ==
              starter.text);
    }
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
    REQUIRE(agents.size() == 8);
    CHECK(agents[0].name == "security-review");
    CHECK(agents[0].overrides_bundled);
    CHECK_FALSE(agents[0].bundled);
    CHECK(agents[1].name == "release-notes");
    CHECK(agents[1].bundled);
    CHECK(agents[2].name == "merge-request");
    CHECK(agents[3].name == "code-review");
    CHECK(agents[4].name == "test-gaps");
    CHECK(agents[5].name == "doc-drift");
    CHECK(agents[6].name == "meeting-minutes");
    CHECK(agents[6].bundled);
    CHECK(agents[7].name == "zeta");
    CHECK_FALSE(agents[7].bundled);
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

TEST_CASE("the compiled-in personas byte-match the shipped files, every one compiled in",
          "[harness][assets][personas]") {
    // The coder persona (41c), compiled in by scripts/generate_agent_assets.py
    // and never seeded: what a project session carries when no -s is given.
    const std::filesystem::path dir = std::filesystem::path{APOGEE_ASSETS_DIR} / "personas";
    std::set<std::string> files;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator{dir}) {
        files.insert(entry.path().filename().string());
    }
    std::set<std::string> compiled;
    for (const apogee::harness::BundledPersona& persona : apogee::harness::bundled_personas()) {
        INFO(persona.name);
        compiled.insert(std::string{persona.name} + ".txt");
        CHECK(read(dir / (std::string{persona.name} + ".txt")) == persona.text);
        // Model-agnostic: no vendor or family a small local model would trip
        // over, and short enough to ride every request.
        for (const std::string_view vendor :
             {"Claude", "Anthropic", "OpenAI", "GPT", "Gemini", "Llama", "Qwen", "Apogee"}) {
            CHECK(persona.text.find(vendor) == std::string_view::npos);
        }
        CHECK(persona.text.size() < 4096);
    }
    CHECK(files == compiled);
    CHECK(apogee::harness::find_bundled_persona("coder") != nullptr);
    CHECK(apogee::harness::find_bundled_persona("nope") == nullptr);
}

TEST_CASE("the compiled-in task recipes byte-match the shipped files, every one compiled in",
          "[harness][assets][recipes]") {
    const std::filesystem::path dir = std::filesystem::path{APOGEE_ASSETS_DIR} / "recipes";
    std::set<std::string> files;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator{dir}) {
        files.insert(entry.path().filename().string());
    }
    std::set<std::string> compiled;
    REQUIRE(apogee::harness::bundled_recipes().size() == 3);
    for (const apogee::harness::BundledRecipe& recipe : apogee::harness::bundled_recipes()) {
        INFO(recipe.name);
        compiled.insert(std::string{recipe.name} + ".yaml");
        CHECK(read(dir / (std::string{recipe.name} + ".yaml")) == recipe.text);
        CHECK(apogee::harness::find_bundled_recipe(recipe.name) == &recipe);
    }
    // scripts/generate_recipe_assets.py compiles every file, and nothing else.
    CHECK(files == compiled);
    CHECK(apogee::harness::find_bundled_recipe("nope") == nullptr);

    // Seeded with the rest, under the layout's recipes row, skip-if-present.
    const apogee::testing::TempDir root{"assets-recipes-" + std::to_string(std::random_device{}())};
    REQUIRE(seed_bundled_assets(root.path()).ok());
    for (const apogee::harness::BundledRecipe& recipe : apogee::harness::bundled_recipes()) {
        CHECK(read(root.path() / "recipes" / (std::string{recipe.name} + ".yaml")) == recipe.text);
    }
}

TEST_CASE(
    "the compiled-in training regimes byte-match the shipped files, every one compiled in, "
    "and none names a backend",
    "[harness][assets][training][regimes]") {
    const std::filesystem::path dir =
        std::filesystem::path{APOGEE_ASSETS_DIR} / "training" / "regimes";
    std::set<std::string> files;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator{dir}) {
        files.insert(entry.path().filename().string());
    }
    std::set<std::string> compiled;
    REQUIRE(apogee::harness::bundled_regimes().size() == 3);
    CHECK(apogee::harness::bundled_regimes()[0].name == "foundation");
    CHECK(apogee::harness::bundled_regimes()[1].name == "harness-helpers");
    CHECK(apogee::harness::bundled_regimes()[2].name == "knowledge");
    for (const apogee::harness::BundledRegime& regime : apogee::harness::bundled_regimes()) {
        INFO(regime.name);
        compiled.insert(std::string{regime.name} + ".yaml");
        CHECK(read(dir / (std::string{regime.name} + ".yaml")) == regime.text);
        CHECK(apogee::harness::find_bundled_regime(regime.name) == &regime);
        CHECK(apogee::harness::bundled_regime_relative_path(regime.name) ==
              "training/regimes/" + std::string{regime.name} + ".yaml");
        // Install-agnostic by construction (42a): kits and tuning only --
        // the teacher, the student and the promoted name are the user's.
        const apogee::harness::RegimeSpec spec = apogee::harness::parse_regime_spec(
            std::string{regime.text}, "shipped", std::string{regime.name});
        CHECK(spec.name == regime.name);
        CHECK_FALSE(spec.description.empty());
        CHECK(spec.teacher.empty());
        CHECK(spec.student.empty());
        CHECK(spec.promote_as.empty());
        REQUIRE_FALSE(spec.kits.empty());
        for (const std::string& kit : spec.kits) {
            INFO(kit);
            CHECK(std::ranges::any_of(apogee::harness::bundled_kits(),
                                      [&kit](const apogee::harness::BundledKit& bundled) {
                                          return bundled.name == kit;
                                      }));
        }
    }
    // scripts/generate_training_assets.py compiles every file, and nothing else.
    CHECK(files == compiled);
    CHECK(apogee::harness::find_bundled_regime("nope") == nullptr);

    // Seeded with the rest, under the layout's training row, skip-if-present.
    const apogee::testing::TempDir root{"assets-regimes-" + std::to_string(std::random_device{}())};
    REQUIRE(seed_bundled_assets(root.path()).ok());
    for (const apogee::harness::BundledRegime& regime : apogee::harness::bundled_regimes()) {
        CHECK(read(root.path() / "training" / "regimes" / (std::string{regime.name} + ".yaml")) ==
              regime.text);
    }
}

TEST_CASE("the compiled-in kits and scripts byte-match the shipped files",
          "[harness][assets][training]") {
    const std::filesystem::path assets{APOGEE_ASSETS_DIR};
    REQUIRE(apogee::harness::bundled_kits().size() == 8);
    std::set<std::string> kit_files;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator{assets / "training" / "kits"}) {
        kit_files.insert(entry.path().filename().string());
    }
    std::set<std::string> compiled_kits;
    for (const apogee::harness::BundledKit& kit : apogee::harness::bundled_kits()) {
        INFO(kit.name);
        compiled_kits.insert(std::string{kit.name} + ".yaml");
        CHECK(read(assets / "training" / "kits" / (std::string{kit.name} + ".yaml")) == kit.text);
        CHECK(apogee::harness::bundled_kit_relative_path(kit.name) ==
              "training/kits/" + std::string{kit.name} + ".yaml");
    }
    REQUIRE(apogee::harness::bundled_training_scripts().size() == 3);
    const apogee::harness::BundledScript& script = apogee::harness::bundled_training_scripts()[0];
    CHECK(script.name == "prepare_dataset.py");
    // scripts/generate_training_assets.py compiles every kit file, and nothing else.
    CHECK(kit_files == compiled_kits);
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
          14 + 8 + 3 + 7 + 3 + 3 + 3 + 1 + 4 + 7 + 1 +
              apogee::harness::bundled_converter_files().size());
}

TEST_CASE("the compiled-in mlx drivers byte-match the shipped files and seed beside the trainers",
          "[harness][assets][mlx]") {
    // The mlx backend's driver (27a), the converter's (27b) and the embedding
    // backend's (53h), held to the training drivers' rule: the file is the
    // source of truth, and the literal never drifts from it.
    const std::filesystem::path assets{APOGEE_ASSETS_DIR};
    REQUIRE(apogee::harness::bundled_mlx_scripts().size() == 3);
    const apogee::harness::BundledScript& converter = apogee::harness::bundled_mlx_scripts()[0];
    CHECK(converter.name == "mlx_convert.py");
    CHECK(read(assets / "mlx" / "mlx_convert.py") == converter.text);
    CHECK(apogee::harness::bundled_mlx_converter_relative_path() ==
          "training/scripts/mlx_convert.py");
    const apogee::harness::BundledScript& embedder = apogee::harness::bundled_mlx_scripts()[1];
    CHECK(embedder.name == "mlx_embed.py");
    CHECK(read(assets / "mlx" / "mlx_embed.py") == embedder.text);
    CHECK(apogee::harness::bundled_mlx_embedder_relative_path() == "training/scripts/mlx_embed.py");
    const apogee::harness::BundledScript& driver = apogee::harness::bundled_mlx_scripts()[2];
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

TEST_CASE("the compiled-in review skills byte-match the shipped files and seed by name",
          "[harness][assets][review]") {
    // 48b: the recipes' rule -- every shipped file compiled in byte for
    // byte, every literal a file, seeded under review-skills/ and the
    // user's once edited.
    const std::filesystem::path dir = std::filesystem::path{APOGEE_ASSETS_DIR} / "review-skills";
    std::size_t files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() == ".md") {
            ++files;
            const apogee::harness::BundledReviewSkill* skill =
                apogee::harness::find_bundled_review_skill(entry.path().stem().string());
            REQUIRE(skill != nullptr);
            CHECK(read(entry.path()) == skill->text);
        }
    }
    CHECK(files == apogee::harness::bundled_review_skills().size());
    CHECK(files == 7);
    CHECK(apogee::harness::bundled_review_skill_relative_path("front-end") ==
          "review-skills/front-end.md");

    const apogee::testing::TempDir root{"assets-review-" + std::to_string(std::random_device{}())};
    REQUIRE(apogee::harness::seed_data_directory(root.path()).ok());
    const std::filesystem::path seeded = root.path() / "review-skills" / "security.md";
    REQUIRE(std::filesystem::exists(seeded));
    CHECK(read(seeded) == apogee::harness::find_bundled_review_skill("security")->text);
    CHECK(apogee::harness::is_unmodified_bundled_asset(root.path(), seeded));
    std::ofstream{seeded, std::ios::binary} << "# my lens\n";
    CHECK_FALSE(apogee::harness::is_unmodified_bundled_asset(root.path(), seeded));
    REQUIRE(apogee::harness::seed_data_directory(root.path()).ok());
    CHECK(read(seeded) == "# my lens\n");
}

TEST_CASE(
    "the compiled-in code-execution server byte-matches the shipped file and seeds beside "
    "the drivers",
    "[harness][assets][mcp]") {
    // 47b: the drivers' rule -- the file is the source of truth, the literal
    // never drifts from it, and the seeded copy is the user's once edited.
    const std::filesystem::path assets{APOGEE_ASSETS_DIR};
    REQUIRE(apogee::harness::bundled_mcp_scripts().size() == 1);
    const apogee::harness::BundledScript& server = apogee::harness::bundled_mcp_scripts()[0];
    CHECK(server.name == "code_exec_server.py");
    CHECK(read(assets / "mcp" / "code_exec_server.py") == server.text);
    CHECK(apogee::harness::bundled_code_exec_server_relative_path() ==
          "training/scripts/code_exec_server.py");

    const apogee::testing::TempDir root{"assets-mcp-" + std::to_string(std::random_device{}())};
    REQUIRE(apogee::harness::seed_data_directory(root.path()).ok());
    const std::filesystem::path seeded =
        root.path() / apogee::harness::bundled_code_exec_server_relative_path();
    REQUIRE(std::filesystem::exists(seeded));
    CHECK(read(seeded) == server.text);
    CHECK(apogee::harness::inspect_seeded_script(root.path(), "code_exec_server.py") ==
          apogee::harness::SeededScript::Current);
    CHECK(apogee::harness::is_unmodified_bundled_asset(root.path(), seeded));
    std::ofstream{seeded, std::ios::binary} << "# my edit\n";
    CHECK(apogee::harness::inspect_seeded_script(root.path(), "code_exec_server.py") ==
          apogee::harness::SeededScript::Edited);
    REQUIRE(apogee::harness::seed_data_directory(root.path()).ok());
    CHECK(read(seeded) == "# my edit\n");
}

TEST_CASE("the compiled-in video driver byte-matches the shipped file and seeds beside the drivers",
          "[harness][assets][video]") {
    // 49b: the drivers' rule -- the file is the source of truth, the literal
    // never drifts from it, and the seeded copy is the user's once edited.
    const std::filesystem::path assets{APOGEE_ASSETS_DIR};
    REQUIRE(apogee::harness::bundled_video_scripts().size() == 4);
    // The speech driver (50a) beside it, held to its file the same way.
    const apogee::harness::BundledScript& voice = apogee::harness::bundled_video_scripts()[2];
    CHECK(voice.name == "voice_say.py");
    CHECK(read(assets / "video" / "voice_say.py") == voice.text);
    CHECK(apogee::harness::bundled_voice_driver_relative_path() == "training/scripts/voice_say.py");
    // The MLX engine's speech driver (53m), held to its file the same way.
    const apogee::harness::BundledScript& voice_mlx = apogee::harness::bundled_video_scripts()[3];
    CHECK(voice_mlx.name == "voice_say_mlx.py");
    CHECK(read(assets / "video" / "voice_say_mlx.py") == voice_mlx.text);
    CHECK(apogee::harness::bundled_voice_mlx_driver_relative_path() ==
          "training/scripts/voice_say_mlx.py");
    // The MLX engine's driver (53l), held to its file the same way.
    const apogee::harness::BundledScript& mlx = apogee::harness::bundled_video_scripts()[1];
    CHECK(mlx.name == "video_generate_mlx.py");
    CHECK(read(assets / "video" / "video_generate_mlx.py") == mlx.text);
    CHECK(apogee::harness::bundled_video_mlx_driver_relative_path() ==
          "training/scripts/video_generate_mlx.py");
    const apogee::harness::BundledScript& driver = apogee::harness::bundled_video_scripts()[0];
    CHECK(driver.name == "video_generate.py");
    CHECK(read(assets / "video" / "video_generate.py") == driver.text);
    CHECK(apogee::harness::bundled_video_driver_relative_path() ==
          "training/scripts/video_generate.py");

    const apogee::testing::TempDir root{"assets-video-" + std::to_string(std::random_device{}())};
    REQUIRE(apogee::harness::seed_data_directory(root.path()).ok());
    const std::filesystem::path seeded =
        root.path() / apogee::harness::bundled_video_driver_relative_path();
    REQUIRE(std::filesystem::exists(seeded));
    CHECK(read(seeded) == driver.text);
    CHECK(apogee::harness::inspect_seeded_script(root.path(), "video_generate.py") ==
          apogee::harness::SeededScript::Current);
    const std::filesystem::path seeded_mlx =
        root.path() / apogee::harness::bundled_video_mlx_driver_relative_path();
    CHECK(read(seeded_mlx) == mlx.text);
    CHECK(apogee::harness::inspect_seeded_script(root.path(), "video_generate_mlx.py") ==
          apogee::harness::SeededScript::Current);
    const std::filesystem::path seeded_voice_mlx =
        root.path() / apogee::harness::bundled_voice_mlx_driver_relative_path();
    CHECK(read(seeded_voice_mlx) == voice_mlx.text);
    CHECK(apogee::harness::inspect_seeded_script(root.path(), "voice_say_mlx.py") ==
          apogee::harness::SeededScript::Current);
    std::ofstream{seeded, std::ios::binary} << "# my edit\n";
    CHECK(apogee::harness::inspect_seeded_script(root.path(), "video_generate.py") ==
          apogee::harness::SeededScript::Edited);
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
    bool lora_seen = false;
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
        // Its sibling (53b): `train promote --adapter`'s PEFT -> GGUF LoRA.
        lora_seen = lora_seen || file.name == "convert/convert_lora_to_gguf.py";
        package_seen = package_seen || file.name == "convert/conversion/__init__.py";
        // Where the script's own `sys.path` tweak looks, so the pinned copy
        // shadows the PyPI one (which lags the pin at the same version).
        gguf_seen = gguf_seen || file.name == "convert/gguf-py/gguf/__init__.py";
        template_seen =
            template_seen || file.name == "convert/models/templates/llama-cpp-rwkv-world.jinja";
    }
    CHECK(entry_seen);
    CHECK(lora_seen);
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

TEST_CASE(
    "the LoRA converter is seeded beside its sibling byte for byte, counted in the tree, and "
    "brought up to date as the tree is",
    "[harness][assets][converter][adapter]") {
    // 53b: the vendored `convert_lora_to_gguf.py` is not a second tree -- it
    // seeds, inspects and refreshes as one more file of the converter's.
    const apogee::testing::TempDir root{"assets-lora-" + std::to_string(std::random_device{}())};
    REQUIRE(apogee::harness::seed_data_directory(root.path()).ok());
    const std::filesystem::path tree =
        root.path() / apogee::harness::bundled_converter_relative_dir();
    const std::filesystem::path seeded = tree / "convert_lora_to_gguf.py";
    const std::filesystem::path vendored = std::filesystem::path{APOGEE_THIRD_PARTY_DIR} /
                                           "llama.cpp-convert" / "convert_lora_to_gguf.py";
    REQUIRE(std::filesystem::is_regular_file(seeded));
    CHECK(read(seeded) == read(vendored));
    // It imports what its sibling does, from beside it.
    CHECK(read(seeded).find("from conversion import") != std::string::npos);
    CHECK(read(seeded).find("Path(__file__).parent / 'gguf-py'") != std::string::npos);
    CHECK(apogee::harness::inspect_converter_tree(tree).current());

    // An earlier copy, recorded as retired, is this build's again after the
    // refresh; the same bytes unrecorded are the user's edit, and kept.
    std::ofstream{seeded, std::ios::binary} << "# an older pin's lora converter\n";
    const std::string entry = "convert/convert_lora_to_gguf.py " +
                              apogee::models::sha256_hex("# an older pin's lora converter\n");
    const std::vector<std::string_view> list{entry};
    CHECK(apogee::harness::inspect_converter_tree(tree, list).stale == 1);
    CHECK(apogee::harness::inspect_converter_tree(tree, {}).edited == 1);
    apogee::harness::AssetSeedResult result;
    apogee::harness::refresh_converter_tree(root.path(), tree, result, list);
    REQUIRE(result.ok());
    CHECK(result.updated ==
          std::vector<std::string>{"training/scripts/convert/convert_lora_to_gguf.py"});
    CHECK(read(seeded) == read(vendored));
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
         {apogee::harness::bundled_training_scripts(), apogee::harness::bundled_mlx_scripts(),
          apogee::harness::bundled_mcp_scripts(), apogee::harness::bundled_video_scripts()}) {
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
    write("code_exec_server.py", "# an earlier server\n");  // 47b: refreshed alike
    std::filesystem::remove(seeded_script(root.path(), "prepare_dataset.py"));
    std::vector<std::string> retired{
        "mlx_generate.py " + apogee::models::sha256_hex("# 27a's driver\n"),
        "train_mlx.py " + apogee::models::sha256_hex("# v0.1.0's trainer\n"),
        "code_exec_server.py " + apogee::models::sha256_hex("# an earlier server\n"),
    };
    std::ranges::sort(retired);
    const std::vector<std::string_view> list{retired.begin(), retired.end()};

    const auto state = [&root, &list](std::string_view name) {
        return apogee::harness::inspect_seeded_script(root.path(), name, list);
    };
    CHECK(state("mlx_generate.py") == SeededScript::Stale);
    CHECK(state("train_mlx.py") == SeededScript::Stale);
    CHECK(state("code_exec_server.py") == SeededScript::Stale);
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
                                                     "training/scripts/mlx_generate.py",
                                                     "training/scripts/code_exec_server.py"});
    CHECK(result.created.empty());
    CHECK(read(seeded_script(root.path(), "mlx_generate.py")) == bundled_text("mlx_generate.py"));
    CHECK(read(seeded_script(root.path(), "train_mlx.py")) == bundled_text("train_mlx.py"));
    CHECK(read(seeded_script(root.path(), "code_exec_server.py")) ==
          bundled_text("code_exec_server.py"));
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
    // Every driver the refresh walks: the trainers', the mlx backend's, the
    // code-execution server and, since 49e retired its first copy, video's.
    for (const std::span<const apogee::harness::BundledScript> scripts :
         {apogee::harness::bundled_training_scripts(), apogee::harness::bundled_mlx_scripts(),
          apogee::harness::bundled_mcp_scripts(), apogee::harness::bundled_video_scripts()}) {
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
    // Every version an earlier Apogee seeded: 27a's driver, v0.1.0's
    // trainers and preparer before their 2026-10-04 edits, and the MLX
    // converter before its mlx-vlm route (53f).
    for (const std::string_view earlier :
         {"mlx_generate.py 4676e348595cdbfdb4a23191f7b008e718c29b097da45b5f563c75091e8c2335",
          "mlx_convert.py 26e95774b422a532805992082bf019fefd3ba305ba449eaf304a47ad1799ef41",
          "prepare_dataset.py 868db898b9bb043399a4f57b2ee5f1ad4bf6e614022b0d0249f1b118ec0de41f",
          "train_mlx.py 7f5b206c455094c5d63b72dd804f9ba49a0647d84cf87b723be6192db4ddc478",
          "train_peft.py 8663c7508fb6630ed0d63bbbb856c55ca817196e05178ea71a1621d77199f508"}) {
        INFO(earlier);
        CHECK(std::ranges::binary_search(retired, earlier));
    }
}
