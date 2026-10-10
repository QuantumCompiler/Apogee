#include "cli/tui_workbench.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "cli/models.h"
#include "logger/session.h"
#include "support/cli_home.h"
#include "tui/list_view.h"
#include "tui/pump.h"
#include "tui/shell.h"

/// The workbench views (32d) on the real cores: each view draws what its
/// command reads, each key that edits a file leaves the command's file byte
/// for byte, a removal asks first and a declined one writes nothing, a stale
/// row is refused as the command refuses it, and the chats and suites views
/// hand the session what was chosen.
namespace {

using apogee::tui::Key;
namespace fs = std::filesystem;

constexpr const char* kConfig = R"(backends:
  local:
    type: mock
    model: mock-1
  spare:
    type: mock
    model: mock-2

models:
  default: local

suites:
  duo:
    description: The root and its helper.
    members:
      chat: local
      utility: spare
)";

[[nodiscard]] std::string slurp(const fs::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// One view on the shell's stage, over an install.
struct Bench {
    Bench(const apogee::testing::CliHome& home,
          apogee::tui::ListOptions (*make)(const apogee::commands::RootContext&))
        : Bench(home, [&](const apogee::commands::RootContext& context) { return make(context); }) {
    }

    template <typename Make>
    Bench(const apogee::testing::CliHome& home, Make make) {
        context.config_path = home.config_path().string();
        view = std::make_unique<apogee::tui::ListView>(pump, apogee::tui::Theme{.color = false},
                                                       make(context));
        shell.add(view->view());
        shell.activate(0);  // shown: read
    }

    void settle() {
        for (int i = 0; i < 3; ++i) {
            view->settle();
            (void)pump.drain();
        }
    }

    [[nodiscard]] std::string frame() {
        settle();
        return shell.render_text(400, 30);
    }

    void press(const Key& key) {
        (void)shell.press(key);
        settle();
    }

    /// Moves the selection down to the row showing `text`.
    void select(const std::string& text) {
        for (int i = 0; i < 20; ++i) {
            const std::string drawn = frame();
            const std::size_t marker = drawn.find(" › ");
            if (marker != std::string::npos &&
                drawn.substr(marker, drawn.find('\n', marker) - marker).find(text) !=
                    std::string::npos) {
                return;
            }
            press(Key::named(Key::Name::Down));
        }
        FAIL("no row shows " << text << "\n" << frame());
    }

    apogee::commands::RootContext context;
    apogee::tui::ManualPump pump;
    apogee::tui::Shell shell{{.title = "apogee test", .theme = {.color = false}}};
    std::unique_ptr<apogee::tui::ListView> view;
};

[[nodiscard]] bool has(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

void save_chat(const std::string& id, const std::string& title) {
    apogee::logger::Session session;
    session.chat_id = id;
    session.title = title;
    session.turns = 2;
    session.started_at = "2026-10-09T10:00:00Z";
    session.updated_at = "2026-10-09T10:05:00Z";
    apogee::logger::save(session);
}

}  // namespace

TEST_CASE("the models view draws exactly the rows models list reads", "[cli][tui][workbench]") {
    const apogee::testing::CliHome home{kConfig};
    Bench bench{home, apogee::commands::models_view_options};
    const std::string drawn = bench.frame();
    // `models list --output-format json`, read the same way, row for row.
    std::string out;
    std::string err;
    REQUIRE(home.run({"models", "list", "--output-format", "json"}, &out, &err) == 0);
    const nlohmann::json document = nlohmann::json::parse(out);
    REQUIRE(document["data"].size() >= 2);
    const apogee::tui::ListOptions options = apogee::commands::models_view_options(bench.context);
    const auto [heading, rows] = options.load();
    REQUIRE(rows.size() == document["data"].size());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const nlohmann::json& row = document["data"].at(i);
        CHECK(rows.at(i).cells.at(0) == row["backend"].get<std::string>());
        CHECK(rows.at(i).cells.at(1) == row["backend_type"].get<std::string>());
        CHECK(rows.at(i).cells.at(2) == row["model"].get<std::string>());
        CHECK(has(drawn, row["backend"].get<std::string>()));
    }
    CHECK(has(drawn, "models.default = local"));
    CHECK(has(drawn, "Enter info · d make default · r read again"));
    // Enter reads `models info` for the row.
    bench.select("spare");
    bench.press(Key::named(Key::Name::Return));
    CHECK(has(bench.frame(), "backend:"));
    bench.press(Key::named(Key::Name::Escape));
    CHECK_FALSE(has(bench.frame(), "Esc closes"));
}

TEST_CASE("make default leaves the file config set-default leaves", "[cli][tui][workbench]") {
    const apogee::testing::CliHome twin{kConfig};
    std::string said;
    REQUIRE(twin.run({"config", "set-default", "spare"}, &said) == 0);
    for (auto make :
         {apogee::commands::models_view_options, apogee::commands::config_view_options}) {
        const apogee::testing::CliHome home{kConfig};
        Bench bench{home, make};
        bench.select("spare");
        bench.press(Key::character("d"));
        CHECK(has(bench.frame(), "models.default = spare"));
        CHECK(slurp(home.config_path()) == slurp(twin.config_path()));
    }
}

TEST_CASE("remove asks first, writes config delete-backend's file on a yes and nothing on a no",
          "[cli][tui][workbench]") {
    const apogee::testing::CliHome twin{kConfig};
    std::string said;
    REQUIRE(twin.run({"config", "delete-backend", "spare"}, &said) == 0);

    const apogee::testing::CliHome home{kConfig};
    const std::string before = slurp(home.config_path());
    Bench bench{home, apogee::commands::config_view_options};
    CHECK(has(bench.frame(), "KEY"));
    bench.select("spare");
    bench.press(Key::character("x"));
    CHECK(has(bench.frame(),
              "Remove backend 'spare' from " + home.config_path().string() + "? [y/N]"));
    bench.press(Key::character("n"));
    CHECK(has(bench.frame(), "not done"));
    CHECK(slurp(home.config_path()) == before);

    bench.select("spare");
    bench.press(Key::character("x"));
    bench.press(Key::character("y"));
    CHECK(has(bench.frame(), "removed backend 'spare' from " + home.config_path().string()));
    CHECK(slurp(home.config_path()) == slurp(twin.config_path()));
    CHECK_FALSE(has(bench.frame(), " spare "));
}

TEST_CASE("a stale row is refused as the command refuses it, and nothing is written",
          "[cli][tui][workbench]") {
    const apogee::testing::CliHome home{kConfig};
    Bench bench{home, apogee::commands::config_view_options};
    bench.select("spare");
    // The backend goes behind the view's back; the view still shows it.
    std::string said;
    REQUIRE(home.run({"config", "delete-backend", "spare"}, &said) == 0);
    const std::string after_cli = slurp(home.config_path());
    std::string refused;
    CHECK(home.run({"config", "delete-backend", "spare"}, &refused) != 0);
    bench.press(Key::character("x"));
    bench.press(Key::character("y"));
    CHECK(has(bench.frame(), "not done"));
    CHECK(slurp(home.config_path()) == after_cli);
}

TEST_CASE("make default on a suite leaves the file config set-default-suite leaves",
          "[cli][tui][workbench]") {
    const apogee::testing::CliHome twin{kConfig};
    std::string said;
    REQUIRE(twin.run({"config", "set-default-suite", "duo"}, &said) == 0);
    const apogee::testing::CliHome home{kConfig};
    Bench bench{home, [](const apogee::commands::RootContext& context) {
                    return apogee::commands::suites_view_options(context, {});
                }};
    const std::string drawn = bench.frame();
    CHECK(has(drawn, "duo"));
    CHECK(has(drawn, "chat=local utility=spare"));
    CHECK(has(drawn, "The root and its helper."));
    bench.press(Key::character("d"));
    CHECK(has(bench.frame(), "models.default_suite = duo"));
    CHECK(slurp(home.config_path()) == slurp(twin.config_path()));
}

TEST_CASE(
    "the chats view draws chats list's rows, opens one in the session and deletes as chats "
    "delete does",
    "[cli][tui][workbench]") {
    const apogee::testing::CliHome home{kConfig};
    save_chat("20261009-100000-aaaa", "planning");
    save_chat("20261009-090000-bbbb", "debugging");
    std::vector<std::string> opened;
    int shown = 0;
    const apogee::commands::WorkbenchHooks hooks{.open_chat =
                                                     [&opened](const std::string& id) {
                                                         opened.push_back(id);
                                                         return std::string{"opened"};
                                                     },
                                                 .use_suite = {},
                                                 .show_session = [&shown]() { ++shown; }};
    Bench bench{home, [&hooks](const apogee::commands::RootContext& /*context*/) {
                    return apogee::commands::chats_view_options(hooks);
                }};
    std::string out;
    std::string err;
    REQUIRE(home.run({"chats", "list", "--output-format", "json"}, &out, &err) == 0);
    const nlohmann::json listed = nlohmann::json::parse(out);
    const std::string drawn = bench.frame();
    for (const nlohmann::json& row : listed["data"]) {
        CHECK(has(drawn, row["id"].get<std::string>()));
        CHECK(has(drawn, row["name"].get<std::string>()));
    }
    bench.select("planning");
    bench.press(Key::named(Key::Name::Return));
    CHECK(opened == std::vector<std::string>{"20261009-100000-aaaa"});
    CHECK(shown == 1);
    bench.press(Key::character("i"));
    CHECK(has(bench.frame(), "20261009-100000-aaaa"));
    bench.press(Key::named(Key::Name::Escape));

    bench.select("debugging");
    bench.press(Key::character("x"));
    CHECK(has(bench.frame(), "Delete chat debugging (20261009-090000-bbbb)"));
    bench.press(Key::character("y"));
    CHECK(has(bench.frame(), "deleted 20261009-090000-bbbb"));
    CHECK_FALSE(fs::exists(apogee::logger::session_path("20261009-090000-bbbb")));
    CHECK(fs::exists(apogee::logger::session_path("20261009-100000-aaaa")));
    // `chats delete` refuses what the view refused: the chat is gone.
    CHECK(home.run({"chats", "delete", "20261009-090000-bbbb"}, &out) != 0);
}

TEST_CASE("the suites view hands the session the suite chosen", "[cli][tui][workbench]") {
    const apogee::testing::CliHome home{kConfig};
    std::vector<std::string> used;
    const apogee::commands::WorkbenchHooks hooks{.open_chat = {},
                                                 .use_suite =
                                                     [&used](const std::string& suite) {
                                                         used.push_back(suite);
                                                         return std::string{"used"};
                                                     },
                                                 .show_session = []() {}};
    Bench bench{home, [&hooks](const apogee::commands::RootContext& context) {
                    return apogee::commands::suites_view_options(context, hooks);
                }};
    (void)bench.frame();
    bench.press(Key::named(Key::Name::Return));
    CHECK(used == std::vector<std::string>{"duo"});
}

TEST_CASE("a workbench view is not typing: numbers switch views and q quits",
          "[cli][tui][workbench]") {
    const apogee::testing::CliHome home{kConfig};
    Bench bench{home, apogee::commands::config_view_options};
    (void)bench.frame();
    CHECK(bench.shell.press(Key::character("q")));
    CHECK(bench.shell.quit_requested());
}
