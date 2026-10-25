#include "operations/backend_fields.h"

#include <CLI/CLI.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "cli/root.h"
#include "contracts/config.h"
#include "contracts/config_edit.h"
#include "modelstore/sidecar.h"
#include "support/cli_home.h"
#include "support/env_guard.h"

/// `config set-backend` (46b) through the real command: one flag per editable
/// field, spelled as `add-backend` spells it; only the named fields touched,
/// a comment kept, `""` clearing one; never a key; every refusal named.
namespace {

using Catch::Matchers::ContainsSubstring;

[[nodiscard]] std::string slurp(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// The long spellings `name` takes, help and hidden ones aside.
[[nodiscard]] std::set<std::string> flags_of(const std::string& name) {
    const apogee::commands::RootCommand root;
    const CLI::App* config = root.app().get_subcommand("config");
    const CLI::App* verb = config->get_subcommand(name);
    std::set<std::string> out;
    for (const CLI::Option* option : verb->get_options()) {
        if (option == verb->get_help_ptr() || option->get_group().empty() ||
            !option->nonpositional()) {
            continue;
        }
        for (const std::string& spelling : option->get_lnames()) {
            out.insert(spelling);
        }
    }
    return out;
}

constexpr std::string_view kConfig = R"(backends:
  local:
    type: llamacpp   # in process
    model_path: /models/gemma.gguf   # 12B
    mmproj_path: /models/gemma-mmproj.gguf
  claude:
    type: anthropic
    model: claude-sonnet-5   # the default model
models:
  default: local
)";

}  // namespace

TEST_CASE("set-backend has a flag for every editable field, and every one add-backend has",
          "[set-backend][cli][enumeration]") {
    std::set<std::string> fields;
    for (const std::string_view field : apogee::harness::backend_editable_fields()) {
        std::string spelling{field};
        std::ranges::replace(spelling, '_', '-');
        fields.insert(spelling);
    }
    const std::set<std::string> set_backend = flags_of("set-backend");
    CHECK(set_backend == fields);
    // add-backend's fields, but its key and --force, are set-backend's.
    std::set<std::string> add_backend = flags_of("add-backend");
    add_backend.erase("api-key");
    add_backend.erase("force");
    CHECK(std::ranges::includes(set_backend, add_backend));
}

TEST_CASE("set-backend edits exactly the fields named, in place, and clears with an empty value",
          "[set-backend][cli]") {
    const apogee::testing::CliHome home{std::string{kConfig}};
    std::string out;
    std::string err;
    REQUIRE(home.run({"config", "set-backend", "claude", "--model", "claude-opus-5-5",
                      "--temperature", "0.2"},
                     &out, &err) == 0);
    CHECK_THAT(out, ContainsSubstring("edited backend 'claude'"));
    CHECK_THAT(out, ContainsSubstring("set model, temperature"));
    std::string expected{kConfig};
    expected.replace(expected.find("model: claude-sonnet-5"), 22, "model: claude-opus-5-5");
    expected.insert(expected.find("    model: claude-opus-5-5"), "    temperature: 0.2\n");
    CHECK(slurp(home.config_path()) == expected);

    REQUIRE(home.run({"config", "set-backend", "local", "--mmproj-path", ""}, &out, &err) == 0);
    CHECK_THAT(out, ContainsSubstring("cleared mmproj_path"));
    expected.erase(expected.find("    mmproj_path: /models/gemma-mmproj.gguf\n"), 43);
    CHECK(slurp(home.config_path()) == expected);

    // Refusals, each named, the file untouched.
    CHECK(home.run({"config", "set-backend", "claude"}, &out, &err) == 1);
    CHECK_THAT(err, ContainsSubstring("name a field to set"));
    CHECK(home.run({"config", "set-backend", "nope", "--model", "x"}, &out, &err) == 1);
    CHECK_THAT(err, ContainsSubstring("no backend named 'nope'"));
    CHECK(home.run({"config", "set-backend", "claude", "--api-key", "sk-planted"}, &out, &err) ==
          1);
    CHECK_THAT(err, ContainsSubstring("a key is never taken on the command line"));
    CHECK_THAT(err, ContainsSubstring("apogee auth add"));
    CHECK(home.run({"config", "set-backend", "claude", "--context-size", "lots"}, &out, &err) == 1);
    CHECK(home.run({"config", "set-backend", "claude", "--type", ""}, &out, &err) == 1);
    CHECK_THAT(err, ContainsSubstring("cannot be cleared"));
    CHECK(slurp(home.config_path()) == expected);
    // The key never reached the file, nor anything said.
    CHECK(slurp(home.config_path()).find("sk-planted") == std::string::npos);
    CHECK(out.find("sk-planted") == std::string::npos);
}

TEST_CASE("set-backend completes a field's closed set and refuses a word outside it",
          "[set-backend][cli][completion]") {
    // `mode` takes `subscription` or `bare`, nothing else (found free text at
    // v0.1.8's pre-merge docs pass, 2026-10-16): Tab offers both, a typo is
    // refused before the file is touched, and "" still clears it.
    const apogee::testing::CliHome home{std::string{kConfig}};
    std::string out;
    std::string err;
    REQUIRE(home.run({"__complete", "config", "set-backend", "claude", "--mode", ""}, &out, &err) ==
            0);
    CHECK(out == "subscription\nbare\n");  // as declared: the default first
    CHECK(home.run({"config", "set-backend", "claude", "--mode", "wizard"}, &out, &err) != 0);
    CHECK(slurp(home.config_path()) == kConfig);
    REQUIRE(home.run({"config", "set-backend", "claude", "--mode", "bare"}, &out, &err) == 0);
    CHECK_THAT(slurp(home.config_path()), ContainsSubstring("mode: bare"));
    REQUIRE(home.run({"config", "set-backend", "claude", "--mode", ""}, &out, &err) == 0);
    CHECK(slurp(home.config_path()) == kConfig);
}

TEST_CASE("set-backend in a JSONC config: the field in place, its comments kept",
          "[set-backend][cli][jsonc]") {
    const apogee::testing::CliHome home{
        "{\n  // The backends.\n  \"backends\": {\n    \"claude\": {\n      \"type\": "
        "\"anthropic\",\n"
        "      \"model\": \"claude-sonnet-5\"  // the default model\n    }\n  }\n}\n",
        "config.json"};
    const std::string before = slurp(home.config_path());
    std::string out;
    std::string err;
    REQUIRE(home.run({"config", "set-backend", "claude", "--model", "claude-opus-5-5"}, &out,
                     &err) == 0);
    std::string expected = before;
    expected.replace(expected.find("\"claude-sonnet-5\""), 17, "\"claude-opus-5-5\"");
    CHECK(slurp(home.config_path()) == expected);
}

TEST_CASE("set-backend lists a vendor's server tools, completed, refused where it runs none",
          "[set-backend][cli][server-tools]") {
    // 47a: the consent is one visible line the one editor writes.
    const apogee::testing::CliHome home{std::string{kConfig}};
    std::string out;
    std::string err;
    REQUIRE(home.run({"__complete", "config", "set-backend", "claude", "--server-tools", ""}, &out,
                     &err) == 0);
    CHECK(out == "web_search\ncode_execution\n");
    REQUIRE(home.run({"config", "set-backend", "claude", "--server-tools", "web_search"}, &out,
                     &err) == 0);
    CHECK_THAT(slurp(home.config_path()), ContainsSubstring("    server_tools: [web_search]\n"));
    // A local entry runs none: refused in the loader's words, the file untouched.
    const std::string before = slurp(home.config_path());
    CHECK(home.run({"config", "set-backend", "local", "--server-tools", "web_search"}, &out,
                   &err) != 0);
    CHECK_THAT(err, ContainsSubstring("a llamacpp backend runs no server-side tools"));
    CHECK(slurp(home.config_path()) == before);
    REQUIRE(home.run({"config", "set-backend", "claude", "--server-tools", ""}, &out, &err) == 0);
    CHECK(slurp(home.config_path()) == kConfig);
}

namespace {

/// A base and two adapters outside any install, and a config naming them.
struct AdapterFiles {
    apogee::testing::TempDir dir{"set-backend-adapters-" + std::to_string(std::random_device{}())};
    std::filesystem::path base = dir.path() / "base.gguf";
    std::filesystem::path style = dir.path() / "style.gguf";
    std::filesystem::path tone = dir.path() / "tone.gguf";
    std::filesystem::path mlx_model = dir.path() / "mlx-model";

    AdapterFiles() {
        std::ofstream{base, std::ios::binary} << "base weights";
        std::ofstream{style, std::ios::binary} << "style delta";
        std::ofstream{tone, std::ios::binary} << "tone delta";
        std::filesystem::create_directories(mlx_model);
        std::ofstream{mlx_model / "model.safetensors", std::ios::binary} << "weights";
    }

    [[nodiscard]] std::string config() const {
        return "backends:\n  local:\n    type: llamacpp   # in process\n    model_path: " +
               base.string() + "   # the base\n    context_size: 4096\n  mlx:\n    type: mlx\n" +
               "    model_path: " + mlx_model.string() +
               "\n  bare:\n    type: llamacpp\n  claude:\n    type: anthropic\n"
               "models:\n  default: local\n";
    }
};

}  // namespace

TEST_CASE("set-backend pins adapters to the base as it is now, in place, and clears them",
          "[set-backend][cli][adapters]") {
    // 53a: the one editor writes the list; the base's sha256 is recorded
    // beside each adapter where the base can be read.
    const AdapterFiles files;
    const apogee::testing::CliHome home{files.config()};
    std::string out;
    std::string err;
    REQUIRE(home.run({"config", "set-backend", "local", "--adapters",
                      files.style.string() + ":0.5," + files.tone.string()},
                     &out, &err) == 0);
    const std::string pin = apogee::models::file_sha256(files.base);
    CHECK_THAT(out, ContainsSubstring("set adapters; adapters pinned to " + files.base.string() +
                                      " (sha256 " + pin.substr(0, 12) + ")"));
    std::string expected = files.config();
    const std::string under = "    model_path: " + files.base.string() + "   # the base\n";
    expected.insert(
        expected.find(under) + under.size(),
        "    adapters: [{path: " + files.style.string() + ", scale: 0.5, base_sha256: \"" + pin +
            "\"}, {path: " + files.tone.string() + ", scale: 1, base_sha256: \"" + pin + "\"}]\n");
    CHECK(slurp(home.config_path()) == expected);
    // The base was read once, and is remembered by its identity.
    CHECK(std::filesystem::exists(home.home() / "cache" / "weight-hashes.json"));

    // Cleared: the file it was, byte for byte.
    REQUIRE(home.run({"config", "set-backend", "local", "--adapters", ""}, &out, &err) == 0);
    CHECK_THAT(out, ContainsSubstring("cleared adapters"));
    CHECK(slurp(home.config_path()) == files.config());

    // An mlx entry takes one adapter directory, pinned to its model
    // directory's shards.
    const std::filesystem::path adapter = files.dir.path() / "mlx-adapter";
    std::filesystem::create_directories(adapter);
    std::ofstream{adapter / "adapter_config.json"} << "{}";
    std::ofstream{adapter / "adapters.safetensors"} << "delta";
    REQUIRE(home.run({"config", "set-backend", "mlx", "--adapters", adapter.string()}, &out,
                     &err) == 0);
    const apogee::harness::Config read = apogee::harness::load_config(home.config_path());
    const auto& adapters = read.find_backend("mlx")->adapters;
    REQUIRE(adapters.size() == 1);
    CHECK(adapters[0].path == adapter.string());
    CHECK(adapters[0].base_sha256.size() == 64);
    CHECK(adapters[0].base_sha256 != pin);
}

TEST_CASE("set-backend refuses adapters it cannot pin, the file untouched",
          "[set-backend][cli][adapters]") {
    const AdapterFiles files;
    const apogee::testing::CliHome home{files.config()};
    std::string out;
    std::string err;
    const auto refused = [&](const std::vector<std::string>& args, const std::string& reason) {
        INFO(reason);
        CHECK(home.run(args, &out, &err) == 1);
        CHECK_THAT(err, ContainsSubstring(reason));
        CHECK(slurp(home.config_path()) == files.config());
    };
    refused({"config", "set-backend", "local", "--adapters", files.dir.path().string() + "/gone"},
            "the adapter " + files.dir.path().string() + "/gone is not there");
    refused({"config", "set-backend", "local", "--adapters", files.style.string() + ":3"},
            "its scale 3 is outside (0, 2]");
    refused({"config", "set-backend", "claude", "--adapters", files.style.string()},
            "a backend of type anthropic applies no adapters");
    refused({"config", "set-backend", "bare", "--adapters", files.style.string()},
            "adapters apply over the entry's model_path, and it names none");
    refused({"config", "set-backend", "mlx", "--adapters",
             files.style.string() + "," + files.tone.string()},
            "an mlx entry applies one adapter");
    refused({"config", "set-backend", "mlx", "--adapters", files.style.string()},
            "is not an mlx-lm adapter directory");
    refused({"config", "set-backend", "local", "--adapters", files.dir.path().string()},
            "is not a file -- a llamacpp entry applies a GGUF LoRA file");
}

TEST_CASE("set-backend moving the base under pinned adapters re-pins them or refuses by name",
          "[set-backend][cli][adapters][repin]") {
    // 55e: a model_path edit used to move the base under the entry's pinned
    // adapters and say only "set model_path"; the next turn refused by hash
    // and the doctor failed. The same edit re-pairs a drafter (53i).
    const AdapterFiles files;
    const apogee::testing::CliHome home{files.config()};
    const std::filesystem::path copy = files.dir.path() / "copy" / "base.gguf";
    const std::filesystem::path other = files.dir.path() / "other.gguf";
    std::filesystem::create_directories(copy.parent_path());
    std::ofstream{copy, std::ios::binary} << "base weights";
    std::ofstream{other, std::ios::binary} << "other weights";
    const std::string pin = apogee::models::file_sha256(files.base);
    const std::string other_pin = apogee::models::file_sha256(other);
    std::string out;
    std::string err;
    REQUIRE(
        home.run({"config", "set-backend", "local", "--adapters", files.style.string() + ":0.5"},
                 &out, &err) == 0);

    // The same weights elsewhere: every pin holds, said, only model_path moved.
    const std::string pinned = slurp(home.config_path());
    REQUIRE(home.run({"config", "set-backend", "local", "--model-path", copy.string()}, &out,
                     &err) == 0);
    CHECK_THAT(out, ContainsSubstring("set model_path; adapters still pinned to " + copy.string() +
                                      " (sha256 " + pin.substr(0, 12) + ")"));
    CHECK(slurp(home.config_path()) ==
          apogee::harness::set_backend_field(pinned, "local", "model_path", copy.string()));

    // Other weights: refused naming the adapter and both hashes, nothing
    // edited, and each way out a command that runs as printed.
    const std::string moved = slurp(home.config_path());
    CHECK(home.run({"config", "set-backend", "local", "--model-path", other.string()}, &out,
                   &err) == 1);
    CHECK_THAT(err, ContainsSubstring("'local' applies adapters pinned to other weights than " +
                                      other.string() + " holds (sha256 " + other_pin.substr(0, 12) +
                                      "): " + files.style.string() + " (sha256 " +
                                      pin.substr(0, 12) + ")"));
    CHECK_THAT(err, ContainsSubstring("nothing was edited"));
    const std::string repin = "apogee config set-backend local --model-path " + other.string() +
                              " --adapters " + files.style.string() + ":0.5";
    const std::string take_off =
        "apogee config set-backend local --model-path " + other.string() + " --adapters \"\"";
    CHECK_THAT(err, ContainsSubstring("re-pin them in the same edit: " + repin));
    CHECK_THAT(err, ContainsSubstring("take them off: " + take_off));
    CHECK(slurp(home.config_path()) == moved);

    // The re-pin door, run as printed: the set pinned to the new weights.
    REQUIRE(home.run({"config", "set-backend", "local", "--model-path", other.string(),
                      "--adapters", files.style.string() + ":0.5"},
                     &out, &err) == 0);
    CHECK_THAT(out, ContainsSubstring("adapters pinned to " + other.string() + " (sha256 " +
                                      other_pin.substr(0, 12) + ")"));
    const apogee::harness::Config repinned = apogee::harness::load_config(home.config_path());
    REQUIRE(repinned.find_backend("local")->adapters.size() == 1);
    CHECK(repinned.find_backend("local")->adapters.front().base_sha256 == other_pin);

    // The take-off door, run as printed: the base moves, the set is gone.
    REQUIRE(home.run({"config", "set-backend", "local", "--model-path", files.base.string(),
                      "--adapters", ""},
                     &out, &err) == 0);
    CHECK(apogee::harness::load_config(home.config_path()).find_backend("local")->adapters.empty());
}

TEST_CASE("set-backend refuses to move the base under an adapter it cannot check",
          "[set-backend][cli][adapters][repin]") {
    // 55e: a hand-written adapter pinned to nothing cannot be shown to apply
    // over the new weights; one gone cannot apply at all.
    const AdapterFiles files;
    const std::filesystem::path other = files.dir.path() / "other.gguf";
    std::ofstream{other, std::ios::binary} << "other weights";
    std::string config = files.config();
    const std::string under = "    model_path: " + files.base.string() + "   # the base\n";
    config.insert(config.find(under) + under.size(),
                  "    adapters: [{path: " + files.style.string() + "}]\n");
    const apogee::testing::CliHome home{config};
    std::string out;
    std::string err;
    CHECK(home.run({"config", "set-backend", "local", "--model-path", other.string()}, &out,
                   &err) == 1);
    CHECK_THAT(err, ContainsSubstring(files.style.string() + " (pinned to no base)"));
    CHECK(slurp(home.config_path()) == config);
    std::filesystem::remove(files.style);
    CHECK(home.run({"config", "set-backend", "local", "--model-path", other.string()}, &out,
                   &err) == 1);
    CHECK_THAT(err, ContainsSubstring("the adapter " + files.style.string() + " is not there"));
    // Clearing model_path leaves them nothing to apply over.
    CHECK(home.run({"config", "set-backend", "local", "--model-path", ""}, &out, &err) == 1);
    CHECK_THAT(err, ContainsSubstring("'local' applies adapters over its model_path, and this "
                                      "edit leaves it none"));
    CHECK(slurp(home.config_path()) == config);
    // An edit that touches neither the base nor its type is no move.
    REQUIRE(home.run({"config", "set-backend", "local", "--context-size", "8192"}, &out, &err) ==
            0);
}

TEST_CASE("set-backend completes --adapters with files",
          "[set-backend][cli][adapters][completion]") {
    const AdapterFiles files;
    const apogee::testing::CliHome home{files.config()};
    const apogee::testing::EnvGuard protocol{"APOGEE_COMPLETION_PROTOCOL", "2"};
    std::string out;
    std::string err;
    REQUIRE(home.run({"__complete", "config", "set-backend", "local", "--adapters", ""}, &out,
                     &err) == 0);
    CHECK(out == ":files\n");
}

TEST_CASE("set-backend pins adapters in a JSONC config in place, its comments kept",
          "[set-backend][cli][adapters][jsonc]") {
    const AdapterFiles files;
    const std::string config =
        "{\n  // The backends.\n  \"backends\": {\n    \"local\": {\n"
        "      \"type\": \"llamacpp\", // in process\n"
        "      \"model_path\": \"" +
        files.base.string() + "\" // the base\n    }\n  }\n}\n";
    const apogee::testing::CliHome home{config, "config.json"};
    std::string out;
    std::string err;
    REQUIRE(
        home.run({"config", "set-backend", "local", "--adapters", files.style.string() + ":0.5"},
                 &out, &err) == 0);
    const std::string edited = slurp(home.config_path());
    CHECK_THAT(edited, ContainsSubstring("// The backends."));
    CHECK_THAT(edited, ContainsSubstring("// in process"));
    CHECK_THAT(edited, ContainsSubstring("// the base"));
    const auto adapters =
        apogee::harness::load_config(home.config_path()).find_backend("local")->adapters;
    CHECK(adapters == std::vector<apogee::harness::AdapterConfig>{
                          {.path = files.style.string(),
                           .scale = 0.5,
                           .base_sha256 = apogee::models::file_sha256(files.base)}});
    REQUIRE(home.run({"config", "set-backend", "local", "--adapters", ""}, &out, &err) == 0);
    CHECK(slurp(home.config_path()) == config);
}
