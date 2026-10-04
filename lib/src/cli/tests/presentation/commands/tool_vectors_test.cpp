#include "commands/tool_vectors.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <memory>
#include <string>

#include "agent/tool.h"
#include "backends/mock.h"
#include "backends/openai.h"
#include "commands/helpers.h"
#include "contracts/config.h"
#include "harness/harness.h"
#include "support/env_guard.h"
#include "support/fake_transport.h"
#include "transport/http_client.h"

/// The tool-vector cache under `cache/` and how a surface picks what ranks
/// its tools (26g): vectors only from an embedder that costs nothing, words
/// otherwise, and nothing at all for a registry small enough to offer whole.
namespace {

using apogee::commands::ToolVectorCache;
using apogee::testing::TempDir;

void write(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out{path};
    out << text;
}

apogee::agent::ToolRegistry registry_of(std::size_t count) {
    apogee::agent::ToolRegistry registry;
    for (std::size_t index = 0; index < count; ++index) {
        apogee::agent::Tool tool;
        tool.name = "tool_" + std::to_string(index);
        tool.description = "Tool number " + std::to_string(index);
        tool.run = [](std::string_view) { return apogee::agent::ToolOutcome{"ok"}; };
        registry.add(std::move(tool));
    }
    return registry;
}

}  // namespace

TEST_CASE("tool vectors round-trip through the cache file", "[commands][tool_vectors]") {
    const TempDir home{"tool-vectors"};
    const std::filesystem::path file = home.path() / "cache" / "tool-vectors.json";
    {
        ToolVectorCache cache{file};
        CHECK_FALSE(cache.load("m:a").has_value());
        cache.store({{"m:a", {1.0F, 2.0F}}, {"m:b", {3.0F}}});
        CHECK(cache.load("m:a") == std::vector<float>{1.0F, 2.0F});
    }
    REQUIRE(std::filesystem::exists(file));
    ToolVectorCache reread{file};
    CHECK(reread.load("m:a") == std::vector<float>{1.0F, 2.0F});
    CHECK(reread.load("m:b") == std::vector<float>{3.0F});
}

TEST_CASE("a cache file that cannot be read is an empty cache, and is rewritten",
          "[commands][tool_vectors]") {
    const TempDir home{"tool-vectors-bad"};
    const std::filesystem::path file = home.path() / "cache" / "tool-vectors.json";
    for (const std::string& bad :
         {std::string{"not json"}, std::string{R"({"version":99,"vectors":{"m:a":[1]}})"},
          std::string{R"({"version":1,"vectors":{"m:a":["x"]}})"}}) {
        write(file, bad);
        ToolVectorCache cache{file};
        CHECK_FALSE(cache.load("m:a").has_value());
        cache.store({{"m:c", {4.0F}}});
        CHECK(ToolVectorCache{file}.load("m:c") == std::vector<float>{4.0F});
    }
}

TEST_CASE("two writers keep each other's vectors", "[commands][tool_vectors]") {
    const TempDir home{"tool-vectors-two"};
    const std::filesystem::path file = home.path() / "cache" / "tool-vectors.json";
    ToolVectorCache first{file};
    ToolVectorCache second{file};
    (void)first.load("x");
    (void)second.load("x");
    first.store({{"m:a", {1.0F}}});
    second.store({{"m:b", {2.0F}}});
    ToolVectorCache reread{file};
    CHECK(reread.load("m:a").has_value());
    CHECK(reread.load("m:b").has_value());
}

TEST_CASE("the cache lives under the config's own data directory", "[commands][tool_vectors]") {
    CHECK(apogee::commands::tool_vector_cache_path("/data/home/config/config.yaml") ==
          std::filesystem::path{"/data/home/cache/tool-vectors.json"});
}

TEST_CASE("a surface ranks by meaning only through an embedder that costs nothing",
          "[commands][tool_vectors][selection]") {
    const TempDir home{"tool-selection-surface"};
    const std::filesystem::path config_path = home.path() / "config" / "config.yaml";

    apogee::harness::Config config;
    apogee::harness::BackendConfig mock;
    mock.type = apogee::harness::BackendType::Mock;
    config.backends.emplace("free", mock);
    config.backends.emplace("paid", mock);
    config.backends.emplace("chat", mock);

    apogee::harness::Harness harness{config};
    auto free = std::make_shared<apogee::backends::MockEmbeddingProvider>("free");
    free->set_model_name("mock-embed");
    harness.register_provider("free", free);
    apogee::backends::OpenAIProvider::Options paid_options;
    paid_options.backend_name = "paid";
    paid_options.api_key = "sk-test";
    harness.register_provider(
        "paid", std::make_shared<apogee::backends::OpenAIProvider>(
                    std::move(paid_options), std::make_unique<apogee::backends::HttpClient>(
                                                 apogee::testing::FakeTransport::ok("{}"))));
    apogee::backends::MockProvider::Options chat_options;
    chat_options.backend_name = "chat";
    harness.register_provider(
        "chat", std::make_shared<apogee::backends::MockProvider>(std::move(chat_options)));
    harness.use_default_router();

    const apogee::agent::ToolRegistry small = registry_of(16);
    const apogee::agent::ToolRegistry large = registry_of(17);
    std::string ranked_by;

    SECTION("a registry offered whole needs no selection at all") {
        CHECK(apogee::commands::make_tool_selection(harness, config, small, config_path,
                                                    ranked_by) == nullptr);
        CHECK(ranked_by.empty());
    }
    SECTION("a free embedder ranks by meaning, and its vectors are cached") {
        apogee::harness::Config with = config;
        with.models.default_embedding = "free";
        const auto selection =
            apogee::commands::make_tool_selection(harness, with, large, config_path, ranked_by);
        REQUIRE(selection != nullptr);
        CHECK(selection->active());
        CHECK(ranked_by == "meaning, by free");
        (void)selection->begin_turn("tool number 3", {});
        CHECK(selection->ranker().semantic());
        CHECK(std::filesystem::exists(home.path() / "cache" / "tool-vectors.json"));
    }
    SECTION("a metered embedder is never called: words rank instead") {
        apogee::harness::Config with = config;
        with.models.default_embedding = "paid";
        const auto selection =
            apogee::commands::make_tool_selection(harness, with, large, config_path, ranked_by);
        REQUIRE(selection != nullptr);
        CHECK(ranked_by == "words (paid bills each call)");
        (void)selection->begin_turn("tool number 3", {});
        CHECK_FALSE(selection->ranker().semantic());
        CHECK_FALSE(std::filesystem::exists(home.path() / "cache" / "tool-vectors.json"));
    }
    SECTION("no embedder at all: words") {
        apogee::harness::Config with = config;
        with.models.default_embedding = "chat";
        const auto selection =
            apogee::commands::make_tool_selection(harness, with, large, config_path, ranked_by);
        REQUIRE(selection != nullptr);
        CHECK(ranked_by.starts_with("words (no embedding model"));
    }
}
