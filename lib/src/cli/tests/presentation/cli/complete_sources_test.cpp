#include "cli/complete_sources.h"

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
#include "cli/command.h"
#include "cli/config_cmd.h"
#include "cli/embed.h"
#include "cli/helpers.h"
#include "cli/models_pull.h"
#include "cli/registry.h"
#include "cli/root.h"
#include "cli/task_cmd.h"
#include "contracts/assets.h"
#include "contracts/config.h"
#include "contracts/layout.h"
#include "embedstore/store.h"
#include "knowledge/record.h"
#include "machine/json_reporter.h"
#include "models/quantize.h"
#include "modelstore/store.h"
#include "platform/child_process.h"
#include "secrets/resolve.h"
#include "support/env_guard.h"
#include "support/mlx_model.h"
#include "tasks/ledger.h"
#include "tasks/task.h"
#include "training/convert.h"
#include "training/datasets.h"
#include "training/manifest.h"
#include "training/mlx_convert.h"
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

/// `apogee <args>` in process, against the data directory `APOGEE_HOME`
/// names: its exit code, its stdout and stderr together in `said`.
int run_apogee(const std::vector<std::string>& args, std::string* said = nullptr) {
    const std::ostringstream out;
    std::streambuf* old_out = std::cout.rdbuf(out.rdbuf());
    std::streambuf* old_err = std::cerr.rdbuf(out.rdbuf());
    int code = -1;
    {
        c::RootCommand command{c::default_registry()};
        std::vector<const char*> argv{"apogee"};
        for (const std::string& arg : args) {
            argv.push_back(arg.c_str());
        }
        code = command.run(static_cast<int>(argv.size()), argv.data());
    }
    std::cout.rdbuf(old_out);
    std::cerr.rdbuf(old_err);
    if (said != nullptr) {
        *said = out.str();
    }
    return code;
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

    // `models info` shows a backend or one set of weights (M4): both, by
    // handle -- a whole model is not one thing to show.
    const std::vector<std::string> info = home.names(c::kBackendOrWeightsValue).names;
    CHECK(has(info, "claude"));
    CHECK(has(info, "org--both/gguf/111111111111"));
    CHECK(has(info, "org--both/safetensors/222222222222"));
    CHECK(has(info, "org--gguf/gguf/333333333333"));
    CHECK_FALSE(has(info, "org--both"));
}

TEST_CASE("delete offers backends whose model is stored; add-backend the stored GGUFs none has",
          "[commands][completion][sources]") {
    // M7, from the user's delete transcript: `models delete gemma-4-E2B-F16`
    // -- a backend's name -- and `config add-backend` hand-typing what the
    // store already knows.
    Home home;
    const std::filesystem::path models = home.home / "models";
    const std::filesystem::path registered =
        models / "org--m" / "gguf" / "111111111111" / "m-F16.gguf";
    // Not GGUFs at all: completion reads directories and the config, never a
    // header, so what the files hold cannot matter.
    write_file(registered, "not a gguf");
    write_file(models / "org--m" / "gguf" / "222222222222" / "m-Q4_K_M.gguf", "not a gguf");
    write_file(models / "org--m" / "gguf" / "222222222222" / "m-Q4_K_M-mmproj.gguf", "x");
    write_file(models / "org--x" / "gguf" / "333333333333" / "claude.gguf", "not a gguf");
    apogee::harness::BackendConfig local;
    local.type = apogee::harness::BackendType::LlamaCpp;
    local.model_path = registered.string();
    home.config.backends["m-f16"] = local;
    apogee::harness::BackendConfig elsewhere;
    elsewhere.type = apogee::harness::BackendType::LlamaCpp;
    elsewhere.model_path = "/models/elsewhere.gguf";
    home.config.backends["elsewhere"] = elsewhere;

    const std::vector<std::string> deletable = home.names(c::kModelOrBackendValue).names;
    CHECK(has(deletable, "org--m"));
    CHECK(has(deletable, "org--m/gguf/222222222222"));
    CHECK(has(deletable, "m-f16"));
    // A backend with nothing in the store is nothing to delete.
    CHECK_FALSE(has(deletable, "claude"));
    CHECK_FALSE(has(deletable, "elsewhere"));

    // The stored GGUFs no backend points at, by file name -- never a
    // projector, never one a backend has, never a name already taken.
    CHECK(home.names(c::kNewBackendValue).names == std::vector<std::string>{"m-Q4_K_M"});
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
    CHECK(has(keys, "attachments.graph"));  // 27p's block, set or not
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
    // `--graph` on chat and complete, and `attachments.graph` (27p).
    for (const std::string_view name : apogee::harness::attachment_graph_method_names()) {
        CHECK(apogee::harness::attachment_graph_method_from_string(name).has_value());
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

TEST_CASE("a model suite completes from the config's suites, never the eval suites",
          "[commands][completion][sources][suites]") {
    // 27d: MODEL_SUITE is the `suites:` names -- what `--suite` and the suite
    // verbs take -- and the training track's SUITE stays its own.
    Home home;
    CHECK(home.names(c::kModelSuiteValue).names.empty());
    CHECK(home.names(c::kModelSuiteValue).none.find("config add-suite") != std::string::npos);
    home.config = apogee::harness::parse_config(
        "backends:\n  a:\n    type: mock\nsuites:\n  research:\n    members:\n      chat: a\n"
        "  fast:\n    members:\n      chat: a\n",
        "test");
    CHECK(home.names(c::kModelSuiteValue).names == std::vector<std::string>{"fast", "research"});
    CHECK_FALSE(has(home.names(c::kSuiteValue).names, "research"));
}

TEST_CASE("a --suite that takes off offers it, and execute's never does",
          "[commands][completion][sources][suites]") {
    // ADR 0007, both ways: `chat --suite off` is accepted, so it is offered;
    // `execute --suite off` is refused, so it is not.
    Home home;
    home.config = apogee::harness::parse_config(
        "backends:\n  a:\n    type: mock\nsuites:\n  fast:\n    members:\n      chat: a\n", "test");
    const std::vector<std::string> or_off = home.names(c::kModelSuiteOrOffValue).names;
    CHECK(or_off == std::vector<std::string>{"fast", "off"});
    for (const std::string& name : or_off) {
        INFO(name);
        apogee::harness::Config probe = home.config;
        CHECK(c::select_suite(probe, name).empty());
    }
    CHECK_FALSE(has(home.names(c::kModelSuiteValue).names, "off"));
}

TEST_CASE("halt and cancel offer the tasks each takes, by the verbs' own test",
          "[commands][completion][sources][task]") {
    const Home home;
    const std::filesystem::path root = apogee::harness::tasks_dir();
    namespace t = apogee::tasks;
    const std::vector<std::string_view> statuses{t::kPlanning,  t::kRunning,  t::kHalted,
                                                 t::kCancelled, t::kStalled,  t::kFailed,
                                                 t::kDone,      t::kExhausted};
    // One task per status, saved again before each verb is tried on it.
    const auto save_all = [&] {
        for (std::size_t i = 0; i < statuses.size(); ++i) {
            t::Task task;
            task.id = "task-20261006-12000" + std::to_string(i);
            task.goal = "goal";
            task.status = std::string{statuses[i]};
            REQUIRE(t::save_task(root, task).empty());
        }
    };
    const auto id_of = [&](std::string_view status) {
        const auto at = std::ranges::find(statuses, status);
        return "task-20261006-12000" + std::to_string(at - statuses.begin());
    };
    save_all();
    const std::vector<std::string> haltable = home.names(c::kHaltableTaskValue).names;
    const std::vector<std::string> cancellable = home.names(c::kCancellableTaskValue).names;
    CHECK(haltable.size() == 3);
    CHECK(has(haltable, id_of(t::kPlanning)));
    CHECK(has(haltable, id_of(t::kRunning)));
    CHECK(has(haltable, id_of(t::kHalted)));
    CHECK(cancellable.size() == 6);
    CHECK_FALSE(has(cancellable, id_of(t::kDone)));
    CHECK_FALSE(has(cancellable, id_of(t::kExhausted)));
    // Status still offers every one.
    CHECK(home.names(c::kTaskValue).names.size() == statuses.size());

    // The offer is the contract: each offered id the verb takes, each other
    // one it refuses.
    for (const auto& [verb, offered] : {std::pair{std::string{"halt"}, haltable},
                                        std::pair{std::string{"cancel"}, cancellable}}) {
        for (const std::string_view status : statuses) {
            save_all();
            const std::string id = id_of(status);
            INFO(verb << " " << status);
            std::string said;
            CHECK((run_apogee({"task", verb, id}, &said) == 0) == has(offered, id));
        }
    }
}

TEST_CASE("resume offers the unfinished tasks started in the folder it runs in",
          "[commands][completion][sources][task]") {
    // `task resume` refuses a finished task and one started elsewhere; Tab,
    // run in the same folder, offers neither -- by the verb's own test.
    const Home home;
    const std::filesystem::path root = apogee::harness::tasks_dir();
    namespace t = apogee::tasks;
    const std::filesystem::path here = std::filesystem::current_path();
    const std::filesystem::path elsewhere = home.home / "elsewhere";
    std::filesystem::create_directories(elsewhere);
    const std::vector<std::pair<std::string_view, std::filesystem::path>> made{
        {t::kHalted, here},      {t::kStalled, here},     {t::kFailed, here},
        {t::kCancelled, here},   {t::kDone, here},        {t::kExhausted, here},
        {t::kHalted, elsewhere}, {t::kRunning, elsewhere}};
    std::vector<t::Task> tasks;
    for (std::size_t i = 0; i < made.size(); ++i) {
        t::Task task;
        task.id = "task-20261006-13000" + std::to_string(i);
        task.goal = "goal";
        task.status = std::string{made[i].first};
        task.working_directory = made[i].second.string();
        task.session_id = "20261006-130000-abcd";
        REQUIRE(t::save_task(root, task).empty());
        tasks.push_back(task);
    }
    const std::vector<std::string> offered = home.names(c::kResumableTaskValue).names;
    CHECK(offered.size() == 4);
    for (const t::Task& task : tasks) {
        const bool resumable = task.status != t::kDone && task.status != t::kExhausted &&
                               task.working_directory == here.string();
        CHECK(has(offered, task.id) == resumable);
    }

    // The verb, both ways: an offered task gets past every refusal of its
    // record (here, to its missing conversation); any other is refused by
    // exactly the test completion asked.
    const std::filesystem::path config_path = home.home / "config" / "config.yaml";
    write_file(config_path, "backends:\n  m:\n    type: mock\nmodels:\n  default: m\n");
    for (const t::Task& task : tasks) {
        INFO(task.id << " " << task.status << " in " << task.working_directory);
        std::string said;
        CHECK(run_apogee({"--config", config_path.string(), "task", "resume", task.id}, &said) ==
              1);
        const std::string refusal = c::task_resume_refusal(task, here);
        if (has(offered, task.id)) {
            CHECK(refusal.empty());
            CHECK(said.find("cannot be read") != std::string::npos);
        } else {
            REQUIRE_FALSE(refusal.empty());
            CHECK(said.find(refusal) != std::string::npos);
        }
    }
}

TEST_CASE("symphonies edit offers names, delete only config entries",
          "[commands][completion][sources][symphonies]") {
    // `edit` takes a starter, an entry or a spec file by name, never a path;
    // `delete` removes a config entry and refuses the rest.
    Home home;
    const std::filesystem::path config_path = home.home / "config" / "config.yaml";
    write_file(config_path,
               "backends:\n  a:\n    type: mock\nsymphonies:\n  mine:\n    stages:\n"
               "      - {name: one, role: chat, prompt: 'Say {{input}}'}\n");
    write_file(apogee::harness::symphonies_dir() / "filed.yaml",
               "stages:\n  - {name: one, role: chat, prompt: 'Say {{input}}'}\n");
    home.config = apogee::harness::load_config(config_path);

    const c::NameList named = home.names(c::kSymphonyNameValue);
    CHECK_FALSE(named.paths);
    CHECK(has(named.names, "mine"));
    CHECK(has(named.names, "filed"));
    CHECK(has(named.names, "summarize-verify"));
    CHECK(home.names(c::kSymphonyValue).paths);  // `show`, `play`: a path too

    const std::vector<std::string> entries = home.names(c::kSymphonyEntryValue).names;
    CHECK(entries == std::vector<std::string>{"mine"});
    // Every other name the catalog holds, delete refuses.
    for (const std::string& name : named.names) {
        if (has(entries, name)) {
            continue;
        }
        INFO(name);
        CHECK(run_apogee({"--config", config_path.string(), "symphonies", "delete", name}) == 1);
    }
    // An editor that changes nothing: each name is found, nothing written.
    // Where this build starts no child (Windows: run_foreground, like
    // start_child, has no implementation there yet) each name is still
    // found -- edit gets as far as the editor and says it cannot open one.
    const apogee::testing::EnvGuard editor{"EDITOR", "true"};
    const bool editors_run = apogee::platform::supports_child_processes();
    for (const std::string& name : named.names) {
        INFO(name);
        std::string said;
        const int status =
            run_apogee({"--config", config_path.string(), "symphonies", "edit", name}, &said);
        INFO(said);
        if (editors_run) {
            CHECK(status == 0);
            CHECK(said.find("no change") != std::string::npos);
        } else {
            CHECK(status == 1);
            CHECK(said.find("could not open the editor") != std::string::npos);
        }
    }
    for (const std::string& name : entries) {
        CHECK(run_apogee({"--config", config_path.string(), "symphonies", "delete", name}) == 0);
    }
}

TEST_CASE("convert --type offers the precisions of the engine the line names",
          "[commands][completion][sources][models]") {
    // The parser takes both engines' precisions; the command, one engine's
    // -- the one `--mlx` chooses. Each offered word passes that check.
    const Home home;
    const std::vector<std::string> gguf = home.names(c::kConvertPrecisionValue).names;
    CHECK(gguf == apogee::training::converter_out_types());
    const std::vector<std::string> mlx =
        home.names(c::kConvertPrecisionValue, {}, {{"--mlx", ""}}).names;
    CHECK(mlx == apogee::training::mlx_precision_names());
    for (const std::string& name : mlx) {
        INFO(name);
        CHECK(apogee::training::find_mlx_precision(name) != nullptr);
    }
    CHECK_FALSE(has(gguf, "4bit"));
    CHECK_FALSE(has(mlx, "q8_0"));
}

TEST_CASE("the store's MLX models complete as its GGUFs do, by name, handle and backend",
          "[commands][completion][sources][mlx]") {
    // 27b's rows: a stored MLX model is a model, a set of weights, the model
    // file of a backend -- and, while none points at it, a new backend's name.
    Home home;
    const std::filesystem::path models = home.home / "models";
    const std::filesystem::path pulled =
        models / "mlx-community--Llama-3.2-1B-Instruct-4bit" / "mlx" / "aaaaaaaaaaaa";
    const std::filesystem::path converted = models / "org--m" / "mlx" / "bbbbbbbbbbbb";
    apogee::testing::write_mlx_model(pulled);
    apogee::testing::write_mlx_model(converted, {.bits = 8});
    const std::string pulled_handle = "mlx-community--Llama-3.2-1B-Instruct-4bit/mlx/aaaaaaaaaaaa";
    const std::string converted_handle = "org--m/mlx/bbbbbbbbbbbb";

    const std::vector<std::string> any = home.names(c::kModelValue).names;
    CHECK(has(any, "org--m"));
    CHECK(has(any, converted_handle));
    CHECK(has(any, pulled_handle));
    // `convert` reads SafeTensors and `quantize` GGUFs: neither offers MLX.
    CHECK_FALSE(has(home.names(c::kSnapshotValue).names, converted_handle));
    CHECK_FALSE(has(home.names(c::kGgufValue).names, converted_handle));
    const std::vector<std::string> info = home.names(c::kBackendOrWeightsValue).names;
    CHECK(has(info, converted_handle));
    CHECK(has(info, pulled_handle));

    // Nothing points at either: both are new names, as add-backend fills them.
    std::vector<std::string> fresh = home.names(c::kNewBackendValue).names;
    std::ranges::sort(fresh);
    CHECK(fresh == std::vector<std::string>{"Llama-3.2-1B-Instruct-4bit", "m-8bit"});

    // A backend over one: deletable by its name, and no longer a new name.
    apogee::harness::BackendConfig fast;
    fast.type = apogee::harness::BackendType::Mlx;
    fast.model_path = converted.string() + "/";
    home.config.backends["fast"] = fast;
    const std::vector<std::string> deletable = home.names(c::kModelOrBackendValue).names;
    CHECK(has(deletable, "fast"));
    CHECK(has(deletable, converted_handle));
    CHECK_FALSE(has(deletable, "claude"));
    CHECK(home.names(c::kNewBackendValue).names ==
          std::vector<std::string>{"Llama-3.2-1B-Instruct-4bit"});

    // The offer is the contract: what delete and info are offered, they take.
    const apogee::models::StoreRoots roots = apogee::models::StoreRoots::at(models);
    for (const std::string& name : deletable) {
        INFO(name);
        CHECK(c::plan_delete(roots, home.config, name).ok);
    }
    const std::filesystem::path config_path = home.home / "config" / "config.yaml";
    write_file(config_path, "backends:\n  claude:\n    type: anthropic\n    model: x\n");
    for (const std::string& name : {pulled_handle, converted_handle}) {
        INFO(name);
        CHECK(run_apogee({"--config", config_path.string(), "models", "info", name, "-q"}) == 0);
    }
    for (const std::string& name : {"Llama-3.2-1B-Instruct-4bit", "m-8bit"}) {
        INFO(name);
        std::string said;
        CHECK(run_apogee({"--config", config_path.string(), "config", "add-backend", name},
                         &said) == 0);
        CHECK(said.find("filled from the store") != std::string::npos);
    }
}
