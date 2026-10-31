#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "contracts/config.h"
#include "contracts/config_edit.h"
#include "support/env_guard.h"

/// The config in JSONC (28i): the loader reads it as the same config its
/// YAML twin was, and every verb of the one editor edits it in place --
/// the edited span and nothing else, every comment kept byte for byte.
namespace {

using apogee::harness::ConfigEditError;
using apogee::harness::ConfigError;
using apogee::harness::parse_config;

/// A comment-dense JSONC config: the case the editor exists for. An edit
/// that disturbs one byte of commentary here fails a test.
constexpr std::string_view kJsonc = R"(// My config.
//
// Hand-kept; the tooling works around me.

{
  // Who answers.
  "models": {
    "default": "claude" // the everyday one
  },

  "suites": {
    // A research bundle.
    "research": {
      "members": {
        "chat": "claude",
        "utility": "local" // small
      }
    }
  },

  // Collections.
  "embeddings": {
    "notes": {
      "chunk_size": 512
    }
  },

  "permissions": {
    "write_file": "ask", // careful
    "run_command": "deny"
  },

  "tools": {
    // The websites fetch_url reaches without asking.
    "allowed_hosts": ["docs.python.org"]
    // "search": {"provider": "searxng"}
  },

  "backends": {
    // ── cloud ──
    "claude": {
      "type": "mock",
      "model": "mock-1"
    },

    // ── local ──
    "local": {
      "type": "llamacpp",
      "model_path": "/models/a.gguf" // the current weights
    }
    // "spare": {"type": "mock"}
  }

  // The end.
}
)";

/// The same config in YAML, comments and all.
constexpr std::string_view kYaml = R"(# My config.
models:
  default: claude
suites:
  research:
    members:
      chat: claude
      utility: local
embeddings:
  notes:
    chunk_size: 512
permissions:
  write_file: ask
  run_command: deny
tools:
  allowed_hosts: [docs.python.org]
backends:
  claude:
    type: mock
    model: mock-1
  local:
    type: llamacpp
    model_path: /models/a.gguf
)";

std::vector<std::string> comment_lines(std::string_view text) {
    std::vector<std::string> out;
    std::istringstream lines{std::string{text}};
    std::string line;
    while (std::getline(lines, line)) {
        if (const std::size_t at = line.find("//"); at != std::string::npos) {
            out.push_back(line.substr(at));
        }
    }
    return out;
}

std::vector<std::string> lines_of(std::string_view text) {
    std::vector<std::string> out;
    std::istringstream lines{std::string{text}};
    std::string line;
    while (std::getline(lines, line)) {
        out.push_back(line);
    }
    return out;
}

/// An edit of `before` that loads, and keeps every comment -- each as many
/// times as it was there, whole-line or trailing -- but those in `taken`,
/// the comments inside an entry the edit deleted. And every line it did not
/// edit is still there: the lines `before` lost are at most `changed`, none
/// of them a comment.
void edited_in_place(std::string_view before, const std::string& after,
                     const std::vector<std::string>& taken = {}, std::size_t changed = 1) {
    INFO(after);
    CHECK(after != before);
    (void)parse_config(after, "<edited>");
    std::vector<std::string> kept = comment_lines(after);
    for (const std::string& comment : comment_lines(before)) {
        INFO(comment);
        if (const auto at = std::ranges::find(kept, comment); at != kept.end()) {
            kept.erase(at);
        } else {
            CHECK(std::ranges::find(taken, comment) != taken.end());
        }
    }
    std::vector<std::string> lost = lines_of(before);
    for (const std::string& line : lines_of(after)) {
        if (const auto at = std::ranges::find(lost, line); at != lost.end()) {
            lost.erase(at);
        }
    }
    for (const std::string& line : lost) {
        INFO("lost: " << line);
        CHECK((line.find("//") == std::string::npos ||
               std::ranges::any_of(taken,
                                   [&](const std::string& comment) {
                                       return line.find(comment) != std::string::npos;
                                   }) ||
               after.find(line.substr(line.find("//"))) != std::string::npos));
    }
    CHECK(lost.size() <= changed);
}

apogee::harness::BackendConfig mock_backend() {
    apogee::harness::BackendConfig backend;
    backend.type = apogee::harness::BackendType::Mock;
    backend.model = "mock-2";
    return backend;
}

}  // namespace

TEST_CASE("a JSONC config means what its YAML twin means", "[config][jsonc]") {
    const apogee::harness::Config json = parse_config(kJsonc, "jsonc");
    const apogee::harness::Config yaml = parse_config(kYaml, "yaml");
    CHECK(json.models == yaml.models);
    CHECK(json.backends.size() == 2);
    CHECK(json.find_backend("local")->model_path == yaml.find_backend("local")->model_path);
    CHECK(json.find_backend("claude")->model == "mock-1");
    CHECK(json.permissions.levels == yaml.permissions.levels);
    CHECK(json.tools.allowed_hosts == yaml.tools.allowed_hosts);
    CHECK(json.suites.size() == 1);
    CHECK(json.embeddings.size() == 1);

    // ${ENV} expands on read, exactly as in YAML; the text keeps the reference.
    const apogee::testing::EnvGuard key{"APOGEE_JSONC_TEST_KEY", "sk-test"};
    const apogee::harness::Config keyed = parse_config(
        R"({"backends": {"c": {"type": "anthropic", "api_key": "${APOGEE_JSONC_TEST_KEY}"}}})",
        "jsonc");
    CHECK(keyed.find_backend("c")->api_key == "sk-test");
    // A number reads as the token the file spells, as YAML read it.
    CHECK(parse_config(R"({"backends": {"l": {"type": "llamacpp", "model_path": "/m.gguf",
                          "temperature": 0.70, "context_size": 4096}}})",
                       "jsonc")
              .find_backend("l")
              ->context_size == 4096);
    // Nothing but comments is an empty config, as an empty YAML file is.
    CHECK(parse_config("// nothing yet\n", "jsonc").backends.empty());
    CHECK(parse_config("{}", "jsonc").backends.empty());
}

TEST_CASE("a JSONC config that is not JSON says so, with the line", "[config][jsonc]") {
    using Catch::Matchers::ContainsSubstring;
    CHECK_THROWS_WITH(parse_config("{\n  \"models\": {,}\n}\n", "cfg.json"),
                      ContainsSubstring("cfg.json") && ContainsSubstring("not valid JSON") &&
                          ContainsSubstring("line 2"));
    CHECK_THROWS_WITH(parse_config("{\"backends\": {}, \"backends\": {}}", "cfg.json"),
                      ContainsSubstring("appears twice"));
    // The typed walk's own refusals are the same in either format.
    CHECK_THROWS_WITH(parse_config(R"({"permissions": {"write_file": "sometimes"}})", "cfg.json"),
                      ContainsSubstring("permissions.write_file"));
    CHECK_THROWS_AS(parse_config(R"({"backends": ["not", "a", "mapping"]})", "cfg.json"),
                    ConfigError);
}

TEST_CASE("every verb of the one editor edits a JSONC config in place, comments kept",
          "[config_edit][jsonc]") {
    using namespace apogee::harness;

    struct Verb {
        std::string name;
        std::function<std::string(std::string_view)> edit;
        std::vector<std::string> taken = {};
        std::size_t changed = 1;
    };

    const std::vector<Verb> verbs = {
        {"append_backend",
         [](std::string_view c) { return append_backend(c, "spare", mock_backend(), false); }},
        {"append_backend --force",
         [](std::string_view c) { return append_backend(c, "claude", mock_backend(), true); }},
        {"delete_backend", [](std::string_view c) { return delete_backend(c, "claude"); }, {}, 4},
        {"set_backend_model_path",
         [](std::string_view c) { return set_backend_model_path(c, "local", "/models/b.gguf"); }},
        {"set_backend_mmproj_path",
         [](std::string_view c) { return set_backend_mmproj_path(c, "local", "/models/p.gguf"); }},
        {"set_models_role",
         [](std::string_view c) { return set_models_role(c, "default", "local"); }},
        {"set_models_role new",
         [](std::string_view c) { return set_models_role(c, "default_utility", "local"); }},
        {"append_embedding",
         [](std::string_view c) { return append_embedding(c, "docs", EmbeddingConfig{}, false); }},
        {"delete_embedding",
         [](std::string_view c) { return delete_embedding(c, "notes"); },
         {},
         5},
        {"set_embedding_graph_enabled",
         [](std::string_view c) { return set_embedding_graph_enabled(c, "notes", true); }},
        {"append_mcp_server",
         [](std::string_view c) {
             McpServerConfig server;
             server.command = "/srv/w.py";
             return append_mcp_server(c, "w", server, false);
         }},
        {"append_agent",
         [](std::string_view c) {
             AgentConfig agent;
             agent.prompts = {"prompts/r.txt"};
             return append_agent(c, "r", agent, false);
         }},
        {"append_graph",
         [](std::string_view c) {
             NamedGraphConfig graph;
             graph.collections = {"notes"};
             return append_graph(c, "work", graph, false);
         }},
        {"append_suite",
         [](std::string_view c) {
             SuiteConfig suite;
             suite.members["chat"] = {.backend = "local"};
             return append_suite(c, "fast", suite, false);
         }},
        {"delete_suite",
         [](std::string_view c) { return delete_suite(c, "research"); },
         {"// small"},
         7},
        {"set_suite_member",
         [](std::string_view c) {
             return set_suite_member(c, "research", "embedding", SuiteMember{.backend = "local"});
         }},
        {"set_suite_member remove",
         [](std::string_view c) { return set_suite_member(c, "research", "chat", std::nullopt); },
         {},
         1},
        {"set_suite_consultable",
         [](std::string_view c) { return set_suite_consultable(c, "research", {"utility"}); }},
        {"set_suite_consult_caps",
         [](std::string_view c) {
             ConsultCaps caps;
             caps.per_turn = 2;
             return set_suite_consult_caps(c, "research", caps);
         }},
        {"set_suite_validate",
         [](std::string_view c) {
             ValidateConfig validate;
             validate.tool_args = true;
             return set_suite_validate(c, "research", validate);
         }},
        {"set_suite_orchestrate",
         [](std::string_view c) { return set_suite_orchestrate(c, "research", true); }},
        {"set_default_suite", [](std::string_view c) { return set_default_suite(c, "research"); }},
        {"set_default_video",
         [](std::string_view c) {
             return apogee::harness::set_default_video(c, "org--clip/pipeline/aaaaaaaaaaaa");
         }},
        {"append_symphony",
         [](std::string_view c) {
             SymphonySpec spec;
             spec.description = "Two lines";
             SymphonyStage stage;
             stage.name = "only";
             stage.role = "chat";
             stage.prompt = "First line\nthen {{input}}";
             spec.stages.push_back(stage);
             return append_symphony(c, "duo", spec, false);
         }},
        {"set_attachments_graph",
         [](std::string_view c) { return set_attachments_graph(c, "off"); }},
        {"set_permission",
         [](std::string_view c) { return set_permission(c, "write_file", "allow"); }},
        {"set_permission new",
         [](std::string_view c) { return set_permission(c, "edit_file", "allow"); }},
        {"add_allowed_host", [](std::string_view c) { return add_allowed_host(c, "pypi.org"); }},
        {"remove_allowed_host",
         [](std::string_view c) { return remove_allowed_host(c, "docs.python.org"); }},
    };
    for (const Verb& verb : verbs) {
        INFO(verb.name);
        edited_in_place(kJsonc, verb.edit(kJsonc), verb.taken, verb.changed);
    }
}

TEST_CASE("a JSONC edit lands where it reads naturally, and only there", "[config_edit][jsonc]") {
    using namespace apogee::harness;
    // A value replaced in place keeps the comment after it.
    CHECK(set_backend_model_path(kJsonc, "local", "/models/b.gguf") == [] {
        std::string expected{kJsonc};
        expected.replace(expected.find("/models/a.gguf"), 14, "/models/b.gguf");
        return expected;
    }());
    CHECK(set_models_role(kJsonc, "default", "local")
              .find("    \"default\": \"local\" // the everyday one\n") != std::string::npos);
    // A host joins the one-line list it is in.
    CHECK(add_allowed_host(kJsonc, "pypi.org")
              .find("\"allowed_hosts\": [\"docs.python.org\", \"pypi.org\"]\n") !=
          std::string::npos);
    // A permission set where it stands, keeping its comment.
    CHECK(set_permission(kJsonc, "write_file", "allow")
              .find("\"write_file\": \"allow\", // careful\n") != std::string::npos);
    // A new section goes after the last one, its value in the house shape.
    const std::string attached = set_attachments_graph(kJsonc, "off");
    CHECK(attached.find("    // \"spare\": {\"type\": \"mock\"}\n  },\n  \"attachments\": {\n"
                        "    \"graph\": \"off\"\n  }\n\n  // The end.\n}\n") != std::string::npos);
    // A block-scalar prompt in YAML is one JSON string with its newlines.
    SymphonySpec spec;
    SymphonyStage stage;
    stage.name = "only";
    stage.role = "chat";
    stage.prompt = "First line\nthen {{input}}";
    spec.stages.push_back(stage);
    const std::string played = append_symphony(kJsonc, "duo", spec, false);
    CHECK(played.find("\"prompt\": \"First line\\nthen {{input}}\"") != std::string::npos);
    CHECK(parse_config(played, "<t>").symphonies.at("duo").stages.at(0).prompt ==
          "First line\nthen {{input}}");
}

TEST_CASE("a JSONC append and its delete are exact inverses", "[config_edit][jsonc]") {
    using namespace apogee::harness;
    const std::string backend = append_backend(kJsonc, "spare", mock_backend(), false);
    CHECK(backend.find("    \"local\": {\n      \"type\": \"llamacpp\",\n      \"model_path\": "
                       "\"/models/a.gguf\" // the current weights\n    },\n    \"spare\": {\n"
                       "      \"type\": \"mock\",\n      \"model\": \"mock-2\"\n    }\n"
                       "    // \"spare\": {\"type\": \"mock\"}\n") != std::string::npos);
    CHECK(delete_backend(backend, "spare") == kJsonc);
    CHECK(delete_embedding(append_embedding(kJsonc, "docs", EmbeddingConfig{}, false), "docs") ==
          kJsonc);
    McpServerConfig server;
    server.command = "/srv/w.py";
    const std::string with_server = append_mcp_server(kJsonc, "w", server, false);
    CHECK(set_mcp_server_enabled(set_mcp_server_enabled(with_server, "w", false), "w", true) ==
          with_server);
    // A section the append created stays, emptied -- as the YAML editor
    // leaves a section header it wrote.
    CHECK(delete_mcp_server(with_server, "w").find("\"mcp_servers\": {}") != std::string::npos);
    AgentConfig agent;
    agent.prompts = {"prompts/r.txt"};
    CHECK(delete_agent(append_agent(kJsonc, "r", agent, false), "r").find("\"agents\": {}") !=
          std::string::npos);
    NamedGraphConfig graph;
    graph.collections = {"notes"};
    CHECK(delete_graph(append_graph(kJsonc, "work", graph, false), "work").find("\"graphs\": {}") !=
          std::string::npos);
    CHECK(remove_allowed_host(add_allowed_host(kJsonc, "pypi.org"), "pypi.org") == kJsonc);
    CHECK(set_permission(set_permission(kJsonc, "write_file", "allow"), "write_file", "ask") ==
          kJsonc);
}

TEST_CASE("the editor's refusals are the same in JSONC", "[config_edit][jsonc]") {
    using namespace apogee::harness;
    CHECK_THROWS_AS(append_backend(kJsonc, "claude", mock_backend(), false), ConfigEditError);
    // A case-folded name under --force: whatever YAML does, JSONC does.
    const auto throws = [](std::string_view content) {
        try {
            (void)append_backend(content, "CLAUDE", mock_backend(), true);
            return false;
        } catch (const ConfigEditError&) {
            return true;
        }
    };
    CHECK(throws(kJsonc) == throws(kYaml));
    CHECK_THROWS_AS(delete_backend(kJsonc, "nope"), ConfigEditError);
    CHECK_THROWS_AS(remove_allowed_host(kJsonc, "pypi.org"), ConfigEditError);
    CHECK_THROWS_AS(set_permission(kJsonc, "bad tool", "allow"), ConfigEditError);
    // An add that changes nothing returns the text as it was.
    CHECK(add_allowed_host(kJsonc, "DOCS.python.org.") == kJsonc);
    // Text that is not JSON is refused as such, before any transform.
    CHECK_THROWS_WITH(set_permission("{\"permissions\": {,}}", "write_file", "allow"),
                      Catch::Matchers::ContainsSubstring("not valid JSON"));
}

TEST_CASE("section names and format read JSONC", "[config_edit][jsonc]") {
    using namespace apogee::harness;
    CHECK(section_entry_names(kJsonc, "backends") == std::vector<std::string>{"claude", "local"});
    CHECK(section_entry_names(kJsonc, "agents").empty());
    // Whitespace only: trailing spaces, runs of blank lines, the last newline.
    CHECK(format_config("{\n  \"a\": 1,   \n\n\n  \"b\": 2 // x  \n}") ==
          "{\n  \"a\": 1,\n\n  \"b\": 2 // x\n}\n");
    CHECK(format_config(kJsonc) == kJsonc);
}

TEST_CASE("one verb means one thing in either format", "[config_edit][jsonc][parity]") {
    using namespace apogee::harness;
    const auto both = [](const std::function<std::string(std::string_view)>& verb) {
        return std::pair{parse_config(verb(kYaml), "yaml"), parse_config(verb(kJsonc), "jsonc")};
    };
    {
        const auto [yaml, json] = both(
            [](std::string_view c) { return append_backend(c, "spare", mock_backend(), false); });
        CHECK(json.find_backend("spare")->model == yaml.find_backend("spare")->model);
        CHECK(json.backends.size() == yaml.backends.size());
    }
    {
        const auto [yaml, json] = both([](std::string_view c) {
            return set_suite_member(c, "research", "utility",
                                    SuiteMember{.backend = "claude", .context_size = 1024});
        });
        CHECK(json.suites.at("research").members == yaml.suites.at("research").members);
    }
    {
        const auto [yaml, json] =
            both([](std::string_view c) { return add_allowed_host(c, "[::1]"); });
        CHECK(json.tools.allowed_hosts == yaml.tools.allowed_hosts);
        CHECK(json.tools.allowed_hosts.back() == "::1");
    }
    {
        const auto [yaml, json] =
            both([](std::string_view c) { return set_models_role(c, "default", "local"); });
        CHECK(json.models == yaml.models);
    }
}

// --- renaming a backend (46a) ----------------------------------------------

namespace {

constexpr std::string_view kRenameJsonc = R"({
  // The backends.
  "backends": {
    // The local model, quantized.
    "gemma": {  // the one we use
      "type": "llamacpp",
      "model_path": "/models/gemma.gguf"  // 12B
    },
    "gemma-old": {"type": "mock"},
    "claude": {"type": "anthropic"}
  },
  "models": {
    "default": "gemma",  // the chat
    "default_utility": "gemma",
    "default_vision": "claude"
  },
  "suites": {
    "duo": {"members": {"chat": "gemma", "utility": {"backend": "gemma", "window": 8192}}}
  },
  "embeddings": {
    "gemma": {"backend": "gemma", "rerank": "gemma", "graph": {"extract_backend": "gemma"}}
  },
  "mcp_servers": {
    "gemma": {"command": "/bin/echo"}
  },
  "training": {
    "judge_backend": "gemma",
    "cycle": {
      "backend": "gemma",
      "sources": [{"type": "sessions", "log_consent": true, "backend": "gemma"}]
    }
  }
}
)";

}  // namespace

TEST_CASE("rename_backend in a JSONC config: the key where it stands, every reference in place",
          "[config][jsonc][rename]") {
    const std::string renamed =
        apogee::harness::rename_backend(kRenameJsonc, "gemma", "gemma4-12b");
    std::string expected{kRenameJsonc};
    const auto swap = [&expected](std::string_view from, std::string_view to) {
        const std::size_t at = expected.find(from);
        REQUIRE(at != std::string::npos);
        expected.replace(at, from.size(), to);
    };
    swap("\"gemma\": {  // the one we use", "\"gemma4-12b\": {  // the one we use");
    swap("\"default\": \"gemma\",", "\"default\": \"gemma4-12b\",");
    swap("\"default_utility\": \"gemma\"", "\"default_utility\": \"gemma4-12b\"");
    swap("{\"chat\": \"gemma\", \"utility\": {\"backend\": \"gemma\"",
         "{\"chat\": \"gemma4-12b\", \"utility\": {\"backend\": \"gemma4-12b\"");
    swap(
        "{\"backend\": \"gemma\", \"rerank\": \"gemma\", \"graph\": {\"extract_backend\": "
        "\"gemma\"}}",
        "{\"backend\": \"gemma4-12b\", \"rerank\": \"gemma4-12b\", \"graph\": "
        "{\"extract_backend\": \"gemma4-12b\"}}");
    swap("\"judge_backend\": \"gemma\"", "\"judge_backend\": \"gemma4-12b\"");
    swap("\"backend\": \"gemma\",\n      \"sources\"",
         "\"backend\": \"gemma4-12b\",\n      \"sources\"");
    swap("\"log_consent\": true, \"backend\": \"gemma\"}",
         "\"log_consent\": true, \"backend\": \"gemma4-12b\"}");
    CHECK(renamed == expected);
    CHECK(apogee::harness::rename_backend(renamed, "gemma4-12b", "gemma") == kRenameJsonc);
    const apogee::harness::Config config = apogee::harness::parse_config(renamed, "renamed");
    CHECK(config.find_backend("gemma4-12b") != nullptr);
    CHECK(config.mcp_server_names() == std::vector<std::string>{"gemma"});
}

// --- 55f: names, lists and hashes, in JSONC as in YAML ------------------------

TEST_CASE("every name add-backend takes lives a whole life in JSONC, and the rest are refused",
          "[config_edit][jsonc][names]") {
    using namespace apogee::harness;
    // The refusal is one rule, said alike in both formats.
    const auto refusal = [](std::string_view content) {
        try {
            (void)append_backend(content, "qwen@home", mock_backend(), false);
        } catch (const ConfigEditError& e) {
            return std::string{e.what()};
        }
        return std::string{};
    };
    CHECK_THAT(refusal(kJsonc), Catch::Matchers::ContainsSubstring(
                                    "is not one both config formats can edit -- a name is "
                                    "letters, digits, '_', '-', '.' and '/'"));
    CHECK_THAT(refusal(kJsonc), Catch::Matchers::ContainsSubstring("config rename-backend"));
    CHECK(refusal(kJsonc) == refusal(kYaml));

    // Each accepted name: added, edited, cleared and deleted, the file back
    // to its bytes -- the bridge writes it plain, so the editor finds it.
    for (const std::string_view name :
         {"qwen-home", "qwen_home", "Qwen3.5-9B-Q4KM", "org/model", "_x", "9b", "true", "a.b"}) {
        INFO(name);
        const std::string added = append_backend(kJsonc, name, mock_backend(), false);
        const std::string edited = set_backend_field(added, name, "max_tokens", "512");
        CHECK(parse_config(edited, "<test>").find_backend(name)->max_tokens == 512);
        CHECK(set_backend_field(edited, name, "max_tokens", "") == added);
        CHECK(delete_backend(added, name) == kJsonc);
        const std::string renamed = rename_backend(added, name, "renamed");
        CHECK(rename_backend(renamed, "renamed", name) == added);
    }
}

TEST_CASE("an odd name a JSONC config already holds is reached through rename-backend",
          "[config_edit][jsonc][names]") {
    using namespace apogee::harness;
    // Hand-written, or added before 55f: the bridge quotes the key, so the line
    // editor cannot find it -- but rename-backend reads the JSONC itself.
    constexpr std::string_view kOdd = R"({
  "backends": {
    "qwen@home": {"type": "mock"} // the odd one
  }
}
)";
    CHECK_THROWS_WITH(set_backend_field(kOdd, "qwen@home", "max_tokens", "9"),
                      "backend 'qwen@home' not found in config");
    const std::string renamed = rename_backend(kOdd, "qwen@home", "qwen-home");
    CHECK(renamed == R"({
  "backends": {
    "qwen-home": {"type": "mock"} // the odd one
  }
}
)");
    const std::string set = set_backend_field(renamed, "qwen-home", "max_tokens", "9");
    CHECK(parse_config(set, "<test>").find_backend("qwen-home")->max_tokens == 9);
    CHECK_THAT(set, Catch::Matchers::ContainsSubstring("// the odd one"));
    CHECK(parse_config(delete_backend(set, "qwen-home"), "<test>").backends.empty());
}

TEST_CASE("a list field and a hash-bearing value edit alike in JSONC and in YAML",
          "[config_edit][jsonc][block-list][hash][parity]") {
    using namespace apogee::harness;
    // The JSONC twin of a YAML block list: the bridge writes it as a flow
    // list, so JSONC never met the fold -- held here to mean what YAML now
    // means.
    constexpr std::string_view kListJsonc = R"({
  "backends": {
    "claude": {
      "type": "anthropic", // the vendor's own API
      "api_key": "sk-test-literal",
      "server_tools": ["web_search", "code_execution"], // the vendor's tools
      "max_tokens": 1024 // the cap
    }
  },
  "tools": {
    "search": {
      "provider": "searxng",
      "url": "http://127.0.0.1:8888/#frag" // my instance
    }
  }
}
)";
    constexpr std::string_view kListYaml = R"(backends:
  claude:
    type: anthropic   # the vendor's own API
    api_key: sk-test-literal
    server_tools:   # the vendor's tools
      - web_search
      - code_execution
    max_tokens: 1024   # the cap
tools:
  search:
    provider: searxng
    url: "http://127.0.0.1:8888/#frag"   # my instance
)";
    const auto both = [&](const std::function<std::string(std::string_view)>& verb) {
        const std::string json = verb(kListJsonc);
        const std::string yaml = verb(kListYaml);
        CHECK_THAT(json, Catch::Matchers::ContainsSubstring("// the cap"));
        CHECK_THAT(yaml, Catch::Matchers::ContainsSubstring("# the cap"));
        return std::pair{parse_config(yaml, "yaml"), parse_config(json, "jsonc")};
    };
    {
        const auto [yaml, json] = both([](std::string_view content) {
            return set_backend_field(content, "claude", "server_tools", "");
        });
        CHECK(json.find_backend("claude")->server_tools.empty());
        CHECK(yaml.find_backend("claude")->server_tools.empty());
        CHECK(json.find_backend("claude")->api_key == "sk-test-literal");
        CHECK(yaml.find_backend("claude")->api_key == "sk-test-literal");
        CHECK(json.find_backend("claude")->max_tokens == yaml.find_backend("claude")->max_tokens);
    }
    {
        const auto [yaml, json] = both([](std::string_view content) {
            return set_backend_field(content, "claude", "server_tools", "web_search");
        });
        CHECK(json.find_backend("claude")->server_tools == std::vector<std::string>{"web_search"});
        CHECK(yaml.find_backend("claude")->server_tools ==
              json.find_backend("claude")->server_tools);
        CHECK(yaml.find_backend("claude")->api_key == json.find_backend("claude")->api_key);
    }
    {
        const auto [yaml, json] = both([](std::string_view content) {
            return set_scalar(content, "tools.search.url", "http://10.0.0.2:8080/#x");
        });
        CHECK(json.tools.search.url == "http://10.0.0.2:8080/#x");
        CHECK(yaml.tools.search.url == json.tools.search.url);
    }
}
