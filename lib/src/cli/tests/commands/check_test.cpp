#include "commands/check.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "embedstore/store.h"
#include "harness/config.h"
#include "harness/layout.h"
#include "httpserver/admin_auth.h"
#include "knowledge/record.h"
#include "knowledge/store.h"
#include "secrets/store.h"
#include "support/env_guard.h"
#include "support/gguf_builder.h"

/// The doctor, against a matrix of deliberately broken installs.
///
/// The guardrail this item names, and the shape matters: every case below is a
/// state a real user reaches, and the assertion is not merely "it complained"
/// but *which severity it used*. Severity is the whole product here. A doctor
/// that reports a keyless install as broken teaches its user to ignore it, and
/// then the one real failure scrolls past unread.
namespace {

using apogee::commands::CheckInputs;
using apogee::commands::CheckReport;
using apogee::commands::Status;

/// A throwaway install root, removed on scope exit.
struct Install {
    std::filesystem::path root;

    Install() {
        // Claimed by ATOMIC creation, not by "pick a name and hope".
        //
        // ctest runs these cases as parallel PROCESSES, so a per-process
        // counter produced the same path in several of them at once and they
        // deleted each other's directories -- a failure that only appeared in
        // the full run and never in isolation. `create_directory` returning
        // true is the only way to know the name was ours.
        const std::filesystem::path base = std::filesystem::temp_directory_path();
        for (int attempt = 0;; ++attempt) {
            const std::filesystem::path candidate =
                base /
                ("apogee-check-" + std::to_string(counter()) + "-" + std::to_string(attempt));
            std::error_code code;
            if (std::filesystem::create_directory(candidate, code) && !code) {
                root = candidate;
                return;
            }
        }
    }

    ~Install() {
        std::error_code code;
        std::filesystem::remove_all(root, code);
    }

    Install(const Install&) = delete;
    Install& operator=(const Install&) = delete;
    Install(Install&&) = delete;
    Install& operator=(Install&&) = delete;

    /// Creates every directory the contract declares -- what an installer does.
    void seed() const {
        for (const apogee::harness::LayoutEntry& entry : apogee::harness::data_directories()) {
            std::filesystem::create_directories(root / entry.relative_path);
            if (entry.private_mode) {
                std::filesystem::permissions(root / entry.relative_path,
                                             std::filesystem::perms::owner_all,
                                             std::filesystem::perm_options::replace);
            }
        }
    }

    void write(const std::filesystem::path& relative, std::string_view content) const {
        const std::filesystem::path path = root / relative;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        out << content;
    }

    static int counter() {
        static int next = 0;
        return ++next;
    }
};

/// Finds the row whose name contains `needle`.
/// A pointer into a temporary report would dangle at the end of the statement
/// -- `row_with(run_checks(inputs), ...)` read freed memory once. Deleted, so
/// it cannot be written: name the report first.
[[nodiscard]] const apogee::commands::CheckRow* row_with(CheckReport&&, std::string_view) = delete;

[[nodiscard]] const apogee::commands::CheckRow* row_with(const CheckReport& report,
                                                         std::string_view needle) {
    for (const apogee::commands::CheckRow& row : report.rows) {
        if (row.name.find(needle) != std::string::npos) {
            return &row;
        }
    }
    return nullptr;
}

CheckInputs inputs_for(const Install& install) {
    CheckInputs inputs;
    inputs.home = install.root;
    inputs.config_path = install.root / "config" / "config.yaml";
    inputs.config_missing = true;
    inputs.env = [](std::string_view) { return std::string{}; };
    return inputs;
}

/// Loads a config written into the install, as the command would.
void load_into(CheckInputs& inputs) {
    inputs.config_missing = !std::filesystem::exists(inputs.config_path);
    if (inputs.config_missing) {
        return;
    }
    try {
        inputs.config = apogee::harness::load_config(inputs.config_path);
        inputs.config_error.clear();
    } catch (const apogee::harness::ConfigError& e) {
        inputs.config_error = e.what();
    }
}

}  // namespace

TEST_CASE("a fresh install with no keys and no models passes", "[commands][check]") {
    // THE acceptance criterion. Apogee runs fully local and bundles nothing, so
    // "no API key" and "no model" describe a correct installation. If this ever
    // reports a failure, `apogee check` has started lying about the product's
    // own default state.
    Install install;
    install.seed();

    CheckInputs inputs = inputs_for(install);
    install.write("config/config.yaml", "backends:\n  local:\n    type: mock\n");
    load_into(inputs);

    const CheckReport report = run_checks(inputs);

    INFO(render_report(report, false));
    CHECK(report.passed());
    CHECK(report.count(Status::Fail) == 0);
}

TEST_CASE("a missing config is a warning, not a failure", "[commands][check]") {
    // Before `apogee config init` has ever run. There is an obvious next step,
    // and the row prints it.
    Install install;
    install.seed();

    const CheckReport report = run_checks(inputs_for(install));

    const auto* row = row_with(report, "config.yaml");
    REQUIRE(row != nullptr);
    CHECK(row->status == Status::Warn);
    CHECK(row->remedy == "apogee config init");
    CHECK(report.passed());
}

TEST_CASE("an unparseable config is a failure", "[commands][check]") {
    Install install;
    install.seed();

    CheckInputs inputs = inputs_for(install);
    install.write("config/config.yaml", "backends:\n  broken: [this is not a mapping\n");
    load_into(inputs);

    const CheckReport report = run_checks(inputs);
    const auto* row = row_with(report, "config.yaml");
    REQUIRE(row != nullptr);
    CHECK(row->status == Status::Fail);
    CHECK_FALSE(report.passed());
}

TEST_CASE("a dangling model_path fails and names the command to fix it", "[commands][check]") {
    // The acceptance criterion's first broken-install case, and the one that
    // pins the never-edits-config rule: check reports, and hands back a
    // command. Editing the entry out would be guessing what it meant.
    Install install;
    install.seed();

    CheckInputs inputs = inputs_for(install);
    install.write("config/config.yaml",
                  "backends:\n  local:\n    type: llamacpp\n    model_path: /nope/missing.gguf\n");
    load_into(inputs);

    const CheckReport report = run_checks(inputs);

    const auto* row = row_with(report, "backend: local");
    REQUIRE(row != nullptr);
    CHECK(row->status == Status::Fail);
    CHECK(row->detail.find("/nope/missing.gguf") != std::string::npos);
    CHECK_FALSE(row->remedy.empty());
}

TEST_CASE("a present but unloadable GGUF fails", "[commands][check]") {
    // Ommi's recorded lesson, and the reason this reads bytes rather than
    // calling exists(): a Git LFS pointer or a truncated download is PRESENT,
    // the right name, and completely unusable. Checking presence alone reports
    // healthy and the failure surfaces much later, inside llama.cpp.
    Install install;
    install.seed();
    install.write("models/pointer.gguf",
                  "version https://git-lfs.github.com/spec/v1\noid sha256:abc\n");

    CheckInputs inputs = inputs_for(install);
    install.write("config/config.yaml",
                  "backends:\n  local:\n    type: llamacpp\n    model_path: " +
                      (install.root / "models" / "pointer.gguf").string() + "\n");
    load_into(inputs);

    const CheckReport report = run_checks(inputs);

    const auto* row = row_with(report, "backend: local");
    REQUIRE(row != nullptr);
    CHECK(row->status == Status::Fail);
    CHECK(row->detail.find("GGUF magic") != std::string::npos);
    CHECK_FALSE(report.passed());
}

TEST_CASE("a truncated GGUF fails even though its magic is valid", "[commands][check]") {
    // THE case that separates a full header read from the four-byte magic check
    // this used to do. A half-finished download starts with a perfectly good
    // GGUF magic, so the old check called it "model loads" -- for precisely the
    // file that cannot be loaded.
    const std::string whole = apogee::testing::minimal_gguf("llama");

    Install install;
    install.seed();
    install.write("models/half.gguf", whole.substr(0, whole.size() / 2));

    CheckInputs inputs = inputs_for(install);
    install.write("config/config.yaml",
                  "backends:\n  local:\n    type: llamacpp\n    model_path: " +
                      (install.root / "models" / "half.gguf").string() + "\n");
    load_into(inputs);

    const CheckReport report = run_checks(inputs);

    const auto* row = row_with(report, "backend: local");
    REQUIRE(row != nullptr);
    CHECK(row->status == Status::Fail);
    CHECK(row->detail.find("unreadable GGUF") != std::string::npos);
    CHECK_FALSE(row->remedy.empty());
    CHECK_FALSE(report.passed());
}

TEST_CASE("a valid GGUF header passes", "[commands][check]") {
    Install install;
    install.seed();
    // A real minimal GGUF, not "GGUF" plus padding: the padded version parses
    // as an empty-but-valid header, so it would pass without ever exercising
    // the metadata or tensor sections.
    install.write("models/good.gguf", apogee::testing::minimal_gguf("llama"));

    CheckInputs inputs = inputs_for(install);
    install.write("config/config.yaml",
                  "backends:\n  local:\n    type: llamacpp\n    model_path: " +
                      (install.root / "models" / "good.gguf").string() + "\n");
    load_into(inputs);

    const CheckReport report = run_checks(inputs);
    const auto* row = row_with(report, "backend: local");
    REQUIRE(row != nullptr);
    CHECK(row->status == Status::Ok);
    // The architecture the header actually declares -- proof the row came from
    // a real parse rather than from a magic-bytes glance.
    CHECK(row->detail.find("llama") != std::string::npos);
    CHECK(report.passed());
}

TEST_CASE("a cloud backend with no key warns and never prints the key",
          "[commands][check][secrets]") {
    // Two claims. First, severity: a missing key is a WARNING, because a
    // keyless install is valid. Second, and non-negotiable: the key itself must
    // never reach the output, however the row is worded (SPEC: secrets are
    // never logged).
    Install install;
    install.seed();

    CheckInputs inputs = inputs_for(install);
    install.write("config/config.yaml", "backends:\n  gpt:\n    type: openai\n");
    load_into(inputs);

    const CheckReport missing = run_checks(inputs);
    const auto* row = row_with(missing, "backend: gpt");
    REQUIRE(row != nullptr);
    CHECK(row->status == Status::Warn);
    CHECK(row->remedy.find("OPENAI_API_KEY") != std::string::npos);
    CHECK(missing.passed());

    // Now with one present.
    constexpr std::string_view kKey = "sk-SUPERSECRET-value";
    const apogee::testing::EnvGuard guard{"APOGEE_TEST_KEY", std::string{kKey}};
    install.write("config/config.yaml",
                  "backends:\n  gpt:\n    type: openai\n    api_key: \"${APOGEE_TEST_KEY}\"\n");
    load_into(inputs);

    const CheckReport present = run_checks(inputs);
    const auto* ok_row = row_with(present, "backend: gpt");
    REQUIRE(ok_row != nullptr);
    CHECK(ok_row->status == Status::Ok);

    const std::string rendered = render_report(present, false);
    CHECK(rendered.find(kKey) == std::string::npos);
    CHECK(rendered.find("SUPERSECRET") == std::string::npos);
}

TEST_CASE("a role pointer naming a backend that does not exist fails", "[commands][check]") {
    // It fails at the point of use with a routing error that never mentions
    // config, which is exactly the class of problem a doctor exists to move
    // forward in time.
    Install install;
    install.seed();

    CheckInputs inputs = inputs_for(install);
    // `models.default`, not a top-level `default_backend` -- the schema the
    // config engine actually reads.
    install.write("config/config.yaml",
                  "models:\n  default: ghost\nbackends:\n  real:\n    type: mock\n");
    load_into(inputs);

    const CheckReport report = run_checks(inputs);
    const auto* row = row_with(report, "default_backend");
    REQUIRE(row != nullptr);
    CHECK(row->status == Status::Fail);
    CHECK(row->detail.find("ghost") != std::string::npos);
}

TEST_CASE("a missing layout directory fails and --fix repairs it", "[commands][check]") {
    Install install;
    install.seed();
    std::filesystem::remove_all(install.root / "sessions");

    CheckInputs inputs = inputs_for(install);
    const CheckReport before = run_checks(inputs);
    const auto* row = row_with(before, "sessions/");
    REQUIRE(row != nullptr);
    CHECK(row->status == Status::Fail);

    const std::vector<std::string> fixed = apply_fixes(inputs);
    CHECK_FALSE(fixed.empty());

    const CheckReport after = run_checks(inputs);
    const auto* repaired = row_with(after, "sessions/");
    REQUIRE(repaired != nullptr);
    CHECK(repaired->status == Status::Ok);
}

TEST_CASE("check --fix never touches config", "[commands][check]") {
    // The rule stated as a test rather than a comment. `--fix` may repair the
    // local install; it may not decide what a dangling model_path meant.
    Install install;
    install.seed();
    constexpr std::string_view kConfig =
        "backends:\n  local:\n    type: llamacpp\n    model_path: /nope/missing.gguf\n";
    install.write("config/config.yaml", kConfig);

    CheckInputs inputs = inputs_for(install);
    load_into(inputs);
    REQUIRE_FALSE(run_checks(inputs).passed());

    (void)apply_fixes(inputs);

    std::ifstream in(inputs.config_path, std::ios::binary);
    const std::string after{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    CHECK(after == kConfig);
}

TEST_CASE("a world-readable private directory fails and --fix tightens it",
          "[commands][check][modes]") {
    if (!apogee::harness::supports_private_modes()) {
        SUCCEED("no POSIX modes on this platform");
        return;
    }

    Install install;
    install.seed();
    std::filesystem::permissions(install.root / "sessions",
                                 std::filesystem::perms::owner_all |
                                     std::filesystem::perms::group_read |
                                     std::filesystem::perms::others_read,
                                 std::filesystem::perm_options::replace);

    CheckInputs inputs = inputs_for(install);
    const CheckReport before = run_checks(inputs);
    const auto* row = row_with(before, "sessions/");
    REQUIRE(row != nullptr);
    CHECK(row->status == Status::Fail);
    CHECK(row->detail.find("readable by other users") != std::string::npos);

    (void)apply_fixes(inputs);
    const CheckReport repaired_report = run_checks(inputs);
    const auto* repaired = row_with(repaired_report, "sessions/");
    REQUIRE(repaired != nullptr);
    CHECK(repaired->status == Status::Ok);
}

TEST_CASE("every layout directory is checked", "[commands][check]") {
    // The doctor enumerates the contract rather than restating it, so adding a
    // row to harness/layout.h automatically gets it validated. This pins that
    // wiring: if check ever grows its own list, one of these goes missing.
    Install install;
    install.seed();

    const CheckReport report = run_checks(inputs_for(install));
    for (const apogee::harness::LayoutEntry& entry : apogee::harness::data_directories()) {
        const std::string label = std::string{entry.relative_path} + "/";
        INFO(label);
        CHECK(row_with(report, label) != nullptr);
    }
}

TEST_CASE("warnings alone do not fail the run", "[commands][check]") {
    // The exit-code contract: a script gating on `apogee check` must not be
    // stopped by a keyless backend.
    CheckReport report;
    report.rows.push_back({Status::Warn, "Config", "backend: gpt", "no key", ""});
    report.rows.push_back({Status::Skipped, "Filesystem", "sessions/", "not applicable", ""});
    CHECK(report.passed());

    report.rows.push_back({Status::Fail, "Config", "default_backend", "dangling", ""});
    CHECK_FALSE(report.passed());
}

// --- collections: a typo never silently means auto ------------------------------------

namespace {

/// Writes `yaml` as the install's config and loads it into the inputs.
CheckInputs inputs_with_config(const Install& install, std::string_view yaml) {
    CheckInputs inputs = inputs_for(install);
    std::filesystem::create_directories(inputs.config_path.parent_path());
    std::ofstream out(inputs.config_path, std::ios::binary | std::ios::trunc);
    out << yaml;
    out.close();
    load_into(inputs);
    return inputs;
}

}  // namespace

TEST_CASE("check rejects a retriever typo on a collection", "[commands][check][embeddings]") {
    const Install install;
    const CheckInputs inputs = inputs_with_config(
        install,
        "backends:\n  mock:\n    type: mock\nembeddings:\n  notes:\n    retriever: hybird\n");
    const CheckReport report = run_checks(inputs);
    const apogee::commands::CheckRow* row = row_with(report, "collection: notes");
    REQUIRE(row != nullptr);
    CHECK(row->status == Status::Fail);
    CHECK(row->detail.find("hybird") != std::string::npos);
    CHECK(row->detail.find("lexical, vector, hybrid, auto") != std::string::npos);
}

TEST_CASE(
    "the doctor's Graph section: an unconfigured extractor fails, an enabled graph that is "
    "not built warns, a built one reports its shape",
    "[commands][check][graph]") {
    const Install install;
    const CheckReport bad =
        run_checks(inputs_with_config(install,
                                      "backends:\n  mock:\n    type: mock\nembeddings:\n  notes:\n "
                                      "   graph:\n      extract_backend: ghost\n"));
    REQUIRE(row_with(bad, "graph: notes") != nullptr);
    CHECK(row_with(bad, "graph: notes")->status == Status::Fail);
    CHECK(row_with(bad, "graph: notes")->detail.find("ghost") != std::string::npos);

    // Nothing said about the graph: no row at all.
    const CheckReport quiet = run_checks(inputs_with_config(
        install,
        "backends:\n  mock:\n    type: mock\nembeddings:\n  notes:\n    chunk_size: 512\n"));
    CHECK(row_with(quiet, "graph: notes") == nullptr);

    const std::string enabled =
        "backends:\n  mock:\n    type: mock\nembeddings:\n  notes:\n    graph:\n      enabled: "
        "true\n      hops: 2\n";
    const CheckReport unbuilt = run_checks(inputs_with_config(install, enabled));
    REQUIRE(row_with(unbuilt, "graph: notes") != nullptr);
    CHECK(row_with(unbuilt, "graph: notes")->status == Status::Warn);
    CHECK(row_with(unbuilt, "graph: notes")->detail.find("hops 2") != std::string::npos);
    CHECK(row_with(unbuilt, "graph: notes")->detail.find("no collection on disk yet") !=
          std::string::npos);

    // The collection on disk but no graph built yet: still a warning.
    {
        apogee::embedstore::Store store{install.root / "embeddings" / "notes.db"};
        store.replace_source("a.md", {"alpha"});
    }
    const CheckReport data_only = run_checks(inputs_with_config(install, enabled));
    REQUIRE(row_with(data_only, "graph: notes") != nullptr);
    CHECK(row_with(data_only, "graph: notes")->status == Status::Warn);
    CHECK(row_with(data_only, "graph: notes")->detail.find("not built") != std::string::npos);

    // A built graph on disk: the shape, as OK.
    {
        apogee::embedstore::Store store{install.root / "embeddings" / "notes.db"};
        const std::int64_t node = store.upsert_node("Atlas", "system", "").id;
        (void)store.add_mention(node, store.chunks_by_source("a.md").front().id);
    }
    const CheckReport built = run_checks(inputs_with_config(install, enabled));
    REQUIRE(row_with(built, "graph: notes") != nullptr);
    CHECK(row_with(built, "graph: notes")->status == Status::Ok);
    CHECK(row_with(built, "graph: notes")->detail.find("built: 1 node(s), 0 edge(s)") !=
          std::string::npos);
    CHECK(row_with(built, "graph: notes")->detail.find("1 stale source(s)") != std::string::npos);
}

TEST_CASE("check rejects a rerank or backend pin naming a backend that is not configured",
          "[commands][check][embeddings]") {
    const Install install;
    const CheckReport rerank = run_checks(inputs_with_config(
        install, "backends:\n  mock:\n    type: mock\nembeddings:\n  notes:\n    rerank: ghost\n"));
    REQUIRE(row_with(rerank, "collection: notes") != nullptr);
    CHECK(row_with(rerank, "collection: notes")->status == Status::Fail);
    CHECK(row_with(rerank, "collection: notes")->detail.find("ghost") != std::string::npos);

    const CheckReport backend = run_checks(inputs_with_config(
        install,
        "backends:\n  mock:\n    type: mock\nembeddings:\n  notes:\n    backend: ghost\n"));
    CHECK(row_with(backend, "collection: notes")->status == Status::Fail);

    // `off` is a setting, and a real backend is fine: both pass.
    const CheckReport ok = run_checks(inputs_with_config(
        install,
        "backends:\n  mock:\n    type: mock\nembeddings:\n  notes:\n    retriever: vector\n    "
        "rerank: off\n    backend: mock\n"));
    REQUIRE(row_with(ok, "collection: notes") != nullptr);
    CHECK(row_with(ok, "collection: notes")->status == Status::Ok);
    CHECK(row_with(ok, "collection: notes")->detail == "vector");
}

TEST_CASE("the doctor reports the admin token: absent is fine, present must be private",
          "[check][secrets]") {
    // A per-install secret is never seeded, so install parity never sees it;
    // but when it exists, it had better be 0600 -- and --fix makes it so.
    const apogee::testing::TempDir home{"check-secrets"};
    const std::filesystem::path config_path = home.path() / "config" / "config.yaml";
    std::filesystem::create_directories(config_path.parent_path());
    std::ofstream{config_path} << apogee::harness::config_template();

    CheckInputs inputs;
    inputs.home = home.path();
    inputs.config_path = config_path;
    inputs.config = apogee::harness::load_config(config_path);

    const auto token_row = [&]() {
        for (const apogee::commands::CheckRow& row : apogee::commands::run_checks(inputs).rows) {
            if (row.name == "Admin token") {
                return row;
            }
        }
        FAIL("no Admin token row");
        return apogee::commands::CheckRow{};
    };

    CHECK(token_row().status == apogee::commands::Status::Ok);  // not generated yet

    const std::filesystem::path token = apogee::httpserver::admin_token_path(config_path);
    std::ofstream{token} << "deadbeef\n";
    if (!apogee::harness::supports_private_modes()) {
        CHECK(token_row().status == apogee::commands::Status::Skipped);
        return;
    }
    std::filesystem::permissions(token, std::filesystem::perms::owner_all |
                                            std::filesystem::perms::group_read |
                                            std::filesystem::perms::others_read);
    CHECK(token_row().status == apogee::commands::Status::Fail);

    const std::vector<std::string> fixed = apogee::commands::apply_fixes(inputs);
    bool tightened = false;
    for (const std::string& line : fixed) {
        if (line.find("admin-token") != std::string::npos &&
            line.find("0600") != std::string::npos) {
            tightened = true;
        }
    }
    CHECK(tightened);
    CHECK(token_row().status == apogee::commands::Status::Ok);
    CHECK((std::filesystem::status(token).permissions() & std::filesystem::perms::mask) ==
          (std::filesystem::perms::owner_read | std::filesystem::perms::owner_write));
}

TEST_CASE("the doctor names where each key comes from, and never the key",
          "[commands][check][secrets]") {
    // Three rungs, three wordings -- a user reading the report learns which
    // to change. The store rung is the new one; the others are the chain
    // the factory uses, reported from the same resolver.
    Install install;
    install.seed();
    CheckInputs inputs = inputs_for(install);
    install.write("config/config.yaml",
                  "backends:\n"
                  "  cfg:\n    type: openai\n    api_key: \"${APOGEE_CHECK_KEY}\"\n"
                  "  stored:\n    type: anthropic\n"
                  "  ambient:\n    type: google\n");
    const apogee::testing::EnvGuard guard{"APOGEE_CHECK_KEY", "sk-CFGSECRET"};
    load_into(inputs);
    inputs.env = [](std::string_view name) {
        return name == "GOOGLE_API_KEY" ? std::string{"sk-ENVSECRET"} : std::string{};
    };
    apogee::secrets::CredentialStore store{apogee::secrets::credentials_path(inputs.config_path)};
    store.put("anthropic", "sk-STORESECRET");

    const CheckReport report = run_checks(inputs);
    const auto* cfg = row_with(report, "backend: cfg");
    REQUIRE(cfg != nullptr);
    CHECK(cfg->status == Status::Ok);
    CHECK(cfg->detail.find("from config") != std::string::npos);
    const auto* stored = row_with(report, "backend: stored");
    REQUIRE(stored != nullptr);
    CHECK(stored->status == Status::Ok);
    CHECK(stored->detail.find("from the credential store") != std::string::npos);
    const auto* ambient = row_with(report, "backend: ambient");
    REQUIRE(ambient != nullptr);
    CHECK(ambient->status == Status::Ok);
    CHECK(ambient->detail.find("from GOOGLE_API_KEY") != std::string::npos);

    const auto* store_row = row_with(report, "Credential store");
    REQUIRE(store_row != nullptr);
    if (apogee::harness::supports_private_modes()) {
        CHECK(store_row->status == Status::Ok);
        CHECK(store_row->detail.find("1 key") != std::string::npos);
    }

    const std::string rendered = render_report(report, false);
    CHECK(rendered.find("SECRET") == std::string::npos);

    // A missing key points at the store first.
    store.clear("anthropic");
    const CheckReport after = run_checks(inputs);
    const auto* missing = row_with(after, "backend: stored");
    REQUIRE(missing != nullptr);
    CHECK(missing->status == Status::Warn);
    CHECK(missing->remedy.find("apogee auth add anthropic") != std::string::npos);
    CHECK(missing->remedy.find("ANTHROPIC_API_KEY") != std::string::npos);
}

TEST_CASE("the doctor reports the credential store: absent is fine, present must be private",
          "[commands][check][secrets]") {
    Install install;
    install.seed();
    CheckInputs inputs = inputs_for(install);
    install.write("config/config.yaml", "backends:\n  gpt:\n    type: openai\n");
    load_into(inputs);

    const auto store_row = [&]() {
        const CheckReport report = run_checks(inputs);
        const auto* row = row_with(report, "Credential store");
        REQUIRE(row != nullptr);
        return *row;  // a copy: the report dies with this frame
    };
    CHECK(store_row().status == Status::Ok);  // nothing stored yet
    CHECK(store_row().detail.find("none") != std::string::npos);

    apogee::secrets::CredentialStore store{apogee::secrets::credentials_path(inputs.config_path)};
    store.put("openai", "sk-x");
    if (!apogee::harness::supports_private_modes()) {
        CHECK(store_row().status == Status::Skipped);
        return;
    }
    CHECK(store_row().status == Status::Ok);
    std::filesystem::permissions(store.path(), std::filesystem::perms::owner_all |
                                                   std::filesystem::perms::group_read |
                                                   std::filesystem::perms::others_read);
    CHECK(store_row().status == Status::Fail);
    CHECK(store_row().remedy.find("--fix") != std::string::npos);

    bool tightened = false;
    for (const std::string& line : apogee::commands::apply_fixes(inputs)) {
        if (line.find("credentials.json") != std::string::npos &&
            line.find("0600") != std::string::npos) {
            tightened = true;
        }
    }
    CHECK(tightened);
    CHECK(store_row().status == Status::Ok);

    // A corrupt store is a warning that names the file, not a crash.
    std::ofstream{store.path(), std::ios::trunc} << "{ not json";
    CHECK(store_row().status == Status::Warn);
    CHECK(store_row().detail.find("credentials.json") != std::string::npos);
}

TEST_CASE("the doctor's Tools section: keys, the root, the switches, and git",
          "[commands][check][tools]") {
    Install install;
    install.seed();
    CheckInputs inputs = inputs_for(install);
    install.write("config/config.yaml",
                  "permissions:\n  write_file: allow\n  wrte_file: deny\n"
                  "  mcp__srv__tool: allow\n"
                  "tools:\n  fs_root: /nonexistent/sandbox\n  disabled: [shell, teleport]\n");
    load_into(inputs);
    const CheckReport report = run_checks(inputs);

    const auto* known = row_with(report, "permissions.write_file");
    REQUIRE(known != nullptr);
    CHECK(known->status == Status::Ok);
    CHECK(known->detail == "allow");
    const auto* typo = row_with(report, "permissions.wrte_file");
    REQUIRE(typo != nullptr);
    CHECK(typo->status == Status::Warn);
    CHECK(typo->detail.find("not a native destructive tool") != std::string::npos);
    const auto* namespaced = row_with(report, "permissions.mcp__srv__tool");
    REQUIRE(namespaced != nullptr);
    CHECK(namespaced->status == Status::Ok);  // a future MCP tool's key is not a typo
    const auto* unlisted = row_with(report, "permissions.delete_file");
    REQUIRE(unlisted != nullptr);
    CHECK(unlisted->detail.find("default") != std::string::npos);

    const auto* root = row_with(report, "fs_root");
    REQUIRE(root != nullptr);
    CHECK(root->status == Status::Warn);
    CHECK(root->detail.find("/nonexistent/sandbox") != std::string::npos);

    int disabled_rows = 0;
    bool teleport_warned = false;
    for (const apogee::commands::CheckRow& row : report.rows) {
        if (row.name == "tools.disabled") {
            ++disabled_rows;
            if (row.detail.find("teleport") != std::string::npos) {
                teleport_warned = row.status == Status::Warn;
            }
        }
    }
    CHECK(disabled_rows == 2);
    CHECK(teleport_warned);
    // Warnings only: a toolless install still passes.
    CHECK(report.passed());

    // Every default is an Ok row and the shipped template has no warnings here.
    install.write("config/config.yaml", apogee::harness::config_template());
    load_into(inputs);
    for (const apogee::commands::CheckRow& row : run_checks(inputs).rows) {
        if (row.section == "Tools") {
            INFO(row.name << ": " << row.detail);
            CHECK(row.status != Status::Warn);
        }
    }
}

TEST_CASE("the doctor's MCP section: PATH lookup, a missing file, a lost execute bit fixed",
          "[commands][check][mcp]") {
    Install install;
    install.seed();
    CheckInputs inputs = inputs_for(install);
    install.write("mcp/local/server.py", "#!/bin/sh\n");
    const std::filesystem::path script = install.root / "mcp" / "local" / "server.py";
    install.write("config/config.yaml",
                  "mcp_servers:\n"
                  "  onpath:\n    command: sh\n"
                  "  nowhere:\n    command: definitely-not-a-program-apogee\n"
                  "  missing:\n    command: " +
                      (install.root / "mcp" / "gone" / "server.py").string() +
                      "\n"
                      "  local:\n    command: " +
                      script.string() +
                      "\n"
                      "  off:\n    command: /nonexistent\n    enabled: false\n"
                      "  blank:\n    enabled: true\n");
    load_into(inputs);
    const CheckReport report = run_checks(inputs);

    const auto* onpath = row_with(report, "server: onpath");
    REQUIRE(onpath != nullptr);
    CHECK(onpath->status == Status::Ok);
    const auto* nowhere = row_with(report, "server: nowhere");
    REQUIRE(nowhere != nullptr);
    CHECK(nowhere->status == Status::Fail);
    CHECK(nowhere->remedy.find("delete-mcp-server nowhere") != std::string::npos);
    const auto* missing = row_with(report, "server: missing");
    REQUIRE(missing != nullptr);
    CHECK(missing->status == Status::Fail);
    CHECK(missing->remedy.find("delete-mcp-server missing") != std::string::npos);
    const auto* off = row_with(report, "server: off");
    REQUIRE(off != nullptr);
    CHECK(off->status == Status::Ok);  // disabled: never dialled, never checked
    const auto* blank = row_with(report, "server: blank");
    REQUIRE(blank != nullptr);
    CHECK(blank->status == Status::Warn);

    if (!apogee::harness::supports_private_modes()) {
        return;
    }
    // A scaffolded server without its execute bit is a warning --fix repairs.
    std::filesystem::permissions(
        script, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace);
    const CheckReport before = run_checks(inputs);
    const auto* local = row_with(before, "server: local");
    REQUIRE(local != nullptr);
    CHECK(local->status == Status::Warn);
    CHECK(local->remedy.find("--fix") != std::string::npos);
    bool fixed = false;
    for (const std::string& line : apogee::commands::apply_fixes(inputs)) {
        fixed = fixed || line.find("made executable") != std::string::npos;
    }
    CHECK(fixed);
    const CheckReport after = run_checks(inputs);
    CHECK(row_with(after, "server: local")->status == Status::Ok);
    // --fix never touched the config.
    CHECK(apogee::harness::load_config(inputs.config_path).find_mcp_server("nowhere") != nullptr);
}

TEST_CASE("the Agents section: bundled files present or warned, and every entry checked",
          "[commands][check][agents]") {
    const Install install;
    install.seed();
    // Seeding through the one path materialises the bundled files, so they
    // report present; a fresh tree without them warns with --fix as remedy.
    (void)apogee::harness::seed_data_directory(install.root);
    CheckInputs inputs =
        inputs_with_config(install,
                           "backends:\n  mock:\n    type: mock\nmodels:\n  default: mock\n"
                           "agents:\n"
                           "  good:\n    prompts: [prompts/security-review.txt]\n"
                           "    schemas: [schemas/security-review-output.json]\n    model: mock\n"
                           "  lost:\n    prompts: [prompts/nowhere.txt]\n    model: ghost\n"
                           "    collection: missing\n    mcp: [nosuch]\n"
                           "  badschema:\n    prompts: [prompts/security-review.txt]\n"
                           "    schemas: [schemas/broken.json]\n");
    install.write("schemas/broken.json", "{\"type\": 12}");
    const CheckReport report = run_checks(inputs);

    const apogee::commands::CheckRow* bundled =
        row_with(report, "bundled: prompts/release-notes.txt");
    REQUIRE(bundled != nullptr);
    CHECK(bundled->status == Status::Ok);

    const apogee::commands::CheckRow* good = row_with(report, "agent: good");
    REQUIRE(good != nullptr);
    CHECK(good->status == Status::Ok);
    CHECK(good->detail.find("read-only") != std::string::npos);

    int lost_fail = 0;
    int lost_warn = 0;
    for (const apogee::commands::CheckRow& row : report.rows) {
        if (row.name != "agent: lost") {
            continue;
        }
        lost_fail += row.status == Status::Fail ? 1 : 0;
        lost_warn += row.status == Status::Warn ? 1 : 0;
        if (row.detail.find("file missing") != std::string::npos) {
            CHECK(row.remedy.find("apogee agents create lost --force") != std::string::npos);
        }
        if (row.detail.find("model 'ghost'") != std::string::npos) {
            CHECK(row.remedy.find("add-backend ghost") != std::string::npos);
        }
    }
    CHECK(lost_fail == 2);  // the file and the model
    CHECK(lost_warn == 2);  // the collection and the server

    const apogee::commands::CheckRow* bad = row_with(report, "agent: badschema");
    REQUIRE(bad != nullptr);
    CHECK(bad->status == Status::Fail);
    CHECK(bad->detail.find("not a valid draft-07") != std::string::npos);
    CHECK_FALSE(report.passed());

    // A fresh tree: the bundled files are a warning, never a failure, and
    // --fix is the remedy.
    const Install fresh;
    fresh.seed();
    const CheckInputs fresh_inputs =
        inputs_with_config(fresh, "backends:\n  mock:\n    type: mock\n");
    const CheckReport fresh_report = run_checks(fresh_inputs);
    const apogee::commands::CheckRow* missing =
        row_with(fresh_report, "bundled: schemas/merge-request-output.json");
    REQUIRE(missing != nullptr);
    CHECK(missing->status == Status::Warn);
    CHECK(missing->remedy == "apogee check --fix");
    CHECK(fresh_report.passed());
    (void)apply_fixes(fresh_inputs);
    CHECK(std::filesystem::exists(fresh.root / "schemas" / "merge-request-output.json"));
}

TEST_CASE(
    "the doctor's Knowledge section: no records is fine, records are counted, and the "
    "raw archive must be private",
    "[commands][check][knowledge]") {
    const Install install;
    CheckInputs inputs = inputs_with_config(install, "backends:\n  mock:\n    type: mock\n");
    const auto rows_named = [](const apogee::commands::CheckReport& report, std::string_view name) {
        std::vector<apogee::commands::CheckRow> out;
        for (const apogee::commands::CheckRow& row : report.rows) {
            if (row.section == "Knowledge" && row.name == name) {
                out.push_back(row);
            }
        }
        return out;
    };

    // A fresh install: both rows ok, and honest about being empty.
    apogee::commands::CheckReport report = apogee::commands::run_checks(inputs);
    REQUIRE(rows_named(report, "collection: knowledge").size() == 1);
    CHECK(rows_named(report, "collection: knowledge").front().status ==
          apogee::commands::Status::Ok);
    CHECK(rows_named(report, "collection: knowledge").front().detail.find("no records yet") !=
          std::string::npos);
    REQUIRE(rows_named(report, "raw archive").size() == 1);
    CHECK(rows_named(report, "raw archive").front().detail.find("none archived") !=
          std::string::npos);

    // Two records captured: counted, the index verified, the archive private.
    {
        apogee::knowledge::Store store{install.root / "embeddings" / "knowledge.db",
                                       install.root / "knowledge" / "raw"};
        for (const char* id : {"kr-20260913T120000Z-000001", "kr-20260913T120001Z-000001"}) {
            apogee::knowledge::Record record;
            record.id = id;
            record.intent = "why";
            record.status = "shipped";
            record.timestamp = "2026-09-13T12:00:00.000000Z";
            store.put(record, {}, "raw words");
        }
    }
    report = apogee::commands::run_checks(inputs);
    CHECK(rows_named(report, "collection: knowledge").front().detail.find("2 record(s)") !=
          std::string::npos);
    CHECK(rows_named(report, "collection: knowledge").front().detail.find("text index ok") !=
          std::string::npos);
    CHECK(rows_named(report, "raw archive").front().status == apogee::commands::Status::Ok);
    CHECK(rows_named(report, "raw archive").front().detail.find("2 conversation(s)") !=
          std::string::npos);

    // The collection the config names is the one inspected.
    CheckInputs renamed = inputs_with_config(
        install, "backends:\n  mock:\n    type: mock\nknowledge:\n  db: decisions\n");
    report = apogee::commands::run_checks(renamed);
    CHECK(rows_named(report, "collection: decisions").size() == 1);
    CHECK(rows_named(report, "collection: knowledge").empty());

#if !defined(_WIN32)
    // A world-readable archive fails, and --fix tightens it.
    std::error_code code;
    std::filesystem::permissions(install.root / "knowledge" / "raw",
                                 std::filesystem::perms::owner_all |
                                     std::filesystem::perms::others_read |
                                     std::filesystem::perms::others_exec,
                                 std::filesystem::perm_options::replace, code);
    report = apogee::commands::run_checks(inputs);
    CHECK(rows_named(report, "raw archive").front().status == apogee::commands::Status::Fail);
    CHECK(rows_named(report, "raw archive").front().remedy.find("chmod 700") != std::string::npos);
    const std::vector<std::string> done = apogee::commands::apply_fixes(inputs);
    bool tightened = false;
    for (const std::string& line : done) {
        tightened = tightened || line.find("knowledge/raw") != std::string::npos;
    }
    CHECK(tightened);
    report = apogee::commands::run_checks(inputs);
    CHECK(rows_named(report, "raw archive").front().status == apogee::commands::Status::Ok);
#endif
}
