#include "cli/tui_agents.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "cli/command.h"
#include "secrets/store.h"
#include "support/cli_home.h"
#include "tui/list_view.h"
#include "tui/pump.h"
#include "tui/shell.h"

/// The shell's tooling views (37g) held to the commands: Agents' rows `agents
/// list --output-format json`'s, MCP's `mcp list`'s, Auth's the new `auth
/// list --output-format json`'s -- metadata only, a stored key never drawn.
/// As children of the built binary (skipped where the build made none): `e`
/// and `d` leaving the config `mcp enable|disable` leaves, `t` answering as
/// `mcp test` answers over a real stdio server, `x` deleting an agent's
/// entry as `agents delete` does with nobody to ask about its files.
namespace {

namespace fs = std::filesystem;
using apogee::tui::Key;

[[nodiscard]] bool has(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
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

/// A home with a disabled server, `quiet`, and `extra` more of the config.
struct Home {
    explicit Home(const std::string& extra = {})
        : home{
              "backends:\n  local:\n    type: mock\n  cloud:\n    type: anthropic\n    model: "
              "claude\nmodels:\n  default: local\nmcp_servers:\n  quiet:\n    command: "
              "/bin/false\n    enabled: false\n" +
              extra} {
        context.config_path = home.config_path().string();
    }

    apogee::testing::CliHome home;
    apogee::commands::RootContext context;
};

/// A view on a shell over a manual pump.
struct Stage {
    explicit Stage(apogee::tui::ListOptions options)
        : view{pump, apogee::tui::Theme{.color = false}, std::move(options)} {
        shell.add(view.view());
        shell.activate(0);
    }

    [[nodiscard]] std::string frame() {
        for (int i = 0; i < 3; ++i) {
            view.settle();
            (void)pump.drain();
        }
        return shell.render_text(200, 50);
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

    apogee::tui::ManualPump pump;
    apogee::tui::Shell shell{{.title = "apogee test", .theme = {.color = false}}};
    apogee::tui::ListView view;
};

[[nodiscard]] std::string joined(const std::vector<std::string>& lines) {
    std::string out;
    for (const std::string& line : lines) {
        out += line + "\n";
    }
    return out;
}

}  // namespace

TEST_CASE("the Agents view draws agents list's document, its card the agent's definition",
          "[cli][tui][tooling]") {
    const Home home;
    std::string out;
    std::string err;
    REQUIRE(home.home.run({"agents", "create", "reviewer"}, &out, &err) == 0);
    REQUIRE(home.home.run({"agents", "list", "--output-format", "json"}, &out, &err) == 0);
    const nlohmann::json document = nlohmann::json::parse(out);
    const apogee::tui::ListOptions options =
        apogee::commands::agents_view_options(home.context, {});
    const auto [heading, rows] = options.load();
    REQUIRE(rows.size() == document["data"].size());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const nlohmann::json& agent = document["data"].at(i);
        CHECK(rows.at(i).key == agent["name"].get<std::string>());
        CHECK(rows.at(i).cells.at(3) == (agent["bundled"].get<bool>() ? "bundled" : "config"));
    }
    const auto reviewer = std::ranges::find_if(
        rows, [](const apogee::tui::ListRow& row) { return row.key == "reviewer"; });
    REQUIRE(reviewer != rows.end());
    const std::string card = joined(options.detail(*reviewer));
    CHECK(has(card, "name:"));
    CHECK(has(card, "reviewer"));
    CHECK(has(card, "prompts:"));
    // A bundled agent with no entry has nothing for `x` to delete.
    REQUIRE(options.actions.front().key == "x");
    CHECK(options.actions.front().applies(*reviewer));
    for (const apogee::tui::ListRow& row : rows) {
        if (row.cells.at(3) == "bundled") {
            CHECK_FALSE(options.actions.front().applies(row));
        }
    }
}

TEST_CASE("the MCP view draws mcp list's document, and Auth auth list's, metadata only",
          "[cli][tui][tooling]") {
    const Home home;
    std::string out;
    std::string err;
    REQUIRE(home.home.run({"mcp", "list", "--output-format", "json"}, &out, &err) == 0);
    const nlohmann::json servers = nlohmann::json::parse(out);
    const apogee::tui::ListOptions mcp = apogee::commands::mcp_view_options(home.context, {});
    const auto [mcp_heading, mcp_rows] = mcp.load();
    REQUIRE(mcp_rows.size() == servers["data"].size());
    CHECK(mcp_rows.front().cells == std::vector<std::string>{"quiet", "disabled", "-", "-"});
    CHECK(has(joined(mcp.detail(mcp_rows.front())), "command:"));
    // `e` offered on a disabled server, `d` not.
    CHECK(mcp.actions.at(0).key == "e");
    CHECK(mcp.actions.at(0).applies(mcp_rows.front()));
    CHECK_FALSE(mcp.actions.at(1).applies(mcp_rows.front()));

    // A key stored: the view draws who holds one and when, never the key.
    const std::string key = "sk-ant-tui-auth-view-0123456789";
    apogee::secrets::CredentialStore store{
        apogee::secrets::credentials_path(home.home.config_path())};
    store.put("anthropic", key);
    REQUIRE(home.home.run({"auth", "list", "--output-format", "json"}, &out, &err) == 0);
    CHECK_FALSE(has(out, key));
    const nlohmann::json credentials = nlohmann::json::parse(out);
    const apogee::tui::ListOptions auth = apogee::commands::auth_view_options(home.context);
    const auto [heading, rows] = auth.load();
    REQUIRE(rows.size() == credentials["data"].size());
    REQUIRE(rows.size() == 1);
    CHECK(rows.front().cells == std::vector<std::string>{credentials["data"][0]["provider"],
                                                         credentials["data"][0]["stored_at"]});
    const std::string drawn = joined(heading);
    CHECK(has(drawn, "backend cloud (anthropic): its key from " +
                         credentials["backends"][0]["source"].get<std::string>()));
    CHECK_FALSE(has(drawn, key));
    Stage stage{apogee::commands::auth_view_options(home.context)};
    CHECK_FALSE(has(stage.frame(), key));
    CHECK(has(stage.frame(), "anthropic"));
}

TEST_CASE("enable and disable from the MCP view leave mcp enable and disable's config",
          "[cli][tui][tooling][child]") {
    const fs::path binary = built_binary();
    if (binary.empty()) {
        SKIP("this build made no apogee binary to run as the shell's child");
    }
    const Home twin;
    std::string out;
    std::string err;
    REQUIRE(twin.home.run({"mcp", "enable", "quiet"}, &out, &err) == 0);
    const std::string enabled = slurp(twin.home.config_path());
    const std::string said = out;

    const Home home;
    Stage stage{apogee::commands::mcp_view_options(home.context, binary)};
    CHECK(has(stage.frame(), "e enable"));
    stage.press(Key::character("e"));
    CHECK(has(stage.frame(), said.substr(0, said.find('\n'))));
    CHECK(slurp(home.home.config_path()) == enabled);
    REQUIRE(twin.home.run({"mcp", "disable", "quiet"}, &out, &err) == 0);
    (void)stage.frame();
    stage.press(Key::character("d"));
    CHECK(slurp(home.home.config_path()) == slurp(twin.home.config_path()));
}

TEST_CASE("t answers as mcp test does, over a real stdio server", "[cli][tui][tooling][child]") {
    const fs::path binary = built_binary();
    if (binary.empty()) {
        SKIP("this build made no apogee binary to run as the shell's child");
    }
    // Apogee's own read-only tools as the server, reading the home's work.
    const auto config = [&binary](const fs::path& work) {
        return "  tools:\n    command: " + binary.string() + "\n    args: [__mcp-tools]\n" +
               "tools:\n  fs_root: " + work.string() + "\n";
    };
    const apogee::testing::TempDir work{"tui-mcp-work"};
    std::ofstream{work.path() / "note.txt"} << "hello from the note\n";
    const Home home{config(work.path())};
    std::string out;
    std::string err;
    REQUIRE(home.home.run({"mcp", "test", "tools", "read_file", R"({"path": "note.txt"})"}, &out,
                          &err) == 0);
    REQUIRE(has(out, "hello from the note"));

    Stage stage{apogee::commands::mcp_view_options(home.context, binary)};
    stage.view.settle();
    while (!has(stage.frame(), "› tools")) {
        stage.press(Key::named(Key::Name::Down));
    }
    stage.press(Key::character("t"));
    CHECK(has(stage.frame(), "test: tools ▏"));
    stage.type(R"(read_file '{"path": "note.txt"}')");
    stage.press(Key::named(Key::Name::Return));
    const apogee::tui::ListOptions options =
        apogee::commands::mcp_view_options(home.context, binary);
    // The command's own words, its stderr's narration then its answer, as a
    // script reading both streams sees them.
    CHECK(options.asks.front().ask({.key = "tools"}, R"(tools read_file '{"path": "note.txt"}')") ==
          err + out);
    CHECK(has(stage.frame(), "hello from the note"));
}

TEST_CASE("deleting an agent from its view is agents delete's, its files kept",
          "[cli][tui][tooling][child]") {
    const fs::path binary = built_binary();
    if (binary.empty()) {
        SKIP("this build made no apogee binary to run as the shell's child");
    }
    std::string twin_config;
    {
        const Home twin;
        std::string out;
        std::string err;
        REQUIRE(twin.home.run({"agents", "create", "reviewer"}, &out, &err) == 0);
        REQUIRE(twin.home.run({"agents", "delete", "reviewer"}, &out, &err) == 0);
        twin_config = slurp(twin.home.config_path());
    }
    const Home home;
    std::string out;
    std::string err;
    REQUIRE(home.home.run({"agents", "create", "reviewer"}, &out, &err) == 0);
    const fs::path prompt = home.home.home() / "prompts" / "reviewer.txt";
    REQUIRE(fs::exists(prompt));
    Stage stage{apogee::commands::agents_view_options(home.context, binary)};
    while (!has(stage.frame(), "› reviewer")) {
        stage.press(Key::named(Key::Name::Down));
    }
    stage.press(Key::character("x"));
    CHECK(has(stage.frame(),
              "Delete agent reviewer's entry ('apogee agents delete reviewer')? "
              "Its prompt and schema files stay [y/N]"));
    stage.press(Key::character("n"));
    CHECK(has(slurp(home.home.config_path()), "reviewer"));
    stage.press(Key::character("x"));
    stage.press(Key::character("y"));
    CHECK(has(stage.frame(), "removed agent 'reviewer'"));
    CHECK(slurp(home.home.config_path()) == twin_config);
    CHECK(fs::exists(prompt));
}
