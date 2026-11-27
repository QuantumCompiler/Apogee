#include "contracts/mcp_serve.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>
#include <vector>

#include "contracts/config.h"
#include "contracts/config_edit.h"
#include "contracts/errors.h"

/// The calls `apogee mcp serve` may make (54b) as data: the table, the
/// loader's refusal, the editor's list in both formats, the rule's one
/// wording, and the bound.
namespace {

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

TEST_CASE("complete is the one call a backend can list for mcp serve", "[mcp-serve][contracts]") {
    CHECK(std::vector<std::string_view>(apogee::harness::mcp_serve_tool_names().begin(),
                                        apogee::harness::mcp_serve_tool_names().end()) ==
          std::vector<std::string_view>{"complete"});
    CHECK(apogee::harness::mcp_serve_refusal({}).empty());
    CHECK(apogee::harness::mcp_serve_refusal({"complete"}).empty());
    CHECK(apogee::harness::mcp_serve_refusal({"complete", "chat"}) ==
          "'chat' is not a call apogee mcp serve makes (known: complete)");
}

TEST_CASE("an entry's mcp_serve loads once per name, absent is empty, and an unknown name fails",
          "[mcp-serve][contracts][config]") {
    const apogee::harness::Config config = apogee::harness::parse_config(
        "backends:\n  local:\n    type: mock\n    mcp_serve: [complete, complete]\n", "<test>");
    CHECK(config.find_backend("local")->mcp_serve == std::vector<std::string>{"complete"});
    // Nothing served without the line: the default is consent withheld.
    CHECK(apogee::harness::parse_config("backends:\n  local:\n    type: mock\n", "<test>")
              .find_backend("local")
              ->mcp_serve.empty());
    CHECK_THAT(load_failure("backends:\n  local:\n    type: mock\n    mcp_serve: [chat]\n"),
               ContainsSubstring("backends.local.mcp_serve: 'chat' is not a call apogee mcp "
                                 "serve makes (known: complete)"));
    CHECK_THAT(load_failure("backends:\n  local:\n    type: mock\n    mcp_serve: complete\n"),
               ContainsSubstring("expected a list of strings"));
    // Never a type list at load: whether an entry may serve is its
    // provider's word, asked by the editor and the server.
    CHECK(apogee::harness::parse_config(
              "backends:\n  claude:\n    type: anthropic\n    mcp_serve: [complete]\n", "<test>")
              .find_backend("claude")
              ->mcp_serve == std::vector<std::string>{"complete"});

    const apogee::harness::Config jsonc = apogee::harness::parse_config(
        R"({"backends": {"local": {"type": "mock", "mcp_serve": ["complete"]}}})", "<test>");
    CHECK(jsonc.find_backend("local")->mcp_serve == std::vector<std::string>{"complete"});
}

TEST_CASE("the editor writes mcp_serve as a list, in place, in both formats",
          "[mcp-serve][contracts][config_edit]") {
    constexpr std::string_view kYaml =
        "backends:\n  local:\n    type: llamacpp   # on this machine\n    model_path: /m.gguf\n";
    const std::string set =
        apogee::harness::set_backend_field(kYaml, "local", "mcp_serve", "complete");
    CHECK(set ==
          "backends:\n  local:\n    type: llamacpp   # on this machine\n"
          "    mcp_serve: [complete]\n    model_path: /m.gguf\n");
    CHECK(apogee::harness::parse_config(set, "<test>").find_backend("local")->mcp_serve ==
          std::vector<std::string>{"complete"});
    // Cleared, the line goes and the file is as it was.
    CHECK(apogee::harness::set_backend_field(set, "local", "mcp_serve", "") == kYaml);
    // A value that is no list of names is refused, naming the field.
    CHECK_THROWS_WITH(
        apogee::harness::set_backend_field(kYaml, "local", "mcp_serve", "com plete"),
        "mcp_serve: 'com plete' is not a comma-separated list of tool names (e.g. complete)");
    // A name it cannot run is refused by the edit's own re-parse.
    CHECK_THAT(
        load_failure(apogee::harness::set_backend_field(kYaml, "local", "mcp_serve", "chat")),
        ContainsSubstring("'chat' is not a call apogee mcp serve makes"));

    const std::string jsonc =
        "{\n  // mine\n  \"backends\": {\n    \"local\": {\n      \"type\": \"mock\"\n    "
        "}\n  }\n}\n";
    const std::string edited =
        apogee::harness::set_backend_field(jsonc, "local", "mcp_serve", "complete");
    CHECK_THAT(edited, ContainsSubstring("// mine"));
    CHECK_THAT(edited, ContainsSubstring(R"("mcp_serve": ["complete"])"));
    CHECK(apogee::harness::parse_config(edited, "<test>").find_backend("local")->mcp_serve ==
          std::vector<std::string>{"complete"});
}

TEST_CASE("the eligibility rule is said in one wording, naming why", "[mcp-serve][contracts]") {
    CHECK(apogee::harness::mcp_serve_ineligible("cloud", {"complete"}, true, "") ==
          "'cloud' cannot serve complete: its provider bills each call -- a served call runs on "
          "another host's initiative, so only a backend local and unmetered by its own "
          "provider's word may serve one");
    CHECK(apogee::harness::mcp_serve_ineligible("odd", {"complete"}, true, "no key is set") ==
          "'odd' cannot serve complete: whether its provider bills each call could not be asked "
          "(no key is set), and unknown counts as billed -- a served call runs on another "
          "host's initiative, so only a backend local and unmetered by its own provider's word "
          "may serve one");
}

TEST_CASE("a served call's bound is 1024 tokens, or the entry's own when lower",
          "[mcp-serve][contracts]") {
    CHECK(apogee::harness::kServedMaxTokens == 1024);
    CHECK(apogee::harness::served_max_tokens(std::nullopt) == 1024);
    CHECK(apogee::harness::served_max_tokens(256) == 256);
    // Never raised by the entry: the bound is the served one.
    CHECK(apogee::harness::served_max_tokens(8192) == 1024);
    CHECK(apogee::harness::served_max_tokens(0) == 1024);
}
