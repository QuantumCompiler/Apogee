#include "harness/config.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "support/env_guard.h"

using apogee::harness::BackendType;
using apogee::harness::Config;
using apogee::harness::ConfigError;
using apogee::harness::StatusMode;
using apogee::testing::EnvGuard;
using apogee::testing::EnvUnsetGuard;
using apogee::testing::TempDir;

namespace {

constexpr std::string_view kFullSample = R"YAML(models:
  default: claude
  default_embedding: embedder
  default_extraction: extractor

paths:
  gguf_dir: /models/gguf
  hf_dir:
  mcp_dir: "${HOME}/mcp"
  embeddings_dir: /data/embeddings

backends:
  claude:
    type: anthropic
    api_key: "${TEST_APOGEE_KEY}"
    model: claude-sonnet-5
    context_size: 200000
    max_tokens: 8192
    temperature: 0.7
    system_prompt: "You are helpful."

  embedder:
    type: llamacpp
    model_path: /models/embed.gguf
    context_size: 2048

  extractor:
    type: openai
    api_key: plain-key
    model: gpt-5

status_mode: verbose
color: false
)YAML";

Config load_text(std::string_view text) {
    return apogee::harness::parse_config(text, "<test>");
}

}  // namespace

TEST_CASE("a full config parses into typed structs", "[config]") {
    const EnvGuard key{"TEST_APOGEE_KEY", "sk-from-env"};
    const Config config = load_text(kFullSample);

    REQUIRE(config.backends.size() == 3);
    REQUIRE(config.models.default_backend == "claude");
    REQUIRE(config.models.default_embedding == "embedder");
    REQUIRE(config.models.default_extraction == "extractor");
    REQUIRE(config.status_mode == StatusMode::Verbose);
    REQUIRE_FALSE(config.color);

    const auto* claude = config.find_backend("claude");
    REQUIRE(claude != nullptr);
    CHECK(claude->type == BackendType::Anthropic);
    CHECK(claude->model == "claude-sonnet-5");
    CHECK(claude->context_size == 200000);
    CHECK(claude->max_tokens == 8192);
    REQUIRE(claude->temperature.has_value());
    CHECK(*claude->temperature == 0.7);
    CHECK(claude->system_prompt == "You are helpful.");

    const auto* embedder = config.find_backend("embedder");
    REQUIRE(embedder != nullptr);
    CHECK(embedder->type == BackendType::LlamaCpp);
    CHECK(embedder->model_path == "/models/embed.gguf");
    CHECK_FALSE(embedder->max_tokens.has_value());

    CHECK(config.paths.gguf_dir == "/models/gguf");
    CHECK(config.paths.hf_dir.empty());
}

TEST_CASE("${ENV_VAR} is expanded on read, in every string field", "[config]") {
    const EnvGuard key{"TEST_APOGEE_KEY", "sk-from-env"};
    const EnvGuard home{"HOME", "/home/tester"};

    const Config config = load_text(kFullSample);
    CHECK(config.find_backend("claude")->api_key == "sk-from-env");
    CHECK(config.paths.mcp_dir == "/home/tester/mcp");
}

TEST_CASE("expand_env handles the awkward cases", "[config]") {
    const EnvGuard set{"TEST_APOGEE_VALUE", "xyz"};
    const EnvUnsetGuard missing{"TEST_APOGEE_UNDEFINED"};

    using apogee::harness::expand_env;

    CHECK(expand_env("${TEST_APOGEE_VALUE}") == "xyz");
    CHECK(expand_env("a-${TEST_APOGEE_VALUE}-b") == "a-xyz-b");

    // An undefined variable expands to empty rather than failing: a config
    // naming a key you have not set must still LOAD.
    CHECK(expand_env("${TEST_APOGEE_UNDEFINED}").empty());
    CHECK(expand_env("[${TEST_APOGEE_UNDEFINED}]") == "[]");

    CHECK(expand_env("$$HOME") == "$HOME");  // escaped
    CHECK(expand_env("$HOME") == "$HOME");   // no braces: literal
    CHECK(expand_env("${unterminated") == "${unterminated");
    CHECK(expand_env("plain").empty() == false);
    CHECK(expand_env("").empty());
    CHECK(expand_env("100%") == "100%");
}

TEST_CASE("an empty or keyless config loads", "[config]") {
    // "A fresh install with no API keys and no models passes apogee check."
    CHECK(load_text("").backends.empty());
    CHECK(load_text("# only a comment\n").backends.empty());
    CHECK(load_text("backends:\n").backends.empty());

    const Config config = load_text("backends:\n  local:\n    type: llamacpp\n");
    REQUIRE(config.backends.size() == 1);
    CHECK(config.find_backend("local")->api_key.empty());
}

TEST_CASE("an unknown backend type is a clear error naming the accepted set", "[config]") {
    try {
        load_text("backends:\n  x:\n    type: banana\n");
        FAIL("expected a ConfigError");
    } catch (const ConfigError& e) {
        const std::string message = e.what();
        CHECK(message.find("banana") != std::string::npos);
        CHECK(message.find("anthropic") != std::string::npos);
        CHECK(message.find("llamacpp") != std::string::npos);
    }
}

TEST_CASE("a missing type is an error, not a silent default", "[config]") {
    CHECK_THROWS_AS(load_text("backends:\n  x:\n    model: foo\n"), ConfigError);
}

TEST_CASE("names differing only by case are rejected at load, never merged", "[config]") {
    // Ommi's Viper-lowercasing bug, made explicit: there these became ONE
    // backend and one of the two definitions silently won.
    try {
        load_text("backends:\n  Qwen:\n    type: mock\n  qwen:\n    type: mock\n");
        FAIL("expected a ConfigError");
    } catch (const ConfigError& e) {
        const std::string message = e.what();
        CHECK(message.find("Qwen") != std::string::npos);
        CHECK(message.find("qwen") != std::string::npos);
    }
}

TEST_CASE("backend lookup is case-insensitive", "[config]") {
    const Config config = load_text("backends:\n  Claude:\n    type: mock\n");
    CHECK(config.find_backend("claude") != nullptr);
    CHECK(config.find_backend("CLAUDE") != nullptr);
    CHECK(config.find_backend("clyde") == nullptr);
    // ...but the name is reported as the file spells it.
    REQUIRE(config.backend_names() == std::vector<std::string>{"Claude"});
}

TEST_CASE("scalar fields reject values of the wrong shape", "[config]") {
    CHECK_THROWS_AS(load_text("backends:\n  x:\n    type: mock\n    context_size: big\n"),
                    ConfigError);
    CHECK_THROWS_AS(load_text("backends:\n  x:\n    type: mock\n    temperature: warm\n"),
                    ConfigError);
    CHECK_THROWS_AS(load_text("status_mode: sideways\n"), ConfigError);
    CHECK_THROWS_AS(load_text("color: maybe\n"), ConfigError);
    CHECK_THROWS_AS(load_text("backends: 3\n"), ConfigError);
    CHECK_THROWS_AS(load_text("backends:\n  x: 3\n"), ConfigError);
}

TEST_CASE("garbage input yields a message, never a crash", "[config]") {
    // The loader's contract under abuse: every one of these must throw
    // ConfigError specifically -- not std::bad_alloc, not a YAML::Exception
    // escaping the boundary, and above all not a segfault.
    const std::vector<std::string> garbage{
        "backends:\n  bad: [unclosed\n",
        "\t\t\t",
        "{{{{{{",
        "backends:\n  - not\n  - a\n  - map\n",
        "backends:\n  :\n    type: mock\n",
        std::string(64 * 1024, 'a'),
        std::string("backends:\n  x:\n    type: mock\n    model: ") + std::string(9000, 'z') + "\n",
        "a: *undefined_anchor\n",
        "\xff\xfe\x00\x01 binary",
        "---\n---\n---\n",
    };

    for (const std::string& input : garbage) {
        try {
            const Config config = apogee::harness::parse_config(input, "<garbage>");
            (void)config;  // parsing successfully is fine too -- not crashing is the point
        } catch (const ConfigError&) {
            // The expected failure mode.
        }
    }
    SUCCEED("no crash on malformed input");
}

TEST_CASE("every backend type round-trips through its name", "[config]") {
    // Keeps the enum, the name table, and the parser honest as later items
    // widen the type set -- a new enumerator with no table row fails here.
    const auto names = apogee::harness::backend_type_names();
    REQUIRE(names.size() == 9);
    // Named rather than only counted: a miscount is obvious, but a row
    // silently RENAMED would keep the count and break every config using it.
    CHECK(std::find(names.begin(), names.end(), "claude-cli") != names.end());
    CHECK(std::find(names.begin(), names.end(), "ollama-cli") != names.end());
    CHECK(std::find(names.begin(), names.end(), "codex-cli") != names.end());
    for (const std::string_view name : names) {
        const auto type = apogee::harness::backend_type_from_string(name);
        REQUIRE(type.has_value());
        CHECK(apogee::harness::to_string(*type) == name);
    }
    CHECK_FALSE(apogee::harness::backend_type_from_string("nope").has_value());
    // Case-sensitive on purpose: `type: Anthropic` should be a clear error
    // rather than quietly accepted in a file people diff.
    CHECK_FALSE(apogee::harness::backend_type_from_string("Anthropic").has_value());
}

TEST_CASE("the shipped sample config byte-matches the embedded template", "[config][template]") {
    // Ommi's template-drift test, ported. It exists because the failure it
    // catches is invisible: `config init` quietly stops writing an option the
    // docs still describe, and nobody notices until a user asks why the key
    // they read about does nothing.
    const std::filesystem::path sample = std::filesystem::path{APOGEE_ASSETS_DIR} / "config.yaml";
    REQUIRE(std::filesystem::exists(sample));

    std::ifstream in(sample, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();

    CHECK(buffer.str() == apogee::harness::config_template());
}

TEST_CASE("the shipped template loads, with zero backends and zero keys", "[config][template]") {
    const Config config = load_text(apogee::harness::config_template());
    CHECK(config.backends.empty());
    CHECK(config.models.default_backend.empty());
    CHECK(config.status_mode == StatusMode::Line);
    CHECK(config.color);
}

TEST_CASE("load_config reports an unreadable file by name", "[config]") {
    const TempDir dir{"config-missing"};
    const std::filesystem::path missing = std::filesystem::path{dir.path()} / "nope.yaml";
    try {
        (void)apogee::harness::load_config(missing);
        FAIL("expected a ConfigError");
    } catch (const ConfigError& e) {
        const std::string message = e.what();
        CHECK(message.find("nope.yaml") != std::string::npos);
        CHECK(message.find("config init") != std::string::npos);
    }
}

// --- embeddings: and auto_rag: --------------------------------------------------

TEST_CASE("embeddings entries parse into typed settings", "[config][embeddings]") {
    const auto config = apogee::harness::parse_config(R"YAML(
embeddings:
  adrs:
    chunk_size: 768
    chunk_overlap: 96
    description: "Architecture decision records"
  notes:
)YAML",
                                                      "<test>");
    REQUIRE(config.embeddings.size() == 2);

    const apogee::harness::EmbeddingConfig* adrs = config.find_embedding("adrs");
    REQUIRE(adrs != nullptr);
    CHECK(adrs->chunk_size == 768);
    CHECK(adrs->chunk_overlap == 96);
    CHECK(adrs->description == "Architecture decision records");

    // A bare name is a valid entry: registered, with every setting defaulted.
    const apogee::harness::EmbeddingConfig* notes = config.find_embedding("notes");
    REQUIRE(notes != nullptr);
    CHECK_FALSE(notes->chunk_size.has_value());
    CHECK_FALSE(notes->chunk_overlap.has_value());

    CHECK(config.embedding_names() == std::vector<std::string>{"adrs", "notes"});
    CHECK(config.find_embedding("nope") == nullptr);
}

TEST_CASE("a collection's graph block parses, defaults, and validates hops at load",
          "[config][embeddings][graph]") {
    const auto config = apogee::harness::parse_config(R"YAML(
embeddings:
  adrs:
    graph:
      enabled: true
      extract_backend: local
      hops: 2
      max_entities: 4
  notes:
    chunk_size: 512
)YAML",
                                                      "<test>");
    const apogee::harness::EmbeddingConfig* adrs = config.find_embedding("adrs");
    REQUIRE(adrs != nullptr);
    CHECK(adrs->graph.enabled);
    CHECK(adrs->graph.extract_backend == "local");
    CHECK(adrs->graph.hops == 2);
    CHECK(adrs->graph.max_entities == 4);
    // Absent: off, the role, 1 hop, 8 entities.
    const apogee::harness::EmbeddingConfig* notes = config.find_embedding("notes");
    REQUIRE(notes != nullptr);
    CHECK_FALSE(notes->graph.enabled);
    CHECK(notes->graph.extract_backend.empty());
    CHECK(notes->graph.hops == 1);
    CHECK(notes->graph.max_entities == 8);

    // Out of range is a load failure naming the key, never a silent clamp.
    CHECK_THROWS_WITH(
        apogee::harness::parse_config("embeddings:\n  a:\n    graph:\n      hops: 3\n", "<t>"),
        Catch::Matchers::ContainsSubstring("embeddings.a.graph.hops"));
    CHECK_THROWS_WITH(
        apogee::harness::parse_config("embeddings:\n  a:\n    graph:\n      hops: 0\n", "<t>"),
        Catch::Matchers::ContainsSubstring("out of range"));
    CHECK_THROWS_WITH(apogee::harness::parse_config(
                          "embeddings:\n  a:\n    graph:\n      max_entities: 0\n", "<t>"),
                      Catch::Matchers::ContainsSubstring("max_entities"));
    CHECK_THROWS_WITH(apogee::harness::parse_config(
                          "embeddings:\n  a:\n    graph:\n      enabled: maybe\n", "<t>"),
                      Catch::Matchers::ContainsSubstring("enabled"));
    CHECK_THROWS_WITH(apogee::harness::parse_config("embeddings:\n  a:\n    graph: yes\n", "<t>"),
                      Catch::Matchers::ContainsSubstring("expected a mapping"));
}

TEST_CASE("collection names are looked up and collide case-insensitively", "[config][embeddings]") {
    // The same rule as backends: two names that fold together would be the
    // same file on a case-insensitive filesystem, so the loader names both
    // rather than letting one shadow the other.
    const auto config = apogee::harness::parse_config("embeddings:\n  Notes:\n", "<test>");
    CHECK(config.find_embedding("notes") != nullptr);

    CHECK_THROWS_WITH(apogee::harness::parse_config("embeddings:\n  Notes:\n  notes:\n", "<test>"),
                      Catch::Matchers::ContainsSubstring("collides"));
}

TEST_CASE("embeddings settings of the wrong shape are rejected by name", "[config][embeddings]") {
    CHECK_THROWS_WITH(
        apogee::harness::parse_config("embeddings:\n  a:\n    chunk_size: lots\n", "<test>"),
        Catch::Matchers::ContainsSubstring("embeddings.a.chunk_size"));
    CHECK_THROWS_WITH(apogee::harness::parse_config("embeddings:\n  - a\n", "<test>"),
                      Catch::Matchers::ContainsSubstring("embeddings:"));
}

TEST_CASE("auto_rag is a top-level scalar that defaults to off", "[config][embeddings]") {
    CHECK(apogee::harness::parse_config("", "<test>").auto_rag.empty());
    CHECK(apogee::harness::parse_config("auto_rag: notes\n", "<test>").auto_rag == "notes");
}

TEST_CASE("embedding_model is a backend field with no default in the loader",
          "[config][embeddings]") {
    const auto config = apogee::harness::parse_config(
        "backends:\n  gpt:\n    type: openai\n    api_key: k\n    embedding_model: "
        "text-embedding-3-large\n  plain:\n    type: openai\n    api_key: k\n",
        "<test>");
    CHECK(config.find_backend("gpt")->embedding_model == "text-embedding-3-large");
    // Empty, not a vendor default: the loader reports what the file says and
    // the provider decides what empty means, so the default can differ per
    // vendor without the loader knowing any vendor.
    CHECK(config.find_backend("plain")->embedding_model.empty());
}

TEST_CASE("a collection's backend, retriever and rerank pins parse as written",
          "[config][embeddings]") {
    const auto config = apogee::harness::parse_config(
        "embeddings:\n  adrs:\n    backend: embedder\n    retriever: vector\n    rerank: haiku\n",
        "<test>");
    const apogee::harness::EmbeddingConfig* adrs = config.find_embedding("adrs");
    REQUIRE(adrs != nullptr);
    CHECK(adrs->backend == "embedder");
    CHECK(adrs->retriever == "vector");
    CHECK(adrs->rerank == "haiku");
    // The LOADER does not validate the retriever spelling -- that is `check`'s
    // job, with the one shared validator -- so a typo loads and is reported,
    // rather than refusing to load a config over one field.
    CHECK(apogee::harness::parse_config("embeddings:\n  a:\n    retriever: hybird\n", "<test>")
              .find_embedding("a")
              ->retriever == "hybird");
}

TEST_CASE("permissions: and tools: parse, and a bad level fails at load",
          "[config][permissions][tools]") {
    const Config config = load_text(
        "permissions:\n  write_file: allow\n  run_command: deny\n"
        "tools:\n  fs_root: /srv/work\n  disabled: [shell, rag]\n");
    CHECK(config.permissions.level("write_file") == apogee::harness::PermissionLevel::Allow);
    CHECK(config.permissions.level("run_command") == apogee::harness::PermissionLevel::Deny);
    CHECK(config.permissions.level("delete_file") == apogee::harness::PermissionLevel::Ask);
    CHECK(config.tools.fs_root == "/srv/work");
    CHECK(config.tools.is_disabled("shell"));
    CHECK(config.tools.is_disabled("rag"));
    CHECK_FALSE(config.tools.is_disabled("git"));

    // A level that is not one of the three is refused, naming them -- a
    // typo must not silently read as "not allow".
    CHECK_THROWS_AS(load_text("permissions:\n  write_file: yes\n"), ConfigError);
    CHECK_THROWS_AS(load_text("permissions: allow\n"), ConfigError);
    CHECK_THROWS_AS(load_text("tools:\n  disabled: shell\n"), ConfigError);
    CHECK(apogee::harness::permission_level_from_string("allow") ==
          apogee::harness::PermissionLevel::Allow);
    CHECK_FALSE(apogee::harness::permission_level_from_string("Allow").has_value());
    CHECK(apogee::harness::to_string(apogee::harness::PermissionLevel::Deny) == "deny");

    // The shipped template lists every destructive tool at ask.
    const Config shipped = load_text(apogee::harness::config_template());
    for (const char* tool :
         {"write_file", "delete_file", "run_command", "write_note", "delete_note"}) {
        INFO(tool);
        CHECK(shipped.permissions.levels.contains(tool));
        CHECK(shipped.permissions.level(tool) == apogee::harness::PermissionLevel::Ask);
    }
    CHECK(shipped.tools.fs_root.empty());
    CHECK(shipped.tools.disabled.empty());
}

TEST_CASE("mcp_servers: parses, expands ~ and ${ENV}, validates enabled, refuses collisions",
          "[config][mcp]") {
    const apogee::testing::EnvGuard guard{"APOGEE_MCP_TEST_DIR", "/srv/mcp"};
    const Config config = load_text(
        "mcp_servers:\n"
        "  weather:\n    command: ~/mcp/weather/server.py\n    args: [\"--root\", "
        "\"${APOGEE_MCP_TEST_DIR}\"]\n"
        "    env: [\"KEY=${APOGEE_MCP_TEST_DIR}/x\"]\n    enabled: false\n"
        "  bare:\n    command: some-server\n");
    const apogee::harness::McpServerConfig* weather = config.find_mcp_server("weather");
    REQUIRE(weather != nullptr);
    CHECK(weather->command.find('~') == std::string::npos);
    CHECK(weather->command.ends_with("/mcp/weather/server.py"));
    CHECK(weather->args == std::vector<std::string>{"--root", "/srv/mcp"});
    CHECK(weather->env == std::vector<std::string>{"KEY=/srv/mcp/x"});
    CHECK_FALSE(weather->enabled);
    REQUIRE(config.find_mcp_server("BARE") != nullptr);  // case-insensitive, like backends
    CHECK(config.find_mcp_server("bare")->enabled);      // the default
    CHECK(config.mcp_server_names() == std::vector<std::string>{"bare", "weather"});

    CHECK_THROWS_AS(load_text("mcp_servers:\n  a:\n    enabled: maybe\n"), ConfigError);
    CHECK_THROWS_AS(load_text("mcp_servers:\n  a:\n    args: notalist\n"), ConfigError);
    CHECK_THROWS_AS(load_text("mcp_servers:\n  a:\n    command: x\n  A:\n    command: y\n"),
                    ConfigError);
    CHECK(load_text(apogee::harness::config_template()).mcp_servers.empty());
}

TEST_CASE("agents: parses every field, validates the enums, resolves paths, refuses collisions",
          "[config][agents]") {
    const apogee::testing::EnvGuard guard{"APOGEE_AGENT_TEST_DIR", "/srv/agents"};
    const Config config = load_text(
        "agents:\n"
        "  reviewer:\n"
        "    description: \"Reviews code\"\n"
        "    model: local\n"
        "    prompts: [prompts/reviewer.txt, \"${APOGEE_AGENT_TEST_DIR}/extra.txt\"]\n"
        "    schemas: [~/schemas/reviewer-output.json]\n"
        "    output_format: json\n"
        "    tools: all\n"
        "    mcp: [weather, Git]\n"
        "    questions: true\n"
        "    collection: adrs\n"
        "    save_dir: ~/reports\n"
        "    save_filename: review\n"
        "    save_subdir: reviewer\n"
        "  bare:\n");
    const apogee::harness::AgentConfig* reviewer = config.find_agent("reviewer");
    REQUIRE(reviewer != nullptr);
    CHECK(reviewer->description == "Reviews code");
    CHECK(reviewer->model == "local");
    REQUIRE(reviewer->prompts.size() == 2);
    CHECK(reviewer->prompts[0] == "prompts/reviewer.txt");   // relative: left for the loader
    CHECK(reviewer->prompts[1] == "/srv/agents/extra.txt");  // ${ENV} expanded
    REQUIRE(reviewer->schemas.size() == 1);
    CHECK(reviewer->schemas[0].find('~') == std::string::npos);  // ~ expanded
    CHECK(reviewer->schemas[0].ends_with("/schemas/reviewer-output.json"));
    CHECK(reviewer->output_format == apogee::harness::AgentOutputFormat::Json);
    CHECK(reviewer->tools == apogee::harness::AgentToolPolicy::All);
    CHECK(reviewer->mcp == std::vector<std::string>{"weather", "Git"});
    CHECK(reviewer->questions);
    CHECK(reviewer->collection == "adrs");
    CHECK(reviewer->save_dir.ends_with("/reports"));
    CHECK(reviewer->save_filename == "review");
    CHECK(reviewer->save_subdir == "reviewer");

    // The defaults: read-only, auto, no questions -- the safe policy.
    const apogee::harness::AgentConfig* bare = config.find_agent("BARE");
    REQUIRE(bare != nullptr);
    CHECK(bare->tools == apogee::harness::AgentToolPolicy::ReadOnly);
    CHECK(bare->output_format == apogee::harness::AgentOutputFormat::Auto);
    CHECK_FALSE(bare->questions);
    CHECK(config.agent_names() == std::vector<std::string>{"bare", "reviewer"});

    // Every enum validated at load: a typo never silently means `all`.
    CHECK_THROWS_AS(load_text("agents:\n  a:\n    tools: sometimes\n"), ConfigError);
    CHECK_THROWS_AS(load_text("agents:\n  a:\n    output_format: yaml\n"), ConfigError);
    CHECK_THROWS_AS(load_text("agents:\n  a:\n    questions: maybe\n"), ConfigError);
    CHECK_THROWS_AS(load_text("agents:\n  a:\n    prompts: notalist\n"), ConfigError);
    CHECK_THROWS_AS(load_text("agents:\n  a:\n    tools: all\n  A:\n    tools: none\n"),
                    ConfigError);
    CHECK(load_text(apogee::harness::config_template()).agents.empty());
    CHECK(apogee::harness::agent_tool_policy_from_string("read-only") ==
          apogee::harness::AgentToolPolicy::ReadOnly);
    CHECK(apogee::harness::to_string(apogee::harness::AgentToolPolicy::None) == "none");
    CHECK(apogee::harness::to_string(apogee::harness::AgentOutputFormat::Markdown) == "markdown");
}

TEST_CASE("is_vendor_cli names exactly the four CLI types", "[config][vendor]") {
    using apogee::harness::BackendType;
    using apogee::harness::is_vendor_cli;
    CHECK(is_vendor_cli(BackendType::ClaudeCli));
    CHECK(is_vendor_cli(BackendType::CodexCli));
    CHECK(is_vendor_cli(BackendType::GeminiCli));
    CHECK(is_vendor_cli(BackendType::OllamaCli));
    CHECK_FALSE(is_vendor_cli(BackendType::Anthropic));
    CHECK_FALSE(is_vendor_cli(BackendType::OpenAI));
    CHECK_FALSE(is_vendor_cli(BackendType::Google));
    CHECK_FALSE(is_vendor_cli(BackendType::LlamaCpp));
    CHECK_FALSE(is_vendor_cli(BackendType::Mock));
}

TEST_CASE("knowledge: parses auto_capture and db, defaults the collection, and refuses a path",
          "[config][knowledge]") {
    const apogee::harness::Config empty = apogee::harness::parse_config("", "<test>");
    CHECK_FALSE(empty.knowledge.auto_capture);
    CHECK(empty.knowledge.db.empty());
    CHECK(empty.knowledge.collection() == "knowledge");

    const apogee::harness::Config set = apogee::harness::parse_config(
        "knowledge:\n  auto_capture: true\n  db: decisions\n", "<test>");
    CHECK(set.knowledge.auto_capture);
    CHECK(set.knowledge.db == "decisions");
    CHECK(set.knowledge.collection() == "decisions");

    CHECK_THROWS_AS(
        apogee::harness::parse_config("knowledge:\n  auto_capture: yes-please\n", "<t>"),
        apogee::harness::ConfigError);
    CHECK_THROWS_AS(apogee::harness::parse_config("knowledge: 7\n", "<t>"),
                    apogee::harness::ConfigError);
    CHECK_THROWS_AS(apogee::harness::parse_config("knowledge:\n  db: ../escape\n", "<t>"),
                    apogee::harness::ConfigError);
    CHECK_THROWS_AS(apogee::harness::parse_config("knowledge:\n  db: a/b\n", "<t>"),
                    apogee::harness::ConfigError);
    // The shipped template documents the section, commented out.
    CHECK(std::string{apogee::harness::config_template()}.find("# knowledge:") !=
          std::string::npos);
    CHECK(std::string{apogee::harness::config_template()}.find("#   auto_capture: false") !=
          std::string::npos);
}

TEST_CASE(
    "a graphs: section parses in file order, defaults, validates, and refuses a fold "
    "collision",
    "[config][graphs]") {
    const auto config = apogee::harness::parse_config(R"YAML(
graphs:
  work:
    collections: [docs, meetings]
    extract_backend: local
    hops: 2
    max_entities: 4
  bare:
    collections: [tickets]
)YAML",
                                                      "<test>");
    REQUIRE(config.graphs.size() == 2);
    CHECK(config.graph_names() == std::vector<std::string>{"work", "bare"});
    const apogee::harness::NamedGraphConfig* work = config.find_graph("WORK");
    REQUIRE(work != nullptr);
    CHECK(work->collections == std::vector<std::string>{"docs", "meetings"});
    CHECK(work->extract_backend == "local");
    CHECK(work->hops == 2);
    CHECK(work->max_entities == 4);
    const apogee::harness::NamedGraphConfig* bare = config.find_graph("bare");
    REQUIRE(bare != nullptr);
    CHECK(bare->extract_backend.empty());
    CHECK(bare->hops == 1);
    CHECK(bare->max_entities == 8);
    CHECK(config.find_graph("nope") == nullptr);
    CHECK(apogee::harness::parse_config("", "<test>").graphs.empty());

    CHECK_THROWS_AS(apogee::harness::parse_config("graphs:\n  w:\n    hops: 3\n", "<test>"),
                    apogee::harness::ConfigError);
    CHECK_THROWS_AS(apogee::harness::parse_config("graphs:\n  w:\n    max_entities: 0\n", "<test>"),
                    apogee::harness::ConfigError);
    CHECK_THROWS_AS(
        apogee::harness::parse_config("graphs:\n  w:\n    collections: docs\n", "<test>"),
        apogee::harness::ConfigError);
    CHECK_THROWS_AS(
        apogee::harness::parse_config("graphs:\n  Work:\n    collections: [a]\n  work:\n"
                                      "    collections: [b]\n",
                                      "<test>"),
        apogee::harness::ConfigError);
    // The shipped template ships the section commented out.
    CHECK(load_text(apogee::harness::config_template()).graphs.empty());
}
