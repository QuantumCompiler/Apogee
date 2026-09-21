#include "secrets/store.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>

#include "harness/layout.h"
#include "support/env_guard.h"

/// The credential store: a private file, a type with no key field, and a
/// corrupt file that degrades to nothing rather than to a crash or a clobber.
namespace {

using apogee::secrets::CredentialMetadata;
using apogee::secrets::credentials_path;
using apogee::secrets::CredentialStore;

struct Fixture {
    apogee::testing::TempDir home{"secrets-store-" + std::to_string(std::random_device{}())};
    std::filesystem::path config = home.path() / "config" / "config.yaml";
    CredentialStore store{credentials_path(config)};
};

std::string bytes(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

}  // namespace

TEST_CASE("the store lives beside the config, is private, and round-trips a key",
          "[secrets][store]") {
    Fixture fixture;
    CHECK(fixture.store.path() == fixture.home.path() / "config" / "credentials.json");
    CHECK_FALSE(fixture.store.key_for("openai").has_value());
    CHECK(fixture.store.list().empty());

    fixture.store.put("openai", "sk-test-1234");
    REQUIRE(std::filesystem::exists(fixture.store.path()));
    if (apogee::harness::supports_private_modes()) {
        const std::filesystem::perms mode =
            std::filesystem::status(fixture.store.path()).permissions() &
            std::filesystem::perms::mask;
        CHECK(mode == (std::filesystem::perms::owner_read | std::filesystem::perms::owner_write));
    }
    REQUIRE(fixture.store.key_for("openai").has_value());
    CHECK(*fixture.store.key_for("openai") == "sk-test-1234");

    // Metadata: the provider and when, and structurally nothing else.
    const std::vector<CredentialMetadata> listed = fixture.store.list();
    REQUIRE(listed.size() == 1);
    CHECK(listed[0].provider == "openai");
    CHECK(listed[0].stored_at.size() == 20);
    CHECK(listed[0].stored_at.back() == 'Z');

    // Replace, add a second, list sorted, clear.
    fixture.store.put("openai", "sk-test-5678");
    fixture.store.put("anthropic", "sk-ant-1");
    CHECK(*fixture.store.key_for("openai") == "sk-test-5678");
    REQUIRE(fixture.store.list().size() == 2);
    CHECK(fixture.store.list()[0].provider == "anthropic");
    CHECK(fixture.store.clear("openai"));
    CHECK_FALSE(fixture.store.clear("openai"));
    CHECK_FALSE(fixture.store.key_for("openai").has_value());
    CHECK(fixture.store.key_for("anthropic").has_value());

    // The file is the documented shape.
    const nlohmann::json on_disk = nlohmann::json::parse(bytes(fixture.store.path()));
    CHECK(on_disk["version"] == 1);
    CHECK(on_disk["credentials"]["anthropic"]["key"] == "sk-ant-1");
    // Every write stays private.
    if (apogee::harness::supports_private_modes()) {
        const std::filesystem::perms mode =
            std::filesystem::status(fixture.store.path()).permissions() &
            std::filesystem::perms::mask;
        CHECK(mode == (std::filesystem::perms::owner_read | std::filesystem::perms::owner_write));
    }
}

TEST_CASE("a corrupt store reads as empty with a warning and is never overwritten",
          "[secrets][store]") {
    Fixture fixture;
    std::filesystem::create_directories(fixture.store.path().parent_path());
    std::ofstream{fixture.store.path()} << "{ not json";

    // Reads degrade: nothing stored, and the warning says why.
    CHECK_FALSE(fixture.store.key_for("openai").has_value());
    CHECK(fixture.store.list().empty());
    CHECK(fixture.store.warning().find("credentials.json") != std::string::npos);

    // Writes refuse: whatever is in that file may be someone's only copy.
    CHECK_THROWS_AS(fixture.store.put("openai", "sk-new"), std::runtime_error);
    CHECK_THROWS_AS(fixture.store.clear("openai"), std::runtime_error);
    CHECK(bytes(fixture.store.path()) == "{ not json");

    // A file of the right shape with an unexpected entry skips just that entry.
    std::ofstream{fixture.store.path(), std::ios::trunc}
        << R"({"version":1,"credentials":{"openai":{"key":"ok"},"bad":42}})";
    CHECK(*fixture.store.key_for("openai") == "ok");
    CHECK(fixture.store.list().size() == 1);
}
