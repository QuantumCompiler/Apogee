#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <string_view>

#include "backends/factory.h"
#include "cli/auth_cmd.h"
#include "cli/check.h"
#include "cli/models.h"
#include "cli/tui_workbench.h"
#include "contracts/config.h"
#include "contracts/layout.h"
#include "events/bus.h"
#include "harness/harness.h"
#include "httpserver/admin_auth_routes.h"
#include "httpserver/admin_config.h"
#include "httpserver/admin_tasks.h"
#include "logger/operational.h"
#include "operations/read_views.h"
#include "secrets/resolve.h"
#include "secrets/store.h"
#include "support/cli_home.h"
#include "support/env_guard.h"
#include "tasks/ledger.h"

/// The leak test: one distinctive key, stored every way a key can be stored,
/// then every surface that reports on keys is rendered and searched for it.
///
/// This is the acceptance criterion of the credential store stated as a
/// single assertion: a secret goes in, and nothing that renders, serializes,
/// logs or publishes ever carries it. The surfaces here are the complete set
/// -- the day a new one appears, it is added here.
namespace {

constexpr std::string_view kKey = "sk-LEAKPROBE-7f3a9c1e5b2d";
constexpr std::string_view kFragment = "LEAKPROBE";
/// A task's declared answer, distinctive enough to find anywhere (27i).
constexpr std::string_view kAnswerProbe = "ANSWERPROBE-4e9b1c7d";

struct World {
    apogee::testing::TempDir home{"secrets-leak-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard home_guard{"APOGEE_HOME", home.path().string()};
    std::filesystem::path config_path = home.path() / "config" / "config.yaml";
    apogee::secrets::CredentialStore store{apogee::secrets::credentials_path(config_path)};
    apogee::secrets::EnvSnapshot env;
    apogee::harness::Config config;

    World() {
        std::filesystem::create_directories(config_path.parent_path());
        std::filesystem::create_directories(home.path() / "logs");
        // Every rung at once: config, store and environment each hold the key.
        std::ofstream{config_path} << "backends:\n"
                                      "  cfg:\n"
                                      "    type: anthropic\n"
                                      "    api_key: \""
                                   << kKey
                                   << "\"\n"
                                      "  stored:\n"
                                      "    type: openai\n"
                                      "  ambient:\n"
                                      "    type: google\n"
                                      // An MCP server whose environment holds the
                                      // key: its view says only that it sets one.
                                      "mcp_servers:\n"
                                      "  probe:\n"
                                      "    command: /bin/true\n"
                                      "    enabled: false\n"
                                      "    env:\n"
                                      "      - \"TOKEN="
                                   << kKey << "\"\n";
        config = apogee::harness::load_config(config_path);
        store.put("openai", kKey);
        env = apogee::secrets::EnvSnapshot::capture([](std::string_view name) {
            return name == "GEMINI_API_KEY" ? std::string{kKey} : "";
        });
    }

    [[nodiscard]] apogee::httpserver::AdminAuthContext context() const {
        return apogee::httpserver::AdminAuthContext{.config_path = config_path, .env = &env};
    }
};

void expect_clean(const std::string& surface, const std::string& text) {
    INFO(surface << ":\n" << text);
    CHECK(text.find(kKey) == std::string::npos);
    CHECK(text.find(kFragment) == std::string::npos);
}

std::string drain(apogee::events::Subscription& subscription) {
    std::string all;
    while (const auto event = subscription.subscriber().wait_for(std::chrono::milliseconds{10})) {
        all += event->type + " " + event->data.dump() + "\n";
    }
    return all;
}

std::string logs_under(const std::filesystem::path& dir) {
    std::string all;
    for (const auto& entry : std::filesystem::directory_iterator{dir}) {
        std::ifstream in{entry.path()};
        std::ostringstream out;
        out << in.rdbuf();
        all += out.str();
    }
    return all;
}

}  // namespace

TEST_CASE("a stored key reaches no listing, report, response, event or log", "[secrets][leak]") {
    const World world;
    apogee::events::Subscription subscription =
        apogee::events::subscribe(apogee::events::default_bus());
    apogee::logger::set_enabled(true);

    struct Restore {
        Restore() = default;

        ~Restore() {
            apogee::logger::set_enabled(false);
        }

        Restore(const Restore&) = delete;
        Restore& operator=(const Restore&) = delete;
        Restore(Restore&&) = delete;
        Restore& operator=(Restore&&) = delete;
    } restore;

    // The key IS resolvable from every rung -- otherwise the test proves
    // nothing about the surfaces.
    REQUIRE(
        apogee::secrets::resolve_api_key(world.config.backends.at("cfg"), &world.store, world.env)
            .key == kKey);
    REQUIRE(apogee::secrets::resolve_api_key(world.config.backends.at("stored"), &world.store,
                                             world.env)
                .key == kKey);
    REQUIRE(apogee::secrets::resolve_api_key(world.config.backends.at("ambient"), &world.store,
                                             world.env)
                .key == kKey);

    // The terminal listing.
    const apogee::commands::AuthListing listing =
        apogee::commands::gather_auth_listing(world.config, world.store, world.env);
    REQUIRE(listing.backends.size() == 3);
    expect_clean("auth list", apogee::commands::render_auth_listing(listing));

    // The doctor, both as rows and rendered.
    apogee::commands::CheckInputs inputs;
    inputs.home = world.home.path();
    inputs.config_path = world.config_path;
    inputs.config = world.config;
    inputs.env = [](std::string_view name) {
        return name == "GEMINI_API_KEY" ? std::string{kKey} : "";
    };
    const apogee::commands::CheckReport report = apogee::commands::run_checks(inputs);
    for (const apogee::commands::CheckRow& row : report.rows) {
        expect_clean("check row " + row.name, row.name + row.detail + row.remedy);
    }
    expect_clean("check report", apogee::commands::render_report(report, false));
    // The reads' machine faces (28h): the doctor's document, the model
    // listing's, the agents' and the MCP servers'.
    expect_clean("check --output-format json",
                 apogee::commands::render_report_document(report, std::nullopt).dump());
    expect_clean(
        "models list --output-format json",
        apogee::commands::render_model_document(
            apogee::commands::build_model_rows(world.config, {}, world.config_path, &world.env),
            true)
            .dump());
    expect_clean("agents list --output-format json",
                 apogee::operations::agents_document(world.config).dump());
    expect_clean("mcp list --output-format json",
                 apogee::operations::mcp_servers_document(world.config).dump());

    // Every admin response: the listing, a PUT's echo, a DELETE's, and the
    // backend view.
    expect_clean("GET /v1/admin/auth",
                 apogee::httpserver::admin_list_credentials(world.context()).body);
    apogee::httpserver::HttpRequest put;
    put.method = "PUT";
    put.remote_address = "127.0.0.1";
    put.body = nlohmann::json{{"key", std::string{kKey}}}.dump();
    expect_clean("PUT /v1/admin/auth/anthropic",
                 apogee::httpserver::admin_put_credential(world.context(), "anthropic", put).body);
    expect_clean("DELETE /v1/admin/auth/anthropic",
                 apogee::httpserver::admin_clear_credential(world.context(), "anthropic").body);
    for (const auto& [name, entry] : world.config.backends) {
        expect_clean("backend view " + name, apogee::httpserver::backend_view(name, entry).dump());
    }

    // A build's reasons, whether or not the provider came up.
    apogee::harness::Harness harness{world.config};
    apogee::backends::BuildOptions options;
    options.config_path = world.config_path;
    options.env = &world.env;
    const apogee::backends::BuildResult built = apogee::backends::build_providers(harness, options);
    expect_clean("build summary", built.skipped_summary());
    for (const auto& status : built.statuses) {
        expect_clean("build status", status.reason);
    }

    // Whatever any of that published or logged.
    expect_clean("event bus", drain(subscription));
    expect_clean("operational log", logs_under(world.home.path() / "logs"));

    // And the file itself is the only place it lives -- private.
    if (apogee::harness::supports_private_modes()) {
        CHECK((std::filesystem::status(world.store.path()).permissions() &
               std::filesystem::perms::mask) ==
              (std::filesystem::perms::owner_read | std::filesystem::perms::owner_write));
    }
}

/// The ledger sweep (27i): a task's declared answer is stored in its ledger
/// like any other field -- the docs and `--help` say plainly that a
/// credential does not belong in one -- so this holds where it may travel.
/// It belongs in the ledger, in `task status` (which shows what the task was
/// handed), and in the conversation, as the tool result the model read; and
/// nowhere else: not the run's own lines, not `task list` or `chats list`,
/// not the operational log, not the event bus -- and, since 27j, none of the
/// surfaces a host or a remote client reads: `task status` and `task list`
/// as JSON, the admin plane's task routes, and machine mode's stream of a
/// task's run, each of which says an answer existed and never what it said.
/// Nor does any of those name the ledger's path.
TEST_CASE("the shell's workbench views draw no key, wherever it was stored",
          "[secrets][leak][tui]") {
    // The full-screen shell's views (32d) report on backends too: every row,
    // heading and detail each draws, searched for the key in every rung.
    const World world;
    apogee::commands::RootContext context;
    context.config_path = world.config_path.string();
    for (const apogee::tui::ListOptions& options :
         {apogee::commands::models_view_options(context),
          apogee::commands::config_view_options(context),
          apogee::commands::suites_view_options(context, {}),
          apogee::commands::chats_view_options({})}) {
        const auto [heading, rows] = options.load();
        std::string drawn;
        for (const std::string& line : heading) {
            drawn += line + "\n";
        }
        for (const apogee::tui::ListRow& row : rows) {
            drawn += row.key + ":";
            for (const std::string& cell : row.cells) {
                drawn += " " + cell;
            }
            drawn += "\n";
            if (options.detail) {
                for (const std::string& line : options.detail(row)) {
                    drawn += "  " + line + "\n";
                }
            }
        }
        expect_clean("the shell's " + options.title + " view", drawn);
    }
}

TEST_CASE("a task's declared answer is found only where it belongs", "[tasks][leak][policy]") {
    apogee::testing::CliHome home{""};
    const std::filesystem::path script = home.home() / "script.json";
    std::ofstream{script} << R"({"turns": [{"text": "1. Ask."},
        {"text": "", "tool_calls": [{"name": "ask_user", "arguments": {"questions": [
          {"header": "Colour", "question": "Which colour?",
           "options": [{"label": "Red"}, {"label": "Green"}]}]}}]},
        {"text": "Settled.\nTASK STATUS: DONE"}]})";
    std::ofstream{home.config_path()}
        << "backends:\n  m:\n    type: mock\n    model_path: " << script.string()
        << "\nmodels:\n  default: m\nmemory:\n  recall: false\n";
    std::filesystem::create_directories(home.home() / "logs");

    apogee::events::Subscription subscription =
        apogee::events::subscribe(apogee::events::default_bus());
    apogee::logger::set_enabled(true);

    struct Restore {
        Restore() = default;

        ~Restore() {
            apogee::logger::set_enabled(false);
        }

        Restore(const Restore&) = delete;
        Restore& operator=(const Restore&) = delete;
        Restore(Restore&&) = delete;
        Restore& operator=(Restore&&) = delete;
    } restore;

    std::string ran;
    REQUIRE(home.run({"task", "run", "Pick a colour", "--tools", "--on-question",
                      "answer:" + std::string{kAnswerProbe}},
                     &ran) == 0);
    const std::vector<apogee::tasks::Task> tasks = apogee::tasks::list_tasks(home.home() / "tasks");
    REQUIRE(tasks.size() == 1);
    const apogee::tasks::Task& task = tasks.front();

    // Where it belongs -- else the sweep proves nothing.
    const auto read = [](const std::filesystem::path& path) {
        std::ifstream in{path};
        std::ostringstream out;
        out << in.rdbuf();
        return out.str();
    };
    CHECK(read(apogee::tasks::ledger_path(home.home() / "tasks", task.id)).find(kAnswerProbe) !=
          std::string::npos);
    std::string status;
    REQUIRE(home.run({"task", "status", task.id}, &status) == 0);
    CHECK(status.find(kAnswerProbe) != std::string::npos);
    CHECK(read(home.home() / "sessions" / (task.session_id + ".json")).find(kAnswerProbe) !=
          std::string::npos);

    // And nowhere else.
    const auto clean = [](const std::string& surface, const std::string& text) {
        INFO(surface << ":\n" << text);
        CHECK(text.find(kAnswerProbe) == std::string::npos);
    };
    clean("the run's own output", ran);
    std::string listed;
    REQUIRE(home.run({"task", "list"}, &listed) == 0);
    clean("task list", listed);
    REQUIRE(home.run({"chats", "list"}, &listed) == 0);
    clean("chats list", listed);
    clean("event bus", drain(subscription));
    clean("operational log", logs_under(home.home() / "logs"));

    // The surfaces a host or a remote client reads (27j): the same task, and
    // a second run of it in machine mode. None carries the answer, nor the
    // private layout's path to the ledger.
    const std::string ledgers = (home.home() / "tasks").string();
    const auto served = [&clean, &ledgers](const std::string& surface, const std::string& text) {
        clean(surface, text);
        INFO(surface << ":\n" << text);
        CHECK(text.find(ledgers) == std::string::npos);
        CHECK(text.find("task.json") == std::string::npos);
    };
    std::string err;
    REQUIRE(home.run({"task", "status", task.id, "--output-format", "json"}, &listed, &err) == 0);
    CHECK(listed.find("\"on_question\":\"answer\"") != std::string::npos);  // it existed
    served("task status --output-format json", listed);
    REQUIRE(home.run({"task", "list", "--output-format", "json"}, &listed, &err) == 0);
    served("task list --output-format json", listed);
    {
        const apogee::testing::EnvGuard guard{"APOGEE_HOME", home.home().string()};
        apogee::httpserver::HttpRequest request;
        request.method = "GET";
        request.path = "/v1/admin/tasks";
        served("GET /v1/admin/tasks", apogee::httpserver::admin_list_tasks(request).body);
        served("GET /v1/admin/tasks/{id}", apogee::httpserver::admin_get_task(task.id).body);
    }
    std::string stream;
    REQUIRE(home.run({"task", "run", "Pick a colour", "--tools", "--on-question",
                      "answer:" + std::string{kAnswerProbe}, "--output-format", "stream-json"},
                     &stream, &err) == 0);
    CHECK(stream.find("\"task_finished\"") != std::string::npos);
    served("task run --output-format stream-json (stdout)", stream);
    clean("task run --output-format stream-json (stderr)", err);
    clean("event bus, after the surfaces", drain(subscription));
}
