#include "contracts/config_migrate.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
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

namespace {

using apogee::harness::CommentedOption;

[[nodiscard]] std::string_view trim(std::string_view line) {
    const std::size_t first = line.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    return line.substr(first, line.find_last_not_of(" \t\r\n") - first + 1);
}

/// `text`'s lines, each with its terminator.
[[nodiscard]] std::vector<std::string> lines_of(std::string_view text) {
    std::vector<std::string> lines;
    for (std::size_t start = 0; start < text.size();) {
        const std::size_t end = text.find('\n', start);
        const std::size_t next = end == std::string_view::npos ? text.size() : end + 1;
        lines.emplace_back(text.substr(start, next - start));
        start = next;
    }
    return lines;
}

[[nodiscard]] std::string joined(const std::vector<std::string>& lines) {
    std::string out;
    for (const std::string& line : lines) {
        out += line;
    }
    return out;
}

/// The line `option`'s block opens on in `text`, as its declaration finds it.
[[nodiscard]] std::size_t opening_line(const std::vector<std::string>& lines,
                                       const CommentedOption& option) {
    const auto at = std::ranges::find_if(
        lines, [&option](const std::string& line) { return trim(line) == option.opens; });
    REQUIRE(at != lines.end());
    return static_cast<std::size_t>(at - lines.begin());
}

/// `text` without `option`'s block -- what `config upgrade` puts back: the
/// blank lines above it, a bare `//` joining it to the text above, and its
/// lines through the one `closes` reads -- as a file seeded before the
/// release that added it holds the template.
[[nodiscard]] std::string without(std::string_view text, const CommentedOption& option) {
    std::vector<std::string> lines = lines_of(text);
    std::size_t first = opening_line(lines, option);
    std::size_t last = first;
    while (last < lines.size() && trim(lines[last]) != option.closes) {
        ++last;
    }
    REQUIRE(last < lines.size());
    while (first > 0 && trim(lines[first - 1]) == "//") {
        --first;
    }
    while (first > 0 && trim(lines[first - 1]).empty()) {
        --first;
    }
    lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(first),
                lines.begin() + static_cast<std::ptrdiff_t>(last) + 1);
    return joined(lines);
}

/// The declared options, in the order the template carries them.
[[nodiscard]] std::vector<CommentedOption> in_template_order(std::vector<CommentedOption> options) {
    const std::vector<std::string> lines = lines_of(apogee::harness::config_template());
    std::ranges::stable_sort(options, {}, [&lines](const CommentedOption& option) {
        return opening_line(lines, option);
    });
    return options;
}

/// A release's numbers, `0.2.0` as {0, 2, 0}, so `0.1.10` follows `0.1.9`.
[[nodiscard]] std::vector<int> release_of(std::string_view release) {
    std::vector<int> parts;
    std::istringstream in{std::string{release}};
    for (std::string part; std::getline(in, part, '.');) {
        parts.push_back(std::stoi(part));
    }
    return parts;
}

[[nodiscard]] std::vector<std::string> names_of(const std::vector<CommentedOption>& options) {
    std::vector<std::string> names;
    for (const CommentedOption& option : options) {
        names.emplace_back(option.option);
    }
    return names;
}

/// The template's keys alone, laid out as it lays them out: every comment
/// line gone, every trailing comment cut, every blank line dropped -- the
/// audit's probe, a config holding exactly the template's real keys.
[[nodiscard]] std::string keys_alone(std::string_view text) {
    std::string out;
    for (const std::string& line : lines_of(text)) {
        if (trim(line).starts_with("//") || trim(line).empty()) {
            continue;
        }
        std::string kept = line.substr(0, line.find("//"));
        kept.erase(kept.find_last_not_of(" \t\r\n") + 1);
        out += kept + "\n";
    }
    return out;
}

}  // namespace

TEST_CASE("every commented option is one block of this build's template, declared by release",
          "[config][upgrade][commented]") {
    // 55g: the options the template teaches in a comment alone, declared so
    // upgrade can carry them. Each block is found once, is comments through,
    // and the declarations run oldest release first.
    const std::vector<CommentedOption> options = apogee::harness::commented_options();
    REQUIRE_FALSE(options.empty());
    const std::vector<std::string> lines = lines_of(apogee::harness::config_template());
    std::vector<std::string> names;
    std::vector<int> release = release_of(options.front().release);
    for (const CommentedOption& option : options) {
        INFO(option.option);
        CHECK(std::ranges::count_if(lines, [&option](const std::string& line) {
                  return trim(line) == option.opens;
              }) == 1);
        std::size_t line = opening_line(lines, option);
        for (; line < lines.size() && trim(lines[line]) != option.closes; ++line) {
            CHECK(trim(lines[line]).starts_with("//"));
        }
        CHECK(line < lines.size());
        CHECK(release_of(option.release).size() == 3);
        CHECK(release <= release_of(option.release));
        release = release_of(option.release);
        CHECK(std::ranges::find(names, option.option) == names.end());
        names.emplace_back(option.option);
    }
    // The template has every one of them.
    CHECK(apogee::harness::missing_template_options(apogee::harness::config_template()).empty());
}

TEST_CASE("each commented option taken out of the template is put back where it was, once",
          "[config][upgrade][commented]") {
    // A file seeded before the release that added the block: upgrade names
    // it, puts it back byte for byte where the template has it, and a second
    // run adds nothing.
    const std::string written{apogee::harness::config_template()};
    for (const CommentedOption& option : apogee::harness::commented_options()) {
        INFO(option.option);
        const std::string old = without(written, option);
        REQUIRE(old != written);
        CHECK(apogee::harness::missing_template_options(old) ==
              std::vector<std::string>{std::string{option.option}});
        const apogee::harness::Upgrade upgraded =
            apogee::harness::upgrade_config_text(old, written);
        CHECK(upgraded.added == std::vector<std::string>{std::string{option.option}});
        CHECK(upgraded.text == written);
        CHECK(apogee::harness::upgrade_config_text(upgraded.text, written).added.empty());
    }
}

TEST_CASE("a release's commented options taken out come back in the template's order, exactly",
          "[config][upgrade][commented]") {
    // The starter config of the release before each declared one, rebuilt by
    // taking out every block declared since (0.1.9's, for 0.2.0, is exactly
    // the template that release shipped): upgraded, it is this build's.
    const std::string written{apogee::harness::config_template()};
    const std::vector<CommentedOption> options = apogee::harness::commented_options();
    std::vector<std::string_view> releases;
    for (const CommentedOption& option : options) {
        if (releases.empty() || releases.back() != option.release) {
            releases.push_back(option.release);
        }
    }
    CHECK(releases.size() >= 2);
    for (const std::string_view from : releases) {
        INFO("since " << from);
        std::vector<CommentedOption> since;
        std::string old = written;
        for (const CommentedOption& option : options) {
            if (release_of(option.release) >= release_of(from)) {
                since.push_back(option);
                old = without(old, option);
            }
        }
        const std::vector<std::string> expected = names_of(in_template_order(since));
        CHECK(apogee::harness::missing_template_options(old) == expected);
        const apogee::harness::Upgrade upgraded =
            apogee::harness::upgrade_config_text(old, written);
        CHECK(upgraded.added == expected);
        CHECK(upgraded.text == written);
    }
}

TEST_CASE("a commented option goes into a CRLF file in its own line endings",
          "[config][upgrade][commented]") {
    std::string crlf;
    for (const std::string& line : lines_of(apogee::harness::config_template())) {
        crlf += line.substr(0, line.size() - 1) + "\r\n";
    }
    const std::vector<CommentedOption> options = apogee::harness::commented_options();
    const std::string old = without(crlf, options.back());
    const apogee::harness::Upgrade upgraded =
        apogee::harness::upgrade_config_text(old, apogee::harness::config_template());
    CHECK(upgraded.added == std::vector<std::string>{std::string{options.back().option}});
    CHECK(upgraded.text == crlf);
}

TEST_CASE("the template's keys alone gain every commented option, each last in its object",
          "[config][upgrade][commented]") {
    // The audit's probe: a config holding exactly the template's real keys
    // was `current`. Now each block goes into the object the template keeps
    // it in -- with nothing of the template's around it to stand beside,
    // last there -- the values untouched, and a second run adds nothing.
    const std::string_view written = apogee::harness::config_template();
    const std::string old = keys_alone(written);
    (void)apogee::harness::parse_config(old, "<keys alone>");
    const std::vector<std::string> expected =
        names_of(in_template_order(apogee::harness::commented_options()));
    CHECK(apogee::harness::missing_template_options(old) == expected);

    const apogee::harness::Upgrade upgraded = apogee::harness::upgrade_config_text(old, written);
    CHECK(upgraded.added == expected);
    CHECK(apogee::harness::jsonc::parse_json(upgraded.text) ==
          apogee::harness::jsonc::parse_json(old));
    CHECK(apogee::harness::missing_template_options(upgraded.text).empty());
    CHECK(apogee::harness::upgrade_config_text(upgraded.text, written).added.empty());
    // Each where its object is: the role pointer's in `models`, the review
    // targets' at the top level after the last section, the rest in
    // `backends` in the template's order.
    const std::string& text = upgraded.text;
    const std::size_t models = text.find("\"models\": {");
    const std::size_t paths = text.find("\"paths\": {");
    const std::size_t backends = text.find("\"backends\": {");
    const auto at = [&text](std::string_view opens) { return text.find(opens); };
    for (const CommentedOption& option : apogee::harness::commented_options()) {
        INFO(option.option);
        const std::size_t pos = at(option.opens);
        REQUIRE(pos != std::string::npos);
        if (option.option.starts_with("models.")) {
            CHECK((models < pos && pos < paths));
        } else {
            CHECK(backends < pos);
        }
    }
    CHECK(text.find("\n  // Review sweep targets:") > text.find("\n  }\n", backends));
    CHECK(text.ends_with("  // },\n}\n"));
    // Re-indented to the object's own: a backends block four spaces in.
    CHECK(text.find("\n    // draft runs a drafter beside the model") != std::string::npos);
}

TEST_CASE("an object written on one line takes no commented option, and none is counted",
          "[config][upgrade][commented]") {
    // A block of whole lines cannot go inside `{}` without rewriting the
    // file's line, which upgrade never does -- so it is not offered either.
    const std::string_view written = apogee::harness::config_template();
    std::string old = keys_alone(written);
    const std::size_t at = old.find("\"backends\": {\n  }");
    REQUIRE(at != std::string::npos);
    old.replace(at, std::string_view{"\"backends\": {\n  }"}.size(), "\"backends\": {}");
    const std::vector<std::string> missing = apogee::harness::missing_template_options(old);
    CHECK(missing == std::vector<std::string>{"models.default_video", "review"});
    const apogee::harness::Upgrade upgraded = apogee::harness::upgrade_config_text(old, written);
    CHECK(upgraded.added == missing);
    CHECK(upgraded.text.find("\"backends\": {}") != std::string::npos);
    CHECK(apogee::harness::missing_template_options(upgraded.text).empty());
}
