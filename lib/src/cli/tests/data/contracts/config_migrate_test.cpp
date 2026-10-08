#include "contracts/config_migrate.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/config.h"
#include "contracts/config_edit.h"
#include "contracts/jsonc.h"
#include "support/env_guard.h"

/// The config's move to JSONC and its catching up with a newer template
/// (28i): `config migrate` carries a YAML file over losslessly, `config
/// upgrade` inserts what the template has and the file lacks -- goldens on
/// committed fixtures, so a change to either is a diff someone reads.
namespace {

using apogee::harness::ConfigEditError;
using apogee::harness::ConfigError;
using apogee::testing::TempDir;

std::string fixture(std::string_view name) {
    std::ifstream in{std::filesystem::path{APOGEE_TEST_FIXTURES} / "config" / std::string{name},
                     std::ios::binary};
    REQUIRE(in);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::string read(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

void write(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << text;
}

/// Every comment in a YAML text, as the text after its `#`: whole-line ones,
/// and trailing ones after a space -- outside quotes, in this fixture's
/// plain shapes.
std::vector<std::string> yaml_comments(std::string_view yaml) {
    std::vector<std::string> out;
    std::istringstream lines{std::string{yaml}};
    std::string line;
    while (std::getline(lines, line)) {
        const std::size_t first = line.find_first_not_of(' ');
        if (first != std::string::npos && line[first] == '#') {
            out.push_back(line.substr(first + 1));
            continue;
        }
        if (const std::size_t at = line.find(" #"); at != std::string::npos) {
            out.push_back(line.substr(at + 2));
        }
    }
    return out;
}

/// The facts two loads of one config must agree on.
void same_meaning(const apogee::harness::Config& a, const apogee::harness::Config& b) {
    CHECK(a.models == b.models);
    REQUIRE(a.backends.size() == b.backends.size());
    for (const auto& [name, backend] : a.backends) {
        INFO(name);
        const apogee::harness::BackendConfig* other = b.find_backend(name);
        REQUIRE(other != nullptr);
        CHECK(other->type == backend.type);
        CHECK(other->model == backend.model);
        CHECK(other->model_path == backend.model_path);
        CHECK(other->api_key == backend.api_key);
        CHECK(other->context_size == backend.context_size);
        CHECK(other->max_tokens == backend.max_tokens);
    }
    CHECK(a.permissions.levels == b.permissions.levels);
    CHECK(a.tools.allowed_hosts == b.tools.allowed_hosts);
    CHECK(a.tools.disabled == b.tools.disabled);
    CHECK(a.tools.fs_root == b.tools.fs_root);
    CHECK(a.tools.search.url == b.tools.search.url);
    CHECK(a.tools.search.results == b.tools.search.results);
    CHECK(a.status_mode == b.status_mode);
    CHECK(a.color == b.color);
    CHECK(a.auto_rag == b.auto_rag);
    CHECK(a.suites.size() == b.suites.size());
    CHECK(a.agents.size() == b.agents.size());
    CHECK(a.mcp_servers.size() == b.mcp_servers.size());
    CHECK(a.embeddings.size() == b.embeddings.size());
    CHECK(a.graphs.size() == b.graphs.size());
    CHECK(a.attachments.graph == b.attachments.graph);
    CHECK(a.ui.markdown == b.ui.markdown);
    CHECK(a.memory.recall == b.memory.recall);
    CHECK(a.training.trainer == b.training.trainer);
    CHECK(a.training.pipelines.size() == b.training.pipelines.size());
}

}  // namespace

TEST_CASE("migrate carries the worst-case config over exactly", "[config][migrate]") {
    const std::string yaml = fixture("migrate_worst_case.yaml");
    const apogee::harness::Migration migration = apogee::harness::migrate_config_text(yaml);
    // The golden: every key in its order, every comment where it stood,
    // ${ENV} references literal, a block-scalar prompt kept whole.
    CHECK(migration.text == fixture("migrate_worst_case.json"));
    // Every comment but the `#` line inside the block-scalar prompt, which is
    // the prompt's own text: it stays in the string, never a comment.
    constexpr std::string_view kPromptLine = " not a comment -- part of the prompt";
    std::vector<std::string> comments = yaml_comments(yaml);
    std::erase(comments, std::string{kPromptLine});
    CHECK(migration.comments == comments.size());
    for (const std::string& comment : comments) {
        INFO(comment);
        CHECK(migration.text.find("//" + comment) != std::string::npos);
    }
    CHECK(migration.text.find("//" + std::string{kPromptLine}) == std::string::npos);
    CHECK(migration.text.find("\\n#" + std::string{kPromptLine} + "\\n") != std::string::npos);
    CHECK(migration.text.find("\"${ANTHROPIC_API_KEY}\"") != std::string::npos);
    CHECK(migration.text.find("\"${HOME}/models\"") != std::string::npos);

    // And it means what the YAML meant, ${ENV} expanded the same on read.
    const apogee::testing::EnvGuard key{"ANTHROPIC_API_KEY", "sk-test-not-real"};
    const apogee::harness::Config before = apogee::harness::parse_config(yaml, "yaml");
    const apogee::harness::Config after = apogee::harness::parse_config(migration.text, "jsonc");
    same_meaning(before, after);
    CHECK(after.find_backend("claude")->api_key == "sk-test-not-real");
    CHECK(after.find_backend("local")->temperature == before.find_backend("local")->temperature);
}

TEST_CASE("the starter config as it shipped in YAML migrates with every comment",
          "[config][migrate]") {
    const std::string yaml = fixture("legacy_template.yaml");
    const apogee::harness::Migration migration = apogee::harness::migrate_config_text(yaml);
    const std::vector<std::string> comments = yaml_comments(yaml);
    CHECK(migration.comments == comments.size());
    for (const std::string& comment : comments) {
        INFO(comment);
        CHECK(migration.text.find("//" + comment) != std::string::npos);
    }
    same_meaning(apogee::harness::parse_config(yaml, "yaml"),
                 apogee::harness::parse_config(migration.text, "jsonc"));
    // An empty section holding commented examples is an empty object holding
    // them -- where a reader expects them.
    CHECK(migration.text.find("  \"backends\": {\n\n    // ── Anthropic") != std::string::npos);
    CHECK(migration.text.find("  \"models\": {\n    // Role pointers.") != std::string::npos);
    // The file's header stays above the brace.
    CHECK(migration.text.starts_with("// Apogee configuration.\n"));
}

TEST_CASE("migrate refuses what it cannot carry exactly, naming the line", "[config][migrate]") {
    using Catch::Matchers::ContainsSubstring;
    CHECK_THROWS_WITH(
        apogee::harness::migrate_config_text("models:\n  default: \"a long\n    value\"\n"),
        ContainsSubstring("line 2") && ContainsSubstring("quoted value"));
    CHECK_THROWS_WITH(
        apogee::harness::migrate_config_text("tools:\n  allowed_hosts: [a,\n    b]\n"),
        ContainsSubstring("line 2") && ContainsSubstring("flow list"));
    CHECK_THROWS_AS(apogee::harness::migrate_config_text("models: [unclosed\n"), ConfigError);
    CHECK_THROWS_AS(apogee::harness::migrate_config_text("- a list\n- at the top\n"), ConfigError);
    // An empty file, or one of comments alone, is an empty config.
    CHECK(apogee::harness::migrate_config_text("").text == "{}\n");
    CHECK(apogee::harness::migrate_config_text("# nothing yet\n").text == "// nothing yet\n{}\n");
}

TEST_CASE(
    "migrate on disk: the JSON beside, the original kept byte for byte, refusals touch "
    "nothing",
    "[config][migrate]") {
    const TempDir dir{"config-migrate-" + std::to_string(std::random_device{}())};
    const std::filesystem::path yaml = dir.path() / "config" / "config.yaml";
    const std::filesystem::path json = dir.path() / "config" / "config.json";
    const std::filesystem::path backup = dir.path() / "config" / "config.yaml.bak";
    const std::string original = fixture("migrate_worst_case.yaml");

    SECTION("a YAML config converts") {
        write(yaml, original);
        const apogee::harness::MigrationReport report = apogee::harness::migrate_config_file(yaml);
        CHECK(report.from == yaml);
        CHECK(report.to == json);
        CHECK(report.backup == backup);
        CHECK(report.comments == 21);
        CHECK(report.keys == 20);
        CHECK(read(json) == fixture("migrate_worst_case.json"));
        CHECK(read(backup) == original);
        CHECK_FALSE(std::filesystem::exists(yaml));
        // Run again: the YAML is gone, so there is nothing to convert.
        CHECK_THROWS_AS(apogee::harness::migrate_config_file(yaml), ConfigEditError);
        CHECK_THROWS_WITH(apogee::harness::migrate_config_file(json),
                          Catch::Matchers::ContainsSubstring("already JSON"));
    }
    SECTION("both files there: refused, both named, neither touched") {
        write(yaml, original);
        write(json, "{}\n");
        CHECK_THROWS_WITH(apogee::harness::migrate_config_file(yaml),
                          Catch::Matchers::ContainsSubstring(yaml.string()) &&
                              Catch::Matchers::ContainsSubstring(json.string()));
        CHECK(read(yaml) == original);
        CHECK(read(json) == "{}\n");
        CHECK_FALSE(std::filesystem::exists(backup));
    }
    SECTION("the backup's name taken: refused, nothing touched") {
        write(yaml, original);
        write(backup, "older\n");
        CHECK_THROWS_WITH(apogee::harness::migrate_config_file(yaml),
                          Catch::Matchers::ContainsSubstring(backup.string()));
        CHECK(read(yaml) == original);
        CHECK(read(backup) == "older\n");
        CHECK_FALSE(std::filesystem::exists(json));
    }
    SECTION("a config that does not load is fixed first") {
        write(yaml, "backends: [not a mapping]\n");
        CHECK_THROWS_AS(apogee::harness::migrate_config_file(yaml), ConfigError);
        CHECK_FALSE(std::filesystem::exists(json));
        CHECK_FALSE(std::filesystem::exists(backup));
    }
}

TEST_CASE("upgrade inserts exactly the missing options, with their comments, and only once",
          "[config][upgrade]") {
    const std::string old = fixture("upgrade_old.json");
    const std::string written = fixture("upgrade_template.json");
    CHECK(apogee::harness::missing_template_options(old, written) ==
          std::vector<std::string>{"paths", "permissions.delete_file", "permissions.run_command",
                                   "permissions.write_note", "permissions.delete_note", "tools"});

    const apogee::harness::Upgrade upgraded = apogee::harness::upgrade_config_text(old, written);
    CHECK(upgraded.added == apogee::harness::missing_template_options(old, written));
    // The golden: each option at its template position, the comment block
    // that teaches it above it, everything the user wrote unchanged.
    CHECK(upgraded.text == fixture("upgrade_old.upgraded.json"));
    for (const std::string_view mine :
         {"// My config, from an early release.\n", "  // pointers\n",
          "    \"default\": \"local\" // my model\n", "  // what my tools may do\n",
          "    \"write_file\": \"allow\", // trusted\n"}) {
        CHECK(upgraded.text.find(mine) != std::string::npos);
    }
    (void)apogee::harness::parse_config(upgraded.text, "<upgraded>");

    // Current now: a second run changes nothing and adds nothing.
    CHECK(apogee::harness::missing_template_options(upgraded.text, written).empty());
    const apogee::harness::Upgrade again =
        apogee::harness::upgrade_config_text(upgraded.text, written);
    CHECK(again.added.empty());
    CHECK(again.text == upgraded.text);
}

TEST_CASE("upgrade against this build's template", "[config][upgrade]") {
    const std::string_view written = apogee::harness::config_template();
    // The template is current against itself.
    CHECK(apogee::harness::missing_template_options(written).empty());
    // An empty object gains every option the template writes, and loads.
    const apogee::harness::Upgrade from_empty =
        apogee::harness::upgrade_config_text("{}\n", written);
    CHECK_FALSE(from_empty.added.empty());
    (void)apogee::harness::parse_config(from_empty.text, "<upgraded>");
    CHECK(apogee::harness::missing_template_options(from_empty.text).empty());
    CHECK(apogee::harness::jsonc::parse_json(from_empty.text) ==
          apogee::harness::jsonc::parse_json(written));
    // A YAML config is not compared: it migrates first.
    CHECK(apogee::harness::missing_template_options("models:\n  default: x\n").empty());
    const apogee::harness::Upgrade yaml =
        apogee::harness::upgrade_config_text("models:\n  default: x\n", written);
    CHECK(yaml.added.empty());
    CHECK(yaml.text == "models:\n  default: x\n");
}
