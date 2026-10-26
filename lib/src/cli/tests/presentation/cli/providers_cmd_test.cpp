#include "cli/providers_cmd.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include "backends/provider_cache.h"
#include "cli/command.h"
#include "cli/complete_protocol.h"
#include "cli/complete_sources.h"
#include "cli/provider_offer.h"
#include "contracts/config.h"
#include "contracts/layout.h"
#include "support/cli_home.h"

/// `providers register <id>` (M33): one scanned provider registered by the
/// core the Providers view's `r` runs (`register_provider`), over the last
/// scan in the provider cache -- no provider is probed by any case here, and
/// none registered fetches a roster: the Claude and Gemini CLIs list none.
namespace {

using apogee::backends::CredentialState;
using apogee::backends::ProviderStatus;
using apogee::harness::BackendType;

constexpr const char* kConfig = R"(backends:
  local:
    type: mock
)";

[[nodiscard]] ProviderStatus status(std::string id, BackendType type, bool installed) {
    ProviderStatus out;
    out.id = std::move(id);
    out.type = type;
    out.installed = installed;
    out.binary = installed ? "/opt/tools/" + out.id : std::string{};
    out.installed_evidence =
        installed ? out.binary + ", 9.9.9" : "'" + out.id + "' was not found on PATH";
    if (installed && type == BackendType::ClaudeCli) {
        out.credentials = CredentialState::Found;
        out.credential_evidence = "~/.claude.json exists";
    }
    return out;
}

/// The last scan as an explicit one would have left it in this home's cache:
/// the Claude and Gemini CLIs installed, Codex not.
void store_scan() {
    apogee::backends::ProviderCache cache;
    cache.scanned_at = "2026-10-26T12:00:00Z";
    for (const ProviderStatus& row : {status("claude", BackendType::ClaudeCli, true),
                                      status("gemini", BackendType::GeminiCli, true),
                                      status("codex", BackendType::CodexCli, false)}) {
        cache.providers.emplace(row.id, row);
    }
    REQUIRE(apogee::backends::store_provider_cache(apogee::harness::provider_cache_path(), cache));
}

[[nodiscard]] std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream in{text};
    for (std::string line; std::getline(in, line);) {
        lines.push_back(line);
    }
    return lines;
}

}  // namespace

TEST_CASE("providers register sets the default when none is set, as scan --register does",
          "[providers][register]") {
    const apogee::testing::CliHome home{kConfig};
    store_scan();
    // What the batch's core would write for the same provider, in a twin.
    const apogee::testing::CliHome twin{kConfig};
    store_scan();
    const apogee::commands::RegistrationPlan plan =
        apogee::commands::plan_registration({status("claude", BackendType::ClaudeCli, true)},
                                            apogee::harness::load_config(twin.config_path()));
    apogee::commands::apply_registration(twin.config_path(), plan);

    std::string out;
    REQUIRE(home.run({"providers", "register", "claude"}, &out) == 0);
    CHECK(out == apogee::commands::render_registration(plan, /*applied=*/true));
    CHECK(out == "registered claude (claude-cli)\nmodels.default set to 'claude' (none was set)\n");
    CHECK(home.config_text() == twin.config_text());
    CHECK(apogee::harness::load_config(home.config_path()).models.default_backend == "claude");
}

TEST_CASE("providers register --output-format json carries scan --register's lines",
          "[providers][register][json]") {
    const apogee::testing::CliHome home{kConfig};
    store_scan();
    std::string out;
    std::string err;
    REQUIRE(home.run({"providers", "register", "gemini", "--output-format", "json"}, &out, &err) ==
            0);
    CHECK(err.empty());
    const nlohmann::json document = nlohmann::json::parse(out);
    CHECK(document["object"] == "provider.registration");
    CHECK(document["provider"] == "gemini");
    // `providers scan --register --output-format json`'s keys, a line each.
    CHECK(document["registration"] ==
          nlohmann::json(lines_of("registered gemini (gemini-cli)\nmodels.default set to "
                                  "'gemini' (none was set)\n")));
    CHECK(document["rosters"] == nlohmann::json::array());
    CHECK(apogee::harness::load_config(home.config_path()).find_backend("gemini") != nullptr);
}

TEST_CASE("providers register refuses what it cannot ask the last scan about, writing nothing",
          "[providers][register]") {
    const apogee::testing::CliHome home{kConfig};
    const std::string before = home.config_text();
    std::string out;
    std::string err;

    // Never scanned: the scan is named, never run here.
    CHECK(home.run({"providers", "register", "claude"}, &out, &err) == 1);
    CHECK(err.find("this machine has not been scanned for providers yet -- 'apogee providers "
                   "scan' looks") != std::string::npos);
    CHECK(home.config_text() == before);

    store_scan();
    // A provider Apogee does not know, the ones it does named.
    CHECK(home.run({"providers", "register", "nope"}, &out, &err) == 1);
    CHECK(err.find("no provider named 'nope'") != std::string::npos);
    CHECK(err.find("claude") != std::string::npos);
    CHECK(home.config_text() == before);

    // One the last scan did not find: the core's own words, nothing written.
    REQUIRE(home.run({"providers", "register", "codex"}, &out, &err) == 0);
    CHECK(out == "nothing to register -- no provider was detected\n");
    CHECK(home.config_text() == before);

    // No config file: refused as scan --register refuses, and none created.
    std::filesystem::remove(home.config_path());
    CHECK(home.run_default({"providers", "register", "claude"}, &out, &err) == 1);
    CHECK(err.find("run 'apogee config init' first") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(home.home() / "config" / "config.json"));
    CHECK_FALSE(std::filesystem::exists(home.home() / "config" / "config.yaml"));
}

TEST_CASE("providers register completes what the last scan found to register, and takes each",
          "[providers][register][completion]") {
    const apogee::testing::CliHome home{kConfig};
    const apogee::harness::Config config = apogee::harness::load_config(home.config_path());
    apogee::commands::CompletionContext context;
    context.config = &config;
    const apogee::commands::NameList cold =
        apogee::commands::list_names(apogee::commands::kProviderValue, context);
    CHECK(cold.names.empty());
    CHECK(cold.none.find("apogee providers scan") != std::string::npos);

    store_scan();
    const apogee::commands::NameList offered =
        apogee::commands::list_names(apogee::commands::kProviderValue, context);
    CHECK(offered.names == std::vector<std::string>{"claude", "gemini"});
    // The offer is a contract (ADR 0007): each name offered is taken.
    for (const std::string& name : offered.names) {
        INFO(name);
        std::string out;
        CHECK(home.run({"providers", "register", name}, &out) == 0);
        CHECK(out.starts_with("registered " + name + " ("));
    }
}
