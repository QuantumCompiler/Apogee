#include "contracts/server_tools.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <regex>
#include <string>
#include <vector>

#include "contracts/config.h"
#include "contracts/config_edit.h"
#include "contracts/errors.h"

/// The vendors' server-side tools (47a) as data: the table and its reviewed
/// dates, the loader's refusals, the editor's list in both formats, and the
/// one narration wording every wire speaks through.
namespace {

using apogee::harness::BackendType;
using Catch::Matchers::ContainsSubstring;

/// The message a config that should not load fails with.
std::string load_failure(const std::string& text) {
    try {
        (void)apogee::harness::parse_config(text, "<test>");
    } catch (const apogee::harness::ConfigError& e) {
        return e.what();
    }
    return "loaded";
}

}  // namespace

TEST_CASE("each vendor API's server tools are a table, reviewed", "[server-tools][contracts]") {
    const std::regex date{R"(\d{4}-\d{2}-\d{2})"};
    for (const BackendType type :
         {BackendType::Anthropic, BackendType::OpenAI, BackendType::Google}) {
        const apogee::harness::ServerToolSet* set = apogee::harness::server_tool_set(type);
        REQUIRE(set != nullptr);
        CHECK(set->names == std::vector<std::string_view>{"web_search", "code_execution"});
        CHECK(std::regex_match(std::string{set->reviewed}, date));
        CHECK_FALSE(set->vendor.empty());
    }
    // A local model, a vendor CLI and the mock run none.
    for (const BackendType type :
         {BackendType::LlamaCpp, BackendType::ClaudeCli, BackendType::Mlx, BackendType::Mock}) {
        CHECK(apogee::harness::server_tool_set(type) == nullptr);
    }
    CHECK(apogee::harness::server_tool_names() ==
          std::vector<std::string_view>{"web_search", "code_execution"});
}

TEST_CASE("an entry lists only what its vendor runs, and nothing on a backend that runs none",
          "[server-tools][contracts][config]") {
    const apogee::harness::Config config = apogee::harness::parse_config(
        "backends:\n  claude:\n    type: anthropic\n"
        "    server_tools: [web_search, code_execution, web_search]\n",
        "<test>");
    // Said once, in the order written.
    CHECK(config.find_backend("claude")->server_tools ==
          std::vector<std::string>{"web_search", "code_execution"});
    // Absent is empty: nothing is sent without the line.
    CHECK(apogee::harness::parse_config("backends:\n  claude:\n    type: anthropic\n", "<test>")
              .find_backend("claude")
              ->server_tools.empty());

    CHECK_THAT(load_failure("backends:\n  claude:\n    type: anthropic\n"
                            "    server_tools: [image_generation]\n"),
               ContainsSubstring("backends.claude.server_tools: 'image_generation' is not a "
                                 "server-side tool Anthropic runs (known: web_search, "
                                 "code_execution)"));
    CHECK_THAT(load_failure("backends:\n  local:\n    type: llamacpp\n"
                            "    model_path: /m.gguf\n    server_tools: [web_search]\n"),
               ContainsSubstring("a llamacpp backend runs no server-side tools"));
    CHECK_THAT(load_failure("backends:\n  claude:\n    type: anthropic\n"
                            "    server_tools: web_search\n"),
               ContainsSubstring("expected a list of strings"));

    // The JSONC config reads the same list.
    const apogee::harness::Config jsonc = apogee::harness::parse_config(
        R"({"backends": {"gpt": {"type": "openai", "server_tools": ["code_execution"]}}})",
        "<test>");
    CHECK(jsonc.find_backend("gpt")->server_tools == std::vector<std::string>{"code_execution"});
}

TEST_CASE("the editor writes server_tools as a list, in place, in both formats",
          "[server-tools][contracts][config_edit]") {
    constexpr std::string_view kYaml =
        "backends:\n  claude:\n    type: anthropic   # the API\n    model: claude-opus-5-5\n";
    const std::string set = apogee::harness::set_backend_field(kYaml, "claude", "server_tools",
                                                               "web_search, code_execution");
    CHECK(set ==
          "backends:\n  claude:\n    type: anthropic   # the API\n"
          "    server_tools: [web_search, code_execution]\n    model: claude-opus-5-5\n");
    CHECK(apogee::harness::parse_config(set, "<test>").find_backend("claude")->server_tools ==
          std::vector<std::string>{"web_search", "code_execution"});
    // Cleared, the line goes and the file is as it was.
    CHECK(apogee::harness::set_backend_field(set, "claude", "server_tools", "") == kYaml);
    // A value that is no list of names is refused before anything is written.
    CHECK_THROWS_WITH(
        apogee::harness::set_backend_field(kYaml, "claude", "server_tools", "web search"),
        ContainsSubstring("is not a comma-separated list of tool names"));

    const std::string jsonc =
        "{\n  // the vendors\n  \"backends\": {\n    \"gpt\": {\n      \"type\": \"openai\"\n    "
        "}\n  }\n}\n";
    const std::string edited =
        apogee::harness::set_backend_field(jsonc, "gpt", "server_tools", "web_search");
    CHECK_THAT(edited, ContainsSubstring("// the vendors"));
    CHECK(apogee::harness::parse_config(edited, "<test>").find_backend("gpt")->server_tools ==
          std::vector<std::string>{"web_search"});
}

TEST_CASE("every vendor tool is said in one wording, its excerpt one line",
          "[server-tools][contracts]") {
    CHECK(apogee::harness::server_tool_line("web_search", "Anthropic", R"("apogee")") ==
          R"(web_search, run by Anthropic: "apogee")");
    CHECK(apogee::harness::server_tool_line("code_execution", "Google", "") ==
          "code_execution, run by Google");
    // The first line only, cut where it is long, never inside a character.
    CHECK(apogee::harness::narration_excerpt("  import pandas\nprint(1)\n") == "import pandas…");
    CHECK(apogee::harness::narration_excerpt("short") == "short");
    CHECK(apogee::harness::narration_excerpt("") == "");
    const std::string long_line(100, 'x');
    CHECK(apogee::harness::narration_excerpt(long_line, 10) == "xxxxxxxxxx…");
    // "é" is two bytes: a cut at byte 2 of "aé…" backs off to keep it whole.
    CHECK(apogee::harness::narration_excerpt("a\xc3\xa9zzzz", 2) == "a…");
}
