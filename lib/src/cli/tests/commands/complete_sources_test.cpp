#include "commands/complete_sources.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "agentloop/retriever.h"
#include "commands/command.h"
#include "commands/config_cmd.h"
#include "commands/embed.h"
#include "commands/json_reporter.h"
#include "commands/registry.h"
#include "commands/root.h"
#include "embedstore/store.h"
#include "harness/assets.h"
#include "harness/config.h"
#include "harness/layout.h"
#include "knowledge/record.h"
#include "models/quantize.h"
#include "secrets/resolve.h"
#include "support/env_guard.h"
#include "training/datasets.h"
#include "training/manifest.h"
#include "training/python_env.h"
#include "training/trainer.h"

/// Where each name kind's list comes from, against a real data directory --
/// and every fixed word list against the validator it stands beside, so the
/// words offered are the words accepted.
namespace {

namespace c = apogee::commands;

void write_file(const std::filesystem::path& path, const std::string& bytes = "x") {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << bytes;
}

[[nodiscard]] bool has(const std::vector<std::string>& list, std::string_view word) {
    return std::find(list.begin(), list.end(), word) != list.end();
}

struct Home {
    apogee::testing::TempDir root{"complete-src-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", root.path().string()};
    std::filesystem::path home = root.path();
    apogee::harness::Config config = apogee::harness::parse_config(R"(
backends:
  claude:
    type: anthropic
    model: claude-sonnet-5
mcp_servers:
  files:
    command: /bin/echo
embeddings:
  notes:
    retriever: lexical
)",
                                                                   "test");

    [[nodiscard]] c::NameList names(std::string_view kind,
                                    std::vector<std::string> positionals = {},
                                    std::map<std::string, std::string> flags = {}) const {
        c::CompletionContext context;
        context.config = &config;
        context.positionals = std::move(positionals);
        context.flags = std::move(flags);
        return c::list_names(kind, context);
    }
};

/// The quoted words of the first `choices=[...]` after `marker` in `text`.
[[nodiscard]] std::vector<std::string> python_choices(const std::string& text,
                                                      std::string_view marker) {
    const std::size_t at = text.find(marker);
    REQUIRE(at != std::string::npos);
    const std::size_t open = text.find("choices=[", at);
    const std::size_t close = text.find(']', open);
    REQUIRE(open != std::string::npos);
    std::vector<std::string> out;
    const std::string list = text.substr(open, close - open);
    for (std::size_t quote = list.find('"'); quote != std::string::npos;) {
        const std::size_t end = list.find('"', quote + 1);
        out.push_back(list.substr(quote + 1, end - quote - 1));
        quote = list.find('"', end + 1);
    }
    return out;
}

[[nodiscard]] const std::string& bundled_script(std::string_view name) {
    for (const apogee::harness::BundledScript& script :
         apogee::harness::bundled_training_scripts()) {
        if (script.name == name) {
            static std::string text;
            text = std::string{script.text};
            return text;
        }
    }
    FAIL("no bundled " << name);
    static const std::string none;
    return none;
}

}  // namespace

TEST_CASE("every name kind has a source, and an unknown one is refused",
          "[commands][completion][sources]") {
    const Home home;
    for (const std::string_view kind : c::kNameValues) {
        INFO(kind);
        CHECK_NOTHROW((void)home.names(kind));
    }
    CHECK_THROWS_AS((void)home.names("NOT_A_KIND"), std::invalid_argument);
}

TEST_CASE("the store's models complete by name and by handle, per format",
          "[commands][completion][sources]") {
    const Home home;
    const std::filesystem::path models = home.home / "models";
    write_file(models / "org--both" / "gguf" / "111111111111" / "both-F16.gguf");
    write_file(models / "org--both" / "safetensors" / "222222222222" / "config.json", "{}");
    write_file(models / "org--both" / "safetensors" / "222222222222" / "model.safetensors");
    write_file(models / "org--gguf" / "gguf" / "333333333333" / "g.gguf");

    const std::vector<std::string> any = home.names(c::kModelValue).names;
    CHECK(has(any, "org--both"));
    CHECK(has(any, "org--gguf"));
    CHECK(has(any, "org--both/gguf/111111111111"));
    CHECK(has(any, "org--both/safetensors/222222222222"));

    // `convert` reads SafeTensors, `quantize` GGUFs: each offers only those.
    const c::NameList snapshots = home.names(c::kSnapshotValue);
    CHECK(snapshots.paths);
    CHECK(has(snapshots.names, "org--both/safetensors/222222222222"));
    CHECK_FALSE(has(snapshots.names, "org--gguf"));
    const c::NameList ggufs = home.names(c::kGgufValue);
    CHECK(ggufs.paths);
    CHECK(has(ggufs.names, "org--gguf"));
    CHECK_FALSE(has(ggufs.names, "org--both/safetensors/222222222222"));

    // `--from` offers the ids of the model already on the line.
    CHECK(home.names(c::kSnapshotIdValue, {"org--both"}).names ==
          std::vector<std::string>{"222222222222"});
    CHECK(home.names(c::kGgufIdValue, {"org--gguf"}).names ==
          std::vector<std::string>{"333333333333"});
    CHECK(home.names(c::kGgufIdValue).names.empty());  // no model named yet
}

TEST_CASE("collections, datasets, kits and runs complete from the data directory",
          "[commands][completion][sources]") {
    const Home home;
    {
        const apogee::embedstore::Store store{c::collection_path("notes")};
    }
    CHECK(has(home.names(c::kCollectionValue).names, "notes"));
    // A graph target is a named graph or a collection.
    CHECK(has(home.names(c::kGraphValue).names, "notes"));

    write_file(apogee::harness::training_datasets_dir() / "alpha.jsonl", "{}\n");
    const c::NameList datasets = home.names(c::kDatasetValue);
    CHECK(has(datasets.names, "alpha"));
    CHECK(datasets.paths);

    // The bundled kits, before seeding has put them anywhere.
    const std::vector<std::string> kits = home.names(c::kKitValue).names;
    for (const apogee::harness::BundledKit& kit : apogee::harness::bundled_kits()) {
        CHECK(has(kits, kit.name));
    }

    apogee::training::RunManifest run;
    run.run_id = "20260924-090000";
    run.status = "complete";
    REQUIRE(
        apogee::training::write_manifest(apogee::harness::training_dir() / "runs" / run.run_id, run)
            .empty());
    CHECK(has(home.names(c::kRunValue).names, "20260924-090000"));

    CHECK(home.names(c::kServerValue).names == std::vector<std::string>{"files"});
}

TEST_CASE("completing a record id never creates the collection it looks in",
          "[commands][completion][sources]") {
    // Opening a knowledge store creates its database; a <TAB> must not.
    const Home home;
    const c::NameList records = home.names(c::kRecordValue, {}, {{"--db", "decisions"}});
    CHECK(records.names.empty());
    CHECK(records.none.find("decisions") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(c::collection_path("decisions")));
    CHECK_FALSE(std::filesystem::exists(c::collection_path("knowledge")));
}

TEST_CASE("every config key completion offers is one config get answers",
          "[commands][completion][sources]") {
    const Home home;
    const std::filesystem::path config_path = home.home / "config" / "config.yaml";
    write_file(config_path, R"(backends:
  claude:
    type: anthropic
    model: claude-sonnet-5
mcp_servers:
  files:
    command: /bin/echo
embeddings:
  notes:
    retriever: lexical
)");
    const std::vector<std::string> keys = c::config_keys(home.config);
    CHECK(has(keys, "backends.claude.model"));
    CHECK(has(keys, "mcp_servers.files.command"));
    CHECK(has(keys, "embeddings.notes.retriever"));
    CHECK(has(keys, "permissions.write_file"));
    for (const std::string& key : keys) {
        INFO(key);
        std::ostringstream out;
        std::ostringstream err;
        std::streambuf* old_out = std::cout.rdbuf(out.rdbuf());
        std::streambuf* old_err = std::cerr.rdbuf(err.rdbuf());
        int code = -1;
        {
            c::RootCommand command{c::default_registry()};
            const std::string path = config_path.string();
            std::vector<const char*> argv{"apogee", "--config", path.c_str(),
                                          "config", "get",      key.c_str()};
            code = command.run(static_cast<int>(argv.size()), argv.data());
        }
        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);
        CHECK(code == 0);
    }
}

TEST_CASE("every fixed word list is the one its validator accepts",
          "[commands][completion][sources]") {
    using apogee::agentloop::valid_retriever;
    for (const std::string_view name : apogee::agentloop::retriever_names()) {
        CHECK(valid_retriever(name));
    }
    for (const std::string_view name : apogee::agentloop::ingest_retriever_names()) {
        CHECK(valid_retriever(name));
    }
    for (const std::string_view name : apogee::secrets::slot_names()) {
        CHECK(apogee::secrets::slot_type(name).has_value());
    }
    CHECK(apogee::secrets::slot_names().size() == 3);
    const apogee::training::HostShape mac{.apple_silicon = true};
    for (const std::string_view name : apogee::training::trainer_names()) {
        INFO(name);
        CHECK(apogee::training::select_trainer(name, mac).error.empty());
    }
    for (const std::string_view name : c::format_names()) {
        CHECK(c::output_format_from_string(name).has_value());
        CHECK(c::input_format_from_string(name).has_value());
    }
    for (const std::string_view name : apogee::harness::agent_tool_policy_names()) {
        CHECK(apogee::harness::agent_tool_policy_from_string(name).has_value());
    }
    for (const std::string_view name : apogee::harness::agent_output_format_names()) {
        CHECK(apogee::harness::agent_output_format_from_string(name).has_value());
    }
    for (const std::string_view name : apogee::training::create_source_names()) {
        CHECK(apogee::training::create_source_from_string(name).has_value());
    }
    for (const std::string_view name : apogee::knowledge::valid_statuses()) {
        CHECK(apogee::knowledge::is_valid_status(name));
    }
    for (const std::string_view name : apogee::training::requirement_set_names()) {
        CHECK(apogee::training::requirement_set_from_string(name).has_value());
    }
    CHECK(has(apogee::models::quant_type_names(), "Q4_K_M"));
}

TEST_CASE("the words the Python drivers validate are the ones offered",
          "[commands][completion][sources]") {
    // `datasets prepare --format` and `--method` are decided by the drivers;
    // completion offers C++ lists, held here to the drivers' own choices.
    std::vector<std::string> formats =
        python_choices(bundled_script("prepare_dataset.py"), "\"--format\"");
    // "" is the driver's "detect it", the default -- not a word to offer.
    std::erase(formats, std::string{});
    const auto offered = apogee::training::prepare_formats();
    CHECK(std::vector<std::string>(offered.begin(), offered.end()) == formats);

    for (const std::string_view driver : {"train_mlx.py", "train_peft.py"}) {
        INFO(driver);
        const std::vector<std::string> methods =
            python_choices(bundled_script(driver), "\"--method\"");
        const auto accepted = apogee::harness::lora_methods();
        CHECK(std::vector<std::string>(accepted.begin(), accepted.end()) == methods);
    }
}
