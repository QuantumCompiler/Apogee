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
