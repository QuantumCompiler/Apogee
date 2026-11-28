#include "contracts/collection_name.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/config.h"

using apogee::harness::collection_read_error;
using apogee::harness::name_leaves_folder;

// The read rule (CLI-M45): a collection is named, never pathed. It refuses
// only what could leave `embeddings/` once `.db` is appended -- every store
// that exists today keeps reading, whatever its name.

TEST_CASE("a name that could leave the folder is refused, in the rule's words",
          "[harness][collection_name]") {
    CHECK(collection_read_error("") == "a collection needs a name");
    for (const std::string name :
         {"..", "../x", "../../etc/passwd", "/x", "/tmp/foreign", "a/b", "a\\b", "\\x", "x/"}) {
        INFO(name);
        CHECK(name_leaves_folder(name));
        CHECK(collection_read_error(name) == "'" + name + "' is not a plain collection name");
    }
    // A NUL would cut the path where a C API reads it.
    const std::string nul{"a\0b", 3};
    CHECK(name_leaves_folder(nul));
    CHECK_FALSE(collection_read_error(nul).empty());
}

TEST_CASE("every name a store can have under the folder keeps reading",
          "[harness][collection_name]") {
    for (const std::string& name :
         {std::string{"notes"}, std::string{"my notes"}, std::string{"a@b"}, std::string{"a..b"},
          std::string{"."}, std::string{".hidden"}, std::string{"-x"}, std::string{"c,d"},
          std::string{"café"}, std::string{"a\nb"}, std::string(129, 'n'), std::string(244, 'n'),
          std::string{"Notes"}}) {
        INFO(name);
        CHECK_FALSE(name_leaves_folder(name));
        CHECK(collection_read_error(name).empty());
    }
}

TEST_CASE("a root name refuses only where the platform has one", "[harness][collection_name]") {
#ifdef _WIN32
    // Joined to the folder, a drive-relative name rebases the path.
    for (const std::string name : {"D:x", "C:notes", "c:"}) {
        INFO(name);
        CHECK(name_leaves_folder(name));
        CHECK(collection_read_error(name) == "'" + name + "' is not a plain collection name");
    }
#else
    // A colon is a plain character here, and a store named so reads.
    CHECK_FALSE(name_leaves_folder("a:b"));
    CHECK_FALSE(name_leaves_folder("C:notes"));
    CHECK(collection_read_error("D:x").empty());
#endif
}

// The create rule (CLI-M46): which names may be CREATED -- the read rule,
// the config's add rule less `/`, a 200-byte cap, no Windows device name,
// and no name that differs only in case from one that exists. Every name
// it takes is one the config can register; the read rule above still
// reads every store on disk.

using apogee::harness::collection_create_error;
using apogee::harness::collection_references;
using apogee::harness::kNewCollectionNameMax;
using apogee::harness::quoted_collection_name;

TEST_CASE("a new name is one the config can hold, in the rule's words",
          "[harness][collection_name][M46]") {
    for (const std::string name :
         {"notes", "true", "x.db", "a..b", "my_notes", "my-notes", "Notes2", "a.b-c_d"}) {
        INFO(name);
        CHECK(collection_create_error(name, {}).empty());
    }
    for (const std::string name :
         {"my notes", "a@b", "café",  "null",   "Null", "NULL", "~",    ".x",
          "-x",       ":",   "#",     "\"q\"",  "[x]",  ",",    "a\tb", "a\nb",
          ".",        "-",   " lead", "trail ", "*",    "{x}",  "|"}) {
        INFO(name);
        const std::string refused = collection_create_error(name, {});
        CHECK(refused.find("a name is letters, digits, '_', '-' and '.'") != std::string::npos);
        CHECK(refused.find(quoted_collection_name(name)) != std::string::npos);
        CHECK(refused.find('\n') == std::string::npos);
    }
}

TEST_CASE("a name is said on one line, its control characters escaped",
          "[harness][collection_name][M46]") {
    CHECK(quoted_collection_name("my notes") == "'my notes'");
    CHECK(quoted_collection_name("a\nb") == "'a\\nb'");
    CHECK(quoted_collection_name("a\tb\r") == "'a\\tb\\r'");
    CHECK(quoted_collection_name(std::string{"a\x01\x7f"}) == "'a\\x01\\x7f'");
    CHECK(quoted_collection_name("café") == "'café'");
}

TEST_CASE("the read rule comes first, in its own words", "[harness][collection_name][M46]") {
    CHECK(collection_create_error("", {}) == "a collection needs a name");
    for (const std::string name : {"..", "a/b", "a\\b", "../x"}) {
        INFO(name);
        CHECK(collection_create_error(name, {}) == "'" + name + "' is not a plain collection name");
    }
}

TEST_CASE("a new name is at most 200 bytes", "[harness][collection_name][M46]") {
    CHECK(kNewCollectionNameMax == 200);
    CHECK(collection_create_error(std::string(200, 'n'), {}).empty());
    for (const std::size_t length : {std::size_t{201}, std::size_t{244}, std::size_t{245}}) {
        const std::string refused = collection_create_error(std::string(length, 'n'), {});
        INFO(refused);
        CHECK(refused.find(std::to_string(length) + " bytes") != std::string::npos);
        CHECK(refused.find("at most 200") != std::string::npos);
    }
}

TEST_CASE("a Windows device name is refused on every platform, folded",
          "[harness][collection_name][M46]") {
    for (const std::string name : {"con", "CON", "prn", "aux", "Nul", "com1", "COM9", "lpt1",
                                   "lpt9", "con.x", "nul.notes"}) {
        INFO(name);
        const std::string refused = collection_create_error(name, {});
        CHECK(refused.find("device name") != std::string::npos);
    }
    for (const std::string name :
         {"console", "com", "com10", "lpt0", "com0", "auxiliary", "x.con"}) {
        INFO(name);
        CHECK(collection_create_error(name, {}).empty());
    }
}

TEST_CASE("one device-name list for every create rule: the part before the first '.', folded",
          "[harness][collection_name][M56]") {
    // CLI-M56: the dataset create rule asks the same function M46's
    // collection rule asks, so the two lists cannot drift.
    using apogee::harness::windows_device_name;
    for (const std::string name : {"con", "CON", "Con.txt", "prn", "aux", "NUL", "com1", "COM9",
                                   "com1.x", "lpt1", "Lpt1", "lpt9", "nul.notes.jsonl"}) {
        INFO(name);
        CHECK(windows_device_name(name));
    }
    for (const std::string name : {"", "console", "com", "com10", "lpt", "lpt0", "com0",
                                   "auxiliary", "x.con", ".con", "conx"}) {
        INFO(name);
        CHECK_FALSE(windows_device_name(name));
    }
}

TEST_CASE("a new name that folds onto one that exists is refused, naming it",
          "[harness][collection_name][M46]") {
    const std::vector<std::string> existing{"notes", "Team"};
    const std::string refused = collection_create_error("Notes", existing);
    CHECK(refused.find("'Notes'") != std::string::npos);
    CHECK(refused.find("'notes'") != std::string::npos);
    CHECK(refused.find("only in case") != std::string::npos);
    CHECK_FALSE(collection_create_error("team", existing).empty());
    // The same spelling is not a fold: it is that collection.
    CHECK(collection_create_error("notes", existing).empty());
    CHECK(collection_create_error("other", existing).empty());
}

TEST_CASE("the names a config refers to are every collection key it holds",
          "[harness][collection_name][M46]") {
    apogee::harness::Config config;
    config.embeddings["my notes"] = {};
    config.auto_rag = "auto one";
    config.knowledge.db = "team notes";
    apogee::harness::AgentConfig agent;
    agent.collection = "agent one";
    config.agents["h"] = agent;
    apogee::harness::NamedGraphConfig graph;
    graph.collections = {"notes", "graph one"};
    config.graphs.emplace_back("team", graph);
    const std::vector<std::string> names = collection_references(config);
    for (const std::string name :
         {"my notes", "auto one", "team notes", "agent one", "notes", "graph one"}) {
        INFO(name);
        CHECK(std::ranges::find(names, name) != names.end());
    }
    // The default knowledge collection is a name the rule takes anyway, and
    // an unset key refers to nothing.
    CHECK(std::ranges::find(names, std::string{}) == names.end());
    CHECK(collection_references(apogee::harness::Config{}).empty());
}
