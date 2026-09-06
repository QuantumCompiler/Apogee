#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "commands/complete_protocol.h"
#include "commands/uninstall.h"
#include "harness/config.h"
#include "harness/layout.h"

/// The lifecycle surfaces: shell completion, and what uninstall plans to remove.
namespace {

using apogee::commands::CompletionRequest;

/// A config with two backends, parsed rather than hand-built so the test is
/// exercising the same shape a user's file produces.
[[nodiscard]] apogee::harness::Config two_backends() {
    return apogee::harness::parse_config(R"(
backends:
  claude:
    type: anthropic
    model: claude-sonnet-5
  local:
    type: llamacpp
    model_path: /models/x.gguf
)",
                                         "test");
}

[[nodiscard]] const std::vector<apogee::commands::CommandSpec>& commands() {
    using apogee::commands::CommandSpec;
    static const std::vector<CommandSpec> kCommands{
        // The root's own flags live under an empty name, so `apogee --<TAB>`
        // needs no special case at the call site.
        CommandSpec{"", {"--config", "--help", "--version", "-V", "-h"}},
        CommandSpec{"chat", {"--help", "--model", "--search", "--tools", "-h", "-m"}},
        CommandSpec{"check", {"--fix", "--help", "--no-color", "-h"}},
        CommandSpec{"chats", {"--help", "-h"}},
        CommandSpec{"complete", {"--help", "--image", "--model", "--tools", "-h", "-m"}},
        CommandSpec{"config", {"--help", "-h"}},
        CommandSpec{"uninstall", {"--help", "--keep-data", "--yes", "-h", "-y"}},
        CommandSpec{"version", {"--help", "-h"}},
    };
    return kCommands;
}

[[nodiscard]] bool contains(const std::vector<std::string>& haystack, std::string_view needle) {
    return std::find(haystack.begin(), haystack.end(), needle) != haystack.end();
}

/// A throwaway directory, claimed by atomic creation. See check_test.cpp for
/// why picking a name and hoping is not enough under parallel ctest.
struct TempDir {
    std::filesystem::path path;

    TempDir() {
        static int counter = 0;
        const std::filesystem::path base = std::filesystem::temp_directory_path();
        for (int attempt = 0;; ++attempt) {
            const std::filesystem::path candidate =
                base /
                ("apogee-lifecycle-" + std::to_string(++counter) + "-" + std::to_string(attempt));
            std::error_code code;
            if (std::filesystem::create_directory(candidate, code) && !code) {
                path = candidate;
                return;
            }
        }
    }

    ~TempDir() {
        std::error_code code;
        std::filesystem::remove_all(path, code);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;
};

}  // namespace

TEST_CASE("the model flag completes to live backend names", "[commands][completion]") {
    // The acceptance criterion, and the whole reason the protocol is a verb
    // rather than a generated file: these names come from the user's config,
    // so a completion baked at build time could never know them.
    CompletionRequest request;
    request.words = {"complete", "--model"};
    request.current = "";

    const std::vector<std::string> candidates =
        completion_candidates(request, two_backends(), commands());

    CHECK(contains(candidates, "claude"));
    CHECK(contains(candidates, "local"));
    CHECK(candidates.size() == 2);
}

TEST_CASE("the short model flag completes too", "[commands][completion]") {
    CompletionRequest request;
    request.words = {"complete", "-m"};
    request.current = "cl";

    const std::vector<std::string> candidates =
        completion_candidates(request, two_backends(), commands());
    REQUIRE(candidates.size() == 1);
    CHECK(candidates.front() == "claude");
}

TEST_CASE("config verbs taking a backend complete to backend names", "[commands][completion]") {
    for (const std::string_view verb :
         {"set-default", "set-default-embedding", "set-default-extraction", "delete-backend"}) {
        INFO(verb);
        CompletionRequest request;
        request.words = {"config", std::string{verb}};
        request.current = "";

        const std::vector<std::string> candidates =
            completion_candidates(request, two_backends(), commands());
        CHECK(contains(candidates, "claude"));
        CHECK(contains(candidates, "local"));
    }
}

TEST_CASE("an empty line completes to subcommands", "[commands][completion]") {
    CompletionRequest request;
    request.current = "";

    const std::vector<std::string> candidates =
        completion_candidates(request, two_backends(), commands());
    CHECK(contains(candidates, "chat"));
    CHECK(contains(candidates, "check"));
    CHECK(contains(candidates, "uninstall"));
}

TEST_CASE("a partial subcommand filters", "[commands][completion]") {
    CompletionRequest request;
    request.current = "ch";

    const std::vector<std::string> candidates =
        completion_candidates(request, two_backends(), commands());
    CHECK(contains(candidates, "chat"));
    CHECK(contains(candidates, "chats"));
    CHECK(contains(candidates, "check"));
    CHECK_FALSE(contains(candidates, "version"));
}

TEST_CASE("completion survives a config with no backends", "[commands][completion]") {
    // A broken or empty config must make completion unhelpful, never
    // disruptive: this runs while the user is mid-keystroke.
    CompletionRequest request;
    request.words = {"complete", "--model"};
    request.current = "";

    const std::vector<std::string> candidates =
        completion_candidates(request, apogee::harness::Config{}, commands());
    CHECK(candidates.empty());
}

TEST_CASE("a prefix filter is a prefix, not a substring", "[commands][completion]") {
    // `filter_prefix` matching anywhere would offer "claude" for the input
    // "aud", which is not how any shell's completion behaves.
    using apogee::commands::filter_prefix;
    const std::vector<std::string> names{"claude", "local", "cloud"};

    CHECK(filter_prefix(names, "cl").size() == 2);
    CHECK(filter_prefix(names, "aud").empty());
    CHECK(filter_prefix(names, "").size() == 3);
    CHECK(filter_prefix(names, "zzz").empty());
    // A prefix longer than a candidate must not read past its end.
    CHECK(filter_prefix(names, "claudexyz").empty());
}

TEST_CASE("uninstall names the user data it would destroy", "[commands][uninstall]") {
    // The prompt's whole job. "Remove ~/.apogee?" does not convey that fifty
    // conversations are inside it, and this is not an undoable action.
    TempDir home;
    REQUIRE(apogee::harness::seed_data_directory(home.path).ok());

    std::ofstream{home.path / "sessions" / "chat.json"} << "{}";

    const apogee::commands::UninstallPlan plan = apogee::commands::plan_uninstall(home.path, {});

    CHECK(plan.touches_user_data());
    CHECK(contains(plan.user_data, "sessions"));
    // models/ is empty, so it is not raised -- a warning about nothing trains
    // the user to click through the one that matters.
    CHECK_FALSE(contains(plan.user_data, "models"));

    const std::string described = apogee::commands::describe_plan(plan);
    CHECK(described.find("YOUR OWN DATA") != std::string::npos);
    CHECK(described.find("sessions/") != std::string::npos);
    CHECK(described.find("cannot be undone") != std::string::npos);
}

TEST_CASE("an empty install raises no user-data warning", "[commands][uninstall]") {
    TempDir home;
    REQUIRE(apogee::harness::seed_data_directory(home.path).ok());

    const apogee::commands::UninstallPlan plan = apogee::commands::plan_uninstall(home.path, {});

    CHECK_FALSE(plan.touches_user_data());
    CHECK(apogee::commands::describe_plan(plan).find("YOUR OWN DATA") == std::string::npos);
}

TEST_CASE("uninstall removes the tree and reports what it removed", "[commands][uninstall]") {
    TempDir home;
    REQUIRE(apogee::harness::seed_data_directory(home.path).ok());
    std::ofstream{home.path / "sessions" / "chat.json"} << "{}";

    apogee::commands::UninstallPlan plan = apogee::commands::plan_uninstall(home.path, {});

    std::vector<std::string> errors;
    const std::vector<std::string> removed = apogee::commands::execute_uninstall(plan, errors);

    CHECK(errors.empty());
    CHECK(contains(removed, home.path.string()));
    CHECK_FALSE(std::filesystem::exists(home.path));
}

TEST_CASE("uninstall --keep-data leaves the data directory alone", "[commands][uninstall]") {
    // The flag exists for reinstalling without losing conversations, so the
    // one thing it must never do is take them with it.
    TempDir home;
    REQUIRE(apogee::harness::seed_data_directory(home.path).ok());

    apogee::commands::UninstallPlan plan = apogee::commands::plan_uninstall(home.path, {});
    plan.data_directory.clear();  // what the --keep-data flag does
    plan.user_data.clear();

    std::vector<std::string> errors;
    (void)apogee::commands::execute_uninstall(plan, errors);

    CHECK(std::filesystem::exists(home.path));
    CHECK(std::filesystem::exists(home.path / "sessions"));
}

TEST_CASE("an already-removed install plans nothing and says so", "[commands][uninstall]") {
    const apogee::commands::UninstallPlan plan =
        apogee::commands::plan_uninstall("/nonexistent/apogee-home", {});

    CHECK(plan.data_directory.empty());
    CHECK(plan.binary.empty());
    CHECK(apogee::commands::describe_plan(plan).find("already removed") != std::string::npos);
}

TEST_CASE("every user-data directory in the contract is one uninstall warns about",
          "[commands][uninstall]") {
    // Enumerated from harness/layout.h rather than restated, so a new
    // user-owned directory is covered by the prompt automatically. A second
    // list here is the drift this whole area exists to prevent.
    TempDir home;
    REQUIRE(apogee::harness::seed_data_directory(home.path).ok());

    for (const apogee::harness::LayoutEntry& entry : apogee::harness::data_directories()) {
        if (!entry.user_data) {
            continue;
        }
        std::ofstream{home.path / entry.relative_path / "something"} << "x";
    }

    const apogee::commands::UninstallPlan plan = apogee::commands::plan_uninstall(home.path, {});

    for (const apogee::harness::LayoutEntry& entry : apogee::harness::data_directories()) {
        if (!entry.user_data) {
            continue;
        }
        INFO(entry.relative_path);
        CHECK(contains(plan.user_data, entry.relative_path));
    }
}

TEST_CASE("a dash completes the flags of the command in play", "[commands][completion][flags]") {
    // The gap a user reported: subcommands and backend names completed, but
    // typing `--` after a command offered nothing at all. Flags are the most
    // common thing anyone tab-completes.
    CompletionRequest request;
    request.words = {"complete"};
    request.current = "--";

    const std::vector<std::string> candidates =
        completion_candidates(request, two_backends(), commands());

    CHECK(contains(candidates, "--model"));
    CHECK(contains(candidates, "--image"));
    CHECK(contains(candidates, "--tools"));
    // Another command's flags must not leak in.
    CHECK_FALSE(contains(candidates, "--keep-data"));
    // Nor may a short flag answer a long-flag prefix.
    CHECK_FALSE(contains(candidates, "-m"));
}

TEST_CASE("a partial flag filters", "[commands][completion][flags]") {
    CompletionRequest request;
    request.words = {"uninstall"};
    request.current = "--k";

    const std::vector<std::string> candidates =
        completion_candidates(request, two_backends(), commands());
    REQUIRE(candidates.size() == 1);
    CHECK(candidates.front() == "--keep-data");
}

TEST_CASE("a single dash offers short flags too", "[commands][completion][flags]") {
    CompletionRequest request;
    request.words = {"complete"};
    request.current = "-";

    const std::vector<std::string> candidates =
        completion_candidates(request, two_backends(), commands());
    CHECK(contains(candidates, "-m"));
    CHECK(contains(candidates, "--model"));
}

TEST_CASE("the root's own flags complete before any subcommand", "[commands][completion][flags]") {
    CompletionRequest request;
    request.current = "--";

    const std::vector<std::string> candidates =
        completion_candidates(request, two_backends(), commands());
    CHECK(contains(candidates, "--config"));
    CHECK(contains(candidates, "--version"));
    // A subcommand's flag must not appear at the root.
    CHECK_FALSE(contains(candidates, "--keep-data"));
}

TEST_CASE("a flag expecting a value still wins over flag completion",
          "[commands][completion][flags]") {
    // `-m <TAB>` wants backend names, not more flags -- the word before the
    // cursor decides.
    CompletionRequest request;
    request.words = {"complete", "-m"};
    request.current = "";

    const std::vector<std::string> candidates =
        completion_candidates(request, two_backends(), commands());
    CHECK(contains(candidates, "claude"));
    CHECK_FALSE(contains(candidates, "--tools"));
}
