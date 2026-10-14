#include "operations/backend_fields.h"

#include <CLI/CLI.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "cli/root.h"
#include "contracts/config.h"
#include "contracts/config_edit.h"
#include "support/cli_home.h"

/// `config set-backend` (46b) through the real command: one flag per editable
/// field, spelled as `add-backend` spells it; only the named fields touched,
/// a comment kept, `""` clearing one; never a key; every refusal named.
namespace {

using Catch::Matchers::ContainsSubstring;

[[nodiscard]] std::string slurp(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// The long spellings `name` takes, help and hidden ones aside.
[[nodiscard]] std::set<std::string> flags_of(const std::string& name) {
    const apogee::commands::RootCommand root;
    const CLI::App* config = root.app().get_subcommand("config");
    const CLI::App* verb = config->get_subcommand(name);
    std::set<std::string> out;
    for (const CLI::Option* option : verb->get_options()) {
        if (option == verb->get_help_ptr() || option->get_group().empty() ||
            !option->nonpositional()) {
            continue;
        }
        for (const std::string& spelling : option->get_lnames()) {
            out.insert(spelling);
        }
    }
    return out;
}

constexpr std::string_view kConfig = R"(backends:
  local:
    type: llamacpp   # in process
    model_path: /models/gemma.gguf   # 12B
    mmproj_path: /models/gemma-mmproj.gguf
  claude:
    type: anthropic
    model: claude-sonnet-5   # the default model
models:
  default: local
)";

}  // namespace

TEST_CASE("set-backend has a flag for every editable field, and every one add-backend has",
          "[set-backend][cli][enumeration]") {
    std::set<std::string> fields;
    for (const std::string_view field : apogee::harness::backend_editable_fields()) {
        std::string spelling{field};
        std::ranges::replace(spelling, '_', '-');
        fields.insert(spelling);
    }
    const std::set<std::string> set_backend = flags_of("set-backend");
    CHECK(set_backend == fields);
    // add-backend's fields, but its key and --force, are set-backend's.
    std::set<std::string> add_backend = flags_of("add-backend");
    add_backend.erase("api-key");
    add_backend.erase("force");
    CHECK(std::ranges::includes(set_backend, add_backend));
}

TEST_CASE("set-backend edits exactly the fields named, in place, and clears with an empty value",
          "[set-backend][cli]") {
    const apogee::testing::CliHome home{std::string{kConfig}};
    std::string out;
    std::string err;
    REQUIRE(home.run({"config", "set-backend", "claude", "--model", "claude-opus-5-5",
                      "--temperature", "0.2"},
                     &out, &err) == 0);
    CHECK_THAT(out, ContainsSubstring("edited backend 'claude'"));
    CHECK_THAT(out, ContainsSubstring("set model, temperature"));
    std::string expected{kConfig};
    expected.replace(expected.find("model: claude-sonnet-5"), 22, "model: claude-opus-5-5");
    expected.insert(expected.find("    model: claude-opus-5-5"), "    temperature: 0.2\n");
    CHECK(slurp(home.config_path()) == expected);

    REQUIRE(home.run({"config", "set-backend", "local", "--mmproj-path", ""}, &out, &err) == 0);
    CHECK_THAT(out, ContainsSubstring("cleared mmproj_path"));
    expected.erase(expected.find("    mmproj_path: /models/gemma-mmproj.gguf\n"), 43);
    CHECK(slurp(home.config_path()) == expected);

    // Refusals, each named, the file untouched.
    CHECK(home.run({"config", "set-backend", "claude"}, &out, &err) == 1);
    CHECK_THAT(err, ContainsSubstring("name a field to set"));
    CHECK(home.run({"config", "set-backend", "nope", "--model", "x"}, &out, &err) == 1);
    CHECK_THAT(err, ContainsSubstring("no backend named 'nope'"));
    CHECK(home.run({"config", "set-backend", "claude", "--api-key", "sk-planted"}, &out, &err) ==
          1);
    CHECK_THAT(err, ContainsSubstring("a key is never taken on the command line"));
    CHECK_THAT(err, ContainsSubstring("apogee auth add"));
    CHECK(home.run({"config", "set-backend", "claude", "--context-size", "lots"}, &out, &err) == 1);
    CHECK(home.run({"config", "set-backend", "claude", "--type", ""}, &out, &err) == 1);
    CHECK_THAT(err, ContainsSubstring("cannot be cleared"));
    CHECK(slurp(home.config_path()) == expected);
    // The key never reached the file, nor anything said.
    CHECK(slurp(home.config_path()).find("sk-planted") == std::string::npos);
    CHECK(out.find("sk-planted") == std::string::npos);
}

TEST_CASE("set-backend in a JSONC config: the field in place, its comments kept",
          "[set-backend][cli][jsonc]") {
    const apogee::testing::CliHome home{
        "{\n  // The backends.\n  \"backends\": {\n    \"claude\": {\n      \"type\": "
        "\"anthropic\",\n"
        "      \"model\": \"claude-sonnet-5\"  // the default model\n    }\n  }\n}\n",
        "config.json"};
    const std::string before = slurp(home.config_path());
    std::string out;
    std::string err;
    REQUIRE(home.run({"config", "set-backend", "claude", "--model", "claude-opus-5-5"}, &out,
                     &err) == 0);
    std::string expected = before;
    expected.replace(expected.find("\"claude-sonnet-5\""), 17, "\"claude-opus-5-5\"");
    CHECK(slurp(home.config_path()) == expected);
}
