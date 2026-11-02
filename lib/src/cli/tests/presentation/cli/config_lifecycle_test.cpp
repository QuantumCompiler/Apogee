#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "cli/complete_protocol.h"
#include "cli/registry.h"
#include "cli/root.h"
#include "contracts/config.h"
#include "contracts/config_edit.h"
#include "contracts/config_migrate.h"
#include "platform/file_lock.h"
#include "support/cli_home.h"

/// The config file's move to JSONC, as a user meets it (28i): a fresh init
/// writes `config.json`; an older install's `config.yaml` keeps working
/// with one line saying so until `config migrate`, after which runs are
/// silent; both files side by side are refused; `config upgrade` carries a
/// newer template's options in and `check` says when there are any -- in
/// its human face and its JSON one.
namespace {

using apogee::testing::CliHome;

constexpr const char* kYaml = R"(# mine
backends:
  local:
    type: mock   # the test model
    model: mock-1
models:
  default: local
)";

constexpr std::string_view kNotice = "is in the older YAML format";

struct Run {
    int code = -1;
    std::string out;
    std::string err;
};

Run run_default(const CliHome& home, const std::vector<std::string>& args) {
    Run result;
    result.code = home.run_default(args, &result.out, &result.err);
    return result;
}

std::string read(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

std::size_t count(std::string_view text, std::string_view needle) {
    std::size_t found = 0;
    for (std::size_t at = text.find(needle); at != std::string_view::npos;
         at = text.find(needle, at + 1)) {
        ++found;
    }
    return found;
}

nlohmann::json config_row(const Run& run) {
    const nlohmann::json report = nlohmann::json::parse(run.out);
    for (const nlohmann::json& row : report.at("rows")) {
        if (row.at("section") == "Config") {
            return row;
        }
    }
    FAIL("no Config row");
    return {};
}

std::string fixture(std::string_view name) {
    return read(std::filesystem::path{APOGEE_TEST_FIXTURES} / "config" / std::string{name});
}

}  // namespace

TEST_CASE("config init writes config.json, and not beside an older config.yaml",
          "[commands][config][jsonc]") {
    SECTION("a fresh install") {
        const CliHome home{"", "config.json"};
        std::filesystem::remove(home.config_path());
        const Run init = run_default(home, {"config", "init"});
        CHECK(init.code == 0);
        CHECK(init.out.find("config.json") != std::string::npos);
        CHECK(read(home.config_path()) == apogee::harness::config_template());
        const Run path = run_default(home, {"config", "path"});
        CHECK(path.out == home.config_path().string() + "\n");
        CHECK(path.err.empty());
    }
    SECTION("an older install: init points at migrate and writes nothing") {
        const CliHome home{kYaml};
        const Run init = run_default(home, {"config", "init", "--force"});
        CHECK(init.code != 0);
        CHECK(init.err.find("config migrate") != std::string::npos);
        CHECK_FALSE(std::filesystem::exists(home.home() / "config" / "config.json"));
        CHECK(read(home.config_path()) == kYaml);
    }
}

TEST_CASE("an older install's config.yaml keeps working with one line, until migrate",
          "[commands][config][jsonc][migrate]") {
    const CliHome home{kYaml};
    const Run before = run_default(home, {"models", "list", "--no-color"});
    CHECK(before.code == 0);
    CHECK(count(before.err, kNotice) == 1);
    CHECK(before.err.find("apogee config migrate") != std::string::npos);
    // check says it in its Config row instead, in both faces.
    const Run checked = run_default(home, {"check", "--no-color", "-q"});
    CHECK(checked.err.find(kNotice) == std::string::npos);
    CHECK(checked.out.find("the older YAML format") != std::string::npos);
    CHECK(checked.out.find("run: apogee config migrate") != std::string::npos);
    const nlohmann::json row = config_row(run_default(home, {"check", "--output-format", "json"}));
    CHECK(row.at("status") == "ok");
    CHECK(row.at("name") == "config.yaml");
    CHECK(row.at("remedy") == "apogee config migrate");
    // A --config naming the file reads it as named, without the line.
    std::string out;
    std::string err;
    CHECK(home.run({"models", "list"}, &out, &err) == 0);
    CHECK(err.find(kNotice) == std::string::npos);

    const Run migrated = run_default(home, {"config", "migrate"});
    CHECK(migrated.code == 0);
    CHECK(migrated.err.empty());
    CHECK(migrated.out.find("config.json") != std::string::npos);
    CHECK(migrated.out.find("2 comments carried") != std::string::npos);
    const std::filesystem::path json = home.home() / "config" / "config.json";
    // A comment straight above the first key is that key's; a header comes
    // apart from it by a blank line, and stays above the brace.
    CHECK(read(json) ==
          "{\n  // mine\n  \"backends\": {\n    \"local\": {\n      \"type\": \"mock\", // "
          "the test model\n      \"model\": \"mock-1\"\n    }\n  },\n  \"models\": {\n    "
          "\"default\": \"local\"\n  }\n}\n");
    CHECK(read(home.home() / "config" / "config.yaml.bak") == kYaml);
    CHECK_FALSE(std::filesystem::exists(home.config_path()));

    // The next run is silent, and reads the same install.
    const Run after = run_default(home, {"models", "list", "--no-color"});
    CHECK(after.code == 0);
    CHECK(after.err.find(kNotice) == std::string::npos);
    CHECK(after.out == before.out);
    const Run again = run_default(home, {"config", "migrate"});
    CHECK(again.code == 0);
    CHECK(again.out.find("already JSON") != std::string::npos);
}

TEST_CASE("config.yaml and config.json side by side are refused, both named",
          "[commands][config][jsonc]") {
    const CliHome home{kYaml};
    const std::filesystem::path json = home.home() / "config" / "config.json";
    std::ofstream{json, std::ios::binary} << "{}\n";
    const Run listed = run_default(home, {"models", "list"});
    CHECK(listed.code != 0);
    CHECK(listed.err.find(home.config_path().string()) != std::string::npos);
    CHECK(listed.err.find(json.string()) != std::string::npos);
    // check fails its Config row the same way, and looks at the rest.
    const Run checked = run_default(home, {"check", "--output-format", "json"});
    CHECK(checked.code != 0);
    const nlohmann::json row = config_row(checked);
    CHECK(row.at("status") == "fail");
    CHECK(row.at("detail").get<std::string>().find("which one is current") != std::string::npos);
    // migrate will not pick one either.
    const Run migrated = run_default(home, {"config", "migrate"});
    CHECK(migrated.code != 0);
    CHECK(read(home.config_path()) == kYaml);
    CHECK(read(json) == "{}\n");
}

TEST_CASE("config upgrade adds what the starter config has and the file lacks; check counts it",
          "[commands][config][jsonc][upgrade]") {
    const std::string old = fixture("upgrade_old.json");
    const CliHome home{old, "config.json"};
    const std::size_t missing = apogee::harness::missing_template_options(old).size();
    REQUIRE(missing > 0);
    const std::string count_said = std::to_string(missing) + " options";

    // Behind is healthy: the row is ok, and says how far and what to run.
    const Run human = run_default(home, {"check", "--no-color", "-q"});
    CHECK(human.out.find(count_said) != std::string::npos);
    CHECK(human.out.find("run: apogee config upgrade") != std::string::npos);
    nlohmann::json row = config_row(run_default(home, {"check", "--output-format", "json"}));
    CHECK(row.at("status") == "ok");
    CHECK(row.at("detail").get<std::string>().find(count_said) != std::string::npos);
    CHECK(row.at("remedy") == "apogee config upgrade");

    const Run upgraded = run_default(home, {"config", "upgrade"});
    CHECK(upgraded.code == 0);
    CHECK(upgraded.out.find("added " + count_said) != std::string::npos);
    CHECK(upgraded.out.find("  paths\n") != std::string::npos);
    const std::string after = read(home.config_path());
    CHECK(apogee::harness::missing_template_options(after).empty());
    // Every line the user wrote is still there, in order.
    std::istringstream lines{old};
    std::string line;
    std::size_t from = 0;
    while (std::getline(lines, line)) {
        INFO(line);
        const std::size_t at = after.find(line, from);
        CHECK(at != std::string::npos);
        from = at == std::string::npos ? from : at + line.size();
    }

    // Current: the note gone from both faces, a second run a no-op.
    const Run current = run_default(home, {"check", "--no-color", "-q"});
    CHECK(current.out.find("run: apogee config upgrade") == std::string::npos);
    row = config_row(run_default(home, {"check", "--output-format", "json"}));
    CHECK(row.at("detail") == "parses cleanly");
    CHECK_FALSE(row.contains("remedy"));
    const Run again = run_default(home, {"config", "upgrade"});
    CHECK(again.code == 0);
    CHECK(again.out.find("is current") != std::string::npos);
    CHECK(read(home.config_path()) == after);
}

TEST_CASE("an edit that lands while config upgrade waits on the lock survives the upgrade",
          "[commands][config][jsonc][upgrade][lock]") {
    // 57f, finding 1 (a regression of 55f): upgrade read the file, computed
    // its text, and only then took the lock -- writing that text over
    // whatever another editor landed while it waited. The probe as a test:
    // this holds config.json.lock, starts the upgrade on an older starter
    // config, lets it make its first read, changes the file as another
    // editor would, then lets go.
    const std::string old = fixture("upgrade_old.json");
    const CliHome home{old, "config.json"};
    const std::size_t missing = apogee::harness::missing_template_options(old).size();
    REQUIRE(missing > 0);

    // What another editor lands while the upgrade waits.
    std::string landed;
    SECTION("another editor adds a backend") {
        apogee::harness::BackendConfig backend;
        backend.type = apogee::harness::BackendType::Mock;
        backend.model = "m";
        landed = apogee::harness::append_backend(old, "added_meanwhile", backend, false);
    }
    SECTION("another upgrade lands first") {
        landed = apogee::harness::upgrade_config_text(old, apogee::harness::config_template()).text;
    }

    apogee::platform::FileLock held = apogee::harness::lock_config_file(home.config_path());
    std::atomic<bool> started{false};
    std::atomic<bool> done{false};
    Run upgraded;
    std::string thrown;
    std::thread waiter{[&] {
        started = true;
        try {
            upgraded = run_default(home, {"config", "upgrade"});
        } catch (const std::exception& e) {
            thrown = e.what();
        }
        done = true;
    }};
    while (!started) {
        std::this_thread::yield();
    }
    // Its first read is made well inside this: it finds the file behind and
    // waits on the lock held here.
    std::this_thread::sleep_for(std::chrono::milliseconds{500});
    const bool waited = !done;
    std::string write_error;
    try {
        apogee::harness::write_file_atomically(home.config_path(), landed);
    } catch (const std::exception& e) {
        write_error = e.what();
    }
    held.release();
    waiter.join();
    REQUIRE(write_error.empty());
    CHECK(waited);
    REQUIRE(thrown.empty());
    CHECK(upgraded.code == 0);
    CHECK(upgraded.err.empty());

    const std::string after = read(home.config_path());
    (void)apogee::harness::parse_config(after, "<after>");
    CHECK(apogee::harness::missing_template_options(after).empty());
    if (landed.find("added_meanwhile") != std::string::npos) {
        // Both land: the other editor's backend and the upgraded options.
        CHECK(upgraded.out.find("added " + std::to_string(missing) + " options") !=
              std::string::npos);
        CHECK(
            after ==
            apogee::harness::upgrade_config_text(landed, apogee::harness::config_template()).text);
        const Run listed = run_default(home, {"config", "get", "backends"});
        CHECK(listed.code == 0);
        CHECK(listed.out == "added_meanwhile\nlocal\n");
    } else {
        // Nothing left to add: the file as the first upgrade left it, said
        // current.
        CHECK(upgraded.out.find("is current") != std::string::npos);
        CHECK(upgraded.out.find("added") == std::string::npos);
        CHECK(after == landed);
    }
}

TEST_CASE("an older config that sets a commented option's key is not behind by it",
          "[commands][config][jsonc][upgrade]") {
    // 57f, finding 2 on the command line: a config seeded before 0.1.9 that
    // writes models.default_video itself. Upgrade neither names it added nor
    // puts the template's example under the live key, and check does not
    // count it among the options not in the file.
    //
    // The starter config before 0.1.9: every declared block taken out --
    // its lines, the bare `//` joining it to the text above, the blank lines
    // above it -- as a file seeded then holds the template.
    std::vector<std::string> lines;
    std::istringstream in{std::string{apogee::harness::config_template()}};
    for (std::string line; std::getline(in, line);) {
        lines.push_back(line);
    }
    const auto trimmed = [](const std::string& line) {
        const std::size_t first = line.find_first_not_of(" \t");
        return first == std::string::npos ? std::string{} : line.substr(first);
    };
    const std::vector<apogee::harness::CommentedOption> options =
        apogee::harness::commented_options();
    for (const apogee::harness::CommentedOption& option : options) {
        auto first = std::ranges::find(lines, std::string{option.opens}, trimmed);
        REQUIRE(first != lines.end());
        const auto last = std::find_if(first, lines.end(), [&](const std::string& line) {
            return trimmed(line) == option.closes;
        });
        REQUIRE(last != lines.end());
        while (first != lines.begin() && trimmed(*(first - 1)) == "//") {
            --first;
        }
        while (first != lines.begin() && trimmed(*(first - 1)).empty()) {
            --first;
        }
        lines.erase(first, last + 1);
    }
    std::string old;
    for (const std::string& line : lines) {
        old += line + "\n";
    }
    const std::size_t since = options.size();
    const std::string models_opens = "  \"models\": {\n";
    old.insert(old.find(models_opens) + models_opens.size(), "    \"default_video\": \"mypipe\"\n");
    const CliHome home{old, "config.json"};

    const nlohmann::json row = config_row(run_default(home, {"check", "--output-format", "json"}));
    CHECK(row.at("detail") == "parses cleanly -- " + std::to_string(since - 1) +
                                  " options from this release's starter config not in the file "
                                  "(their defaults apply)");
    const Run upgraded = run_default(home, {"config", "upgrade"});
    CHECK(upgraded.code == 0);
    CHECK(upgraded.out.find("added " + std::to_string(since - 1) + " options") !=
          std::string::npos);
    CHECK(upgraded.out.find("models.default_video") == std::string::npos);
    const std::string after = read(home.config_path());
    CHECK(count(after, "\"default_video\"") == 1);
    CHECK(after.find("\"default_video\": \"mypipe\"") != std::string::npos);
    const Run got = run_default(home, {"config", "get", "models.default_video"});
    CHECK(got.out == "mypipe\n");
    CHECK(config_row(run_default(home, {"check", "--output-format", "json"})).at("detail") ==
          "parses cleanly");
}

TEST_CASE("config upgrade on an older YAML config names migrate", "[commands][config][upgrade]") {
    const CliHome home{kYaml};
    const Run upgraded = run_default(home, {"config", "upgrade"});
    CHECK(upgraded.code != 0);
    CHECK(upgraded.err.find("config migrate") != std::string::npos);
    CHECK(read(home.config_path()) == kYaml);
}

TEST_CASE("config migrate and config upgrade complete as verbs", "[commands][config][completion]") {
    const apogee::commands::RootCommand root{apogee::commands::default_registry()};
    const apogee::commands::CommandSpec tree = apogee::commands::specs_from_app(root.app());
    apogee::commands::CompletionRequest request;
    request.words = {"config"};
    request.current = "";
    const std::vector<std::string> verbs =
        apogee::commands::completion_candidates(request, apogee::harness::Config{}, tree);
    CHECK(std::ranges::find(verbs, "migrate") != verbs.end());
    CHECK(std::ranges::find(verbs, "upgrade") != verbs.end());
    request.current = "up";
    CHECK(apogee::commands::completion_candidates(request, apogee::harness::Config{}, tree) ==
          std::vector<std::string>{"upgrade"});
}
