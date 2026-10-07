#include "harness/roles.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// The role-resolution chain, table-tested rung by rung.
///
/// This exists because two copies of the chain come to disagree: a request
/// runs on one backend from the CLI and another over HTTP. The table
/// below is the contract that makes a second copy detectable — if someone
/// reorders the four rungs in `roles.cpp`, exactly one row here goes red and
/// names which precedence broke.
namespace {

using apogee::harness::Config;
using apogee::harness::ModelRole;
using apogee::harness::resolve_backend_key;
using apogee::harness::RoleRequest;

[[nodiscard]] Config config_with(std::string chat, std::string embedding, std::string extraction) {
    Config config;
    config.models.default_backend = std::move(chat);
    config.models.default_embedding = std::move(embedding);
    config.models.default_extraction = std::move(extraction);
    return config;
}

struct Case {
    std::string what;
    Config config;
    RoleRequest request;
    std::string expected;
};

}  // namespace

TEST_CASE("the resolution chain answers each rung in order", "[harness][roles]") {
    const Config full = config_with("base", "embed", "extract");
    const Config bare = config_with("base", "", "");

    const std::vector<Case> cases{
        // --- rung 1: an explicit override beats everything ------------------
        {"override beats the chat default",
         full,
         {.role = ModelRole::Chat, .override = "picked"},
         "picked"},
        {"override beats an entry backend",
         full,
         {.role = ModelRole::Chat, .override = "picked", .entry_backend = "entry"},
         "picked"},
        {"override beats a role pointer",
         full,
         {.role = ModelRole::Embedding, .override = "picked"},
         "picked"},

        // --- rung 2: the feature's own pin ----------------------------------
        {"entry backend beats a role pointer",
         full,
         {.role = ModelRole::Embedding, .entry_backend = "entry"},
         "entry"},
        {"entry backend beats the chat default",
         full,
         {.role = ModelRole::Chat, .entry_backend = "entry"},
         "entry"},

        // --- rung 3: the role pointer ---------------------------------------
        {"embedding uses its own pointer", full, {.role = ModelRole::Embedding}, "embed"},
        {"extraction uses its own pointer", full, {.role = ModelRole::Extraction}, "extract"},
        {"chat has no pointer of its own and lands on the default",
         full,
         {.role = ModelRole::Chat},
         "base"},

        // --- rung 4: models.default -----------------------------------------
        {"embedding falls through to the default when unset",
         bare,
         {.role = ModelRole::Embedding},
         "base"},
        {"extraction falls through to the default when unset",
         bare,
         {.role = ModelRole::Extraction},
         "base"},
    };

    for (const Case& test : cases) {
        INFO(test.what);
        CHECK(resolve_backend_key(test.config, test.request) == test.expected);
    }
}

TEST_CASE("with every role pointer unset the chain is exactly the old behaviour",
          "[harness][roles]") {
    // The compatibility promise in one assertion: before roles existed, every
    // caller did `override.empty() ? models.default : override`. With the
    // pointers unset the resolver must be indistinguishable from that, or this
    // item silently changed what every existing config does.
    const Config bare = config_with("base", "", "");

    for (const ModelRole role : {ModelRole::Chat, ModelRole::Embedding, ModelRole::Extraction}) {
        for (const std::string_view override : {std::string_view{}, std::string_view{"picked"}}) {
            const std::string legacy =
                override.empty() ? bare.models.default_backend : std::string{override};
            CHECK(resolve_backend_key(bare, RoleRequest{.role = role, .override = override}) ==
                  legacy);
        }
    }
}

TEST_CASE("nothing configured resolves to nothing, not to a guess", "[harness][roles]") {
    // A resolver that invented a fallback here would turn "you have no config"
    // into "no backend named 'default'", which sends the user looking for a
    // backend instead of for their missing config.
    const Config empty;
    CHECK(resolve_backend_key(empty, RoleRequest{.role = ModelRole::Chat}).empty());
    CHECK(resolve_backend_key(empty, RoleRequest{.role = ModelRole::Embedding}).empty());
}

TEST_CASE("whitespace-only config values fall through instead of naming a backend",
          "[harness][roles]") {
    // A pointer of "  " is a typo, not a backend name. Returning it produces
    // "no backend named '  '"; falling through produces what the user meant.
    Config config = config_with("base", "   ", "\t\n");
    CHECK(resolve_backend_key(config, RoleRequest{.role = ModelRole::Embedding}) == "base");
    CHECK(resolve_backend_key(config, RoleRequest{.role = ModelRole::Extraction}) == "base");

    // And surrounding whitespace on a real value is trimmed rather than passed
    // through to a lookup that would miss.
    config.models.default_embedding = "  embed  ";
    CHECK(resolve_backend_key(config, RoleRequest{.role = ModelRole::Embedding}) == "embed");
}

TEST_CASE("a role names the config key it reads", "[harness][roles]") {
    // So an error message can say which line of the config to edit.
    CHECK(apogee::harness::to_string(ModelRole::Chat) == "default");
    CHECK(apogee::harness::to_string(ModelRole::Embedding) == "default_embedding");
    CHECK(apogee::harness::to_string(ModelRole::Extraction) == "default_extraction");
}

TEST_CASE("a helper role falls back to the conversation's backend, then models.default",
          "[harness][roles][helpers]") {
    // 26b: an unset helper runs on whatever the chat is on -- `-m` or
    // `/model` -- so a user who configures none sees nothing move.
    using apogee::harness::resolve_backend;
    using apogee::harness::ResolvedFrom;
    Config config = config_with("base", "", "");
    config.models.default_vision = "sees";
    config.models.default_transcription = "hears";
    config.models.default_utility = "helps";

    for (const auto [role, pointer] :
         {std::pair{ModelRole::Vision, "sees"}, std::pair{ModelRole::Transcription, "hears"},
          std::pair{ModelRole::Utility, "helps"}}) {
        CAPTURE(pointer);
        // The pointer beats the conversation.
        const auto pointed =
            resolve_backend(config, RoleRequest{.role = role, .conversation = "on"});
        CHECK(pointed.key == pointer);
        CHECK(pointed.from == ResolvedFrom::RolePointer);
        // An override and an entry pin still beat the pointer.
        CHECK(resolve_backend_key(
                  config, RoleRequest{.role = role, .override = "picked", .conversation = "on"}) ==
              "picked");
        CHECK(resolve_backend_key(config, RoleRequest{.role = role,
                                                      .entry_backend = "entry",
                                                      .conversation = "on"}) == "entry");
    }

    const Config unset = config_with("base", "", "");
    for (const ModelRole role : {ModelRole::Vision, ModelRole::Transcription, ModelRole::Utility}) {
        // No pointer: the conversation's backend...
        const auto chat = resolve_backend(unset, RoleRequest{.role = role, .conversation = "on"});
        CHECK(chat.key == "on");
        CHECK(chat.from == ResolvedFrom::Conversation);
        // ...and with no conversation, models.default.
        const auto base = resolve_backend(unset, RoleRequest{.role = role});
        CHECK(base.key == "base");
        CHECK(base.from == ResolvedFrom::Default);
        // Whitespace is no backend.
        CHECK(resolve_backend_key(unset, RoleRequest{.role = role, .conversation = "  "}) ==
              "base");
    }

    // The other roles never read the conversation: chat, embedding and
    // extraction resolve exactly as before.
    for (const ModelRole role : {ModelRole::Chat, ModelRole::Embedding, ModelRole::Extraction}) {
        CHECK(resolve_backend_key(unset, RoleRequest{.role = role, .conversation = "on"}) ==
              "base");
    }
}

TEST_CASE("the helper roles name their keys and are the only helpers",
          "[harness][roles][helpers]") {
    using apogee::harness::is_helper;
    using apogee::harness::to_string;
    CHECK(to_string(ModelRole::Vision) == "default_vision");
    CHECK(to_string(ModelRole::Transcription) == "default_transcription");
    CHECK(to_string(ModelRole::Utility) == "default_utility");
    CHECK(is_helper(ModelRole::Vision));
    CHECK(is_helper(ModelRole::Transcription));
    CHECK(is_helper(ModelRole::Utility));
    CHECK_FALSE(is_helper(ModelRole::Chat));
    CHECK_FALSE(is_helper(ModelRole::Embedding));
    CHECK_FALSE(is_helper(ModelRole::Extraction));
}

namespace {

/// Which rung answered, as the golden table spells it.
[[nodiscard]] std::string rung_name(apogee::harness::ResolvedFrom from) {
    using apogee::harness::ResolvedFrom;
    switch (from) {
        case ResolvedFrom::Nothing:
            return "nothing";
        case ResolvedFrom::Override:
            return "override";
        case ResolvedFrom::EntryBackend:
            return "entry";
        case ResolvedFrom::Suite:
            return "suite";
        case ResolvedFrom::RolePointer:
            return "pointer";
        case ResolvedFrom::Conversation:
            return "conversation";
        case ResolvedFrom::Default:
            return "default";
    }
    return "?";
}

/// Every role pointer set, none, or exactly one -- each with `models.default`
/// absent, set, or whitespace -- against every request shape: the whole
/// resolution table a config with no suite produces, one row per question.
[[nodiscard]] std::string no_suite_table() {
    using apogee::harness::resolve_backend;
    const std::vector<std::pair<ModelRole, std::string_view>> roles{
        {ModelRole::Chat, "chat"},
        {ModelRole::Embedding, "embedding"},
        {ModelRole::Extraction, "extraction"},
        {ModelRole::Vision, "vision"},
        {ModelRole::Transcription, "transcription"},
        {ModelRole::Utility, "utility"}};
    std::string out;
    for (const std::string_view base : {"", "base", "  "}) {
        // -1 is every pointer unset, 6 every pointer set, else that one alone.
        for (int pointers = -1; pointers <= 6; ++pointers) {
            Config config;
            config.models.default_backend = std::string{base};
            const auto pointed = [pointers](int index) {
                return pointers == 6 || pointers == index;
            };
            config.models.default_embedding = pointed(1) ? "embedding-p" : "";
            config.models.default_extraction = pointed(2) ? "extraction-p" : "";
            config.models.default_vision = pointed(3) ? "vision-p" : "";
            config.models.default_transcription = pointed(4) ? "transcription-p" : "";
            config.models.default_utility = pointed(5) ? "utility-p" : "";
            for (const auto& [role, label] : roles) {
                for (const std::string_view override : {"", "picked"}) {
                    for (const std::string_view entry : {"", "entry", " "}) {
                        for (const std::string_view conversation : {"", "on"}) {
                            const apogee::harness::Resolution resolved =
                                resolve_backend(config, RoleRequest{.role = role,
                                                                    .override = override,
                                                                    .entry_backend = entry,
                                                                    .conversation = conversation});
                            out += "default='" + std::string{base} +
                                   "' pointers=" + std::to_string(pointers) +
                                   " role=" + std::string{label} + " override='" +
                                   std::string{override} + "' entry='" + std::string{entry} +
                                   "' conversation='" + std::string{conversation} + "' -> '" +
                                   resolved.key + "' " + rung_name(resolved.from) + "\n";
                        }
                    }
                }
            }
        }
    }
    return out;
}

}  // namespace

TEST_CASE("with no suite, the resolution table is the one from before suites existed",
          "[harness][roles][suites]") {
    // The golden was written by the resolver as it stood before the suite
    // rung was added (27d): with no suite configured or selected, every
    // question answers exactly as it did then -- the same backend from the
    // same rung, byte for byte.
    const std::filesystem::path golden =
        std::filesystem::path{APOGEE_TEST_FIXTURES} / "harness" / "roles_no_suite.golden";
    const std::string table = no_suite_table();
    std::ifstream in{golden, std::ios::binary};
    REQUIRE(in.good());
    const std::string expected{std::istreambuf_iterator<char>{in},
                               std::istreambuf_iterator<char>{}};
    CHECK(table == expected);
}

namespace {

/// `base` as the default, a pointer for embedding and utility, and a suite
/// `s` naming chat, embedding and utility -- active or not.
[[nodiscard]] Config suite_config(bool active) {
    Config config = config_with("base", "embedding-p", "");
    config.models.default_utility = "utility-p";
    apogee::harness::SuiteConfig suite;
    suite.members["chat"] = {.backend = "suite-chat"};
    suite.members["embedding"] = {.backend = "suite-embedding"};
    suite.members["utility"] = {.backend = "  suite-utility  "};
    config.suites["s"] = suite;
    config.models.default_suite = active ? "s" : "";
    return config;
}

}  // namespace

TEST_CASE("the suite rung sits between the feature's pin and the role pointer",
          "[harness][roles][suites]") {
    using apogee::harness::resolve_backend;
    using apogee::harness::ResolvedFrom;
    const Config active = suite_config(true);

    struct Row {
        std::string what;
        RoleRequest request;
        std::string key;
        ResolvedFrom from;
    };

    const std::vector<Row> rows{
        // An explicit choice and a feature's pin still win.
        {"override beats the suite",
         {.role = ModelRole::Utility, .override = "picked", .conversation = "on"},
         "picked",
         ResolvedFrom::Override},
        {"a feature's pin beats the suite",
         {.role = ModelRole::Embedding, .entry_backend = "collection-b"},
         "collection-b",
         ResolvedFrom::EntryBackend},
        // The suite speaks for the roles it names...
        {"the suite beats a role pointer",
         {.role = ModelRole::Embedding},
         "suite-embedding",
         ResolvedFrom::Suite},
        {"the suite beats the conversation, trimmed",
         {.role = ModelRole::Utility, .conversation = "on"},
         "suite-utility",
         ResolvedFrom::Suite},
        {"the suite's chat member beats models.default",
         {.role = ModelRole::Chat},
         "suite-chat",
         ResolvedFrom::Suite},
        // ...and a role it leaves out falls through the chain as before.
        {"extraction, unnamed, lands on models.default",
         {.role = ModelRole::Extraction},
         "base",
         ResolvedFrom::Default},
        {"vision, unnamed, falls to the conversation",
         {.role = ModelRole::Vision, .conversation = "on"},
         "on",
         ResolvedFrom::Conversation},
        {"transcription, unnamed and alone, lands on models.default",
         {.role = ModelRole::Transcription},
         "base",
         ResolvedFrom::Default},
    };
    for (const Row& row : rows) {
        INFO(row.what);
        const apogee::harness::Resolution resolved = resolve_backend(active, row.request);
        CHECK(resolved.key == row.key);
        CHECK(resolved.from == row.from);
    }

    // The same suite, not active: nothing it says is read.
    const Config inactive = suite_config(false);
    CHECK(resolve_backend(inactive, RoleRequest{.role = ModelRole::Embedding}).key ==
          "embedding-p");
    CHECK(resolve_backend(inactive, RoleRequest{.role = ModelRole::Chat}).from ==
          ResolvedFrom::Default);
    // Nor when the active name has no entry -- a session's view whose suite
    // was deleted from the file under it.
    Config dangling = suite_config(true);
    dangling.models.default_suite = "gone";
    CHECK(resolve_backend(dangling, RoleRequest{.role = ModelRole::Utility}).key == "utility-p");
}

TEST_CASE("with a suite active, every question answers as the chain says, exhaustively",
          "[harness][roles][suites]") {
    // The whole matrix, against the rule stated once: an override or a pin
    // answers first; then the suite, for the roles it names; and otherwise
    // the answer the same config gives with no suite at all.
    using apogee::harness::resolve_backend;
    using apogee::harness::ResolvedFrom;
    const Config active = suite_config(true);
    const Config inactive = suite_config(false);
    for (const ModelRole role : {ModelRole::Chat, ModelRole::Embedding, ModelRole::Extraction,
                                 ModelRole::Vision, ModelRole::Transcription, ModelRole::Utility}) {
        for (const std::string_view override : {"", "picked"}) {
            for (const std::string_view entry : {"", "entry"}) {
                for (const std::string_view conversation : {"", "on"}) {
                    const RoleRequest request{.role = role,
                                              .override = override,
                                              .entry_backend = entry,
                                              .conversation = conversation};
                    const apogee::harness::Resolution with = resolve_backend(active, request);
                    const apogee::harness::Resolution without = resolve_backend(inactive, request);
                    const std::string_view named = apogee::harness::suite_role(role);
                    const bool speaks = active.find_suite("s")->members.contains(named);
                    CAPTURE(named, override, entry, conversation);
                    if (!override.empty() || !entry.empty() || !speaks) {
                        CHECK(with.key == without.key);
                        CHECK(with.from == without.from);
                    } else {
                        CHECK(with.from == ResolvedFrom::Suite);
                        CHECK(with.key == "suite-" + std::string{named});
                    }
                }
            }
        }
    }
}

TEST_CASE("a suite's member is named, as a pointer is; the roles have suite names",
          "[harness][roles][suites]") {
    using apogee::harness::is_named;
    using apogee::harness::ResolvedFrom;
    using apogee::harness::suite_role;
    CHECK(is_named(ResolvedFrom::RolePointer));
    CHECK(is_named(ResolvedFrom::Suite));
    for (const ResolvedFrom from :
         {ResolvedFrom::Nothing, ResolvedFrom::Override, ResolvedFrom::EntryBackend,
          ResolvedFrom::Conversation, ResolvedFrom::Default}) {
        CHECK_FALSE(is_named(from));
    }
    // Every role's suite name is one the config accepts, in its order.
    const auto accepted = apogee::harness::suite_role_names();
    std::vector<std::string_view> names;
    for (const ModelRole role : {ModelRole::Chat, ModelRole::Embedding, ModelRole::Extraction,
                                 ModelRole::Vision, ModelRole::Transcription, ModelRole::Utility}) {
        names.push_back(suite_role(role));
    }
    CHECK(names == std::vector<std::string_view>(accepted.begin(), accepted.end()));
}
