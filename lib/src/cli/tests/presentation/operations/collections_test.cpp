#include "operations/collections.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "contracts/config.h"
#include "contracts/config_edit.h"
#include "embedstore/store.h"
#include "platform/file_lock.h"
#include "support/env_guard.h"

/// A write into a collection asks its plan first (CLI-M46): the create rule
/// for a new name and a dry run of the edits the write would run after it,
/// so a name the config cannot hold is refused before the first byte -- and
/// a store already on disk, or a name the config refers to, goes ahead with
/// one note.
namespace {

using apogee::commands::CollectionWrite;
using apogee::commands::CollectionWritePlan;
using apogee::commands::plan_collection_write;
using apogee::commands::register_collection;
using Outcome = CollectionWritePlan::Outcome;

[[nodiscard]] std::string bytes_of(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// A home with a config in it, its text given.
struct Home {
    apogee::testing::TempDir root{"m46-collections-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard home{"APOGEE_HOME", root.path().string()};
    std::filesystem::path config_path = root.path() / "config" / "config.json";
    apogee::harness::Config config;

    explicit Home(const std::string& text = "{\n  \"backends\": {}\n}\n") {
        std::filesystem::create_directories(config_path.parent_path());
        std::filesystem::create_directories(root.path() / "embeddings");
        write(text);
    }

    void write(const std::string& text) {
        std::ofstream{config_path, std::ios::binary} << text;
        config = apogee::harness::load_config(config_path);
    }

    [[nodiscard]] CollectionWrite write_of(const std::string& name,
                                           bool enable_graph = false) const {
        CollectionWrite write;
        write.name = name;
        write.config = &config;
        write.config_path = config_path;
        write.entry.chunk_size = 512;
        write.entry.chunk_overlap = 64;
        write.enable_graph = enable_graph;
        return write;
    }

    static void store(const std::string& name) {
        apogee::embedstore::Store store{apogee::commands::collection_path(name)};
        store.replace_source("a.md", {"the zarquon protocol"});
    }
};

}  // namespace

TEST_CASE("a new plain name goes ahead, its registration planned and nothing written",
          "[operations][collections][M46]") {
    const Home home;
    const std::string before = bytes_of(home.config_path);
    const CollectionWritePlan plan = plan_collection_write(home.write_of("notes"));
    CHECK(plan.outcome == Outcome::Go);
    CHECK(plan.is_new);
    CHECK(plan.register_entry);
    CHECK_FALSE(plan.enable_graph);
    CHECK(plan.words.empty());
    CHECK(bytes_of(home.config_path) == before);
    CHECK_FALSE(std::filesystem::exists(apogee::commands::collection_path("notes")));
}

TEST_CASE(
    "a new name the config cannot hold is refused, naming the rule and that nothing was "
    "written",
    "[operations][collections][M46]") {
    const Home home;
    const std::string before = bytes_of(home.config_path);
    const std::vector<std::string> names{"my notes",           "a@b", "café", "null", ".x", "-x",
                                         std::string(245, 'n')};
    for (const std::string& name : names) {
        INFO(name);
        const CollectionWritePlan plan = plan_collection_write(home.write_of(name));
        CHECK(plan.refused());
        CHECK(plan.is_new);
        CHECK(plan.words.find("nothing was written") != std::string::npos);
        CHECK(plan.words.find('\n') == std::string::npos);
        CHECK_FALSE(plan.register_entry);
        CHECK_FALSE(std::filesystem::exists(apogee::commands::collection_path(name)));
    }
    CHECK(
        plan_collection_write(home.write_of("my notes")).words.find("a name is letters, digits") !=
        std::string::npos);
    CHECK(bytes_of(home.config_path) == before);
}

TEST_CASE("a new name whose registration the editor would refuse is refused before the write",
          "[operations][collections][M46]") {
    // CLI-M44's layout refusal -- a section at 4 spaces -- is asked first, by
    // the edit that would run, on the text and kept nowhere.
    const Home home{"embeddings:\n    other:\n        chunk_size: 100\n"};
    const std::string before = bytes_of(home.config_path);
    const CollectionWritePlan plan = plan_collection_write(home.write_of("notes"));
    CHECK(plan.refused());
    CHECK(plan.words.find("config migrate") != std::string::npos);
    CHECK(plan.words.find("nothing was written") != std::string::npos);
    CHECK(bytes_of(home.config_path) == before);
}

TEST_CASE("a store already on disk goes ahead with one note when the config cannot hold it",
          "[operations][collections][M46]") {
    const Home home;
    Home::store("my notes");
    const CollectionWritePlan plan = plan_collection_write(home.write_of("my notes"));
    CHECK(plan.outcome == Outcome::Note);
    CHECK_FALSE(plan.is_new);
    CHECK_FALSE(plan.register_entry);
    CHECK(plan.words.find('\n') == std::string::npos);
    CHECK(plan.words.find("chunking") != std::string::npos);
    CHECK(plan.words.find("graph") != std::string::npos);
    CHECK(plan.words.find("plain name") != std::string::npos);
    CHECK(plan.words.find("re-ingest") != std::string::npos);
}

TEST_CASE("a store already on disk under a plain name registers, as today",
          "[operations][collections][M46]") {
    const Home home;
    Home::store("notes");
    const CollectionWritePlan plan = plan_collection_write(home.write_of("notes"));
    CHECK(plan.outcome == Outcome::Go);
    CHECK_FALSE(plan.is_new);
    CHECK(plan.register_entry);
}

TEST_CASE("a name the config already refers to is not new", "[operations][collections][M46]") {
    const Home home{"{\n  \"agents\": {\n    \"h\": {\"collection\": \"my notes\"}\n  }\n}\n"};
    const CollectionWritePlan plan = plan_collection_write(home.write_of("my notes"));
    CHECK(plan.outcome == Outcome::Note);
    CHECK_FALSE(plan.is_new);
    CHECK_FALSE(plan.register_entry);
}

TEST_CASE("a hand-written entry runs only the edits today's conditions run",
          "[operations][collections][M46]") {
    const Home home{
        "{\n  \"embeddings\": {\n"
        "    \"my notes\": {\"chunk_size\": 64, \"chunk_overlap\": 8, \"graph\": {\"enabled\": "
        "true}},\n"
        "    \"a@b\": {\"chunk_size\": 64}\n  }\n}\n"};
    Home::store("my notes");
    Home::store("a@b");
    SECTION("registered and enabled: no edit, no note, for ingest and build alike") {
        for (const bool graph : {false, true}) {
            const CollectionWritePlan plan =
                plan_collection_write(home.write_of("my notes", graph));
            CHECK(plan.outcome == Outcome::Go);
            CHECK_FALSE(plan.register_entry);
            CHECK_FALSE(plan.enable_graph);
        }
    }
    SECTION("registered, not enabled: an ingest goes ahead with nothing to say") {
        const CollectionWritePlan plan = plan_collection_write(home.write_of("a@b"));
        CHECK(plan.outcome == Outcome::Go);
        CHECK_FALSE(plan.register_entry);
    }
    SECTION("registered, not enabled: a build is told the graph cannot be enabled") {
        const CollectionWritePlan plan = plan_collection_write(home.write_of("a@b", true));
        CHECK(plan.outcome == Outcome::Note);
        CHECK_FALSE(plan.enable_graph);
        CHECK(plan.words.find("graph.enabled") != std::string::npos);
        CHECK(plan.words.find("retrieval") != std::string::npos);
    }
}

TEST_CASE("with no config the create rule still holds, and nothing is planned to edit",
          "[operations][collections][M46]") {
    const Home home;
    CollectionWrite write = home.write_of("notes");
    write.config = nullptr;
    CollectionWritePlan plan = plan_collection_write(write);
    CHECK(plan.outcome == Outcome::Go);
    CHECK_FALSE(plan.register_entry);
    write.name = "my notes";
    plan = plan_collection_write(write);
    CHECK(plan.refused());
    CHECK(plan.words.find("a name is letters, digits") != std::string::npos);
}

TEST_CASE("a new name that differs only in case from a store is refused, naming it",
          "[operations][collections][M46]") {
    const Home home;
    Home::store("notes");
    const bool folds = std::filesystem::exists(apogee::commands::collection_path("NOTES"));
    const CollectionWritePlan plan = plan_collection_write(home.write_of("NOTES"));
    if (folds) {
        // APFS and NTFS: the file is there under any case, so the name is
        // the store's, and today's path is unchanged.
        CHECK(plan.outcome == Outcome::Go);
        CHECK_FALSE(plan.is_new);
    } else {
        CHECK(plan.refused());
        CHECK(plan.words.find("'notes'") != std::string::npos);
        CHECK(plan.words.find("only in case") != std::string::npos);
    }
}

TEST_CASE("the registration after the write lands the planned edits",
          "[operations][collections][M46]") {
    Home home;
    Home::store("notes");
    const CollectionWritePlan plan = plan_collection_write(home.write_of("notes", true));
    REQUIRE(plan.register_entry);
    REQUIRE(plan.enable_graph);
    const apogee::commands::CollectionRegistration done =
        register_collection(home.write_of("notes", true), plan);
    CHECK(done.ok());
    CHECK(done.registered);
    CHECK(done.enabled);
    home.config = apogee::harness::load_config(home.config_path);
    const apogee::harness::EmbeddingConfig* entry = home.config.find_embedding("notes");
    REQUIRE(entry != nullptr);
    CHECK(entry->chunk_size == 512);
    CHECK(entry->graph.enabled);
}

TEST_CASE("a registration that fails after the write says what it left, and is never fatal",
          "[operations][collections][M46]") {
    const Home home;
    Home::store("notes");
    const CollectionWritePlan plan = plan_collection_write(home.write_of("notes"));
    REQUIRE(plan.register_entry);
    const std::string before = bytes_of(home.config_path);
    const apogee::platform::FileLock held = apogee::harness::lock_config_file(home.config_path);
    const apogee::commands::CollectionRegistration done =
        register_collection(home.write_of("notes"), plan, std::chrono::milliseconds{50});
    CHECK_FALSE(done.ok());
    CHECK_FALSE(done.registered);
    CHECK(done.error.find("could not register 'notes'") != std::string::npos);
    CHECK(done.error.find(apogee::commands::collection_path("notes").string()) !=
          std::string::npos);
    CHECK(done.error.find('\n') == std::string::npos);
    CHECK(bytes_of(home.config_path) == before);
}

TEST_CASE("the edits are decided from the file as it is now, not a session's older copy",
          "[operations][collections][M46]") {
    // A chat's config is loaded once: after its first capture registers the
    // collection, the next capture's copy still lacks the entry.
    Home home;
    Home::store("notes");
    const apogee::harness::Config older = home.config;
    CollectionWrite write = home.write_of("notes");
    REQUIRE(register_collection(write, plan_collection_write(write)).registered);
    write.config = &older;
    const CollectionWritePlan plan = plan_collection_write(write);
    CHECK(plan.outcome == Outcome::Go);
    CHECK_FALSE(plan.register_entry);
    CHECK(plan.words.empty());
}

TEST_CASE("a note is one line whatever the name, and a knowledge store's names its own way out",
          "[operations][collections][M46]") {
    const Home home;
    Home::store("a\nb");
    CollectionWritePlan plan = plan_collection_write(home.write_of("a\nb"));
    CHECK(plan.outcome == Outcome::Note);
    CHECK(plan.words.find('\n') == std::string::npos);
    CHECK(plan.words.find("'a\\nb'") != std::string::npos);

    Home::store("team notes");
    CollectionWrite write = home.write_of("team notes");
    write.records = true;
    plan = plan_collection_write(write);
    CHECK(plan.outcome == Outcome::Note);
    CHECK(plan.words.find("--db") != std::string::npos);
    CHECK(plan.words.find("knowledge.db") != std::string::npos);
    CHECK(plan.words.find("re-ingest") == std::string::npos);
    CHECK(plan.words.find("chunking") == std::string::npos);
}
