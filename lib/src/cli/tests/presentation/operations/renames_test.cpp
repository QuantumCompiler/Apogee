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
    CHECK_THAT(err, ContainsSubstring("no stored model named 'org--nope'"));
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
