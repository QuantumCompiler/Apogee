#include "commands/check.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "harness/config.h"
#include "harness/layout.h"
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
    const auto* repaired = row_with(run_checks(inputs), "sessions/");
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
