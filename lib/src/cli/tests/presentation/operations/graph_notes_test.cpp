#include "operations/graph_notes.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "embedstore/store.h"

/// The graph notes' words, one producer for the CLI and the admin plane
/// (CLI-M58, CLI-M59): today's sentences, with no prefix and no tag.
TEST_CASE("the entity-vector skip says the graph, the reason and what search is left",
          "[operations][graph][notes][M58]") {
    CHECK(apogee::commands::entity_vectors_skipped("notes", "backend 'extractor' cannot embed") ==
          "entity vectors skipped for \"notes\" -- backend 'extractor' cannot embed; entity search "
          "will be full-text only.");
}

TEST_CASE("the summary-vector skip says the graph, the reason and what search is left",
          "[operations][graph][notes][M58]") {
    CHECK(apogee::commands::summary_vectors_skipped("notes", "backend 'extractor' cannot embed") ==
          "summary vectors skipped for \"notes\" -- backend 'extractor' cannot embed; summaries "
          "will be full-text searchable only.");
}

TEST_CASE("a member without a database contributes nothing, and a mixed graph built only code",
          "[operations][graph][notes][M58]") {
    CHECK(apogee::commands::member_without_database("ghost") ==
          "member collection \"ghost\" has no database -- it contributes nothing this build.");
    CHECK(apogee::commands::only_code_built("ghosty") ==
          "no member collection of \"ghosty\" has any data yet -- only its code was built.");
}

TEST_CASE("each member whose store is null is said once, in the members' order",
          "[operations][graph][notes][M58]") {
    apogee::commands::GraphMembers members;
    members.stores["zeta"] = nullptr;
    members.stores["alpha"] = nullptr;
    members.stores["held"] =
        std::make_unique<apogee::embedstore::Store>(std::filesystem::path{":memory:"});
    CHECK(apogee::commands::members_without_database(members) ==
          std::vector<std::string>{
              "member collection \"alpha\" has no database -- it contributes nothing this build.",
              "member collection \"zeta\" has no database -- it contributes nothing this build."});
    CHECK(apogee::commands::members_without_database(apogee::commands::GraphMembers{}).empty());
}
