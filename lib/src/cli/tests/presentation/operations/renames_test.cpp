#include "operations/renames.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "backends/mlx_local.h"
#include "backends/model_roster.h"
#include "contracts/config.h"
#include "contracts/config_edit.h"
#include "logger/session.h"
#include "modelstore/mlx_info.h"
#include "modelstore/snapshot.h"
#include "platform/platform.h"
#include "support/cli_home.h"
#include "support/draft_head_configs.h"
#include "support/file_time.h"
#include "support/gguf_builder.h"
#include "support/mlx_model.h"

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

// --- a drafter beside its base (M25) ----------------------------------------

namespace {

/// One MLX set at `dir` as a conversion leaves it: `config` -- one of 53i's
/// (`support/draft_head_configs.h`) -- beside its weights, and its record
/// written through `write_snapshot` with its `kind` and, for a drafter, the
/// `bases` it was converted beside.
void mlx_set(const fs::path& dir, const char* config, const std::string& kind,
             const std::vector<std::string>& bases = {}) {
    write_file(dir / "config.json", config);
    write_file(dir / "model.safetensors", std::string(4096, 'w'));
    apogee::models::Snapshot record;
    record.ref = dir.parent_path().parent_path().filename().string() + "/safetensors/eeeeeeeeeeee";
    record.source = "convert";
    record.pulled_at = "2026-10-21T09:00:00Z";
    record.transform = kind == "drafter" ? "mlx_vlm.convert bf16" : "mlx_vlm.convert 4bit";
    record.kind = kind;
    record.bases = bases;
    record.files = {{.path = "config.json", .size = 412, .sha256 = std::string(64, 'c')},
                    {.path = "model.safetensors", .size = 4096, .sha256 = std::string(64, 'w')}};
    REQUIRE(apogee::models::write_snapshot(dir, record));
}

/// The user's E4B pair as 53i leaves it: the base, mlx-vlm's build, under
/// `Gemma4-E4B-Instruct`; its assistant's drafter under
/// `Gemma4-E4B-Assistant`, its record naming that base; an mlx backend
/// `name` over the base drafting beside it; and an environment whose files
/// the ladder reads as ready -- mlx-lm, an mlx-vlm drafting Gemma 4's
/// assistants, the driver.
struct DraftedHome {
    apogee::testing::CliHome home{std::string{}};
    const apogee::testing::DraftHeadPair& e4b = apogee::testing::kUserDraftHeads[3];
    fs::path base = home.models() / e4b.base_model / "mlx" / "bbbbbbbbbbbb";
    fs::path drafter = home.models() / e4b.head_model / "mlx" / "aaaaaaaaaaaa";

    explicit DraftedHome(const std::string& name) {
        mlx_set(base, e4b.base_config, "vision");
        mlx_set(drafter, e4b.head_config, "drafter", {e4b.base_model});
        std::ofstream{home.config_path(), std::ios::binary | std::ios::trunc}
            << "backends:\n  " << name << ":\n    type: mlx\n    model_path: " << base.string()
            << "\n    draft: " << drafter.string() << "\nmodels:\n  default: " << name << "\n";
        const apogee::backends::MlxHost host = apogee::backends::MlxHost::at(home.home());
        write_file(host.venv / "bin" / "python", "");
        const fs::path site = host.venv / "lib" / "python3.14" / "site-packages";
        write_file(site / "mlx_lm" / "__init__.py", "");
        write_file(site / "mlx_lm" / "_version.py", "__version__ = '0.32.0'\n");
        write_file(site / "mlx_vlm" / "__init__.py", "");
        fs::create_directories(site / "mlx_vlm-0.6.1.dist-info");
        write_file(
            site / "mlx_vlm" / "speculative" / "drafters" / "gemma4_assistant" / "__init__.py",
            "from .gemma4_assistant import Gemma4AssistantDraftModel as Model\n");
        write_file(host.driver, "");
    }

    [[nodiscard]] apogee::harness::BackendConfig entry(const std::string& name) const {
        const apogee::harness::Config config = apogee::harness::load_config(home.config_path());
        REQUIRE(config.find_backend(name) != nullptr);
        return *config.find_backend(name);
    }

    /// The ladder's verdict on backend `name` -- 53i's `Drafter` and `Draft`
    /// rungs among its own -- read on any host as Apple silicon's.
    [[nodiscard]] apogee::backends::MlxReadiness ladder(const std::string& name) const {
        apogee::backends::MlxHost host = apogee::backends::MlxHost::at(home.home());
        host.target = std::string{apogee::backends::kMlxTarget};
        return apogee::backends::probe_mlx_backend(name, entry(name), host);
    }

    /// 53i's pairing rule over backend `name`'s files: empty when it pairs.
    [[nodiscard]] std::string pairing(const std::string& name) const {
        const apogee::harness::BackendConfig backend = entry(name);
        return apogee::models::mlx_drafter_refusal(backend.model_path, backend.draft,
                                                   home.models());
    }

    /// `models info <subject> -q`'s line starting `label`.
    [[nodiscard]] std::string info_line(const std::string& subject,
                                        const std::string& label) const {
        std::string out;
        REQUIRE(home.run({"models", "info", subject, "-q"}, &out) == 0);
        std::istringstream lines{out};
        for (std::string line; std::getline(lines, line);) {
            if (line.starts_with(label)) {
                return line;
            }
        }
        FAIL("no " << label << " line in: " << out);
        return {};
    }

    /// `models info <name> -q`'s runtime line, the ladder's verdict as this
    /// host reads it: ready on Apple silicon, the platform's refusal elsewhere.
    [[nodiscard]] std::string runtime(const std::string& name) const {
        const std::string line = info_line(name, "runtime:");
        if (apogee::platform::host_target() == apogee::backends::kMlxTarget) {
            CHECK(line.starts_with("runtime:      ready"));
        }
        return line;
    }

    [[nodiscard]] static std::string record(const fs::path& dir) {
        return slurp(apogee::models::snapshot_record_path(dir));
    }
};

/// Whether the record in `dir` was last written longer than `ago` ago: one a
/// test aged, and nothing wrote again.
[[nodiscard]] bool written_before(const fs::path& dir, std::chrono::hours ago) {
    return fs::last_write_time(apogee::models::snapshot_record_path(dir)) <
           fs::file_time_type::clock::now() - ago;
}

/// `text` with every `from` replaced by `to`.
[[nodiscard]] std::string replaced(std::string text, const std::string& from,
                                   const std::string& to) {
    for (std::size_t at = text.find(from); at != std::string::npos;
         at = text.find(from, at + to.size())) {
        text.replace(at, from.size(), to);
    }
    return text;
}

}  // namespace

TEST_CASE("models rename of a drafter's folder repoints the draft beside its base, still paired",
          "[renames][models][drafter]") {
    const DraftedHome drafted{"e4b-4bit"};
    REQUIRE(drafted.ladder("e4b-4bit").ready());
    REQUIRE(drafted.pairing("e4b-4bit").empty());
    const std::string runtime = drafted.runtime("e4b-4bit");
    const std::string config = slurp(drafted.home.config_path());
    const std::string record = DraftedHome::record(drafted.drafter);

    std::string out;
    std::string err;
    REQUIRE(run(drafted.home, {"models", "rename", "Gemma4-E4B-Assistant", "Gemma4-E4B-Drafter"},
                &out, &err) == 0);
    INFO(out);
    CHECK_THAT(out, ContainsSubstring(
                        "renamed stored model 'Gemma4-E4B-Assistant' to 'Gemma4-E4B-Drafter'"));
    CHECK_THAT(out, ContainsSubstring("repointed 1 backend: e4b-4bit"));
    // Named after the base it runs, never after the drafter beside it.
    CHECK(out.find("renamed backend") == std::string::npos);
    CHECK(out.find("now names") == std::string::npos);  // no record names it as a base

    // The draft: line alone, its one segment; the record rode inside, as it was.
    const fs::path moved = drafted.home.models() / "Gemma4-E4B-Drafter" / "mlx" / "aaaaaaaaaaaa";
    CHECK_FALSE(fs::exists(drafted.drafter));
    CHECK(slurp(drafted.home.config_path()) ==
          replaced(config, drafted.drafter.string(), apogee::harness::yaml_scalar(moved.string())));
    CHECK(DraftedHome::record(moved) == record);

    // Still paired, with no conversion: the ladder and models info as before.
    CHECK(drafted.entry("e4b-4bit").draft == moved.string());
    CHECK(drafted.pairing("e4b-4bit").empty());
    CHECK(drafted.ladder("e4b-4bit").ready());
    CHECK(drafted.runtime("e4b-4bit") == runtime);
    CHECK(drafted.info_line("e4b-4bit", "draft:").find(moved.string()) != std::string::npos);
}

TEST_CASE("models rename of a base's folder rewrites its drafter's recorded base, and no other",
          "[renames][models][drafter]") {
    const DraftedHome drafted{"e4b-4bit"};
    // A drafter made beside another base, and one pulled with none recorded.
    const fs::path other = drafted.home.models() / "Gemma4-E4B-Other" / "mlx" / "cccccccccccc";
    const fs::path pulled =
        drafted.home.models() / "mlx-community--gemma-4-E4B-it-assistant" / "mlx" / "dddddddddddd";
    mlx_set(other, drafted.e4b.head_config, "drafter", {"Gemma4-E4B-Custom"});
    mlx_set(pulled, drafted.e4b.head_config, "drafter");
    const fs::file_time_type aged = fs::file_time_type::clock::now() - std::chrono::hours{48};
    for (const fs::path& dir : {other, pulled}) {
        apogee::testing::set_modified_time(apogee::models::snapshot_record_path(dir), aged);
    }
    const std::string other_record = DraftedHome::record(other);
    const std::string pulled_record = DraftedHome::record(pulled);
    REQUIRE(drafted.ladder("e4b-4bit").ready());
    const std::string runtime = drafted.runtime("e4b-4bit");
    const std::string config = slurp(drafted.home.config_path());
    const std::string record = DraftedHome::record(drafted.drafter);
    const std::string handle = "Gemma4-E4B-Assistant/mlx/aaaaaaaaaaaa";
    CHECK_THAT(drafted.info_line(handle, "drafter:"),
               ContainsSubstring("its record naming Gemma4-E4B-Instruct"));

    std::string out;
    std::string err;
    REQUIRE(run(drafted.home, {"models", "rename", "Gemma4-E4B-Instruct", "Gemma4-E4B-Base"}, &out,
                &err) == 0);
    INFO(out);
    CHECK_THAT(
        out, ContainsSubstring("renamed stored model 'Gemma4-E4B-Instruct' to 'Gemma4-E4B-Base'"));
    CHECK_THAT(out, ContainsSubstring("repointed 1 backend: e4b-4bit"));
    CHECK_THAT(out,
               ContainsSubstring("drafter " + handle + " now names 'Gemma4-E4B-Base' as its base"));
    CHECK(out.find("Gemma4-E4B-Other/") == std::string::npos);
    CHECK(out.find("mlx-community--") == std::string::npos);

    // The model_path line's segment; the record's base, and nothing else in it.
    const fs::path moved = drafted.home.models() / "Gemma4-E4B-Base" / "mlx" / "bbbbbbbbbbbb";
    CHECK(slurp(drafted.home.config_path()) ==
          replaced(config, drafted.base.string(), apogee::harness::yaml_scalar(moved.string())));
    CHECK(DraftedHome::record(drafted.drafter) ==
          replaced(record, "\"Gemma4-E4B-Instruct\"", "\"Gemma4-E4B-Base\""));
    CHECK(apogee::models::load_snapshot(drafted.drafter)->bases ==
          std::vector<std::string>{"Gemma4-E4B-Base"});
    CHECK(fs::exists(drafted.drafter));  // its set keeps its folder and its id

    // Still paired, with no conversion: the ladder and models info as before.
    CHECK(drafted.pairing("e4b-4bit").empty());
    CHECK(drafted.ladder("e4b-4bit").ready());
    CHECK(drafted.runtime("e4b-4bit") == runtime);
    CHECK_THAT(drafted.info_line(handle, "drafter:"),
               ContainsSubstring("its record naming Gemma4-E4B-Base"));

    // No other record written, and no pairing made that did not hold: the
    // drafter made beside another base is still refused beside this one.
    CHECK(DraftedHome::record(other) == other_record);
    CHECK(DraftedHome::record(pulled) == pulled_record);
    CHECK(written_before(other, std::chrono::hours{24}));
    CHECK(written_before(pulled, std::chrono::hours{24}));
    CHECK_THAT(apogee::models::mlx_drafter_refusal(moved, other, drafted.home.models()),
               ContainsSubstring("'s record names Gemma4-E4B-Custom as its base"));
    CHECK(apogee::models::load_snapshot(pulled)->bases.empty());
}

TEST_CASE("a model renamed by its backends' name takes its drafter's recorded base with it",
          "[renames][models][family][drafter]") {
    // 46a's family rename over an mlx entry named the M3 way, drafting.
    const DraftedHome drafted{"Gemma4-E4B-Instruct-BF16"};
    REQUIRE(drafted.ladder("Gemma4-E4B-Instruct-BF16").ready());
    const std::string runtime = drafted.runtime("Gemma4-E4B-Instruct-BF16");
    std::string out;
    std::string err;
    REQUIRE(run(drafted.home, {"models", "rename", "Gemma4-E4B-Instruct", "Gemma4-E4B-Base"}, &out,
                &err) == 0);
    INFO(out);
    CHECK_THAT(out, ContainsSubstring("renamed backend 'Gemma4-E4B-Instruct-BF16' to "
                                      "'Gemma4-E4B-Base-BF16'"));
    // Its weights' folder follows; the drafter's, another model's, stays.
    CHECK_THAT(out, ContainsSubstring("moved store 'Gemma4-E4B-Instruct' to 'Gemma4-E4B-Base'"));
    CHECK_THAT(out, ContainsSubstring("drafter Gemma4-E4B-Assistant/mlx/aaaaaaaaaaaa now names "
                                      "'Gemma4-E4B-Base' as its base"));
    const apogee::harness::BackendConfig renamed = drafted.entry("Gemma4-E4B-Base-BF16");
    CHECK(renamed.model_path ==
          (drafted.home.models() / "Gemma4-E4B-Base" / "mlx" / "bbbbbbbbbbbb").string());
    CHECK(renamed.draft == drafted.drafter.string());
    CHECK(drafted.pairing("Gemma4-E4B-Base-BF16").empty());
    CHECK(drafted.ladder("Gemma4-E4B-Base-BF16").ready());
    CHECK(drafted.runtime("Gemma4-E4B-Base-BF16") == runtime);
}

TEST_CASE("config rename-backend takes its folder and its drafter's recorded base with it",
          "[renames][config][coupling][drafter]") {
    const DraftedHome drafted{"Gemma4-E4B-Instruct-BF16"};
    std::string out;
    std::string err;
    REQUIRE(run(drafted.home,
                {"config", "rename-backend", "Gemma4-E4B-Instruct-BF16", "Gemma4-E4B-Base-BF16"},
                &out, &err) == 0);
    INFO(out);
    CHECK_THAT(out, ContainsSubstring("moved store 'Gemma4-E4B-Instruct' to 'Gemma4-E4B-Base'"));
    CHECK_THAT(out, ContainsSubstring("drafter Gemma4-E4B-Assistant/mlx/aaaaaaaaaaaa now names "
                                      "'Gemma4-E4B-Base' as its base"));
    CHECK(drafted.entry("Gemma4-E4B-Base-BF16").draft == drafted.drafter.string());
    CHECK(drafted.pairing("Gemma4-E4B-Base-BF16").empty());
    CHECK(drafted.ladder("Gemma4-E4B-Base-BF16").ready());
}

TEST_CASE("a rename that touches no drafter writes the config and every record as before",
          "[renames][models][drafter]") {
    // A GGUF model renamed in a store that also holds an MLX base and a
    // drafter naming it, neither of them run by anything here.
    const StoreHome store{{"gemma-Q4KM", "mine"}};
    const fs::path base = store.home.models() / "Gemma4-E4B-Instruct" / "mlx" / "bbbbbbbbbbbb";
    const fs::path drafter = store.home.models() / "Gemma4-E4B-Assistant" / "mlx" / "aaaaaaaaaaaa";
    mlx_set(base, apogee::testing::kGemma4BaseE4BConfig, "vision");
    mlx_set(drafter, apogee::testing::kGemma4AssistantE4BConfig, "drafter",
            {"Gemma4-E4B-Instruct"});
    const fs::file_time_type aged = fs::file_time_type::clock::now() - std::chrono::hours{48};
    for (const fs::path& dir : {base, drafter}) {
        apogee::testing::set_modified_time(apogee::models::snapshot_record_path(dir), aged);
    }
    const std::string config = slurp(store.home.config_path());
    const std::string gguf_record = slurp(store.weights / "gemma-Q4_K_M.gguf.record.json");
    const std::string base_record = DraftedHome::record(base);
    const std::string drafter_record = DraftedHome::record(drafter);

    std::string out;
    std::string err;
    REQUIRE(run(store.home, {"models", "rename", "org--gemma", "gemma"}, &out, &err) == 0);
    INFO(out);
    CHECK_THAT(out, ContainsSubstring("repointed 2 backends: gemma-Q4KM, mine"));
    CHECK(out.find("drafter") == std::string::npos);
    // The config: each model_path's one segment, as 46a wrote it -- the
    // value written as the editor writes any (quoted on Windows, for its
    // drive's colon).
    CHECK(slurp(store.home.config_path()) ==
          replaced(config, store.file.string(),
                   apogee::harness::yaml_scalar((store.home.models() / "gemma" / "gguf" /
                                                 "111111111111" / "gemma-Q4_K_M.gguf")
                                                    .string())));
    CHECK(slurp(store.home.models() / "gemma" / "gguf" / "111111111111" /
                "gemma-Q4_K_M.gguf.record.json") == gguf_record);
    // Every MLX record as it was, never written again.
    for (const auto& [dir, bytes] :
         {std::pair{base, base_record}, std::pair{drafter, drafter_record}}) {
        INFO(dir.string());
        CHECK(DraftedHome::record(dir) == bytes);
        CHECK(written_before(dir, std::chrono::hours{24}));
    }
}

// --- every reference, every family (55d) -----------------------------------------------

TEST_CASE("an MLX precision is a level the way the MLX chain names its backends",
          "[renames][family][mlx]") {
    using apogee::commands::quant_level;
    // `<base>-<precision>` (M16), read from the one table the chain names from.
    for (const char* level : {"4bit", "8bit", "3bit", "6bit", "mxfp4", "bf16", "f16"}) {
        INFO(level);
        CHECK(quant_level(std::string{"TestM-"} + level) == std::optional<std::string>{level});
    }
    CHECK_FALSE(quant_level("TestM-4bits").has_value());
    CHECK_FALSE(quant_level("TestM-mlx").has_value());
    CHECK_FALSE(quant_level("x-q4km").has_value());  // GGUF's spelling stays M3's
}

TEST_CASE(
    "models rename by an MLX family's shared name renames every precision, the folder "
    "following",
    "[renames][models][family][mlx]") {
    // The audit's probe (55d): `models rename TestM NewName` over `TestM-4bit`
    // exited 1, advising a GGUF-only spelling.
    const apogee::testing::CliHome home{std::string{}};
    const fs::path folder = home.models() / "org--testm-base" / "mlx";
    apogee::testing::write_mlx_model(folder / "111111111111", {.kind = "chat"});
    apogee::testing::write_mlx_model(folder / "222222222222", {.bits = 8, .kind = "chat"});
    std::ofstream{home.config_path(), std::ios::binary | std::ios::trunc}
        << "backends:\n  TestM-4bit:\n    type: mlx\n    model_path: "
        << (folder / "111111111111").string()
        << "\n  TestM-8bit:\n    type: mlx\n    model_path: " << (folder / "222222222222").string()
        << "\nmodels:\n  default: TestM-4bit\n";
    std::string out;
    std::string err;
    // Completed by the name the backends share, as a GGUF family is.
    REQUIRE(home.run({"__complete", "models", "rename", ""}, &out, &err) == 0);
    CHECK(out == "TestM\n");
    REQUIRE(run(home, {"models", "rename", "TestM", "NewName"}, &out, &err) == 0);
    CHECK_THAT(out, ContainsSubstring("renamed backend 'TestM-4bit' to 'NewName-4bit'"));
    CHECK_THAT(out, ContainsSubstring("renamed backend 'TestM-8bit' to 'NewName-8bit'"));
    CHECK_THAT(out, ContainsSubstring("moved store 'org--testm-base' to 'NewName'"));
    const apogee::harness::Config config = apogee::harness::load_config(home.config_path());
    REQUIRE(config.find_backend("NewName-4bit") != nullptr);
    REQUIRE(config.find_backend("NewName-8bit") != nullptr);
    CHECK(config.find_backend("NewName-8bit")->model_path ==
          (home.models() / "NewName" / "mlx" / "222222222222").string());
    CHECK(fs::exists(config.find_backend("NewName-4bit")->model_path));
    CHECK(config.models.default_backend == "NewName-4bit");
    // A name no backend shares is refused in words that say every engine's
    // levels.
    CHECK(run(home, {"models", "rename", "Nope", "Other"}, &out, &err) == 1);
    CHECK_THAT(err, ContainsSubstring("(the backend without its level: -F16, -Q4KM, -4bit)"));
}

TEST_CASE("models rename on an install with no config moves the folder and reads no config",
          "[renames][models]") {
    // The audit's finding (55d): the folder rename read
    // `models.default_video` through an optional only a config file fills.
    const apogee::testing::CliHome home{std::string{}};
    fs::remove(home.config_path());
    REQUIRE_FALSE(fs::exists(home.config_path()));
    const fs::path file = home.models() / "org--plain" / "gguf" / "333333333333" / "p.gguf";
    write_file(file, apogee::testing::minimal_gguf("llama"));
    std::string out;
    std::string err;
    // As an install with no config runs it: no --config, none found.
    REQUIRE(home.run_default({"models", "rename", "org--plain", "Plain"}, &out, &err) == 0);
    CHECK(out == "renamed stored model 'org--plain' to 'Plain', its records inside it\n");
    CHECK(fs::exists(home.models() / "Plain" / "gguf" / "333333333333" / "p.gguf"));
    CHECK_FALSE(fs::exists(home.models() / "org--plain"));
    // Nothing was written for a config that is not there.
    CHECK_FALSE(fs::exists(home.config_path()));
}

TEST_CASE("models rename of a folder takes every reference into it along, adapters too",
          "[renames][models][store_references]") {
    // The one walk (55d): an adapter applied from the folder follows it, its
    // pin kept; one outside the store and the entry's own weights stay.
    const apogee::testing::CliHome home{std::string{}};
    const fs::path gguf = home.models() / "org--m" / "gguf" / "444444444444" / "m.gguf";
    const fs::path lora = home.models() / "org--lora" / "gguf" / "555555555555" / "style.gguf";
    const fs::path outside = home.home() / "mine.gguf";
    for (const fs::path& path : {gguf, lora, outside}) {
        write_file(path, apogee::testing::minimal_gguf("llama"));
    }
    std::ofstream{home.config_path(), std::ios::binary | std::ios::trunc}
        << "backends:\n  local:\n    type: llamacpp\n    model_path: " << gguf.string()
        << "\n    adapters: [{path: " << lora.string()
        << ", scale: 0.5, base_sha256: " << std::string(64, 'a') << "}, {path: " << outside.string()
        << ", base_sha256: " << std::string(64, 'b') << "}]\nmodels:\n  default: local\n";
    std::string out;
    std::string err;
    REQUIRE(run(home, {"models", "rename", "org--lora", "Style"}, &out, &err) == 0);
    CHECK_THAT(out, ContainsSubstring("repointed 1 backend: local"));
    const apogee::harness::Config config = apogee::harness::load_config(home.config_path());
    const apogee::harness::BackendConfig& local = *config.find_backend("local");
    REQUIRE(local.adapters.size() == 2);
    CHECK(local.adapters[0].path ==
          (home.models() / "Style" / "gguf" / "555555555555" / "style.gguf").string());
    CHECK(local.adapters[0].scale == 0.5);
    CHECK(local.adapters[0].base_sha256 == std::string(64, 'a'));
    CHECK(local.adapters[1].path == outside.string());
    CHECK(local.model_path == gguf.string());
}

TEST_CASE("a folder moved by a family or a backend's rename takes the video pointer along",
          "[renames][models][store_references]") {
    // The one walk (55d): every rename that moves a folder repoints every
    // reference into it -- `models.default_video` too, which only the
    // folder rename followed before.
    const apogee::testing::CliHome home{std::string{}};
    const fs::path folder = home.models() / "org--wan";
    apogee::testing::write_mlx_model(folder / "mlx" / "666666666666", {.kind = "chat"});
    write_file(folder / "pipeline" / "777777777777" / "model_index.json",
               R"({"_class_name": "WanPipeline", "_diffusers_version": "0.35.0"})");
    std::ofstream{home.config_path(), std::ios::binary | std::ios::trunc}
        << "backends:\n  wan-4bit:\n    type: mlx\n    model_path: "
        << (folder / "mlx" / "666666666666").string()
        << "\nmodels:\n  default: wan-4bit\n  default_video: org--wan\n";
    std::string out;
    std::string err;

    SECTION("by the family's name") {
        REQUIRE(run(home, {"models", "rename", "wan", "Wan"}, &out, &err) == 0);
        CHECK_THAT(out, ContainsSubstring("moved store 'org--wan' to 'Wan'"));
        CHECK_THAT(out, ContainsSubstring("repointed models.default_video: org--wan -> Wan"));
        const apogee::harness::Config config = apogee::harness::load_config(home.config_path());
        CHECK(config.models.default_video == "Wan");
        CHECK(fs::exists(home.models() / "Wan" / "pipeline" / "777777777777"));
    }
    SECTION("by the backend's own rename, its store following") {
        REQUIRE(run(home, {"config", "rename-backend", "wan-4bit", "big-4bit"}, &out, &err) == 0);
        CHECK_THAT(out, ContainsSubstring("moved store 'org--wan' to 'org--big'"));
        CHECK_THAT(out, ContainsSubstring("repointed models.default_video: org--wan -> org--big"));
        const apogee::harness::Config config = apogee::harness::load_config(home.config_path());
        CHECK(config.models.default_video == "org--big");
        CHECK(config.find_backend("big-4bit")->model_path ==
              (home.models() / "org--big" / "mlx" / "666666666666").string());
    }
}
