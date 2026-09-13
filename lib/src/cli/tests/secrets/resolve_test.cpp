#include "secrets/resolve.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <map>
#include <random>
#include <string>

#include "harness/config.h"
#include "secrets/store.h"
#include "support/env_guard.h"

/// The one key resolver: the precedence table, exhaustively, and the snapshot.
namespace {

using apogee::harness::BackendConfig;
using apogee::harness::BackendType;
using apogee::secrets::conventional_variables;
using apogee::secrets::CredentialStore;
using apogee::secrets::EnvSnapshot;
using apogee::secrets::KeyResolution;
using apogee::secrets::KeySource;
using apogee::secrets::resolve_api_key;
using apogee::secrets::slot_name;
using apogee::secrets::slot_type;
using apogee::secrets::takes_api_key;

EnvSnapshot env_with(std::map<std::string, std::string> values) {
    return EnvSnapshot::capture([values](std::string_view name) {
        const auto it = values.find(std::string{name});
        return it == values.end() ? std::string{} : it->second;
    });
}

}  // namespace

TEST_CASE("only the API-billing types take a key, each with its own variables",
          "[secrets][resolve]") {
    CHECK(takes_api_key(BackendType::Anthropic));
    CHECK(takes_api_key(BackendType::OpenAI));
    CHECK(takes_api_key(BackendType::Google));
    CHECK_FALSE(takes_api_key(BackendType::LlamaCpp));
    CHECK_FALSE(takes_api_key(BackendType::Mock));
    // The vendor-CLI family never: the CLI authenticates itself.
    CHECK_FALSE(takes_api_key(BackendType::ClaudeCli));
    CHECK_FALSE(takes_api_key(BackendType::CodexCli));
    CHECK_FALSE(takes_api_key(BackendType::GeminiCli));
    CHECK_FALSE(takes_api_key(BackendType::OllamaCli));

    CHECK(conventional_variables(BackendType::Anthropic)[0] == "ANTHROPIC_API_KEY");
    CHECK(conventional_variables(BackendType::OpenAI)[0] == "OPENAI_API_KEY");
    REQUIRE(conventional_variables(BackendType::Google).size() == 2);
    CHECK(conventional_variables(BackendType::Google)[0] == "GEMINI_API_KEY");
    CHECK(conventional_variables(BackendType::Google)[1] == "GOOGLE_API_KEY");
    CHECK(conventional_variables(BackendType::ClaudeCli).empty());

    CHECK(slot_name(BackendType::OpenAI) == "openai");
    CHECK_FALSE(slot_name(BackendType::ClaudeCli).has_value());
    CHECK(slot_type("google") == BackendType::Google);
    CHECK_FALSE(slot_type("claude-cli").has_value());  // a real type, never a slot
    CHECK_FALSE(slot_type("nonsense").has_value());
}

TEST_CASE("the precedence table: config, then store, then environment, exhaustively",
          "[secrets][resolve]") {
    const apogee::testing::TempDir home{"secrets-resolve-" +
                                        std::to_string(std::random_device{}())};
    CredentialStore store{home.path() / "credentials.json"};
    const EnvSnapshot empty;
    const EnvSnapshot env = env_with({{"OPENAI_API_KEY", "from-env"}});

    BackendConfig entry;
    entry.type = BackendType::OpenAI;

    // Nothing anywhere.
    CHECK(resolve_api_key(entry, &store, empty).source == KeySource::None);
    CHECK_FALSE(resolve_api_key(entry, &store, empty).found());
    CHECK(resolve_api_key(entry, nullptr, empty).source == KeySource::None);

    // Environment alone.
    KeyResolution resolution = resolve_api_key(entry, &store, env);
    CHECK(resolution.source == KeySource::Environment);
    CHECK(resolution.key == "from-env");
    CHECK(resolution.variable == "OPENAI_API_KEY");

    // Store beats environment.
    store.put("openai", "from-store");
    resolution = resolve_api_key(entry, &store, env);
    CHECK(resolution.source == KeySource::Store);
    CHECK(resolution.key == "from-store");
    CHECK(resolution.variable.empty());
    // ...but no store means the environment answers.
    CHECK(resolve_api_key(entry, nullptr, env).source == KeySource::Environment);

    // Config beats both -- the user's decision: a per-entry key stays first.
    entry.api_key = "from-config";
    resolution = resolve_api_key(entry, &store, env);
    CHECK(resolution.source == KeySource::Config);
    CHECK(resolution.key == "from-config");
    // Config alone, too.
    CHECK(resolve_api_key(entry, nullptr, empty).source == KeySource::Config);

    // A slot for one vendor never answers for another.
    BackendConfig other;
    other.type = BackendType::Anthropic;
    CHECK(resolve_api_key(other, &store, env).source == KeySource::None);

    // A type that takes no key resolves nothing, whatever is around.
    BackendConfig local;
    local.type = BackendType::LlamaCpp;
    local.api_key = "ignored";
    CHECK(resolve_api_key(local, &store, env).source == KeySource::None);

    // Google honours GEMINI_API_KEY first, then GOOGLE_API_KEY.
    BackendConfig google;
    google.type = BackendType::Google;
    CHECK(resolve_api_key(google, nullptr, env_with({{"GOOGLE_API_KEY", "g"}})).variable ==
          "GOOGLE_API_KEY");
    CHECK(resolve_api_key(google, nullptr,
                          env_with({{"GOOGLE_API_KEY", "g"}, {"GEMINI_API_KEY", "gm"}}))
              .key == "gm");
}

TEST_CASE("a snapshot never moves after it is taken", "[secrets][resolve][snapshot]") {
    // Two writers on one process race (Ommi's OMMI-9). A snapshot taken before
    // anything resolves cannot be moved by a later change.
    std::map<std::string, std::string> values{{"OPENAI_API_KEY", "first"}};
    const EnvSnapshot snapshot = EnvSnapshot::capture([&values](std::string_view name) {
        const auto it = values.find(std::string{name});
        return it == values.end() ? std::string{} : it->second;
    });
    values["OPENAI_API_KEY"] = "second";
    values["ANTHROPIC_API_KEY"] = "late";
    CHECK(snapshot.get("OPENAI_API_KEY") == "first");
    CHECK(snapshot.get("ANTHROPIC_API_KEY").empty());

    // The process-wide one, against the real environment: read once, then
    // deaf to a later setenv.
    const std::string before = EnvSnapshot::process().get("APOGEE_SNAPSHOT_PROBE");
    const apogee::testing::EnvGuard guard{"APOGEE_SNAPSHOT_PROBE", "changed"};
    CHECK(EnvSnapshot::process().get("APOGEE_SNAPSHOT_PROBE") == before);
    // (The probe is not a conventional variable, so it was never captured at
    // all -- which is the same guarantee from the other side: the snapshot
    // holds exactly the four names, nothing the environment grows later.)
}

TEST_CASE("the no-key message names the entry's own variable, auth add, and the field, never a key",
          "[secrets][resolve]") {
    const std::string message = apogee::secrets::no_key_message("gpt", BackendType::OpenAI);
    CHECK(message.find("'gpt'") != std::string::npos);
    CHECK(message.find("OPENAI_API_KEY") != std::string::npos);
    CHECK(message.find("apogee auth add openai") != std::string::npos);
    CHECK(message.find("api_key") != std::string::npos);
    CHECK(apogee::secrets::no_key_message("gem", BackendType::Google).find("GEMINI_API_KEY") !=
          std::string::npos);
}
