#include "backends/provider_probe.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "backends/claude_cli.h"
#include "backends/codex_cli.h"
#include "backends/gemini_cli.h"
#include "backends/ollama_cli.h"
#include "backends/provider_cache.h"
#include "backends/provider_status.h"
#include "backends/provider_table.h"
#include "contracts/layout.h"
#include "platform/child_process.h"
#include "secrets/resolve.h"
#include "support/env_guard.h"
#include "support/fake_existence_view.h"

/// Provider detection (28a), against a filesystem and a process runner that
/// are both scripted -- every state of the matrix is an ordinary test, and no
/// real vendor CLI is run (or needed) to cover it.
namespace {

using apogee::backends::BinaryFingerprint;
using apogee::backends::CredentialState;
using apogee::backends::ExistenceView;
using apogee::backends::KeyPresence;
using apogee::backends::ProbeRun;
using apogee::backends::ProbeRunner;
using apogee::backends::ProviderCache;
using apogee::backends::ProviderStatus;
using apogee::backends::ProviderTier;
using apogee::backends::ScanOptions;
using apogee::backends::ScanReport;
using apogee::harness::BackendType;

using FakeView = apogee::testing::FakeExistenceView;

/// A runner answering from a script, keyed `program args...`, recording every
/// call. Anything unscripted answers `1.0.0` and exits 0.
struct ScriptedRunner {
    std::map<std::string, ProbeRun, std::less<>> script;
    std::vector<std::string> calls;
    std::vector<std::chrono::milliseconds> deadlines;

    [[nodiscard]] ProbeRunner runner() {
        return [this](const std::string& program, const std::vector<std::string>& arguments,
                      std::chrono::milliseconds deadline) {
            std::string key = program;
            for (const std::string& word : arguments) {
                key += " " + word;
            }
            calls.push_back(key);
            deadlines.push_back(deadline);
            const auto found = script.find(key);
            if (found != script.end()) {
                return found->second;
            }
            return ProbeRun{ProbeRun::Outcome::Exited, 0, "1.0.0\n"};
        };
    }

    [[nodiscard]] long count(std::string_view prefix) const {
        return std::ranges::count_if(
            calls, [prefix](const std::string& call) { return call.starts_with(prefix); });
    }
};

[[nodiscard]] KeyPresence no_keys() {
    return [](BackendType) { return std::optional<std::string>{}; };
}

[[nodiscard]] const ProviderStatus& row(const ScanReport& report, std::string_view id) {
    const auto found = std::ranges::find(report.providers, id, &ProviderStatus::id);
    REQUIRE(found != report.providers.end());
    return *found;
}

struct Scan {
    FakeView view;
    ScriptedRunner runner;
    KeyPresence keys = no_keys();
    ProviderCache cache;

    ScanReport run(const ScanOptions& options = {}) {
        return apogee::backends::scan_providers(view, runner.runner(), keys, cache, options,
                                                "2026-10-07T12:00:00Z");
    }
};

}  // namespace

TEST_CASE("the knowledge table: four CLIs, three API types, one row each", "[providers][table]") {
    const auto table = apogee::backends::provider_table();
    REQUIRE(table.size() == 7);
    std::set<std::string_view> ids;
    for (const auto& facts : table) {
        ids.insert(facts.id);
        CHECK(apogee::backends::find_provider(facts.id) == &facts);
        CHECK(apogee::backends::provider_for_type(facts.type) == &facts);
        CHECK_FALSE(facts.label.empty());
        if (apogee::harness::is_vendor_cli(facts.type)) {
            CHECK_FALSE(facts.binary.empty());
            CHECK_FALSE(facts.version_arguments.empty());
        } else {
            // An API type is detected by its key alone: nothing to spawn.
            CHECK(facts.binary.empty());
            CHECK(facts.status_arguments.empty());
        }
    }
    CHECK(ids.size() == table.size());
    CHECK(apogee::backends::provider_for_type(BackendType::LlamaCpp) == nullptr);
    CHECK(apogee::backends::provider_for_type(BackendType::Mlx) == nullptr);
    CHECK(apogee::backends::provider_for_type(BackendType::Mock) == nullptr);
    CHECK(apogee::backends::find_provider("deepmind") == nullptr);
}

TEST_CASE("an absent binary is not found, said, and probes nothing", "[providers][probe]") {
    Scan scan;
    const ScanReport report = scan.run();
    REQUIRE(report.providers.size() == 7);
    const ProviderStatus& claude = row(report, "claude");
    CHECK_FALSE(claude.installed);
    CHECK(claude.tier() == ProviderTier::NotFound);
    CHECK(claude.installed_evidence == "'claude' is not on PATH");
    CHECK(report.version_probes == 0);
    CHECK(report.status_probes == 0);
    CHECK(scan.runner.calls.empty());
    CHECK(scan.cache.scanned_at == "2026-10-07T12:00:00Z");
}

TEST_CASE("an installed CLI: its version, and its evidence file looked for by existence",
          "[providers][probe]") {
    Scan scan;
    scan.view.install("claude");
    scan.runner.script["/bin/claude --version"] =
        ProbeRun{ProbeRun::Outcome::Exited, 0, "\n  2.1.289 (Claude Code)  \nmore\n"};

    SECTION("no evidence on disk: installed, the file named") {
        const ProviderStatus claude = row(scan.run(), "claude");
        CHECK(claude.installed);
        CHECK(claude.version == "2.1.289 (Claude Code)");
        CHECK(claude.installed_evidence == "/bin/claude, 2.1.289 (Claude Code)");
        CHECK(claude.credentials == CredentialState::NotFound);
        CHECK(claude.credential_evidence == "no ~/.claude.json");
        CHECK(claude.tier() == ProviderTier::Installed);
        CHECK(scan.runner.deadlines.front() == apogee::backends::kVersionProbeDeadline);
    }
    SECTION("the evidence file exists: credentials found -- never 'authenticated'") {
        scan.view.files.insert("/home/u/.claude.json");
        const ProviderStatus claude = row(scan.run(), "claude");
        CHECK(claude.credentials == CredentialState::Found);
        CHECK(claude.credential_evidence == "~/.claude.json exists");
        CHECK(claude.tier() == ProviderTier::CredentialsFound);
        CHECK(apogee::backends::to_string(claude.tier()) == "credentials found");
    }
    SECTION("no home directory: unknown, and why") {
        scan.view.home_dir.reset();
        const ProviderStatus claude = row(scan.run(), "claude");
        CHECK(claude.credentials == CredentialState::Unknown);
        CHECK(claude.credential_evidence == "no home directory to look in");
    }
}

TEST_CASE("a status command answers by its exit code, under its deadline", "[providers][probe]") {
    Scan scan;
    scan.view.install("codex");

    SECTION("exit 0: credentials found, the command named") {
        const ProviderStatus codex = row(scan.run(), "codex");
        CHECK(codex.credentials == CredentialState::Found);
        CHECK(codex.credential_evidence == "`codex login status` reports a login");
        CHECK(scan.runner.count("/bin/codex login status") == 1);
        CHECK(scan.runner.deadlines.back() == apogee::backends::kStatusProbeDeadline);
    }
    SECTION("a nonzero exit: looked for and not there") {
        scan.runner.script["/bin/codex login status"] =
            ProbeRun{ProbeRun::Outcome::Exited, 1, "Not logged in\n"};
        const ProviderStatus codex = row(scan.run(), "codex");
        CHECK(codex.credentials == CredentialState::NotFound);
        CHECK(codex.credential_evidence == "`codex login status` reports no login");
        CHECK(codex.tier() == ProviderTier::Installed);
    }
    SECTION("a hung status command: unknown, honestly") {
        scan.runner.script["/bin/codex login status"] =
            ProbeRun{ProbeRun::Outcome::TimedOut, -1, ""};
        const ProviderStatus codex = row(scan.run(), "codex");
        CHECK(codex.credentials == CredentialState::Unknown);
        CHECK(codex.credential_evidence == "`codex login status` no answer in 5 s");
    }
    SECTION("off: the last scan's answer stands, nothing spawned") {
        (void)scan.run();
        scan.runner.calls.clear();
        const ScanReport report = scan.run(ScanOptions{.status_commands = false});
        CHECK(report.status_probes == 0);
        CHECK(scan.runner.calls.empty());
        CHECK(row(report, "codex").credentials == CredentialState::Found);
    }
}

TEST_CASE("a provider with no offline evidence says so", "[providers][probe]") {
    Scan scan;
    scan.view.install("ollama");
    const ProviderStatus ollama = row(scan.run(), "ollama");
    CHECK(ollama.installed);
    CHECK(ollama.credentials == CredentialState::Unknown);
    CHECK(ollama.credential_evidence == "the Ollama CLI leaves no login evidence Apogee can check");
    CHECK(ollama.tier() == ProviderTier::Installed);
}

TEST_CASE("a hung version probe is killed, recorded unknown, and not re-run",
          "[providers][probe][fingerprint]") {
    Scan scan;
    scan.view.install("gemini");
    scan.runner.script["/bin/gemini --version"] = ProbeRun{ProbeRun::Outcome::TimedOut, -1, ""};

    const ProviderStatus gemini = row(scan.run(), "gemini");
    CHECK(gemini.installed);
    CHECK(gemini.version.empty());
    CHECK(gemini.version_probed);
    CHECK(gemini.installed_evidence ==
          "/bin/gemini, version unknown (`gemini --version` no answer in 10 s)");

    scan.runner.calls.clear();
    const ScanReport again = scan.run();
    CHECK(again.version_probes == 0);
    CHECK(row(again, "gemini").installed_evidence == gemini.installed_evidence);

    // A refresh asks again.
    const ScanReport refreshed = scan.run(ScanOptions{.refresh = true});
    CHECK(refreshed.version_probes == 1);
}

TEST_CASE("the fingerprint rule: only a changed binary is asked its version again",
          "[providers][probe][fingerprint]") {
    Scan scan;
    scan.view.install("claude", 100);
    scan.view.install("codex", 200);
    scan.view.install("gemini", 300);

    const ScanReport first = scan.run();
    CHECK(first.version_probes == 3);

    scan.runner.calls.clear();
    const ScanReport cached = scan.run();
    CHECK(cached.version_probes == 0);
    CHECK(scan.runner.count("/bin/claude --version") == 0);
    CHECK(row(cached, "claude").version == "1.0.0");

    // Touch one binary: exactly its probe runs.
    scan.view.identities["/bin/codex"].modified = 201;
    scan.runner.script["/bin/codex --version"] =
        ProbeRun{ProbeRun::Outcome::Exited, 0, "codex-cli 0.154.0\n"};
    scan.runner.calls.clear();
    const ScanReport touched = scan.run();
    CHECK(touched.version_probes == 1);
    CHECK(scan.runner.count("/bin/codex --version") == 1);
    CHECK(scan.runner.count("/bin/claude --version") == 0);
    CHECK(scan.runner.count("/bin/gemini --version") == 0);
    CHECK(row(touched, "codex").version == "codex-cli 0.154.0");

    // An update that moves the link to a new file is a change too.
    scan.view.identities["/bin/gemini"].path = "/opt/gemini/0.10.0/gemini";
    scan.runner.calls.clear();
    CHECK(scan.run().version_probes == 1);
    CHECK(scan.runner.count("/bin/gemini --version") == 1);

    // Removed: not found on the next scan, without an error.
    scan.view.on_path.erase("claude");
    const ScanReport removed = scan.run();
    CHECK_FALSE(row(removed, "claude").installed);
    CHECK(row(removed, "claude").tier() == ProviderTier::NotFound);
}

TEST_CASE("a cache-hit sweep over the whole table is cheap", "[providers][probe][fingerprint]") {
    Scan scan;
    for (const auto& facts : apogee::backends::provider_table()) {
        if (!facts.binary.empty()) {
            scan.view.install(std::string{facts.binary});
        }
    }
    (void)scan.run();
    const auto started = std::chrono::steady_clock::now();
    const ScanReport report = scan.run(ScanOptions{.status_commands = false});
    const auto elapsed = std::chrono::steady_clock::now() - started;
    CHECK(report.version_probes == 0);
    CHECK(report.status_probes == 0);
    CHECK(elapsed < std::chrono::milliseconds{100});
}

TEST_CASE("an API type is detected by where its key would come from, never the key",
          "[providers][probe]") {
    Scan scan;
    scan.keys = [](BackendType type) -> std::optional<std::string> {
        if (type == BackendType::Anthropic) {
            return std::string{"ANTHROPIC_API_KEY"};
        }
        if (type == BackendType::Google) {
            return std::string{"the store"};
        }
        return std::nullopt;
    };
    const ScanReport report = scan.run();
    const ProviderStatus& anthropic = row(report, "anthropic");
    CHECK(anthropic.installed);
    CHECK(anthropic.installed_evidence == "key from ANTHROPIC_API_KEY");
    CHECK(anthropic.tier() == ProviderTier::CredentialsFound);
    CHECK(row(report, "google").credential_evidence == "key from the store");
    const ProviderStatus& openai = row(report, "openai");
    CHECK_FALSE(openai.installed);
    CHECK(openai.installed_evidence == "no key resolves");
    CHECK(scan.runner.calls.empty());
}

TEST_CASE("a verified record lifts an installed provider, never an absent one",
          "[providers][probe][verified]") {
    Scan scan;
    scan.view.install("claude");
    scan.cache.verified["claude"] = {"2026-10-06", "claude"};
    const ProviderStatus claude = row(scan.run(), "claude");
    CHECK(claude.tier() == ProviderTier::Verified);
    REQUIRE(claude.verified.has_value());
    CHECK(claude.verified->date == "2026-10-06");

    scan.view.on_path.clear();
    CHECK(row(scan.run(), "claude").tier() == ProviderTier::NotFound);
}

TEST_CASE("the cache reads back what it wrote, and anything else as empty", "[providers][cache]") {
    Scan scan;
    scan.view.install("claude", 42);
    scan.view.files.insert("/home/u/.claude.json");
    scan.cache.verified["codex"] = {"2026-10-01", "work-codex"};
    (void)scan.run();

    const std::string text = apogee::backends::render_provider_cache(scan.cache);
    const ProviderCache back = apogee::backends::parse_provider_cache(text);
    CHECK(back.scanned_at == scan.cache.scanned_at);
    REQUIRE(back.providers.size() == 7);
    const ProviderStatus& claude = back.providers.at("claude");
    CHECK(claude.type == BackendType::ClaudeCli);
    CHECK(claude.installed);
    CHECK(claude.fingerprint == BinaryFingerprint{"/bin/claude", 42});
    CHECK(claude.version_probed);
    CHECK(claude.credentials == CredentialState::Found);
    CHECK(claude.credential_evidence == "~/.claude.json exists");
    CHECK(back.verified.at("codex").backend == "work-codex");
    CHECK(back.providers.at("codex").verified->date == "2026-10-01");
    CHECK(apogee::backends::render_provider_cache(back) == text);

    CHECK_FALSE(apogee::backends::parse_provider_cache("").scanned());
    CHECK_FALSE(apogee::backends::parse_provider_cache("{not json").scanned());
    CHECK_FALSE(apogee::backends::parse_provider_cache(text.substr(0, text.size() / 2)).scanned());
    CHECK_FALSE(apogee::backends::parse_provider_cache("[1,2]").scanned());
    CHECK_FALSE(
        apogee::backends::parse_provider_cache(R"({"schema":2,"scanned_at":"x"})").scanned());
    // Unknown fields from a later build are ignored, not fatal.
    const ProviderCache tolerant = apogee::backends::parse_provider_cache(
        R"({"schema":1,"scanned_at":"t","future":true,"providers":{"claude":{"installed":true,"new":1}}})");
    CHECK(tolerant.scanned());
    CHECK(tolerant.providers.at("claude").installed);
}

TEST_CASE("the verified slot: written per provider, local types ignored, never throws",
          "[providers][cache][verified]") {
    apogee::testing::TempDir dir{"provider-cache-" + std::to_string(std::random_device{}())};
    const std::filesystem::path path = dir.path() / "cache" / "providers.json";

    apogee::backends::record_verified_turn(path, BackendType::CodexCli, "codex", "2026-10-07");
    ProviderCache cache = apogee::backends::load_provider_cache(path);
    REQUIRE(cache.verified.contains("codex"));
    CHECK(cache.verified.at("codex") == apogee::backends::VerifiedRecord{"2026-10-07", "codex"});
    CHECK_FALSE(cache.scanned());  // a verified record is not a scan

    apogee::backends::record_verified_turn(path, BackendType::CodexCli, "work", "2026-10-08");
    CHECK(apogee::backends::load_provider_cache(path).verified.at("codex").backend == "work");

    apogee::backends::record_verified_turn(path, BackendType::LlamaCpp, "local", "2026-10-08");
    apogee::backends::record_verified_turn(path, BackendType::Mock, "mock", "2026-10-08");
    CHECK(apogee::backends::load_provider_cache(path).verified.size() == 1);

    // An unwritable place costs the record, never the turn: a file where the
    // cache directory would be.
    std::ofstream(dir.path() / "blocker") << "x";
    apogee::backends::record_verified_turn(dir.path() / "blocker" / "providers.json",
                                           BackendType::CodexCli, "codex", "2026-10-08");
    CHECK(std::filesystem::is_regular_file(dir.path() / "blocker"));
}

#ifndef _WIN32
TEST_CASE(
    "a sandboxed scan: fake CLIs on PATH, the cache written, removal and corruption "
    "survived",
    "[providers][probe][sandbox]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("this platform spawns no child processes");
    }
    apogee::testing::TempDir root{"provider-sandbox-" + std::to_string(std::random_device{}())};
    const std::filesystem::path bin = root.path() / "bin";
    const std::filesystem::path home = root.path() / "home";
    const std::filesystem::path apogee_home = root.path() / "apogee";
    std::filesystem::create_directories(bin);
    std::filesystem::create_directories(home);
    std::filesystem::create_directories(apogee_home);
    const auto write_tool = [&bin](const std::string& name, const std::string& body) {
        const std::filesystem::path path = bin / name;
        std::ofstream(path) << "#!/bin/sh\n" << body << "\n";
        std::filesystem::permissions(path, std::filesystem::perms::owner_all);
    };
    // Each fake logs that it ran, so the cache hit is measured, not assumed.
    const std::string log = (root.path() / "ran.log").string();
    write_tool("claude", "echo claude \"$@\" >> '" + log + "'; echo '9.9.9 (Claude Code)'");
    write_tool("codex", "echo codex \"$@\" >> '" + log +
                            "'; [ \"$1\" = login ] && exit 1; echo "
                            "'codex-cli 9.9.9'");

    const apogee::testing::EnvGuard path_guard{"PATH", bin.string()};
    const apogee::testing::EnvGuard home_guard{"HOME", home.string()};
    const apogee::testing::EnvGuard apogee_guard{"APOGEE_HOME", apogee_home.string()};
    std::ofstream(home / ".claude.json") << "{}";

    const ScanReport first = apogee::backends::scan_host_providers({}, nullptr);
    CHECK(row(first, "claude").tier() == ProviderTier::CredentialsFound);
    CHECK(row(first, "claude").version == "9.9.9 (Claude Code)");
    CHECK(row(first, "codex").tier() == ProviderTier::Installed);
    CHECK(row(first, "codex").credential_evidence == "`codex login status` reports no login");
    CHECK(row(first, "gemini").tier() == ProviderTier::NotFound);
    const std::filesystem::path cache_file = apogee::harness::provider_cache_path();
    CHECK(cache_file == apogee_home / "cache" / "providers.json");
    REQUIRE(std::filesystem::exists(cache_file));
    CHECK(apogee::backends::load_provider_cache().providers.at("claude").installed);

    // Unchanged binaries: no version asked again.
    std::filesystem::remove(log);
    const ScanReport second = apogee::backends::scan_host_providers({}, nullptr);
    CHECK(second.version_probes == 0);
    std::ifstream ran(log);
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(ran, line)) {
        lines.push_back(line);
    }
    CHECK(lines == std::vector<std::string>{"codex login status"});

    // Removed: not found, no error.
    std::filesystem::remove(bin / "claude");
    CHECK(row(apogee::backends::scan_host_providers({}, nullptr), "claude").tier() ==
          ProviderTier::NotFound);

    // Truncated, then deleted: each rebuilt silently.
    std::filesystem::resize_file(cache_file, 10);
    CHECK(row(apogee::backends::scan_host_providers({}, nullptr), "codex").installed);
    CHECK(apogee::backends::load_provider_cache().scanned());
    std::filesystem::remove(cache_file);
    CHECK(row(apogee::backends::scan_host_providers({}, nullptr), "codex").installed);
    CHECK(std::filesystem::exists(cache_file));
}
#endif

// --- 28c: one configured backend's status, spawn-free ------------------------

TEST_CASE("a configured backend's status: its own binary, the cache's version, never a spawn",
          "[providers][status]") {
    FakeView view;
    const apogee::secrets::EnvSnapshot env;
    ProviderCache cache;
    apogee::harness::BackendConfig claude;
    claude.type = BackendType::ClaudeCli;

    SECTION("no binary: not found, in the use-time error's words") {
        const auto status =
            apogee::backends::backend_provider_status(claude, view, nullptr, env, cache);
        REQUIRE(status.has_value());
        CHECK(status->tier() == ProviderTier::NotFound);
        CHECK(status->installed_evidence == "'claude' was not found on PATH");
        claude.binary = "/opt/elsewhere/claude";
        CHECK(apogee::backends::backend_provider_status(claude, view, nullptr, env, cache)
                  ->installed_evidence == "'/opt/elsewhere/claude' was not found on PATH");
    }
    SECTION("installed, never scanned: evidence looked for, the version not asked") {
        view.install("claude", 7);
        view.files.insert("/home/u/.claude.json");
        const auto status =
            apogee::backends::backend_provider_status(claude, view, nullptr, env, cache);
        CHECK(status->tier() == ProviderTier::CredentialsFound);
        CHECK(status->installed_evidence == "/bin/claude, version not asked yet");
        CHECK(apogee::backends::describe_status(*status) ==
              "credentials found (~/.claude.json exists); version not asked yet");
    }
    SECTION("the cache's version counts only for the same binary, unchanged") {
        view.install("claude", 7);
        Scan scan;
        scan.view.install("claude", 7);
        (void)scan.run();
        const auto same =
            apogee::backends::backend_provider_status(claude, view, nullptr, env, scan.cache);
        CHECK(same->version == "1.0.0");
        CHECK(apogee::backends::describe_status(*same) == "installed (no ~/.claude.json); 1.0.0");
        view.identities["/bin/claude"].modified = 8;  // updated since the scan
        const auto changed =
            apogee::backends::backend_provider_status(claude, view, nullptr, env, scan.cache);
        CHECK(changed->version.empty());
        CHECK(changed->installed_evidence == "/bin/claude, version not asked yet");
    }
    SECTION("a status-command provider: the last scan's answer, or not run yet") {
        apogee::harness::BackendConfig codex;
        codex.type = BackendType::CodexCli;
        view.install("codex");
        CHECK(apogee::backends::backend_provider_status(codex, view, nullptr, env, cache)
                  ->credential_evidence == "`codex login status` not run yet");
        Scan scan;
        scan.view.install("codex");
        (void)scan.run();
        const auto known =
            apogee::backends::backend_provider_status(codex, view, nullptr, env, scan.cache);
        CHECK(known->credentials == CredentialState::Found);
        CHECK(known->credential_evidence == "`codex login status` reports a login");
    }
    SECTION("verified, from the record, while installed") {
        view.install("claude");
        cache.verified["claude"] = {"2026-10-06", "claude"};
        const auto status =
            apogee::backends::backend_provider_status(claude, view, nullptr, env, cache);
        CHECK(status->tier() == ProviderTier::Verified);
        CHECK(apogee::backends::describe_status(*status).starts_with(
            "verified -- answered a turn on 2026-10-06; "));
    }
    SECTION("an API backend: its own key first, through the one chain") {
        apogee::harness::BackendConfig anthropic;
        anthropic.type = BackendType::Anthropic;
        const auto none =
            apogee::backends::backend_provider_status(anthropic, view, nullptr, env, cache);
        CHECK(none->tier() == ProviderTier::NotFound);
        CHECK(none->installed_evidence == "no key resolves");
        anthropic.api_key = "sk-SECRET";
        const auto mine =
            apogee::backends::backend_provider_status(anthropic, view, nullptr, env, cache);
        CHECK(mine->tier() == ProviderTier::CredentialsFound);
        CHECK(apogee::backends::describe_status(*mine) == "credentials found (key from config)");
        CHECK(apogee::backends::describe_status(*mine).find("SECRET") == std::string::npos);
    }
    SECTION("a local type is not a provider") {
        apogee::harness::BackendConfig local;
        local.type = BackendType::LlamaCpp;
        CHECK_FALSE(apogee::backends::backend_provider_status(local, view, nullptr, env, cache)
                        .has_value());
    }
}

TEST_CASE("the doctor's remedy is the use-time error's, word for word", "[providers][status]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("a vendor CLI cannot be built on this platform");
    }
    for (const auto& facts : apogee::backends::provider_table()) {
        if (facts.binary.empty()) {
            CHECK(facts.remedy.empty());
            continue;
        }
        apogee::harness::BackendConfig entry;
        entry.type = facts.type;
        entry.binary = "/nonexistent/apogee-test/" + std::string{facts.binary};
        entry.model = "some-model";  // ollama refuses a missing model first
        std::string said;
        try {
            switch (facts.type) {
                case BackendType::ClaudeCli:
                    (void)apogee::backends::ClaudeCliProvider::from_config("b", entry);
                    break;
                case BackendType::CodexCli:
                    (void)apogee::backends::CodexCliProvider::from_config("b", entry);
                    break;
                case BackendType::GeminiCli:
                    (void)apogee::backends::GeminiCliProvider::from_config("b", entry);
                    break;
                case BackendType::OllamaCli:
                    (void)apogee::backends::OllamaCliProvider::from_config("b", entry);
                    break;
                default:
                    FAIL("a CLI row of an unexpected type");
            }
        } catch (const std::exception& e) {
            said = e.what();
        }
        INFO(said);
        CHECK(said.find(apogee::backends::not_on_path(entry.binary) + ". " +
                        std::string{facts.remedy}) != std::string::npos);
    }
}
