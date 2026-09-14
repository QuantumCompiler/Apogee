#include "tools/git.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "agent/tool.h"
#include "platform/child_process.h"
#include "support/env_guard.h"
#include "tools/process.h"

/// The git toolset against a fixture repository with a local bare "remote":
/// the ref allow-list, the log range rules, and the review diff -- resolved
/// against branches that are NOT checked out, which is the whole point.
namespace {

using apogee::agent::ToolOutcome;
using apogee::agent::ToolRegistry;
using apogee::tools::find_repo;
using apogee::tools::valid_ref;

bool have_git() {
    return apogee::platform::supports_child_processes() &&
           !apogee::platform::find_on_path("git").empty();
}

/// Runs git in `repo`; fails the test on a non-zero exit.
std::string git(const std::filesystem::path& repo, std::vector<std::string> arguments) {
    apogee::platform::ChildCommand command;
    command.program = "git";
    command.arguments = {"-C", repo.string()};
    for (std::string& argument : arguments) {
        command.arguments.push_back(std::move(argument));
    }
    // A fixture must not inherit the developer's signing or hook setup.
    command.extra_environment = {
        {"GIT_CONFIG_GLOBAL", "/dev/null"}, {"GIT_CONFIG_SYSTEM", "/dev/null"},
        {"GIT_AUTHOR_NAME", "t"},           {"GIT_AUTHOR_EMAIL", "t@example.com"},
        {"GIT_COMMITTER_NAME", "t"},        {"GIT_COMMITTER_EMAIL", "t@example.com"}};
    const apogee::tools::ProcessOutcome outcome =
        apogee::tools::run_to_completion(command, std::chrono::seconds{30});
    INFO("git " << command.arguments[2] << ": " << outcome.err);
    REQUIRE(outcome.exit_code.value_or(1) == 0);
    return outcome.out;
}

/// A repository with `main` (README) and two feature branches off it, each
/// adding one file, left checked out on `main` -- plus a bare clone acting as
/// `origin` that holds a branch the working clone has never seen.
struct Fixture {
    apogee::testing::TempDir temp{"tools-git-" + std::to_string(std::random_device{}())};
    std::filesystem::path repo = temp.path() / "repo";
    std::filesystem::path origin = temp.path() / "origin.git";
    ToolRegistry registry;

    Fixture() {
        std::filesystem::create_directories(repo);
        git(repo, {"init", "-q", "-b", "main"});
        std::ofstream{repo / "README.md"} << "hello\n";
        git(repo, {"add", "."});
        git(repo, {"commit", "-q", "-m", "init"});
        add_branch("feature-a", "a.txt", "A");
        add_branch("feature-b", "b.txt", "B");
        // The remote: everything so far, plus a branch only it has.
        git(repo, {"clone", "-q", "--bare", repo.string(), origin.string()});
        git(repo, {"remote", "add", "origin", origin.string()});
        git(repo, {"branch", "remote-only", "main"});
        std::ofstream{repo / "r.txt"} << "R\n";
        git(repo, {"checkout", "-q", "remote-only"});
        git(repo, {"add", "r.txt"});
        git(repo, {"commit", "-q", "-m", "remote only"});
        git(repo, {"push", "-q", "origin", "remote-only"});
        git(repo, {"checkout", "-q", "main"});
        git(repo, {"branch", "-D", "remote-only"});  // now it lives only on origin

        apogee::tools::GitOptions options;
        options.working_directory = repo;
        apogee::tools::register_git_tools(registry, options);
    }

    void add_branch(const std::string& name, const std::string& file, const std::string& text) {
        git(repo, {"checkout", "-q", "-b", name});
        std::ofstream{repo / file} << text << "\n";
        git(repo, {"add", file});
        git(repo, {"commit", "-q", "-m", "add " + file});
        git(repo, {"checkout", "-q", "main"});
    }

    [[nodiscard]] ToolOutcome run(std::string_view tool, const std::string& arguments) const {
        return registry.find(tool)->run(arguments);
    }
};

}  // namespace

TEST_CASE("a ref must start with a letter or digit, so nothing reaches git as an option",
          "[tools][git]") {
    CHECK(valid_ref("main"));
    CHECK(valid_ref("feature/x-1.2"));
    CHECK(valid_ref("v1.0..v2.0"));
    CHECK_FALSE(valid_ref("--all"));
    CHECK_FALSE(valid_ref("-x"));
    CHECK_FALSE(valid_ref(""));
    CHECK_FALSE(valid_ref("a b"));
    CHECK_FALSE(valid_ref("a;b"));
}

TEST_CASE("find_repo walks up to the nearest .git", "[tools][git]") {
    const apogee::testing::TempDir temp{"tools-git-find-" + std::to_string(std::random_device{}())};
    std::filesystem::create_directories(temp.path() / "repo" / ".git");
    std::filesystem::create_directories(temp.path() / "repo" / "a" / "b");
    CHECK(find_repo("", temp.path() / "repo" / "a" / "b") == temp.path() / "repo");
    CHECK(find_repo("/explicit/path", temp.path() / "repo") ==
          std::filesystem::path{"/explicit/path"});
}

TEST_CASE("status, log and show read the fixture; the range rules hold", "[tools][git]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    const Fixture f;
    const ToolOutcome status = f.run("git_status", "{}");
    REQUIRE_FALSE(status.is_error);
    CHECK(status.content.find("Branch:  main  (default: main)") != std::string::npos);

    const ToolOutcome log = f.run("git_log", R"({"branch":"feature-a"})");
    REQUIRE_FALSE(log.is_error);
    CHECK(log.content.find("add a.txt") != std::string::npos);
    CHECK(log.content.find("Branch: feature-a") != std::string::npos);

    // A bare ref means "since that ref": main..feature-b has exactly one commit.
    const ToolOutcome since = f.run("git_log", R"({"range":"main","branch":"x"})");
    CHECK(since.content.find("Range: main..HEAD") != std::string::npos);
    CHECK(f.run("git_log", R"({"range":"--all"})").is_error);
    CHECK(f.run("git_log", R"({"n":0})").is_error);
    CHECK(f.run("git_log", R"({"n":201})").is_error);
    CHECK(f.run("git_log", R"({"branch":"-x"})").is_error);

    CHECK(f.run("git_show", R"({"ref":"feature-b"})").content.find("b.txt") != std::string::npos);
    CHECK(f.run("git_show", R"({"ref":"--all"})").is_error);
    CHECK(f.run("git_show", R"({"repo":"/nonexistent/repo"})").is_error);
}

TEST_CASE("the review diff is the merge-base diff between two un-checked-out branches",
          "[tools][git][review]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    const Fixture f;
    // Explicit arguments: only what feature-b adds over feature-a.
    const ToolOutcome diff =
        f.run("git_diff", R"({"head":"feature-b","base":"feature-a","fetch":"never"})");
    REQUIRE_FALSE(diff.is_error);
    CHECK(diff.content.find("Review: feature-a...feature-b") != std::string::npos);
    CHECK(diff.content.find("b.txt") != std::string::npos);
    CHECK(diff.content.find("a.txt") == std::string::npos);

    // A head alone: the base is the default branch.
    CHECK(f.run("git_diff", R"({"head":"feature-b","fetch":"never"})")
              .content.find("Review: main...feature-b") != std::string::npos);
    // Option injection is refused before git sees it.
    CHECK(f.run("git_diff", R"({"head":"--output=/tmp/x"})").is_error);
    CHECK(f.run("git_diff", R"({"head":"feature-b","file":"--output=/tmp/x"})").is_error);
    CHECK(f.run("git_diff", R"({"head":"feature-b","fetch":"sometimes"})").is_error);
    // One file only.
    CHECK(f.run("git_diff",
                R"({"head":"feature-b","base":"main","file":"README.md","fetch":"never"})")
              .content.find("b.txt") == std::string::npos);
}

TEST_CASE("review refs resolve from the remote when fetching is allowed, and refuse when not",
          "[tools][git][review]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    const Fixture f;
    // `auto`: absent locally, so it is fetched, and resolves to origin/remote-only.
    const ToolOutcome fetched = f.run("git_diff", R"({"head":"remote-only","base":"main"})");
    REQUIRE_FALSE(fetched.is_error);
    CHECK(fetched.content.find("Review: main...origin/remote-only") != std::string::npos);
    CHECK(fetched.content.find("r.txt") != std::string::npos);
    // `never`: a ref only the remote has is a clear refusal, not a guess --
    // and this ref is one the remote has, so a mutant that quietly fetched
    // would succeed here and be caught. (The `auto` case above fetched it as
    // origin/remote-only; the bare name still resolves to nothing locally.)
    const ToolOutcome refused =
        f.run("git_diff", R"({"head":"remote-only","base":"main","fetch":"never"})");
    CHECK(refused.is_error);
    CHECK(refused.content.find("fetching is disabled") != std::string::npos);
}

TEST_CASE(
    "the review defaults set by flags apply when the model passes no refs, and lose to its own",
    "[tools][git][review]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    Fixture f;
    ToolRegistry with_defaults;
    apogee::tools::GitOptions options;
    options.working_directory = f.repo;
    options.review.head = "feature-a";
    options.review.base = "main";
    options.review.fetch = "never";
    apogee::tools::register_git_tools(with_defaults, options);
    const apogee::agent::Tool* diff = with_defaults.find("git_diff");
    REQUIRE(diff != nullptr);

    // No target from the model: the flags' review, deterministically.
    const ToolOutcome defaulted = diff->run("{}");
    REQUIRE_FALSE(defaulted.is_error);
    CHECK(defaulted.content.find("Review: main...feature-a") != std::string::npos);
    CHECK(defaulted.content.find("a.txt") != std::string::npos);
    // The model naming refs explicitly wins -- plain parameters, no side channel.
    CHECK(diff->run(R"({"head":"feature-b","base":"main"})")
              .content.find("Review: main...feature-b") != std::string::npos);
    // And with no defaults at all, no target on the default branch shows uncommitted changes.
    CHECK(f.run("git_diff", "{}").content.find("uncommitted changes") != std::string::npos);
}

TEST_CASE("git_log takes the review defaults too, and a live review re-points both tools",
          "[tools][git][review]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    Fixture f;
    ToolRegistry live;
    apogee::tools::GitOptions options;
    options.working_directory = f.repo;
    const auto review = std::make_shared<apogee::tools::ReviewDefaults>();
    review->head = "feature-a";
    review->base = "main";
    review->fetch = "never";
    options.live_review = review;
    apogee::tools::register_git_tools(live, options);
    const apogee::agent::Tool* log = live.find("git_log");
    const apogee::agent::Tool* diff = live.find("git_diff");
    REQUIRE(log != nullptr);
    REQUIRE(diff != nullptr);

    // The log of what the reviewed branch adds -- not the checked-out one's.
    const ToolOutcome logged = log->run("{}");
    REQUIRE_FALSE(logged.is_error);
    CHECK(logged.content.find("Range: main..feature-a") != std::string::npos);
    CHECK(logged.content.find("add a.txt") != std::string::npos);
    CHECK(logged.content.find("add b.txt") == std::string::npos);
    CHECK(logged.content.find("init") == std::string::npos);
    // An explicit range from the model still wins.
    CHECK(log->run(R"({"range":"main..feature-b"})").content.find("add b.txt") !=
          std::string::npos);

    // Re-pointing the shared value moves both tools without re-registering:
    // what `/branch` does mid-session.
    review->head = "feature-b";
    CHECK(diff->run("{}").content.find("Review: main...feature-b") != std::string::npos);
    CHECK(log->run("{}").content.find("add b.txt") != std::string::npos);
    CHECK(log->run("{}").content.find("add a.txt") == std::string::npos);
    // Switched off: back to the checked-out branch's own log.
    review->head.clear();
    review->base.clear();
    CHECK(log->run("{}").content.find("Branch: main") != std::string::npos);
    CHECK(log->run("{}").content.find("init") != std::string::npos);
}
