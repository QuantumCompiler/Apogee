#include "cli/tui_training.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "cli/command.h"
#include "cli/tui_child.h"
#include "contracts/layout.h"
#include "support/cli_home.h"
#include "training/cycle.h"
#include "training/manifest.h"
#include "training/store.h"
#include "tui/list_view.h"
#include "tui/progress.h"
#include "tui/pump.h"
#include "tui/shell.h"

/// The shell's training views (37f) held to the commands: Train's rows the
/// version ledgers as `train versions --output-format json` states them,
/// under `train status`'s lines; Datasets' rows `datasets list
/// --output-format json`'s, its cards `datasets info`'s and `datasets
/// kits`'s. Every run and act is the command itself as a child of the built
/// binary: a mock run from `r` leaving the manifest `apogee train run` leaves,
/// a rollback the config and ledger `train rollback` leaves, a cycle refused
/// on its held lock in the command's words, a dataset deleted as `datasets
/// delete --yes` deletes it -- each skipped where this build made no binary.
/// A row's name goes after the act's options and a `--` (CLI-M51), so a
/// dash-led dataset or backend reaches the verb as its operand.
namespace {

namespace fs = std::filesystem;
using apogee::tui::Key;

[[nodiscard]] bool has(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

void write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << text;
}

[[nodiscard]] std::string slurp(const fs::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// The built `apogee`, or empty where this build made none.
[[nodiscard]] fs::path built_binary() {
    const fs::path binary{APOGEE_EXECUTABLE};
    std::error_code missing;
    return fs::is_regular_file(binary, missing) ? binary : fs::path{};
}

constexpr const char* kCycle =
    "training:\n  trainer: mock\n  pipelines:\n    nightly:\n      student: tiny\n"
    "      stages:\n        - name: base\n          dataset: starter\n"
    "          eval_suite: hello\n          iters: 2\n  cycle:\n    pipeline: nightly\n"
    "    backend: nightly-model\n    sources:\n      - type: directory\n        dir: "
    "{home}/queue\n";

/// A home as `train_test` seeds one: the layout, a snapshot named `tiny`, a
/// dataset named `starter`, a suite, and `extra` in the config.
struct Home {
    explicit Home(const std::string& extra = {})
        : Home{"backends:\n  local:\n    type: mock\n" + extra, "config.yaml"} {}

    /// The same with the whole config's text in `file` (CLI-M51: a JSONC
    /// `config.json`), `{home}` the home's path as a JSON string's text.
    Home(std::string config, const std::string& file) : home{"{}", file} {
        const std::string spelled = nlohmann::json(home.home().string()).dump();
        const std::string home_text =
            file.ends_with(".json") ? spelled.substr(1, spelled.size() - 2) : home.home().string();
        for (std::size_t at = config.find("{home}"); at != std::string::npos;
             at = config.find("{home}")) {
            config.replace(at, 6, home_text);
        }
        write(home.config_path(), config);
        REQUIRE(apogee::harness::seed_data_directory(home.home()).ok());
        const fs::path student = home.home() / "models" / "tiny" / "safetensors" / "aaaaaaaaaaaa";
        write(student / "config.json",
              R"({"architectures": ["LlamaForCausalLM"], "model_type": "llama"})");
        write(student / "model.safetensors", "w");
        write(training() / "datasets" / "starter.jsonl",
              "{\"messages\": [{\"role\": \"user\", \"content\": \"hi\"}, "
              "{\"role\": \"assistant\", \"content\": \"hello\"}]}\n");
        write(training() / "suites" / "hello.jsonl",
              "{\"prompt\": \"say hello\", \"expected\": \"hello\"}\n");
        context.config_path = home.config_path().string();
    }

    [[nodiscard]] fs::path training() const {
        return home.home() / "training";
    }

    apogee::testing::CliHome home;
    apogee::commands::RootContext context;
};

/// A view on a shell over a manual pump, its runs children of `binary`.
struct Stage {
    Stage(const Home& home, bool train, const fs::path& binary)
        : progress{std::make_shared<apogee::tui::Progress>(pump)},
          view{pump, apogee::tui::Theme{.color = false},
               train ? apogee::commands::train_view_options(home.context, progress, binary)
                     : apogee::commands::datasets_view_options(home.context, progress, binary)} {
        shell.add(view.view());
        shell.activate(0);
    }

    [[nodiscard]] std::string frame() {
        for (int i = 0; i < 3; ++i) {
            view.settle();
            (void)pump.drain();
        }
        return shell.render_text(200, 60);
    }

    void press(const Key& key) {
        (void)shell.press(key);
        (void)frame();
    }

    void type(const std::string& text) {
        for (const char c : text) {
            press(Key::character(std::string(1, c)));
        }
    }

    /// The run's narration, every line the widget holds.
    [[nodiscard]] std::string said() const {
        std::string out;
        for (const std::string& line : progress->lines()) {
            out += line + "\n";
        }
        return out;
    }

    void until_ended() {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{60};
        while (progress->running()) {
            INFO(shell.render_text(200, 60));
            REQUIRE(std::chrono::steady_clock::now() < deadline);
            (void)frame();
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
        view.refresh();
        (void)frame();
    }

    apogee::tui::ManualPump pump;
    std::shared_ptr<apogee::tui::Progress> progress;
    apogee::tui::Shell shell{{.title = "apogee test", .theme = {.color = false}}};
    apogee::tui::ListView view;
};

/// Two promoted versions of `backend`, the second active: their weights and
/// its ledger.
void seed_ledger(const Home& home, const std::string& backend) {
    const fs::path weights = home.home.home() / "models" / "tiny" / "gguf";
    write(weights / "v1" / "tiny-v1.gguf", "v1");
    write(weights / "v2" / "tiny-v2.gguf", "v2");
    apogee::training::VersionLedger ledger;
    ledger.backend = backend;
    ledger.active_version = 2;
    ledger.versions = {{.version = 1,
                        .run_id = "run-20261010-120000",
                        .gguf_path = (weights / "v1" / "tiny-v1.gguf").string(),
                        .promoted_at = "2026-10-10T12:00:00Z",
                        .eval_score = 0.9,
                        .eval_passed = true},
                       {.version = 2,
                        .run_id = "run-20261010-130000",
                        .gguf_path = (weights / "v2" / "tiny-v2.gguf").string(),
                        .promoted_at = "2026-10-10T13:00:00Z"}};
    REQUIRE(apogee::training::save_ledger(home.training() / "versions", ledger).empty());
}

/// Two promoted versions of `tuned`, the second active, and its config entry.
void seed_versions(const Home& home) {
    seed_ledger(home, "tuned");
    const fs::path weights = home.home.home() / "models" / "tiny" / "gguf";
    write(home.home.config_path(), slurp(home.home.config_path()) +
                                       "  tuned:\n    type: llamacpp\n    model_path: " +
                                       (weights / "v2" / "tiny-v2.gguf").string() + "\n");
}

/// `text` with `home`'s path written `HOME`, as written plain and as a JSON
/// string writes it, so two homes' files compare.
[[nodiscard]] std::string written_alike(std::string text, const fs::path& home) {
    const std::string spelled = nlohmann::json(home.string()).dump();
    for (const std::string& path : {home.string(), spelled.substr(1, spelled.size() - 2)}) {
        for (std::size_t at = text.find(path); at != std::string::npos; at = text.find(path)) {
            text.replace(at, path.size(), "HOME");
        }
    }
    return text;
}

/// A JSONC home whose `-x` is a hand-written llamacpp backend with two
/// promoted versions, the second active -- laid out as `seed_versions` lays
/// out `tuned` (CLI-M51).
constexpr const char* kDashedBackend =
    "{\n  \"backends\": {\n    \"local\": {\"type\": \"mock\"},\n"
    "    \"-x\": {\"type\": \"llamacpp\", \"model_path\": "
    "\"{home}/models/tiny/gguf/v2/tiny-v2.gguf\"}\n  }\n}\n";

/// Moves the view's selection to the row drawn `› <name>`.
void select(Stage& stage, const std::string& name) {
    (void)stage.frame();
    for (int step = 0; step < 20 && !has(stage.frame(), "› " + name); ++step) {
        stage.press(Key::named(Key::Name::Down));
    }
    REQUIRE(has(stage.frame(), "› " + name));
}

/// A run's manifest with what differs between two runs -- its id, its clock,
/// the directory named by its id, and the home it is under -- left out.
[[nodiscard]] nlohmann::json comparable(const fs::path& runs, const fs::path& home) {
    std::vector<fs::path> dirs;
    for (const auto& entry : fs::directory_iterator(runs)) {
        dirs.push_back(entry.path());
    }
    REQUIRE(dirs.size() == 1);
    std::string error;
    const std::optional<apogee::training::RunManifest> manifest =
        apogee::training::read_manifest(dirs.front(), error);
    REQUIRE(manifest.has_value());
    nlohmann::json json = apogee::training::manifest_to_json(*manifest);
    for (const char* key : {"run_id", "started_at", "finished_at", "adapter_dir"}) {
        json.erase(key);
    }
    // Each home's own paths, written alike.
    std::string text = json.dump();
    for (std::size_t at = text.find(home.string()); at != std::string::npos;
         at = text.find(home.string())) {
        text.replace(at, home.string().size(), "HOME");
    }
    return nlohmann::json::parse(text);
}

}  // namespace

TEST_CASE("the Train view draws train versions' document under train status's lines",
          "[cli][tui][training]") {
    const Home home;
    seed_versions(home);
    std::string out;
    std::string err;
    REQUIRE(home.home.run({"train", "versions", "--output-format", "json"}, &out, &err) == 0);
    const nlohmann::json document = nlohmann::json::parse(out);
    const apogee::tui::ListOptions options =
        apogee::commands::train_view_options(home.context, nullptr, {});
    const auto [heading, rows] = options.load();
    REQUIRE(rows.size() == 2);
    const nlohmann::json& versions = document["data"].at(0)["versions"];
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const nlohmann::json& version = versions.at(i);
        INFO(version.dump());
        CHECK(rows.at(i).cells.at(0) == document["data"].at(0)["backend_name"].get<std::string>());
        CHECK(rows.at(i).cells.at(3) == version["run_id"].get<std::string>());
        CHECK(rows.at(i).cells.at(2) == version["promoted_at"].get<std::string>().substr(0, 19));
        CHECK(rows.at(i).cells.at(5) ==
              fs::path{version["gguf_path"].get<std::string>()}.filename().string());
    }
    CHECK(rows.at(0).cells.at(1) == "v1");
    CHECK(rows.at(0).cells.at(4) == "pass 90%");
    CHECK(rows.at(1).cells.at(1) == "v2 active");
    CHECK(rows.at(1).cells.at(4) == "-");
    // The heading is `train status`'s text, line for line.
    REQUIRE(home.home.run({"train", "status"}, &out, &err) == 0);
    std::string drawn;
    for (const std::string& line : heading) {
        drawn += line + "\n";
    }
    CHECK(drawn == out);
    CHECK(has(out, "tuned"));
    // `train status`'s document is the admin plane's; `versions <backend>`
    // is that backend's ledger alone.
    REQUIRE(home.home.run({"train", "status", "--output-format", "json"}, &out, &err) == 0);
    const nlohmann::json status = nlohmann::json::parse(out);
    CHECK(status["versions"].at(0)["backend"] == "tuned");
    CHECK(status["recent_runs"].empty());
    REQUIRE(home.home.run({"train", "versions", "tuned", "--output-format", "json"}, &out, &err) ==
            0);
    CHECK(nlohmann::json::parse(out) == document["data"].at(0));
    CHECK(home.home.run({"train", "versions", "nope", "--output-format", "json"}, &out, &err) == 1);
    CHECK(has(err, "no version history for backend 'nope'"));
}

TEST_CASE("the Datasets view draws datasets list's document, and its cards are the commands'",
          "[cli][tui][training]") {
    const Home home;
    std::string out;
    std::string err;
    REQUIRE(home.home.run({"datasets", "list", "--output-format", "json"}, &out, &err) == 0);
    const nlohmann::json document = nlohmann::json::parse(out);
    const apogee::tui::ListOptions options =
        apogee::commands::datasets_view_options(home.context, nullptr, {});
    const auto [heading, rows] = options.load();
    REQUIRE(rows.size() == document["data"].size());
    REQUIRE_FALSE(rows.empty());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const nlohmann::json& row = document["data"].at(i);
        CHECK(rows.at(i).cells.at(0) == row["name"].get<std::string>());
        CHECK(rows.at(i).cells.at(1) == row["lines"].dump());
        CHECK(rows.at(i).cells.at(2) == row["shape"].get<std::string>());
    }
    REQUIRE(home.home.run({"datasets", "info", rows.front().key}, &out, &err) == 0);
    std::string card;
    for (const std::string& line : options.detail(rows.front())) {
        card += line + "\n";
    }
    CHECK(card == out);
    REQUIRE(home.home.run({"datasets", "info", rows.front().key, "--output-format", "json"}, &out,
                          &err) == 0);
    CHECK(nlohmann::json::parse(out) == document["data"].at(0));
    // `k`: `datasets kits`'s own lines.
    REQUIRE(home.home.run({"datasets", "kits"}, &out, &err) == 0);
    CHECK(options.actions.front().key == "k");
    CHECK(options.actions.front().run({}) == out);
    REQUIRE(home.home.run({"datasets", "kits", "--output-format", "json"}, &out, &err) == 0);
    CHECK(nlohmann::json::parse(out)["object"] == "list");
}

TEST_CASE("a mock run from the Train view is apogee train run, its manifest the command's",
          "[cli][tui][training][child]") {
    const fs::path binary = built_binary();
    if (binary.empty()) {
        SKIP("this build made no apogee binary to run as the shell's child");
    }
    nlohmann::json expected;
    {
        const Home twin;
        std::string out;
        std::string err;
        REQUIRE(twin.home.run({"train", "run", "tiny", "--dataset", "starter", "--trainer", "mock",
                               "--iters", "3"},
                              &out, &err) == 0);
        expected = comparable(twin.training() / "runs", twin.home.home());
    }
    const Home home;
    Stage stage{home, true, binary};
    stage.press(Key::character("r"));
    CHECK(has(stage.frame(), "run: run ▏"));  // the command's own words, its arguments to come
    stage.type("tiny --dataset starter --trainer mock --iters 3");
    stage.press(Key::named(Key::Name::Return));
    CHECK(has(stage.frame(),
              "Start 'apogee train run tiny --dataset starter --trainer mock "
              "--iters 3'? A training run holds this machine until it ends [y/N]"));
    stage.press(Key::character("y"));
    stage.until_ended();
    CHECK(
        has(stage.frame(), " train run tiny --dataset starter --trainer mock --iters 3 -- ended"));
    const std::string said = stage.said();
    INFO(said);
    // The command's own narration, in the order it wrote it, then its end.
    CHECK(said.find("[train] run ") < said.find("[train] iter 3/3"));
    CHECK(said.find("[train] iter 3/3") < said.find(" complete"));
    CHECK(said.ends_with("exit 0\n"));
    CHECK(comparable(home.training() / "runs", home.home.home()) == expected);
}

TEST_CASE("a rollback from the Train view leaves train rollback's config and ledger",
          "[cli][tui][training][child]") {
    const fs::path binary = built_binary();
    if (binary.empty()) {
        SKIP("this build made no apogee binary to run as the shell's child");
    }
    std::string twin_config;
    std::string twin_ledger;
    {
        const Home twin;
        seed_versions(twin);
        std::string out;
        std::string err;
        REQUIRE(twin.home.run({"train", "rollback", "tuned"}, &out, &err) == 0);
        twin_config = slurp(twin.home.config_path());
        twin_ledger = slurp(twin.training() / "versions" / "tuned.json");
        // The paths are each home's own: compare them relative to it.
        for (std::string* text : {&twin_config, &twin_ledger}) {
            for (std::size_t at = text->find(twin.home.home().string()); at != std::string::npos;
                 at = text->find(twin.home.home().string())) {
                text->replace(at, twin.home.home().string().size(), "HOME");
            }
        }
    }
    const Home home;
    seed_versions(home);
    Stage stage{home, true, binary};
    (void)stage.frame();
    CHECK(has(stage.frame(), "R rollback"));
    stage.press(Key::character("R"));
    CHECK(has(stage.frame(),
              "Roll tuned back to its previous version ('apogee train rollback tuned')? [y/N]"));
    stage.press(Key::character("y"));
    CHECK(has(stage.frame(), "rolled back tuned: v2 -> v1"));
    std::string config = slurp(home.home.config_path());
    std::string ledger = slurp(home.training() / "versions" / "tuned.json");
    for (std::string* text : {&config, &ledger}) {
        for (std::size_t at = text->find(home.home.home().string()); at != std::string::npos;
             at = text->find(home.home.home().string())) {
            text->replace(at, home.home.home().string().size(), "HOME");
        }
    }
    CHECK(config == twin_config);
    CHECK(ledger == twin_ledger);
    CHECK(has(stage.frame(), "v1 active"));
}

TEST_CASE("a cycle from the Train view is refused on its held lock in the command's words",
          "[cli][tui][training][child]") {
    const fs::path binary = built_binary();
    if (binary.empty()) {
        SKIP("this build made no apogee binary to run as the shell's child");
    }
    const Home home{kCycle};
    std::string error;
    const std::optional<apogee::training::CycleLock> held =
        apogee::training::CycleLock::acquire(apogee::harness::training_cycle_dir(), error);
    REQUIRE(held.has_value());
    std::string out;
    std::string err;
    CHECK(home.home.run({"train", "cycle", "run"}, &out, &err) != 0);
    const std::string refused = err.substr(0, err.find('\n'));
    REQUIRE(has(refused, "another cycle is already running"));

    Stage stage{home, true, binary};
    (void)stage.frame();
    stage.press(Key::character("c"));
    CHECK(has(stage.frame(), "Run the training cycle once ('apogee train cycle run')?"));
    stage.press(Key::character("y"));
    stage.until_ended();
    const std::string said = stage.said();
    INFO(said);
    CHECK(has(said, refused));
    CHECK(said.ends_with("exit 1\n"));
}

TEST_CASE("a dataset deleted from its view is apogee datasets delete's, asked first",
          "[cli][tui][training][child]") {
    const fs::path binary = built_binary();
    if (binary.empty()) {
        SKIP("this build made no apogee binary to run as the shell's child");
    }
    const Home home;
    const fs::path starter = home.training() / "datasets" / "starter.jsonl";
    Stage stage{home, false, binary};
    (void)stage.frame();
    while (!has(stage.frame(), "› starter")) {
        stage.press(Key::named(Key::Name::Down));
    }
    stage.press(Key::character("x"));
    CHECK(has(stage.frame(), "Delete dataset starter ('apogee datasets delete starter')? [y/N]"));
    stage.press(Key::character("n"));
    CHECK(fs::exists(starter));
    stage.press(Key::character("x"));
    stage.press(Key::character("y"));
    CHECK(has(stage.frame(), "will remove:"));
    CHECK_FALSE(fs::exists(starter));
}

TEST_CASE("a dataset named with a leading dash is confirmed by the command that reaches it",
          "[cli][tui][training][M48]") {
    // CLI-M48: the confirm names the command as it runs when pasted -- `--`
    // before a dash-led name -- by the one renderer.
    const Home home;
    const fs::path dashed = home.training() / "datasets" / "-x.jsonl";
    write(dashed,
          "{\"messages\": [{\"role\": \"user\", \"content\": \"hi\"}, "
          "{\"role\": \"assistant\", \"content\": \"hello\"}]}\n");
    Stage stage{home, false, built_binary()};
    (void)stage.frame();
    for (int step = 0; step < 20 && !has(stage.frame(), "› -x"); ++step) {
        stage.press(Key::named(Key::Name::Down));
    }
    REQUIRE(has(stage.frame(), "› -x"));
    stage.press(Key::character("x"));
    CHECK(has(stage.frame(), "Delete dataset -x ('apogee datasets delete -- -x')? [y/N]"));
    stage.press(Key::character("n"));
    CHECK(fs::exists(dashed));
}

TEST_CASE("a dataset named with a leading dash is deleted from its view by the command",
          "[cli][tui][training][child][M51]") {
    // CLI-M51: the act's argv is its options, then `--`, then the row's name,
    // so a dash-led name reaches `datasets delete` as its operand. The file is
    // written directly: an existing name stays deletable whatever a create
    // rule later refuses.
    const fs::path binary = built_binary();
    if (binary.empty()) {
        SKIP("this build made no apogee binary to run as the shell's child");
    }
    const Home home;
    const fs::path dashed = home.training() / "datasets" / "-x.jsonl";
    write(dashed,
          "{\"messages\": [{\"role\": \"user\", \"content\": \"hi\"}, "
          "{\"role\": \"assistant\", \"content\": \"hello\"}]}\n");
    Stage stage{home, false, binary};
    select(stage, "-x");
    stage.press(Key::character("x"));
    stage.press(Key::character("y"));
    const std::string frame = stage.frame();
    INFO(frame);
    CHECK(has(frame, "will remove:"));
    CHECK(has(frame, "removed."));
    CHECK_FALSE(has(frame, "name is required"));
    CHECK_FALSE(has(frame, "exit 106"));
    CHECK_FALSE(fs::exists(dashed));
    CHECK_FALSE(has(frame, "› -x"));  // read again, the row gone
}

TEST_CASE("a dataset named -h is deleted from its view, never answered with the verb's help",
          "[cli][tui][training][child][M51]") {
    const fs::path binary = built_binary();
    if (binary.empty()) {
        SKIP("this build made no apogee binary to run as the shell's child");
    }
    const Home home;
    const fs::path dashed = home.training() / "datasets" / "-h.jsonl";
    write(dashed,
          "{\"messages\": [{\"role\": \"user\", \"content\": \"hi\"}, "
          "{\"role\": \"assistant\", \"content\": \"hello\"}]}\n");
    Stage stage{home, false, binary};
    select(stage, "-h");
    stage.press(Key::character("x"));
    stage.press(Key::character("y"));
    const std::string frame = stage.frame();
    INFO(frame);
    CHECK(has(frame, "removed."));
    CHECK_FALSE(has(frame, "Usage: apogee datasets delete"));
    CHECK_FALSE(fs::exists(dashed));
}

TEST_CASE("a rollback from the Train view reaches a hand-written dash-led backend as the command",
          "[cli][tui][training][child][M51]") {
    // CLI-M51: `train rollback -- -x` on a twin home is the oracle -- its
    // answer, success or its own refusal, and the config and ledger it
    // leaves. The twin runs the built binary too, so a refusal naming the
    // binary names the same one.
    const fs::path binary = built_binary();
    if (binary.empty()) {
        SKIP("this build made no apogee binary to run as the shell's child");
    }
    std::string twin_config;
    std::string twin_ledger;
    std::string twin_said;
    {
        const Home twin{kDashedBackend, "config.json"};
        seed_ledger(twin, "-x");
        const apogee::commands::ChildOutput said =
            apogee::commands::run_child_text(apogee::commands::self_command(
                twin.context, binary, {"train", "rollback", "--", "-x"}));
        twin_said = said.text.substr(0, said.text.find('\n'));
        twin_config = written_alike(slurp(twin.home.config_path()), twin.home.home());
        twin_ledger =
            written_alike(slurp(twin.training() / "versions" / "-x.json"), twin.home.home());
    }
    REQUIRE_FALSE(twin_said.empty());
    const Home home{kDashedBackend, "config.json"};
    seed_ledger(home, "-x");
    Stage stage{home, true, binary};
    (void)stage.frame();
    REQUIRE(has(stage.frame(), "R rollback"));
    stage.press(Key::character("R"));
    stage.press(Key::character("y"));
    const std::string frame = stage.shell.render_text(400, 60);
    INFO(frame);
    CHECK(has(frame, twin_said));
    CHECK_FALSE(has(frame, "is required"));
    CHECK_FALSE(has(frame, "exit 106"));
    CHECK(written_alike(slurp(home.home.config_path()), home.home.home()) == twin_config);
    CHECK(written_alike(slurp(home.training() / "versions" / "-x.json"), home.home.home()) ==
          twin_ledger);
}

TEST_CASE(
    "the Train view's rollback confirm names the command as it runs, a dash-led name after --",
    "[cli][tui][training][M61]") {
    const Home home{kDashedBackend, "config.json"};
    seed_ledger(home, "-x");
    Stage stage{home, true, {}};
    (void)stage.frame();
    REQUIRE(has(stage.frame(), "R rollback"));
    stage.press(Key::character("R"));
    INFO(stage.frame());
    CHECK(has(stage.frame(),
              "Roll -x back to its previous version ('apogee train rollback -- -x')? [y/N]"));
    stage.press(Key::character("n"));
}

TEST_CASE("a typed run's confirm is the command as it would run, each word as the renderer says it",
          "[cli][tui][training][M61]") {
    const Home home;
    Stage stage{home, true, {}};
    stage.press(Key::character("r"));
    CHECK(has(stage.frame(), "run: run ▏"));
    stage.type("tiny --dataset 'my set'");
    stage.press(Key::named(Key::Name::Return));
    INFO(stage.frame());
    CHECK(has(stage.frame(),
              "Start 'apogee train run tiny --dataset \"my set\"'? A training run "
              "holds this machine until it ends [y/N]"));
    stage.press(Key::character("n"));
}
