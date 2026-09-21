#include "tools/rag_query.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <random>
#include <string>

#include "agent/tool.h"
#include "embedstore/store.h"
#include "harness/config.h"
#include "harness/layout.h"
#include "support/env_guard.h"

/// The RAG-query toolset over a seeded lexical store: the retriever that ran
/// is reported, an impossible explicit ask is an error, and the names are
/// validated before they touch the filesystem.
namespace {

using apogee::agent::ToolOutcome;
using apogee::agent::ToolRegistry;

struct World {
    apogee::testing::TempDir home{"tools-rag-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    apogee::harness::Config config;
    ToolRegistry registry;

    World() {
        std::filesystem::create_directories(apogee::harness::embeddings_dir());
        apogee::embedstore::Store store{apogee::harness::embeddings_dir() / "docs.db"};
        store.replace_source("guide.md", {"Apogee runs local models through llama.cpp",
                                          "The admin plane is behind a bearer token"});
        // No harness: no embedder, so vector searches cannot run.
        apogee::tools::register_rag_tools(registry, nullptr, &config);
    }

    [[nodiscard]] ToolOutcome run(std::string_view tool, const std::string& arguments) const {
        return registry.find(tool)->run(arguments);
    }
};

}  // namespace

TEST_CASE("search_documents reports the retriever that ran and every hit's source",
          "[tools][rag][retriever]") {
    const World world;
    const ToolOutcome hits =
        world.run("search_documents", R"({"query":"bearer token","collection":"docs"})");
    REQUIRE_FALSE(hits.is_error);
    CHECK(hits.content.starts_with("collection: docs  retriever: lexical"));
    CHECK(hits.content.find("[guide.md #") != std::string::npos);
    CHECK(hits.content.find("retriever=lexical") != std::string::npos);
    CHECK(hits.content.find("bearer token") != std::string::npos);

    // Explicit lexical is honoured; an explicit vector with no embedder is an
    // ERROR naming lexical -- never a silent fallback under the wrong name.
    CHECK_FALSE(
        world.run("search_documents", R"({"query":"x","collection":"docs","retriever":"lexical"})")
            .is_error);
    const ToolOutcome refused =
        world.run("search_documents", R"({"query":"x","collection":"docs","retriever":"vector"})");
    CHECK(refused.is_error);
    CHECK(refused.content.find("lexical") != std::string::npos);
    CHECK(world.run("search_documents", R"({"query":"x","collection":"docs","retriever":"fuzzy"})")
              .is_error);

    // Nothing matched is a result, not an error.
    CHECK(world.run("search_documents", R"({"query":"zzzz qqqq","collection":"docs"})")
              .content.find("No passages matched") != std::string::npos);
    // Validation before the filesystem.
    CHECK(world.run("search_documents", R"({"query":"x","collection":"../etc"})").is_error);
    CHECK(world.run("search_documents", R"({"query":"x","collection":"missing"})").is_error);
    CHECK(world.run("search_documents", R"({"collection":"docs"})").is_error);
    CHECK(world.run("search_documents", R"({"query":"x","collection":"docs","top_k":0})").is_error);
    CHECK(
        world.run("search_documents", R"({"query":"x","collection":"docs","top_k":99})").is_error);
    CHECK(apogee::tools::valid_collection_name("notes-2026"));
    CHECK_FALSE(apogee::tools::valid_collection_name("a/b"));
}

TEST_CASE("list_collections and collection_info describe what is on disk", "[tools][rag]") {
    const World world;
    CHECK(world.run("list_collections", "{}").content == "  docs  2 chunks");
    const ToolOutcome info = world.run("collection_info", R"({"collection":"docs"})");
    REQUIRE_FALSE(info.is_error);
    CHECK(info.content.find("chunks: 2") != std::string::npos);
    CHECK(info.content.find("none (lexical only)") != std::string::npos);
    CHECK(info.content.find("  guide.md") != std::string::npos);
    CHECK(world.run("collection_info", R"({"collection":"nope"})").is_error);
    // The registry reports no tool as destructive: searching never prompts.
    for (const std::string& name : world.registry.names()) {
        CHECK_FALSE(world.registry.find(name)->writes);
    }
}
