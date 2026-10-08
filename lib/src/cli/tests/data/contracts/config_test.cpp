#include "contracts/config.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
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
    // The hazard, made explicit: a loader that lowercases keys makes these ONE
    // backend, and one of the two definitions silently wins.
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
    REQUIRE(names.size() == 10);
    // Named rather than only counted: a miscount is obvious, but a row
    // silently RENAMED would keep the count and break every config using it.
    CHECK(std::find(names.begin(), names.end(), "claude-cli") != names.end());
    CHECK(std::find(names.begin(), names.end(), "ollama-cli") != names.end());
    CHECK(std::find(names.begin(), names.end(), "codex-cli") != names.end());
    CHECK(std::find(names.begin(), names.end(), "mlx") != names.end());
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

TEST_CASE("the helper role pointers parse, and count in the pointer comparison",
          "[config][roles]") {
    const Config config = load_text(
        "models:\n  default: a\n  default_vision: v\n  default_transcription: t\n"
        "  default_utility: u\nbackends:\n  a:\n    type: mock\n");
    CHECK(config.models.default_vision == "v");
    CHECK(config.models.default_transcription == "t");
    CHECK(config.models.default_utility == "u");

    // The admin plane's restart_required compares them all.
    for (auto field : {&apogee::harness::ModelsConfig::default_vision,
                       &apogee::harness::ModelsConfig::default_transcription,
                       &apogee::harness::ModelsConfig::default_utility}) {
        apogee::harness::ModelsConfig changed = config.models;
        changed.*field = "other";
        CHECK_FALSE(changed == config.models);
    }
}

TEST_CASE("a local backend's cache_type parses, and anything else is refused by name",
          "[config][cache]") {
    using apogee::harness::KvCacheType;
    const Config config = load_text(
        "backends:\n"
        "  eight:\n    type: llamacpp\n    model_path: /m/a.gguf\n    cache_type: q8_0\n"
        "  four:\n    type: llamacpp\n    model_path: /m/a.gguf\n    cache_type: q4_0\n"
        "  full:\n    type: llamacpp\n    model_path: /m/a.gguf\n    cache_type: f16\n"
        "  unset:\n    type: llamacpp\n    model_path: /m/a.gguf\n");
    CHECK(config.find_backend("eight")->cache_type == KvCacheType::Q8_0);
    CHECK(config.find_backend("four")->cache_type == KvCacheType::Q4_0);
    CHECK(config.find_backend("full")->cache_type == KvCacheType::F16);
    // Unset stays unset: the backend's default is not the config's to fix.
    CHECK_FALSE(config.find_backend("unset")->cache_type.has_value());

    for (const char* bad : {"q8", "Q8_0", "bf16", "q5_1"}) {
        try {
            (void)load_text(std::string{"backends:\n  x:\n    type: llamacpp\n    cache_type: "} +
                            bad + "\n");
            FAIL("expected a ConfigError for " << bad);
        } catch (const ConfigError& e) {
            const std::string message = e.what();
            CHECK(message.find(std::string{"'"} + bad + "'") != std::string::npos);
            CHECK(message.find("backends.x.cache_type") != std::string::npos);
            CHECK(message.find("f16, q8_0, q4_0") != std::string::npos);
        }
    }

    for (const KvCacheType type : {KvCacheType::F16, KvCacheType::Q8_0, KvCacheType::Q4_0}) {
        CHECK(apogee::harness::cache_type_from_string(apogee::harness::to_string(type)) == type);
    }
}

TEST_CASE("the shipped sample config byte-matches the embedded template", "[config][template]") {
    // The template-drift test. It exists because the failure it
    // catches is invisible: `config init` quietly stops writing an option the
    // docs still describe, and nobody notices until a user asks why the key
    // they read about does nothing.
    const std::filesystem::path sample = std::filesystem::path{APOGEE_ASSETS_DIR} / "config.json";
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
         {"write_file", "edit_file", "delete_file", "run_command", "write_note", "delete_note"}) {
        INFO(tool);
        CHECK(shipped.permissions.levels.contains(tool));
        CHECK(shipped.permissions.level(tool) == apogee::harness::PermissionLevel::Ask);
    }
    CHECK(shipped.tools.fs_root.empty());
    CHECK(shipped.tools.disabled.empty());
    CHECK_FALSE(shipped.tools.search.configured());  // commented out: search is off
}

TEST_CASE("tools.search: read as written, results bounded, off when absent", "[config][search]") {
    const apogee::testing::EnvGuard guard{"APOGEE_SEARCH_TEST_URL", "http://127.0.0.1:8888"};
    const Config config = load_text(
        "tools:\n  search:\n    provider: searxng\n    url: ${APOGEE_SEARCH_TEST_URL}\n"
        "    results: 8\n");
    CHECK(config.tools.search.configured());
    CHECK(config.tools.search.provider == "searxng");
    CHECK(config.tools.search.url == "http://127.0.0.1:8888");
    CHECK(config.tools.search.results == 8);
    CHECK(load_text("tools:\n  search:\n    url: http://h:1\n").tools.search.results == 5);
    CHECK_FALSE(load_text("tools:\n  allowed_hosts: []\n").tools.search.configured());
    // An unknown provider loads (check names it); a bad count or shape does not.
    CHECK(load_text("tools:\n  search:\n    provider: brave\n").tools.search.provider == "brave");
    CHECK_THROWS_AS(load_text("tools:\n  search:\n    url: x\n    results: 0\n"), ConfigError);
    CHECK_THROWS_AS(load_text("tools:\n  search:\n    url: x\n    results: 21\n"), ConfigError);
    CHECK_THROWS_AS(load_text("tools:\n  search:\n    url: x\n    results: many\n"), ConfigError);
    CHECK_THROWS_AS(load_text("tools:\n  search: http://h:1\n"), ConfigError);
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
    CHECK(std::string{apogee::harness::config_template()}.find("// \"knowledge\": {") !=
          std::string::npos);
    CHECK(std::string{apogee::harness::config_template()}.find("//   \"auto_capture\": false,") !=
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

TEST_CASE("a graphs: entry may hold source trees and languages, a tree's variables expanded",
          "[config][graphs][code]") {
    const EnvGuard root{"APOGEE_TEST_SRC_ROOT", "/home/someone"};
    const auto config = apogee::harness::parse_config(R"YAML(
graphs:
  code:
    sources: ["${APOGEE_TEST_SRC_ROOT}/src/app", /opt/lib]
    languages: [cpp, python]
  mixed:
    collections: [docs]
    sources: [/srv/repo]
)YAML",
                                                      "<test>");
    const apogee::harness::NamedGraphConfig* code = config.find_graph("code");
    REQUIRE(code != nullptr);
    CHECK(code->collections.empty());
    CHECK(code->sources == std::vector<std::string>{"/home/someone/src/app", "/opt/lib"});
    CHECK(code->languages == std::vector<std::string>{"cpp", "python"});
    CHECK(config.find_graph("mixed")->collections == std::vector<std::string>{"docs"});
    CHECK(config.find_graph("mixed")->sources == std::vector<std::string>{"/srv/repo"});
    CHECK_THROWS_AS(apogee::harness::parse_config("graphs:\n  w:\n    sources: /x\n", "<test>"),
                    apogee::harness::ConfigError);
}

TEST_CASE("the training section carries the interpreter the environment is seeded from",
          "[harness][config][training]") {
    const apogee::harness::Config config = apogee::harness::parse_config(
        "training:\n  python: /opt/homebrew/bin/python3.12\n", "<test>");
    CHECK(config.training.python == "/opt/homebrew/bin/python3.12");
    CHECK(apogee::harness::parse_config("backends: {}\n", "<test>").training.python.empty());
    CHECK_THROWS_AS(apogee::harness::parse_config("training: notamap\n", "<test>"),
                    apogee::harness::ConfigError);
    CHECK_THROWS_AS(apogee::harness::parse_config("training:\n  python: [a, b]\n", "<test>"),
                    apogee::harness::ConfigError);
}

TEST_CASE("the training section's run fields parse with their defaults and refuse bad values",
          "[harness][config][training]") {
    const apogee::harness::Config defaults =
        apogee::harness::parse_config("backends: {}\n", "<test>");
    CHECK(defaults.training.trainer.empty());
    CHECK(defaults.training.judge_backend.empty());
    CHECK(defaults.training.eval_suite_path.empty());
    CHECK(defaults.training.retain_versions == 3);
    CHECK(defaults.training.gate_mode.empty());
    CHECK(defaults.training.effective_gate_mode() == "hard");

    const apogee::harness::Config config = apogee::harness::parse_config(
        "training:\n  trainer: peft\n  judge_backend: paid\n  eval_suite_path: ~/suite.jsonl\n"
        "  retain_versions: 0\n  gate_mode: soft\n",
        "<test>");
    CHECK(config.training.trainer == "peft");
    CHECK(config.training.judge_backend == "paid");
    CHECK(config.training.eval_suite_path == "~/suite.jsonl");
    CHECK(config.training.retain_versions == 0);
    CHECK(config.training.effective_gate_mode() == "soft");

    CHECK_THROWS_AS(apogee::harness::parse_config("training:\n  trainer: cuda\n", "<test>"),
                    apogee::harness::ConfigError);
    CHECK_THROWS_AS(apogee::harness::parse_config("training:\n  gate_mode: maybe\n", "<test>"),
                    apogee::harness::ConfigError);
    CHECK_THROWS_AS(apogee::harness::parse_config("training:\n  retain_versions: -1\n", "<test>"),
                    apogee::harness::ConfigError);
    CHECK_THROWS_AS(apogee::harness::parse_config("training:\n  retain_versions: many\n", "<test>"),
                    apogee::harness::ConfigError);
    CHECK(
        apogee::harness::parse_config("training:\n  trainer: mock\n", "<test>").training.trainer ==
        "mock");
    // The shipped template documents the fields as comments: nothing set.
    CHECK(load_text(apogee::harness::config_template()).training.retain_versions == 3);
}

TEST_CASE(
    "the training section's pipelines, regimes and cycle parse with their defaults, and "
    "every load-time refusal names its key",
    "[harness][config][training][pipelines]") {
    const Config config = load_text(
        "training:\n"
        "  pipelines:\n"
        "    skills:\n"
        "      student: Qwen--Qwen2.5-0.5B\n"
        "      stages:\n"
        "        - name: instructions\n"
        "          dataset: instructions\n"
        "          eval_suite: instruction-following\n"
        "          iters: 500\n"
        "        - name: reasoning\n"
        "          dataset: ~/maths.jsonl\n"
        "          eval_suite: reasoning\n"
        "          method: qlora\n"
        "          batch_size: 2\n"
        "          num_layers: 8\n"
        "          grad_checkpoint: true\n"
        "          mask_prompt: true\n"
        "          rehearsal_fraction: 0.1\n"
        "  regimes:\n"
        "    everything:\n"
        "      teacher: paid\n"
        "      student: snap\n"
        "      kits: [instruction-following, reasoning]\n"
        "      count: 50\n"
        "      promote_as: tuned\n"
        "      iters: 300\n"
        "      temperature: 0.7\n"
        "  cycle:\n"
        "    pipeline: skills\n"
        "    backend: nightly\n"
        "    anchor_version: 2\n"
        "    regression_threshold: 0.05\n"
        "    circuit_breaker_k: 5\n"
        "    judge_backend: paid\n"
        "    sources:\n"
        "      - type: directory\n"
        "        dir: ~/queue\n"
        "      - type: sessions\n"
        "        log_consent: true\n"
        "        backend: nightly\n"
        "        since: 2026-09-01\n");
    REQUIRE(config.training.pipelines.size() == 1);
    const apogee::harness::PipelineSpec& skills = config.training.pipelines.at("skills");
    CHECK(skills.name == "skills");
    CHECK(skills.student == "Qwen--Qwen2.5-0.5B");
    REQUIRE(skills.stages.size() == 2);
    CHECK(skills.stages[0].name == "instructions");
    CHECK(skills.stages[0].dataset == "instructions");
    CHECK(skills.stages[0].eval_suite == "instruction-following");
    CHECK(skills.stages[0].iters == 500);
    CHECK(skills.stages[0].method.empty());
    CHECK(skills.stages[0].rehearsal_fraction == 0.0);
    CHECK_FALSE(skills.stages[0].mask_prompt);
    CHECK(skills.stages[1].method == "qlora");
    CHECK(skills.stages[1].dataset.ends_with("/maths.jsonl"));
    CHECK(skills.stages[1].dataset.front() != '~');
    CHECK(skills.stages[1].batch_size == 2);
    CHECK(skills.stages[1].num_layers == 8);
    CHECK(skills.stages[1].grad_checkpoint);
    CHECK(skills.stages[1].mask_prompt);
    CHECK(skills.stages[1].rehearsal_fraction == 0.1);

    REQUIRE(config.training.regimes.size() == 1);
    const apogee::harness::RegimeSpec& everything = config.training.regimes.at("everything");
    CHECK(everything.name == "everything");
    CHECK(everything.teacher == "paid");
    CHECK(everything.student == "snap");
    CHECK(everything.kits == std::vector<std::string>{"instruction-following", "reasoning"});
    CHECK(everything.count == 50);
    CHECK(everything.promote_as == "tuned");
    CHECK(everything.iters == 300);
    CHECK(everything.temperature == 0.7);

    const apogee::harness::CycleConfig& cycle = config.training.cycle;
    CHECK(cycle.configured());
    CHECK(cycle.pipeline == "skills");
    CHECK(cycle.backend == "nightly");
    CHECK(cycle.anchor_version == 2);
    CHECK(cycle.regression_threshold == 0.05);
    CHECK(cycle.circuit_breaker_k == 5);
    CHECK(cycle.judge_backend == "paid");
    REQUIRE(cycle.sources.size() == 2);
    CHECK(cycle.sources[0].type == "directory");
    CHECK(cycle.sources[0].dir.ends_with("/queue"));
    CHECK_FALSE(cycle.sources[0].log_consent);
    CHECK(cycle.sources[1].type == "sessions");
    CHECK(cycle.sources[1].log_consent);
    CHECK(cycle.sources[1].backend == "nightly");
    CHECK(cycle.sources[1].since == "2026-09-01");

    // The defaults: strict no-regression, the breaker at 3, nothing configured.
    const Config defaults = load_text("training:\n  cycle:\n    pipeline: skills\n");
    CHECK(defaults.training.cycle.regression_threshold == 0.0);
    CHECK(defaults.training.cycle.circuit_breaker_k == 3);
    CHECK(defaults.training.cycle.anchor_version == 0);
    CHECK_FALSE(defaults.training.cycle.configured());
    CHECK(load_text("backends: {}\n").training.pipelines.empty());
    CHECK(load_text(apogee::harness::config_template()).training.pipelines.empty());
    CHECK(load_text(apogee::harness::config_template()).training.regimes.empty());
    CHECK_FALSE(load_text(apogee::harness::config_template()).training.cycle.configured());

    // The refusals, each naming its key.
    auto refused = [](std::string_view text, std::string_view needle) {
        try {
            (void)load_text(text);
        } catch (const apogee::harness::ConfigError& e) {
            INFO(e.what());
            return std::string{e.what()}.find(needle) != std::string::npos;
        }
        return false;
    };
    CHECK(refused("training:\n  pipelines:\n    p:\n      student: s\n", "at least one stage"));
    CHECK(refused("training:\n  pipelines:\n    p:\n      stages: []\n", "at least one stage"));
    CHECK(
        refused("training:\n  pipelines:\n    p:\n      stages:\n        - dataset: d\n"
                "          eval_suite: e\n",
                "needs a name"));
    CHECK(
        refused("training:\n  pipelines:\n    p:\n      stages:\n        - name: a\n"
                "          eval_suite: e\n",
                "needs a dataset"));
    CHECK(
        refused("training:\n  pipelines:\n    p:\n      stages:\n        - name: a\n"
                "          dataset: d\n",
                "needs an eval_suite"));
    CHECK(
        refused("training:\n  pipelines:\n    p:\n      stages:\n        - name: a\n"
                "          dataset: d\n          eval_suite: e\n          method: full\n",
                "method: unknown value 'full'"));
    CHECK(
        refused("training:\n  pipelines:\n    p:\n      stages:\n        - name: a\n"
                "          dataset: d\n          eval_suite: e\n          iters: -1\n",
                "iters: must be 0 or positive"));
    CHECK(
        refused("training:\n  pipelines:\n    p:\n      stages:\n        - name: a\n"
                "          dataset: d\n          eval_suite: e\n"
                "          rehearsal_fraction: 1.5\n",
                "rehearsal_fraction: must be between"));
    CHECK(refused("training:\n  pipelines: [a]\n", "training.pipelines: expected a mapping"));
    CHECK(refused("training:\n  regimes:\n    r:\n      temperature: 3\n",
                  "temperature: must be between"));
    CHECK(refused("training:\n  regimes:\n    r:\n      kits: [a, '']\n", "empty kit name"));
    CHECK(refused("training:\n  regimes:\n    r:\n      count: -2\n", "count: must be 0"));
    CHECK(refused("training:\n  cycle:\n    sources:\n      - type: logs\n",
                  "type: unknown value 'logs'"));
    CHECK(refused("training:\n  cycle:\n    sources:\n      - type: sessions\n",
                  "log_consent: true"));
    CHECK(refused("training:\n  cycle:\n    sources:\n      - type: sessions\n", "privacy"));
    CHECK(refused("training:\n  cycle:\n    sources:\n      - type: sessions\n",
                  "self-reinforcement"));
    CHECK(
        refused("training:\n  cycle:\n    sources:\n      - type: sessions\n"
                "        log_consent: false\n",
                "log_consent: true"));
    CHECK(refused("training:\n  cycle:\n    circuit_breaker_k: -1\n",
                  "circuit_breaker_k: must be 0 or positive"));
    CHECK(refused("training:\n  cycle:\n    regression_threshold: 1.5\n",
                  "regression_threshold: must be between"));
    CHECK(refused("training:\n  cycle:\n    regression_threshold: -0.1\n",
                  "regression_threshold: must be between"));
    CHECK(refused("training:\n  cycle:\n    anchor_version: -3\n",
                  "anchor_version: must be 0 or positive"));
    CHECK(
        refused("training:\n  cycle:\n    sources:\n      - type: sessions\n"
                "        log_consent: true\n        since: soon\n",
                "since: 'soon' is not a YYYY-MM-DD date"));
    CHECK(refused("training:\n  cycle:\n    sources: directory\n", "sources: expected a list"));
    CHECK(refused("training:\n  cycle: yes\n", "training.cycle: expected a block"));

    // A spec file goes through the same parser, the stem naming a nameless one.
    const apogee::harness::PipelineSpec from_file = apogee::harness::parse_pipeline_spec(
        "student: snap\nstages:\n  - name: a\n    dataset: d\n    eval_suite: e\n", "mine.yaml",
        "mine");
    CHECK(from_file.name == "mine");
    CHECK(from_file.stages.size() == 1);
    CHECK(apogee::harness::parse_pipeline_spec("name: named\nstages:\n  - name: a\n    dataset: "
                                               "d\n    eval_suite: e\n",
                                               "f", "fallback")
              .name == "named");
    CHECK_THROWS_AS(apogee::harness::parse_pipeline_spec("stages: [", "bad.yaml", "bad"),
                    apogee::harness::ConfigError);
    CHECK_THROWS_AS(apogee::harness::parse_pipeline_spec("student: s\n", "bad.yaml", "bad"),
                    apogee::harness::ConfigError);
    const apogee::harness::RegimeSpec regime =
        apogee::harness::parse_regime_spec("teacher: t\nkits: [a]\n", "r.yaml", "r");
    CHECK(regime.name == "r");
    CHECK(regime.teacher == "t");
    CHECK(regime.kits == std::vector<std::string>{"a"});
    CHECK_THROWS_AS(apogee::harness::parse_regime_spec("- a\n", "r.yaml", "r"),
                    apogee::harness::ConfigError);
}

TEST_CASE("a backend's sampling knobs parse, and each out-of-range value is refused by name",
          "[config][sampling]") {
    // 26h: beside the temperature, the knobs a local model samples with.
    const Config config = load_text(
        "backends:\n"
        "  local:\n    type: llamacpp\n    model_path: /m/a.gguf\n"
        "    top_p: 0.8\n    top_k: 20\n    min_p: 0.05\n    repeat_penalty: 1.1\n"
        "    presence_penalty: -0.5\n    seed: 42\n"
        "  bare:\n    type: llamacpp\n    model_path: /m/a.gguf\n");
    const auto* local = config.find_backend("local");
    REQUIRE(local != nullptr);
    CHECK(local->top_p == 0.8);
    CHECK(local->top_k == 20);
    CHECK(local->min_p == 0.05);
    CHECK(local->repeat_penalty == 1.1);
    CHECK(local->presence_penalty == -0.5);
    CHECK(local->seed == 42);
    // Unset stays unset: the model file and its family are asked next.
    const auto* bare = config.find_backend("bare");
    CHECK_FALSE(bare->top_p.has_value());
    CHECK_FALSE(bare->top_k.has_value());
    CHECK_FALSE(bare->seed.has_value());

    const std::pair<const char*, const char*> refused[] = {
        {"top_p: 0", "top_p"},
        {"top_p: 1.5", "top_p"},
        {"top_k: -1", "top_k"},
        {"min_p: 2", "min_p"},
        {"repeat_penalty: 0", "repeat_penalty"},
        {"presence_penalty: 3", "presence_penalty"},
        {"seed: -1", "seed"},
        {"seed: 4294967295", "seed"},
        {"top_p: warm", "top_p"},
    };
    for (const auto& [line, key] : refused) {
        INFO(line);
        try {
            (void)load_text(std::string{"backends:\n  x:\n    type: llamacpp\n    "} + line + "\n");
            FAIL("expected a ConfigError");
        } catch (const ConfigError& e) {
            CHECK(std::string{e.what()}.find(std::string{"backends.x."} + key) !=
                  std::string::npos);
        }
    }
    // The edges a rule allows.
    const Config edges = load_text(
        "backends:\n  x:\n    type: llamacpp\n    top_p: 1\n    top_k: 0\n    min_p: 0\n"
        "    presence_penalty: 2\n    seed: 4294967294\n");
    CHECK(edges.find_backend("x")->top_p == 1.0);
    CHECK(edges.find_backend("x")->seed == 4294967294);
}

TEST_CASE("a backend's thinking default and budget parse, and a bad one is refused by name",
          "[config][thinking]") {
    // 26i: whether the backend's model reasons first, and for how long.
    const Config config = load_text(
        "backends:\n"
        "  local:\n    type: llamacpp\n    model_path: /m/a.gguf\n"
        "    thinking: auto\n    thinking_budget: 2048\n"
        "  quiet:\n    type: llamacpp\n    model_path: /m/a.gguf\n    thinking: off\n"
        "  bare:\n    type: llamacpp\n    model_path: /m/a.gguf\n");
    CHECK(config.find_backend("local")->thinking == apogee::harness::ThinkingMode::Auto);
    CHECK(config.find_backend("local")->thinking_budget == 2048);
    CHECK(config.find_backend("quiet")->thinking == apogee::harness::ThinkingMode::Off);
    // Unset is the model's own: thinking on, no budget.
    CHECK_FALSE(config.find_backend("bare")->thinking.has_value());
    CHECK_FALSE(config.find_backend("bare")->thinking_budget.has_value());

    const std::pair<const char*, const char*> refused[] = {
        {"thinking: sometimes", "is not a thinking mode (accepted: on, off, auto)"},
        {"thinking_budget: -1", "thinking_budget"},
        {"thinking_budget: 1000001", "thinking_budget"},
        {"thinking_budget: lots", "thinking_budget"},
    };
    for (const auto& [line, said] : refused) {
        INFO(line);
        try {
            (void)load_text(std::string{"backends:\n  x:\n    type: llamacpp\n    "} + line + "\n");
            FAIL("expected a ConfigError");
        } catch (const ConfigError& e) {
            CHECK(std::string{e.what()}.find(said) != std::string::npos);
            CHECK(std::string{e.what()}.find("backends.x.thinking") != std::string::npos);
        }
    }
    // A budget of 0 is a budget: answer straight away.
    CHECK(load_text("backends:\n  x:\n    type: llamacpp\n    thinking_budget: 0\n")
              .find_backend("x")
              ->thinking_budget == 0);
}

TEST_CASE("memory.recall is on unless the config turns it off", "[config][recall]") {
    // 26l: on in chat by default (the user's call).
    CHECK(load_text("backends:\n  x:\n    type: mock\n").memory.recall);
    // A section that does not say keeps it on.
    CHECK(load_text("memory: {}\n").memory.recall);
    CHECK_FALSE(load_text("memory:\n  recall: false\n").memory.recall);
    CHECK_THROWS_AS(load_text("memory:\n  recall: sometimes\n"), ConfigError);
    CHECK_THROWS_AS(load_text("memory: yes\n"), ConfigError);
}

TEST_CASE("attachments.graph is a method word, unset unless the config says one",
          "[config][attachments]") {
    using apogee::harness::AttachmentGraphMethod;
    // 27p: unset is each surface's built-in -- never read as `code`.
    CHECK_FALSE(load_text("backends:\n  x:\n    type: mock\n").attachments.graph.has_value());
    CHECK_FALSE(load_text("attachments: {}\n").attachments.graph.has_value());
    CHECK_FALSE(load_text("attachments:\n  graph:\n").attachments.graph.has_value());
    // The shipped template shows the block commented: a fresh install is unset.
    CHECK_FALSE(
        load_text(std::string{apogee::harness::config_template()}).attachments.graph.has_value());
    CHECK(std::string{apogee::harness::config_template()}.find(
              "// \"attachments\": {\n  //   \"graph\": \"code\"\n  // },\n") != std::string::npos);
    // Block and flow forms; `off` is the word, never YAML 1.1's boolean.
    CHECK(load_text("attachments:\n  graph: off\n").attachments.graph ==
          AttachmentGraphMethod::Off);
    CHECK(load_text("attachments: { graph: off }\n").attachments.graph ==
          AttachmentGraphMethod::Off);
    CHECK(load_text("attachments:\n  graph: code\n").attachments.graph ==
          AttachmentGraphMethod::Code);
    CHECK(load_text("attachments:\n  graph: \"off\"\n").attachments.graph ==
          AttachmentGraphMethod::Off);
    // Anything else is refused naming the set.
    CHECK_THROWS_WITH(load_text("attachments:\n  graph: tree\n"),
                      Catch::Matchers::ContainsSubstring(
                          "attachments.graph: unknown value 'tree' (accepted: code, off)"));
    CHECK_THROWS_WITH(
        load_text("attachments:\n  graph: false\n"),
        Catch::Matchers::ContainsSubstring("unknown value 'false' (accepted: code, off)"));
    CHECK_THROWS_AS(load_text("attachments:\n  graph: [code]\n"), ConfigError);
    CHECK_THROWS_AS(load_text("attachments: code\n"), ConfigError);
    // The words, one place.
    CHECK(apogee::harness::attachment_graph_method_names() ==
          std::vector<std::string_view>{"code", "off"});
    for (const std::string_view name : apogee::harness::attachment_graph_method_names()) {
        const std::optional<AttachmentGraphMethod> method =
            apogee::harness::attachment_graph_method_from_string(name);
        REQUIRE(method.has_value());
        CHECK(apogee::harness::to_string(*method) == name);
    }
    CHECK_FALSE(apogee::harness::attachment_graph_method_from_string("Code").has_value());
    CHECK_FALSE(apogee::harness::attachment_graph_method_from_string("").has_value());
    CHECK(apogee::harness::attachment_graph_values_message("", "x") ==
          "unknown value 'x' (accepted: code, off)");
}

namespace {

/// A config with three mock backends and the suites under test (27d).
std::string with_suites(std::string_view suites, std::string_view models = {}) {
    return std::string{"models:\n  default: root\n"} + std::string{models} +
           "backends:\n  root:\n    type: mock\n  helper:\n    type: mock\n"
           "  embedder:\n    type: mock\nsuites:\n" +
           std::string{suites};
}

}  // namespace

TEST_CASE("a suite's members parse in the short and the long form", "[config][suites]") {
    const Config config = load_text(with_suites(R"YAML(  research:
    description: Deep work, chores on the small one
    members:
      chat: root
      utility:
        backend: helper
        context_size: 4096
        toolset: [fs, git]
      embedding: embedder
  bare:
)YAML",
                                                "  default_suite: research\n"));
    REQUIRE(config.find_suite("research") != nullptr);
    const apogee::harness::SuiteConfig& research = *config.find_suite("RESEARCH");
    CHECK(research.description == "Deep work, chores on the small one");
    CHECK(research.members.size() == 3);
    CHECK(research.members.at("chat").backend == "root");
    CHECK_FALSE(research.members.at("chat").pins());
    CHECK(research.members.at("utility").backend == "helper");
    CHECK(research.members.at("utility").context_size == 4096);
    CHECK(research.members.at("utility").toolset == std::vector<std::string>{"fs", "git"});
    // A suite with nothing under it loads, naming no members.
    REQUIRE(config.find_suite("bare") != nullptr);
    CHECK(config.find_suite("bare")->members.empty());
    CHECK(config.suite_names() == std::vector<std::string>{"bare", "research"});
    CHECK(config.models.default_suite == "research");
    CHECK(apogee::harness::active_suite(config) == &research);
    // An empty toolset is a pin: no tools.
    const Config none = load_text(with_suites(
        "  quiet:\n    members:\n      chat:\n        backend: root\n        toolset: []\n"));
    CHECK(none.find_suite("quiet")->members.at("chat").toolset == std::vector<std::string>{});
}

TEST_CASE("a suite that cannot hold is refused at load, by name", "[config][suites]") {
    const std::vector<std::pair<std::string, std::string>> refused{
        {"  s:\n    members:\n      root: root\n",
         "suites.s.members.root: not a role (accepted: chat, embedding, extraction, vision, "
         "transcription, utility)"},
        {"  s:\n    members:\n      chat: \"\"\n", "suites.s.members.chat: names no backend"},
        {"  s:\n    members:\n      chat:\n        context_size: 4096\n",
         "suites.s.members.chat: names no backend"},
        {"  s:\n    members:\n      chat:\n        backend: root\n        context_size: 0\n",
         "suites.s.members.chat.context_size: must be a positive number of tokens"},
        {"  s:\n    members:\n      chat:\n        backend: root\n        toolset: [fs, web, "
         "browser]\n",
         "suites.s.members.chat.toolset: 'browser' is not a toolset"},
        {"  s:\n    members: [root]\n", "suites.s.members: expected a mapping"},
        {"  s: root\n", "suites.s: expected a mapping"},
        // One backend is one window: two members pinning it two ways cannot hold.
        {"  s:\n    members:\n      utility:\n        backend: helper\n        context_size: "
         "4096\n      vision:\n        backend: HELPER\n        context_size: 8192\n",
         "suites.s: 'helper' is pinned two ways, by utility and vision -- one backend runs at "
         "one window"},
        {"  s:\n    members:\n      chat:\n        backend: root\n        toolset: [fs]\n"
         "      utility:\n        backend: root\n        toolset: [git]\n",
         "one backend runs at one toolset"},
        {"  off:\n    members:\n      chat: root\n", "suites: 'off' is reserved"},
        {"  Off:\n    members:\n      chat: root\n", "suites: 'Off' is reserved"},
        {"  fast:\n    members:\n      chat: root\n  FAST:\n    members:\n      chat: root\n",
         "collides with"},
    };
    for (const auto& [suites, said] : refused) {
        INFO(suites);
        try {
            (void)load_text(with_suites(suites));
            FAIL("expected a ConfigError");
        } catch (const ConfigError& e) {
            CHECK_THAT(std::string{e.what()}, Catch::Matchers::ContainsSubstring(said));
        }
    }
    // Two members on one backend agreeing -- or one pinning, one not -- is fine.
    CHECK_NOTHROW(load_text(with_suites(
        "  s:\n    members:\n      chat: helper\n      utility:\n        backend: helper\n"
        "        context_size: 4096\n")));
}

TEST_CASE("a default suite naming nothing is refused where it is written", "[config][suites]") {
    try {
        (void)load_text(with_suites("  research:\n    members:\n      chat: root\n",
                                    "  default_suite: reserch\n"));
        FAIL("expected a ConfigError");
    } catch (const ConfigError& e) {
        CHECK_THAT(std::string{e.what()},
                   Catch::Matchers::ContainsSubstring(
                       "models.default_suite: no suite named 'reserch' under suites: "
                       "(configured: research)"));
    }
    try {
        (void)load_text("models:\n  default_suite: research\n");
        FAIL("expected a ConfigError");
    } catch (const ConfigError& e) {
        CHECK_THAT(std::string{e.what()},
                   Catch::Matchers::ContainsSubstring("none is configured -- 'apogee config "
                                                      "add-suite'"));
    }
    // Whitespace is no name: no suite, as on every pointer.
    CHECK(apogee::harness::active_suite(load_text("models:\n  default_suite: \"  \"\n")) ==
          nullptr);
    // And no suites, no default: the config of today.
    const Config today = load_text("models:\n  default: root\n");
    CHECK(today.suites.empty());
    CHECK(today.models.default_suite.empty());
    CHECK(apogee::harness::active_suite(today) == nullptr);
}

TEST_CASE("the active suite's pins hold on its members' backends and nowhere else",
          "[config][suites]") {
    Config config = load_text(with_suites(R"YAML(  research:
    members:
      chat: root
      utility:
        backend: helper
        context_size: 4096
        toolset: [fs, git]
)YAML",
                                          "  default_suite: research\n"));
    using apogee::harness::backend_as_run;
    using apogee::harness::suite_pins;
    CHECK(suite_pins(config, "helper").context_size == 4096);
    CHECK(suite_pins(config, "HELPER").toolset == std::vector<std::string>{"fs", "git"});
    CHECK(suite_pins(config, "root") == apogee::harness::MemberPins{});
    CHECK(suite_pins(config, "embedder") == apogee::harness::MemberPins{});
    CHECK(backend_as_run(config, "helper").context_size == 4096);
    CHECK(backend_as_run(config, "helper").type == BackendType::Mock);
    CHECK_FALSE(backend_as_run(config, "root").context_size.has_value());

    // The suite pins only while it is active: the entry as written otherwise.
    config.models.default_suite.clear();
    CHECK(suite_pins(config, "helper") == apogee::harness::MemberPins{});
    CHECK_FALSE(backend_as_run(config, "helper").context_size.has_value());

    // A pin replaces the entry's own window; nothing else of the entry moves.
    config = load_text(
        "backends:\n  helper:\n    type: mock\n    context_size: 32768\n    max_tokens: 99\n"
        "suites:\n  s:\n    members:\n      utility:\n        backend: helper\n"
        "        context_size: 2048\nmodels:\n  default_suite: s\n");
    CHECK(backend_as_run(config, "helper").context_size == 2048);
    CHECK(backend_as_run(config, "helper").max_tokens == 99);
    CHECK(config.find_backend("helper")->context_size == 32768);
}

TEST_CASE("the suite vocabularies are the roles and the toolsets", "[config][suites]") {
    const auto roles = apogee::harness::suite_role_names();
    CHECK(std::vector<std::string_view>(roles.begin(), roles.end()) ==
          std::vector<std::string_view>{"chat", "embedding", "extraction", "vision",
                                        "transcription", "utility"});
    const auto toolsets = apogee::harness::suite_toolset_names();
    CHECK(
        std::vector<std::string_view>(toolsets.begin(), toolsets.end()) ==
        std::vector<std::string_view>{"fs", "shell", "git", "notes", "rag", "graph", "web", "mcp"});
    // The shipped template documents suites and configures none.
    const Config shipped = load_text(apogee::harness::config_template());
    CHECK(shipped.suites.empty());
    CHECK(shipped.models.default_suite.empty());
    CHECK(std::string{apogee::harness::config_template()}.find("// \"suites\": {") !=
          std::string::npos);
}

TEST_CASE("a suite names the members its root may consult, and the caps", "[config][suites]") {
    const Config config = load_text(with_suites(R"YAML(  research:
    members:
      chat: root
      utility:
        backend: helper
        context_size: 4096
      extraction: embedder
    consultable: [utility, extraction]
    consult_caps:
      per_turn: 2
      answer_tokens: 256
  plain:
    members:
      chat: root
)YAML"));
    const apogee::harness::SuiteConfig& research = *config.find_suite("research");
    CHECK(research.consultable == std::vector<std::string>{"utility", "extraction"});
    CHECK(research.consult_caps.per_turn == 2);
    CHECK_FALSE(research.consult_caps.brief_tokens.has_value());
    CHECK(research.consult_caps.answer_tokens == 256);
    // Each cap unset takes its named default.
    CHECK(
        apogee::harness::consult_limits(research.consult_caps) ==
        apogee::harness::ConsultLimits{.per_turn = 2, .brief_tokens = 1024, .answer_tokens = 256});
    const apogee::harness::SuiteConfig& plain = *config.find_suite("plain");
    CHECK(plain.consultable.empty());
    CHECK_FALSE(plain.consult_caps.any());
    CHECK(
        apogee::harness::consult_limits(plain.consult_caps) ==
        apogee::harness::ConsultLimits{.per_turn = 4, .brief_tokens = 1024, .answer_tokens = 512});
    // A block list reads the same as a flow one.
    const Config block = load_text(with_suites(
        "  s:\n    members:\n      utility: helper\n    consultable:\n      - utility\n"));
    CHECK(block.find_suite("s")->consultable == std::vector<std::string>{"utility"});
}

TEST_CASE("a consult that cannot hold is refused at load, by name", "[config][suites]") {
    const std::string members = "  s:\n    members:\n      chat: root\n      utility: helper\n";
    const std::vector<std::pair<std::string, std::string>> refused{
        {members + "    consultable: [chat]\n", "suites.s.consultable: 'chat' is the root itself"},
        {"  s:\n    members:\n      embedding: embedder\n    consultable: [embedding]\n",
         "suites.s.consultable: 'embedding' turns text into vectors and answers nothing"},
        {members + "    consultable: [helper]\n",
         "suites.s.consultable: 'helper' is not a role a suite can consult (accepted: "
         "extraction, vision, transcription, utility)"},
        {members + "    consultable: [vision]\n",
         "suites.s.consultable: 'vision' has no member in this suite"},
        {members + "    consultable: [utility, utility]\n",
         "suites.s.consultable: 'utility' is listed twice"},
        {"  s:\n    consultable: [utility]\n", "'utility' has no member in this suite"},
        {members + "    consultable: utility\n", "suites.s.consultable: expected a list"},
        {members + "    consult_caps:\n      per_turn: 0\n",
         "suites.s.consult_caps.per_turn: must be a positive whole number"},
        {members + "    consult_caps:\n      brief_tokens: lots\n",
         "suites.s.consult_caps.brief_tokens: expected a whole number"},
        {members + "    consult_caps:\n      per_call: 3\n",
         "suites.s.consult_caps.per_call: not a cap (accepted: per_turn, brief_tokens, "
         "answer_tokens)"},
        {members + "    consult_caps: 3\n", "suites.s.consult_caps: expected a mapping"},
    };
    for (const auto& [suites, said] : refused) {
        INFO(suites);
        try {
            (void)load_text(with_suites(suites));
            FAIL("expected a ConfigError");
        } catch (const ConfigError& e) {
            CHECK_THAT(std::string{e.what()}, Catch::Matchers::ContainsSubstring(said));
        }
    }
    // Caps with no consultable member load: validation (27g) spends from them.
    CHECK_NOTHROW(load_text(with_suites(members + "    consult_caps:\n      per_turn: 1\n")));
}

TEST_CASE("the consult vocabularies are the answering roles and the three caps",
          "[config][suites]") {
    const auto roles = apogee::harness::consultable_role_names();
    CHECK(std::vector<std::string_view>(roles.begin(), roles.end()) ==
          std::vector<std::string_view>{"extraction", "vision", "transcription", "utility"});
    const auto caps = apogee::harness::consult_cap_names();
    CHECK(std::vector<std::string_view>(caps.begin(), caps.end()) ==
          std::vector<std::string_view>{"per_turn", "brief_tokens", "answer_tokens"});
    CHECK(apogee::harness::kConsultsPerTurn == 4);
    CHECK(apogee::harness::kConsultBriefTokens == 1024);
    CHECK(apogee::harness::kConsultAnswerTokens == 512);
}

TEST_CASE("a suite's validate: block opts each seam in, and names its verifier",
          "[config][suites][validate]") {
    const Config config = load_text(with_suites(R"YAML(  checked:
    members:
      chat: root
      utility: helper
      extraction: embedder
    validate:
      verifier: extraction
      tool_args: on
      extraction: off
      answers: always
  defaults:
    members:
      chat: root
      utility: helper
    validate:
      tool_args: true
  plain:
    members:
      chat: root
)YAML"));
    const apogee::harness::SuiteConfig& checked = *config.find_suite("checked");
    CHECK(checked.validate.verifier == "extraction");
    CHECK(checked.validate.tool_args == true);
    CHECK(checked.validate.extraction == false);
    CHECK(checked.validate.answers == "always");
    CHECK(apogee::harness::validate_policy(checked.validate) ==
          apogee::harness::ValidatePolicy{.verifier = "extraction",
                                          .tool_args = true,
                                          .extraction = false,
                                          .answers_always = true});
    // Each unset field takes its default: the utility member, the seams off,
    // answers on request.
    const apogee::harness::SuiteConfig& defaults = *config.find_suite("defaults");
    CHECK_FALSE(defaults.validate.verifier.has_value());
    CHECK(apogee::harness::validate_policy(defaults.validate) ==
          apogee::harness::ValidatePolicy{.verifier = "utility", .tool_args = true});
    // No block: nothing on, and the policy is the defaults.
    const apogee::harness::SuiteConfig& plain = *config.find_suite("plain");
    CHECK_FALSE(plain.validate.any());
    CHECK(apogee::harness::validate_policy(plain.validate) == apogee::harness::ValidatePolicy{});
    // The vocabularies.
    const auto seams = apogee::harness::validate_seam_names();
    CHECK(std::vector<std::string_view>(seams.begin(), seams.end()) ==
          std::vector<std::string_view>{"tool_args", "extraction", "answers"});
    const auto whens = apogee::harness::answer_check_names();
    CHECK(std::vector<std::string_view>(whens.begin(), whens.end()) ==
          std::vector<std::string_view>{"request", "always"});
    CHECK(apogee::harness::kDefaultVerifier == "utility");
}

TEST_CASE("a suite's orchestrate: switch reads as a validate seam does, off when absent",
          "[config][suites][orchestrate]") {
    const std::string members = "    members:\n      chat: root\n      utility: helper\n";
    const Config config =
        load_text(with_suites("  plays:\n" + members + "    orchestrate: true\n" + "  word:\n" +
                              members + "    orchestrate: on\n" + "  quiet:\n" + members +
                              "    orchestrate: false\n" + "  plain:\n" + members));
    CHECK(config.find_suite("plays")->orchestrate);
    CHECK(config.find_suite("word")->orchestrate);
    CHECK_FALSE(config.find_suite("quiet")->orchestrate);
    CHECK_FALSE(config.find_suite("plain")->orchestrate);
    try {
        (void)load_text(with_suites("  s:\n" + members + "    orchestrate: maybe\n"));
        FAIL("expected a ConfigError");
    } catch (const ConfigError& e) {
        CHECK_THAT(std::string{e.what()},
                   Catch::Matchers::ContainsSubstring(
                       "suites.s.orchestrate: expected on or off, not 'maybe'"));
    }
}

TEST_CASE("a validation that cannot hold is refused at load, by name",
          "[config][suites][validate]") {
    const std::string members = "  s:\n    members:\n      chat: root\n      utility: helper\n";
    const std::vector<std::pair<std::string, std::string>> refused{
        {members + "    validate:\n      verifier: chat\n",
         "suites.s.validate.verifier: 'chat' is the root itself"},
        {members + "    validate:\n      verifier: embedding\n",
         "suites.s.validate.verifier: 'embedding' is not a role that can check (accepted: "
         "extraction, vision, transcription, utility)"},
        {members + "    validate:\n      verifier: vision\n",
         "suites.s.validate: the verifier is the vision member, and this suite has none"},
        {"  s:\n    members:\n      chat: root\n    validate:\n      tool_args: on\n",
         "suites.s.validate: the verifier is the utility member, and this suite has none"},
        {members + "    validate:\n      tool_args: maybe\n",
         "suites.s.validate.tool_args: expected on or off, not 'maybe'"},
        {members + "    validate:\n      answers: sometimes\n",
         "suites.s.validate.answers: 'sometimes' is not when to check answers (accepted: "
         "request, always)"},
        {members + "    validate:\n      quorum: 2\n",
         "suites.s.validate.quorum: not a validate key (accepted: verifier, tool_args, "
         "extraction, answers)"},
        {members + "    validate: on\n", "suites.s.validate: expected a mapping"},
    };
    for (const auto& [suites, said] : refused) {
        INFO(suites);
        try {
            (void)load_text(with_suites(suites));
            FAIL("expected a ConfigError");
        } catch (const ConfigError& e) {
            CHECK_THAT(std::string{e.what()}, Catch::Matchers::ContainsSubstring(said));
        }
    }
    CHECK_NOTHROW(load_text(with_suites(members + "    validate:\n")));
}
