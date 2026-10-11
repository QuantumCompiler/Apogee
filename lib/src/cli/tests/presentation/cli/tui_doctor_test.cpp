#include "cli/tui_doctor.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "backends/provider_cache.h"
#include "cli/provider_offer.h"
#include "cli/providers_cmd.h"
#include "cli/registry.h"
#include "cli/root.h"
#include "cli/system_cmd.h"
#include "contracts/layout.h"
#include "platform/system_info.h"
#include "support/cli_home.h"
#include "support/env_guard.h"
#include "tui/list_view.h"
#include "tui/pump.h"
#include "tui/shell.h"

/// The shell's doctor views (37b) held to their commands, the 32d way: the
/// Check view's rows are `check --output-format json`'s, its fix pass says
/// `check --fix`'s lines; the Providers view draws the last scan as
/// `providers scan` words it and registers as the registration core does,
/// byte for byte; the System page is `apogee system`'s table.
namespace {

namespace fs = std::filesystem;
using apogee::tui::Key;

constexpr const char* kConfig = R"(backends:
  local:
    type: mock
models:
  default: local
)";

[[nodiscard]] std::string slurp(const fs::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] apogee::commands::RootContext context_of(const apogee::testing::CliHome& home) {
    apogee::commands::RootContext context;
    context.config_path = home.config_path().string();
    return context;
}

/// One view on a shell over a manual pump, as the shell draws it.
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
        return shell.render_text(200, 40);
    }

    void press(const Key& key) {
        (void)shell.press(key);
        (void)frame();
    }

    apogee::tui::ManualPump pump;
    apogee::tui::Shell shell{{.title = "apogee test", .theme = {.color = false}}};
    apogee::tui::ListView view;
};

[[nodiscard]] bool has(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

/// A provider cache as an explicit scan would have left it: the Claude CLI
/// installed with its login's evidence, Codex not found.
[[nodiscard]] std::vector<apogee::backends::ProviderStatus> scanned() {
    apogee::backends::ProviderStatus claude;
    claude.id = "claude";
    claude.type = apogee::harness::BackendType::ClaudeCli;
    claude.installed = true;
    claude.binary = "/opt/tools/claude";
    claude.installed_evidence = "/opt/tools/claude, 9.9.9 (Claude Code)";
    claude.credentials = apogee::backends::CredentialState::Found;
    claude.credential_evidence = "~/.claude.json exists";
    apogee::backends::ProviderStatus codex;
    codex.id = "codex";
    codex.type = apogee::harness::BackendType::CodexCli;
    codex.installed_evidence = "'codex' was not found on PATH";
    return {claude, codex};
}

void store_scan(const std::vector<apogee::backends::ProviderStatus>& statuses) {
    apogee::backends::ProviderCache cache;
    cache.scanned_at = "2026-10-10T12:00:00Z";
    for (const apogee::backends::ProviderStatus& status : statuses) {
        cache.providers.emplace(status.id, status);
    }
    REQUIRE(apogee::backends::store_provider_cache(apogee::harness::provider_cache_path(), cache));
}

class FakeSystem final : public apogee::platform::SystemSource {
public:
    [[nodiscard]] apogee::platform::CpuInfo cpu() const override {
        return {.model = "Fake 9000", .physical_cores = 8, .logical_cores = 8};
    }

    [[nodiscard]] std::optional<apogee::platform::CpuTimes> cpu_times() const override {
        ++reads_;
        return apogee::platform::CpuTimes{.busy = 100 * reads_, .total = 400 * reads_};
    }

    [[nodiscard]] std::optional<apogee::platform::LoadAverage> load_average() const override {
        return apogee::platform::LoadAverage{
            .one_minute = 1, .five_minutes = 2, .fifteen_minutes = 3};
    }

    [[nodiscard]] std::string load_unknown() const override {
        return {};
    }

    [[nodiscard]] apogee::platform::MemoryInfo memory() const override {
        return {.total = std::int64_t{16} << 30U, .available = std::int64_t{4} << 30U};
    }

    [[nodiscard]] std::optional<std::int64_t> process_footprint() const override {
        return std::int64_t{30} << 20U;
    }

    [[nodiscard]] apogee::platform::GpuInfo gpu() const override {
        return {.unknown = "not read on this platform yet"};
    }

    [[nodiscard]] apogee::platform::VolumeInfo volume(const fs::path& /*path*/) const override {
        return {.capacity = std::int64_t{1} << 40U, .available = std::int64_t{1} << 39U};
    }

private:
    mutable std::uint64_t reads_ = 0;
};

}  // namespace

TEST_CASE("the Check view draws check's own rows, and its fix says check --fix's lines",
          "[cli][tui][doctor]") {
    const apogee::testing::CliHome home{kConfig};
    std::string out;
    std::string err;
    (void)home.run({"check", "--output-format", "json"}, &out, &err);
    const nlohmann::json document = nlohmann::json::parse(out);

    const apogee::commands::RootContext context = context_of(home);
    const apogee::tui::ListOptions options = apogee::commands::check_view_options(context);
    const auto [heading, rows] = options.load();
    REQUIRE(rows.size() == document["rows"].size());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const nlohmann::json& row = document["rows"].at(i);
        INFO(row.dump());
        CHECK(rows.at(i).cells ==
              std::vector<std::string>{row["status"], row["section"], row["name"], row["detail"]});
    }
    REQUIRE(heading.size() == 2);
    CHECK(heading.at(0) == "checked " + home.home().string());

    // A hole in the layout, repaired by the command, then made again and
    // repaired from the view: the same lines, the same tree.
    std::string seeded;
    (void)home.run({"check", "--fix"}, &seeded);  // the whole layout first
    fs::remove_all(home.home() / "cache");
    std::string fixed;
    (void)home.run({"check", "--fix"}, &fixed);
    REQUIRE(fs::exists(home.home() / "cache"));
    fs::remove_all(home.home() / "cache");
    Stage stage{apogee::commands::check_view_options(context)};
    CHECK(has(stage.frame(), "f fix"));
    stage.press(Key::character("f"));
    CHECK(has(stage.frame(), "Run the fix pass?"));
    stage.press(Key::character("y"));
    const std::string drawn = stage.frame();
    std::istringstream said{fixed};
    int lines = 0;
    for (std::string line; std::getline(said, line);) {
        if (line.starts_with("fixed: ")) {
            INFO(line << "\n" << drawn);
            CHECK(has(drawn, line));
            ++lines;
        }
    }
    CHECK(lines > 0);
    CHECK(fs::exists(home.home() / "cache"));
}

TEST_CASE("the Providers view draws the last scan as providers scan words it, and probes nothing",
          "[cli][tui][doctor]") {
    const apogee::testing::CliHome home{kConfig};
    const apogee::commands::RootContext context = context_of(home);
    {
        // Never scanned: said, and the scan named.
        const auto [heading, rows] = apogee::commands::providers_view_options(context).load();
        CHECK(rows.empty());
        CHECK(heading.front() == "not scanned yet -- s scans this machine for providers");
    }
    store_scan(scanned());
    const apogee::tui::ListOptions options = apogee::commands::providers_view_options(context);
    const auto [heading, rows] = options.load();
    const apogee::harness::Config config = apogee::harness::load_config(home.config_path());
    const nlohmann::json document = apogee::commands::provider_scan_document(
        apogee::commands::provider_rows(scanned(), &config));
    REQUIRE(rows.size() == document["data"].size());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const nlohmann::json& row = document["data"].at(i);
        CHECK(rows.at(i).key == row["provider"].get<std::string>());
        CHECK(rows.at(i).cells ==
              std::vector<std::string>{row["provider"], row["tier"], row["backend"]});
    }
    CHECK(rows.at(0).cells ==
          std::vector<std::string>{"claude", "credentials found", "not registered"});
    const std::vector<std::string> evidence = options.detail(rows.at(0));
    CHECK(evidence == std::vector<std::string>{"Claude CLI (claude-cli)  credentials found",
                                               "/opt/tools/claude, 9.9.9 (Claude Code)",
                                               "~/.claude.json exists"});
}

TEST_CASE("register from the Providers view writes the registration core's config, byte for byte",
          "[cli][tui][doctor]") {
    // The twin: the core `providers scan --register` runs, over the same scan.
    const apogee::testing::CliHome twin{kConfig};
    store_scan(scanned());
    {
        const apogee::harness::Config config = apogee::harness::load_config(twin.config_path());
        std::vector<apogee::backends::ProviderStatus> claude{scanned().front()};
        apogee::commands::apply_registration(twin.config_path(),
                                             apogee::commands::plan_registration(claude, config));
    }

    const apogee::testing::CliHome home{kConfig};
    store_scan(scanned());
    const apogee::commands::RootContext context = context_of(home);  // outlives the view
    Stage stage{apogee::commands::providers_view_options(context)};
    CHECK(has(stage.frame(), "r register"));
    stage.press(Key::character("r"));
    CHECK(has(stage.frame(), "Found claude -- register it as a backend? [y/N]"));
    stage.press(Key::character("y"));
    CHECK(has(stage.frame(), "registered claude (claude-cli)"));
    CHECK(slurp(home.config_path()) == slurp(twin.config_path()));
    CHECK(has(stage.frame(), "backend: claude"));

    // Again: refused in the command's own words, nothing written.
    const std::string before = slurp(home.config_path());
    stage.press(Key::character("r"));
    stage.press(Key::character("y"));
    CHECK(has(stage.frame(), "claude: already registered as 'claude' -- nothing written"));
    CHECK(slurp(home.config_path()) == before);
}

TEST_CASE("the System page is apogee system's table, read on show", "[cli][tui][doctor]") {
    const apogee::testing::TempDir home{"doctor-system"};
    const apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    const FakeSystem command_os;
    const FakeSystem view_os;
    const auto seams = [](const FakeSystem& os) {
        return apogee::commands::SystemSeams{
            .machine = []() { return apogee::models::MachineBudget{}; },
            .source = &os,
            .wait = [](std::chrono::milliseconds) {}};
    };
    apogee::commands::CommandRegistry registry;
    registry.add(std::make_unique<apogee::commands::SystemCommand>(seams(command_os)));
    apogee::commands::RootCommand root{std::move(registry)};
    const std::ostringstream printed;
    std::streambuf* old_out = std::cout.rdbuf(printed.rdbuf());
    const char* argv[] = {"apogee", "system"};
    (void)root.run(2, argv);
    std::cout.rdbuf(old_out);

    const apogee::tui::ListOptions options = apogee::commands::system_view_options(seams(view_os));
    CHECK(options.page);
    const auto [lines, rows] = options.load();
    CHECK(rows.empty());
    std::string page;
    for (const std::string& line : lines) {
        page += line + "\n";
    }
    CHECK(page == printed.str());
    CHECK(has(page, "Fake 9000"));
}
