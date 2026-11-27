#include "contracts/collection_name.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

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
