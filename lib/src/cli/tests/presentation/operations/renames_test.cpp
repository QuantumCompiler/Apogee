#include "operations/renames.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "backends/model_roster.h"
#include "contracts/config.h"
#include "logger/session.h"
#include "support/cli_home.h"

/// Renames that propagate (46a), through the real commands in process: a
/// backend's key and every reference in one edit, the saved chats swept and
/// an old one resuming on the new name; the store following a backend only
/// where the mapping is one to one, and said where it is not; a stored
/// model's directories moved with what points into them, the backends named
/// after it renamed, a taken name left and said; and every refusal named.
namespace {

namespace fs = std::filesystem;
using Catch::Matchers::ContainsSubstring;

[[nodiscard]] std::string slurp(const fs::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

void write_file(const fs::path& path, const std::string& bytes = "x") {
    fs::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << bytes;
}

/// `apogee <args>` with `input` on stdin.
int run(const apogee::testing::CliHome& home, const std::vector<std::string>& args,
        std::string* out, std::string* err, const std::string& input = {}) {
    const std::istringstream fed{input};
    std::streambuf* old_in = std::cin.rdbuf(fed.rdbuf());
    const int code = home.run(args, out, err);
    std::cin.rdbuf(old_in);
    std::cin.clear();
    return code;
}

/// A store holding `org--gemma`'s GGUF, with a record riding inside it.
struct StoreHome {
    apogee::testing::CliHome home{std::string{}};
    fs::path weights = home.home() / "models" / "org--gemma" / "gguf" / "111111111111";
    fs::path file = weights / "gemma-Q4_K_M.gguf";

    /// `names`, each a llamacpp backend running the stored file.
    explicit StoreHome(const std::vector<std::string>& names) {
        write_file(file, "GGUF");
        write_file(weights / "gemma-Q4_K_M.gguf.record.json", "{\"record\": true}");
        std::string backends;
        for (const std::string& name : names) {
            backends += entry(name);
        }
        std::ofstream{home.config_path(), std::ios::binary | std::ios::trunc}
            << "backends:\n"
            << backends << "models:\n  default: gemma-Q4KM\n";
    }

    [[nodiscard]] std::string at(std::string_view model) const {
        return (home.home() / "models" / std::string{model} / "gguf" / "111111111111" /
                "gemma-Q4_K_M.gguf")
            .string();
    }

    [[nodiscard]] std::string entry(std::string_view name) const {
        return "  " + std::string{name} +
               ":\n    type: llamacpp\n    model_path: " + file.string() + "\n";
    }
};

}  // namespace

TEST_CASE(
    "config rename-backend: every reference in one edit, the chats swept, an old one resuming",
    "[renames][config]") {
    const apogee::testing::CliHome home{std::string{}};
    std::ofstream{home.home() / "script.json"}
        << R"({"turns": [{"text": "said <{{last_user}}>"}]})";
    std::ofstream{home.config_path(), std::ios::binary | std::ios::trunc}
        << "backends:\n  # the model we use\n  helper:   # a mock\n    type: mock\n"
           "    model_path: " +
               (home.home() / "script.json").string() +
               "\n"
               "models:\n  default: helper   # the chat\nmemory:\n  recall: false\n"
               "suites:\n  duo:\n    members:\n      chat: helper\n";
    std::string out;
    std::string err;
    REQUIRE(run(home, {"chat"}, &out, &err, "hi\n") == 0);
    std::vector<apogee::logger::Session> saved = apogee::logger::list_sessions();
    REQUIRE(saved.size() == 1);
    const std::string updated = saved.front().updated_at;
    const std::string before = slurp(home.config_path());

    REQUIRE(run(home, {"config", "rename-backend", "helper", "assistant"}, &out, &err) == 0);
    CHECK_THAT(out, ContainsSubstring("renamed backend 'helper' to 'assistant'"));
    CHECK_THAT(out, ContainsSubstring("1 saved chat now names 'assistant'"));
    // The file: the key and its two references, nothing else.
    std::string expected = before;
    expected.replace(expected.find("  helper:   # a mock"), 20, "  assistant:   # a mock");
    expected.replace(expected.find("default: helper"), 15, "default: assistant");
    expected.replace(expected.find("chat: helper"), 12, "chat: assistant");
    CHECK(slurp(home.config_path()) == expected);
    // The chat names it, its last turn's time kept -- and resumes on it.
    saved = apogee::logger::list_sessions();
    REQUIRE(saved.size() == 1);
    CHECK(saved.front().backend == "assistant");
    CHECK(saved.front().updated_at == updated);
    REQUIRE(run(home, {"chat", "--resume", saved.front().chat_id}, &out, &err, "again\n") == 0);
    CHECK_THAT(out, ContainsSubstring("said <again>"));
    CHECK_FALSE(err.find("no longer in the config") != std::string::npos);

    // Refusals, each named, the file untouched.
    const std::string settled = slurp(home.config_path());
    CHECK(run(home, {"config", "rename-backend", "nope", "x"}, &out, &err) == 1);
    CHECK_THAT(err, ContainsSubstring("no backend named 'nope'"));
    CHECK(run(home, {"config", "rename-backend", "assistant", "assistant"}, &out, &err) == 1);
    CHECK_THAT(err, ContainsSubstring("'assistant' is already its name"));
    CHECK(run(home, {"config", "rename-backend", "assistant", "two words"}, &out, &err) == 1);
    CHECK_THAT(err, ContainsSubstring("cannot name a backend"));
    CHECK(slurp(home.config_path()) == settled);
}

TEST_CASE("the store follows a backend only where the mapping is one to one, and says why not",
          "[renames][config][coupling]") {
    {
        // One to one: named the M3 way, the only referrer -- the directory moves.
        const StoreHome store{{"gemma-Q4KM"}};
        std::string out;
        std::string err;
        REQUIRE(run(store.home, {"config", "rename-backend", "gemma-Q4KM", "mine-Q4KM"}, &out,
                    &err) == 0);
        CHECK_THAT(out, ContainsSubstring("moved store 'org--gemma' to 'org--mine'"));
        CHECK_FALSE(fs::exists(store.home.home() / "models" / "org--gemma"));
        CHECK(fs::exists(store.home.home() / "models" / "org--mine" / "gguf" / "111111111111" /
                         "gemma-Q4_K_M.gguf.record.json"));
        const apogee::harness::Config config =
            apogee::harness::load_config(store.home.config_path());
        REQUIRE(config.find_backend("mine-Q4KM") != nullptr);
        CHECK(config.find_backend("mine-Q4KM")->model_path == store.at("org--mine"));
        CHECK(config.models.default_backend == "mine-Q4KM");
    }
    {
        // Shared: another backend runs the same model -- the store stays.
        const StoreHome store{{"gemma-Q4KM", "gemma-F16"}};
        std::string out;
        std::string err;
        REQUIRE(run(store.home, {"config", "rename-backend", "gemma-Q4KM", "mine-Q4KM"}, &out,
                    &err) == 0);
        CHECK_THAT(out, ContainsSubstring(
                            "store 'org--gemma' unchanged -- 'gemma-F16' still points at it"));
        CHECK(fs::exists(store.file));
    }
    {
        // Underived: a name the user chose -- the store stays.
        const StoreHome store{{"local"}};
        std::string out;
        std::string err;
        REQUIRE(run(store.home, {"config", "rename-backend", "local", "other"}, &out, &err) == 0);
        CHECK_THAT(out, ContainsSubstring(
                            "store 'org--gemma' unchanged -- 'local' is not named after it"));
        CHECK(fs::exists(store.file));
    }
}

TEST_CASE(
    "models rename moves the directories, repoints every path, renames the backends named after it",
    "[renames][models]") {
    const StoreHome store{{"gemma-Q4KM", "gemma-F16", "mine", "gem-F16"}};
    std::string out;
    std::string err;
    REQUIRE(run(store.home, {"models", "rename", "org--gemma", "org--gem"}, &out, &err) == 0);
    CHECK_THAT(out, ContainsSubstring("renamed stored model 'org--gemma' to 'org--gem'"));
    CHECK_THAT(out, ContainsSubstring("repointed 4 backends"));
    CHECK_THAT(out, ContainsSubstring("renamed backend 'gemma-Q4KM' to 'gem-Q4KM'"));
    CHECK_THAT(out,
               ContainsSubstring(
                   "left backend 'gemma-F16' as it is -- 'gem-F16' is already a backend's name"));
    // The records rode inside; every path points at the new directory.
    CHECK(fs::exists(store.home.home() / "models" / "org--gem" / "gguf" / "111111111111" /
                     "gemma-Q4_K_M.gguf.record.json"));
    CHECK_FALSE(fs::exists(store.home.home() / "models" / "org--gemma"));
    const apogee::harness::Config config = apogee::harness::load_config(store.home.config_path());
    for (const std::string name : {"gem-Q4KM", "gemma-F16", "mine", "gem-F16"}) {
        INFO(name);
        REQUIRE(config.find_backend(name) != nullptr);
        CHECK(config.find_backend(name)->model_path == store.at("org--gem"));
    }
    CHECK(config.find_backend("gemma-Q4KM") == nullptr);
    CHECK(config.models.default_backend == "gem-Q4KM");  // the reference followed

    // Refusals, each named.
    CHECK(run(store.home, {"models", "rename", "org--nope", "org--x"}, &out, &err) == 1);
    CHECK_THAT(err, ContainsSubstring("no model named 'org--nope'"));
    CHECK(run(store.home, {"models", "rename", "org--gem", "a/b"}, &out, &err) == 1);
    CHECK_THAT(err, ContainsSubstring("cannot name a stored model"));
    write_file(store.home.home() / "models" / "org--other" / "gguf" / "222222222222" / "o.gguf");
    CHECK(run(store.home, {"models", "rename", "org--gem", "org--other"}, &out, &err) == 1);
    CHECK_THAT(err, ContainsSubstring("a stored model named 'org--other' already exists"));
    write_file(store.home.home() / "models" / "org--gem" / "gguf" / "333333333333" /
               "next.gguf.partial");
    CHECK(run(store.home, {"models", "rename", "org--gem", "org--free"}, &out, &err) == 1);
    CHECK_THAT(err, ContainsSubstring("'org--gem' is being written"));
}

TEST_CASE("models rename to a name keeping the base leaves its backends, saying nothing of them",
          "[renames][models]") {
    // `org--gemma` to `gemma`: the base is `gemma` both before and after, so
    // `gemma-Q4KM` already follows the model -- repointed, never reported as
    // a name taken by itself.
    const StoreHome store{{"gemma-Q4KM"}};
    std::string out;
    std::string err;
    REQUIRE(run(store.home, {"models", "rename", "org--gemma", "gemma"}, &out, &err) == 0);
    CHECK_THAT(out, ContainsSubstring("renamed stored model 'org--gemma' to 'gemma'"));
    CHECK_THAT(out, ContainsSubstring("repointed 1 backend: gemma-Q4KM"));
    CHECK(out.find("left backend") == std::string::npos);
    CHECK(out.find("renamed backend") == std::string::npos);
    const apogee::harness::Config config = apogee::harness::load_config(store.home.config_path());
    REQUIRE(config.find_backend("gemma-Q4KM") != nullptr);
    CHECK(config.find_backend("gemma-Q4KM")->model_path == store.at("gemma"));
}

TEST_CASE("the M3 spelling is read as M3 writes it", "[renames][coupling]") {
    using apogee::commands::derived_level;
    CHECK(apogee::commands::model_base_name("google--gemma-4-E2B") == "gemma-4-E2B");
    CHECK(apogee::commands::model_base_name("local-model") == "local-model");
    CHECK(derived_level("gemma-4-E2B-Q4KM", "google--gemma-4-E2B") ==
          std::optional<std::string>{"Q4KM"});
    CHECK(derived_level("gemma-4-E2B-F16", "google--gemma-4-E2B") ==
          std::optional<std::string>{"F16"});
    CHECK_FALSE(derived_level("gemma-4-E2B", "google--gemma-4-E2B").has_value());
    CHECK_FALSE(derived_level("mine", "google--gemma-4-E2B").has_value());
    CHECK_FALSE(derived_level("gemma-4-E2B-Q4_K_M", "google--gemma-4-E2B").has_value());
}

// --- a model by the name its backends share (the user's request) ------------

namespace {

/// A stored model `dir`, its F16 and Q4_K_M files, and a backend for each
/// named `<base>-F16` / `<base>-Q4KM` -- plus any `others`, each a backend
/// running the Q4_K_M file too.
struct FamilyHome {
    apogee::testing::CliHome home{std::string{}};
    fs::path f16;
    fs::path q4;

    FamilyHome(const std::string& dir, const std::string& base,
               const std::vector<std::string>& others = {}) {
        const fs::path models = home.home() / "models" / dir / "gguf";
        f16 = models / "111111111111" / "m-F16.gguf";
        q4 = models / "222222222222" / "m-Q4_K_M.gguf";
        write_file(f16, "GGUF");
        write_file(q4, "GGUF");
        std::string config = "backends:\n";
        const auto entry = [&config](const std::string& name, const fs::path& file) {
            config +=
                "  " + name + ":\n    type: llamacpp\n    model_path: " + file.string() + "\n";
        };
        entry(base + "-F16", f16);
        entry(base + "-Q4KM", q4);
        for (const std::string& other : others) {
            entry(other, q4);
        }
        config += "models:\n  default: " + base + "-Q4KM\n";
        std::ofstream{home.config_path(), std::ios::binary | std::ios::trunc} << config;
    }

    [[nodiscard]] fs::path dir(const std::string& name) const {
        return home.home() / "models" / name;
    }
};

}  // namespace

TEST_CASE(
    "models rename by the backends' shared name renames every quantization, the folder following",
    "[renames][models][family]") {
    // The folder kept its pulled name while the backends were renamed by
    // hand: the model is what the backends call it.
    const FamilyHome store{"google--gemma-4-12B-it-assistant", "Gemma4-12B-Assistant"};
    std::string out;
    std::string err;
    REQUIRE(run(store.home, {"models", "rename", "Gemma4-12B-Assistant", "Gemma4-12B-Helper"}, &out,
                &err) == 0);
    CHECK_THAT(out, ContainsSubstring(
                        "renamed backend 'Gemma4-12B-Assistant-F16' to 'Gemma4-12B-Helper-F16'"));
    CHECK_THAT(out, ContainsSubstring(
                        "renamed backend 'Gemma4-12B-Assistant-Q4KM' to 'Gemma4-12B-Helper-Q4KM'"));
    CHECK_THAT(out, ContainsSubstring(
                        "moved store 'google--gemma-4-12B-it-assistant' to 'Gemma4-12B-Helper'"));
    CHECK_FALSE(fs::exists(store.dir("google--gemma-4-12B-it-assistant")));
    const apogee::harness::Config config = apogee::harness::load_config(store.home.config_path());
    REQUIRE(config.find_backend("Gemma4-12B-Helper-F16") != nullptr);
    REQUIRE(config.find_backend("Gemma4-12B-Helper-Q4KM") != nullptr);
    CHECK(config.find_backend("Gemma4-12B-Helper-Q4KM")->model_path ==
          (store.dir("Gemma4-12B-Helper") / "gguf" / "222222222222" / "m-Q4_K_M.gguf").string());
    CHECK(fs::exists(config.find_backend("Gemma4-12B-Helper-F16")->model_path));
    CHECK(config.models.default_backend == "Gemma4-12B-Helper-Q4KM");
}

TEST_CASE("models rename to the name it already has brings a drifted folder into line",
          "[renames][models][family]") {
    const FamilyHome store{"google--gemma-4-E4B-it-assistant", "Gemma4-E4B-Assistant"};
    std::string out;
    std::string err;
    REQUIRE(run(store.home, {"models", "rename", "Gemma4-E4B-Assistant", "Gemma4-E4B-Assistant"},
                &out, &err) == 0);
    CHECK_THAT(out,
               ContainsSubstring(
                   "moved store 'google--gemma-4-E4B-it-assistant' to 'Gemma4-E4B-Assistant'"));
    CHECK(out.find("renamed backend") == std::string::npos);
    CHECK(fs::exists(store.dir("Gemma4-E4B-Assistant")));
    // In line now: nothing left to do, and said so.
    CHECK(run(store.home, {"models", "rename", "Gemma4-E4B-Assistant", "Gemma4-E4B-Assistant"},
              &out, &err) == 1);
    CHECK_THAT(err, ContainsSubstring("'Gemma4-E4B-Assistant' is already its name"));
}

TEST_CASE("a folder another backend runs from stays where it is when the family is renamed",
          "[renames][models][family]") {
    const FamilyHome store{"Gemma4-E2B", "Gemma4-E2B", {"mine"}};
    std::string out;
    std::string err;
    REQUIRE(run(store.home, {"models", "rename", "Gemma4-E2B", "Gemma4-Tiny"}, &out, &err) == 0);
    CHECK_THAT(out, ContainsSubstring("renamed backend 'Gemma4-E2B-F16' to 'Gemma4-Tiny-F16'"));
    CHECK_THAT(out, ContainsSubstring("store 'Gemma4-E2B' unchanged -- 'mine' also runs from it"));
    CHECK(fs::exists(store.q4));
}

TEST_CASE("models rename completes the models by their backends' name; config verbs the backends",
          "[renames][completion]") {
    const FamilyHome store{"google--gemma-4-12B-it-assistant", "Gemma4-12B-Assistant"};
    // A stored model nothing runs, by its folder.
    write_file(store.home.home() / "models" / "org--unrun" / "gguf" / "333333333333" / "u.gguf");
    // A cloud backend with its roster cached: a shelf `-m` would offer.
    std::string config = slurp(store.home.config_path());
    config.insert(config.find("backends:\n") + 10, "  claude:\n    type: anthropic\n");
    std::ofstream{store.home.config_path(), std::ios::binary | std::ios::trunc} << config;
    apogee::backends::RosterCache cache;
    cache.rosters["anthropic"] =
        apogee::backends::ProviderRoster{{{"claude-opus-5-5", "Claude Opus 5.5"}}, "2026-10-14"};
    REQUIRE(apogee::backends::save_roster_cache(cache).empty());
    std::string out;
    std::string err;
    REQUIRE(store.home.run({"__complete", "models", "rename", ""}, &out, &err) == 0);
    CHECK(out == "Gemma4-12B-Assistant\norg--unrun\n");
    // The config side offers the backends' names, never a handle or a shelf.
    REQUIRE(store.home.run({"__complete", "config", "rename-backend", ""}, &out, &err) == 0);
    CHECK(out == "claude\nGemma4-12B-Assistant-F16\nGemma4-12B-Assistant-Q4KM\n");
    REQUIRE(store.home.run({"__complete", "config", "set-default", "c"}, &out, &err) == 0);
    CHECK(out == "claude\n");  // no `claude:`, no roster id
    // A session's -m keeps the shelves: a model to run, not a backend's name.
    REQUIRE(store.home.run({"__complete", "chat", "-m", "c"}, &out, &err) == 0);
    CHECK(out == "claude\nclaude:\nclaude-opus-5-5\n");
}

TEST_CASE("the quantization level is read as M3 spells it", "[renames][family]") {
    using apogee::commands::quant_level;
    CHECK(quant_level("Gemma4-12B-Instruct-Q4KM") == std::optional<std::string>{"Q4KM"});
    CHECK(quant_level("Gemma4-12B-Instruct-F16") == std::optional<std::string>{"F16"});
    CHECK(quant_level("x-BF16") == std::optional<std::string>{"BF16"});
    CHECK(quant_level("x-Q80") == std::optional<std::string>{"Q80"});
    CHECK(quant_level("x-IQ4XS") == std::optional<std::string>{"IQ4XS"});
    CHECK_FALSE(quant_level("Gemma4-12B-Instruct").has_value());
    CHECK_FALSE(quant_level("local").has_value());
    CHECK_FALSE(quant_level("x-q4km").has_value());
}
