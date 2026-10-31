#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <filesystem>
#include <memory>
#include <random>
#include <string>

#include "support/env_guard.h"

/// The suite never reads or writes the developer's Apogee home.
///
/// A test that wants a home makes its own -- `CliHome`, or a `TempDir` behind
/// an `EnvGuard` -- but a read it did not think of still lands somewhere:
/// every listing and completion consults the provider and roster caches (28c,
/// M13), so a test that only built rows or asked for candidates read
/// `~/.apogee/cache/` and failed, or passed, by what the developer had last
/// fetched (found 2026-10-10, five cases, when Codex's catalog began filling
/// that cache for anyone with the CLI). So it is structural here, as stdin is
/// (`hermetic_stdin.cpp`): for the whole run, APOGEE_HOME names an empty
/// directory of the run's own, removed when the run ends. A test's own guard
/// still wins for its scope, and one that needs the variable unset says so
/// with `EnvUnsetGuard`, as the root-resolution tests do.
///
/// Ollama's store is guarded the same way (2026-10-21): registration reads
/// its models (`ollama_models_here`), so `OLLAMA_MODELS` names an empty
/// directory of the run's own -- a developer's pulled models never decide
/// what a test registers.
namespace {

class HermeticHome final : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting(const Catch::TestRunInfo& /*info*/) override {
        // Named with a random draw: ctest runs every case as its own process.
        home_ = std::make_unique<apogee::testing::TempDir>("run-home-" +
                                                           std::to_string(std::random_device{}()));
        guard_ = std::make_unique<apogee::testing::EnvGuard>("APOGEE_HOME", home_->path().string());
        ollama_ = std::make_unique<apogee::testing::EnvGuard>(
            "OLLAMA_MODELS", (home_->path() / "ollama-models").string());
    }

    void testRunEnded(const Catch::TestRunStats& /*stats*/) override {
        ollama_.reset();
        guard_.reset();
        home_.reset();
    }

private:
    std::unique_ptr<apogee::testing::TempDir> home_;
    std::unique_ptr<apogee::testing::EnvGuard> guard_;
    std::unique_ptr<apogee::testing::EnvGuard> ollama_;
};

}  // namespace

CATCH_REGISTER_LISTENER(HermeticHome)
