#include "tools/git.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <utility>
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
    INFO("git " << command.arguments[2] << ": " << outcome.err.text());
    REQUIRE(outcome.exit_code.value_or(1) == 0);
    return outcome.out.text();
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

// ---------------------------------------------------------------------------
// Confinement (57o): the git tools read and never prompt, so a repository the
// model names stays inside the root, and nothing a repository's config names
// runs.
// ---------------------------------------------------------------------------

namespace {

/// A one-commit repository at `path`, `file` holding `text`.
void make_repo(const std::filesystem::path& path, const std::string& file, const std::string& text,
               const std::string& message) {
    std::filesystem::create_directories(path);
    git(path, {"init", "-q", "-b", "main"});
    std::ofstream{path / file, std::ios::binary} << text;
    git(path, {"add", "."});
    git(path, {"commit", "-q", "-m", message});
}

/// An executable script at `path` that creates `marker` and then runs `rest`.
void marker_script(const std::filesystem::path& path, const std::filesystem::path& marker,
                   const std::string& rest) {
    std::ofstream{path, std::ios::binary} << "#!/bin/sh\ntouch '" << marker.generic_string()
                                          << "'\n"
                                          << rest << "\n";
    std::filesystem::permissions(path, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::add);
}

}  // namespace

TEST_CASE("a repository the model names must be inside the root, and one outside is never read",
          "[tools][git][sandbox][57o]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    // The sweep's probe: `git_show{repo:'<outside repo>'}` printed that
    // repository's commit. The root is the file tools' own.
    const apogee::testing::TempDir temp{"tools-git-root-" + std::to_string(std::random_device{}())};
    const std::filesystem::path root = temp.path() / "proj";
    const std::filesystem::path outside = temp.path() / "outside";
    make_repo(root, "p.txt", "p\n", "project commit");
    make_repo(root / "inner", "i.txt", "i\n", "inner commit");
    make_repo(outside, "s.txt", "secret\n", "outside commit");
    ToolRegistry registry;
    apogee::tools::GitOptions options;
    options.working_directory = root;
    options.fs_root = root;
    apogee::tools::register_git_tools(registry, options);

    for (const std::string& repo : {outside.generic_string(), std::string{"../outside"},
                                    (root / ".." / "outside").string()}) {
        for (const char* tool : {"git_status", "git_log", "git_diff", "git_show"}) {
            INFO(tool << " " << repo);
            const ToolOutcome outcome =
                registry.find(tool)->run(nlohmann::json{{"repo", repo}}.dump());
            CHECK(outcome.is_error);
            CHECK(outcome.content.find("outside the allowed root") != std::string::npos);
            CHECK(outcome.content.find("outside commit") == std::string::npos);
            CHECK(outcome.content.find("s.txt") == std::string::npos);
        }
    }
    // Inside the root, by any spelling, it reads.
    CHECK(registry.find("git_show")->run(R"({"repo":"inner"})").content.find("inner commit") !=
          std::string::npos);
    CHECK(registry.find("git_log")
              ->run(nlohmann::json{{"repo", root.string()}}.dump())
              .content.find("project commit") != std::string::npos);
    // With no repo, the walk from the working folder, as before.
    CHECK(registry.find("git_show")->run("{}").content.find("project commit") != std::string::npos);
}

TEST_CASE("no git tool runs a command a repository's own config names",
          "[tools][git][sandbox][57o]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    // The sweep's probe set `core.fsmonitor` to touch a marker, and
    // `git_status` ran it. Every way git status, diff, log and show reach
    // a configured command, armed at once: the fsmonitor, a clean filter on
    // a modified file, a textconv and an external diff, a post-index-change
    // hook, a signed commit with `log.showSignature` and its `gpg.program`,
    // and a submodule whose own config has a filter.
    const apogee::testing::TempDir temp{"tools-git-cfg-" + std::to_string(std::random_device{}())};
    const std::filesystem::path root = temp.path() / "proj";
    const std::filesystem::path marks = temp.path() / "marks";
    const std::filesystem::path scripts = temp.path() / "scripts";
    std::filesystem::create_directories(marks);
    std::filesystem::create_directories(scripts);
    std::filesystem::create_directories(root);
    git(root, {"init", "-q", "-b", "main"});
    std::ofstream{root / ".gitattributes", std::ios::binary} << "f.txt filter=x diff=y\n";
    std::ofstream{root / "f.txt", std::ios::binary} << "a\n";
    git(root, {"add", "."});
    git(root, {"commit", "-q", "-m", "one"});

    // A submodule, its own config carrying a filter of its own name.
    const std::filesystem::path sub_source = temp.path() / "subsrc";
    make_repo(sub_source, ".gitattributes", "s.txt filter=z\n", "sub attrs");
    std::ofstream{sub_source / "s.txt", std::ios::binary} << "s\n";
    git(sub_source, {"add", "."});
    git(sub_source, {"commit", "-q", "-m", "sub file"});
    git(root,
        {"-c", "protocol.file.allow=always", "submodule", "add", "-q", sub_source.string(), "sm"});
    git(root, {"commit", "-q", "-m", "submodule"});

    // A signed commit: any gpgsig header has `log` check it.
    const std::string tree = git(root, {"rev-parse", "HEAD^{tree}"});
    const std::string parent = git(root, {"rev-parse", "HEAD"});
    const std::filesystem::path raw = temp.path() / "signed.txt";
    std::ofstream{raw, std::ios::binary}
        << "tree " << tree.substr(0, tree.find('\n')) << "\nparent "
        << parent.substr(0, parent.find('\n'))
        << "\nauthor t <t@example.com> 1700000000 +0000\ncommitter t <t@example.com> "
           "1700000000 +0000\ngpgsig -----BEGIN PGP SIGNATURE-----\n \n abc\n -----END PGP "
           "SIGNATURE-----\n\nsigned\n";
    const std::string signed_commit =
        git(root, {"hash-object", "-t", "commit", "-w", raw.string()});
    git(root,
        {"update-ref", "refs/heads/signed", signed_commit.substr(0, signed_commit.find('\n'))});

    const auto script = [&](const std::string& name, const std::string& rest) {
        const std::filesystem::path path = scripts / name;
        marker_script(path, marks / (name + "_RAN"), rest);
        return path.generic_string();
    };
    std::filesystem::create_directories(scripts / "hooks");
    marker_script(scripts / "hooks" / "post-index-change", marks / "HOOK_RAN", "exit 0");
    git(root, {"config", "core.fsmonitor", script("FSMONITOR", "exit 1")});
    git(root, {"config", "filter.x.clean", script("FILTER", "cat")});
    git(root, {"config", "diff.y.textconv", script("TEXTCONV", "cat \"$1\"")});
    git(root, {"config", "diff.external", script("EXTDIFF", "exit 0")});
    git(root, {"config", "core.hooksPath", (scripts / "hooks").generic_string()});
    git(root, {"config", "log.showSignature", "true"});
    git(root, {"config", "gpg.program", script("GPG", "exit 1")});
    git(root / "sm", {"config", "filter.z.clean", script("SUBFILTER", "cat")});

    // Modified, same size, a new mtime: status and diff must hash each file.
    const auto touch_both = [&] {
        std::ofstream{root / "f.txt", std::ios::binary} << "b\n";
        std::ofstream{root / "sm" / "s.txt", std::ios::binary} << "t\n";
        const auto later = std::filesystem::file_time_type::clock::now() + std::chrono::seconds{5};
        std::filesystem::last_write_time(root / "f.txt", later);
        std::filesystem::last_write_time(root / "sm" / "s.txt", later);
    };
    const auto ran = [&marks] {
        std::vector<std::string> names;
        for (const auto& entry : std::filesystem::directory_iterator{marks}) {
            names.push_back(entry.path().filename().string());
        }
        std::sort(names.begin(), names.end());
        return names;
    };
    const auto clear = [&marks] {
        for (const auto& entry : std::filesystem::directory_iterator{marks}) {
            std::filesystem::remove(entry.path());
        }
    };

    // The control: plain git runs them -- so the fixture is armed, and the
    // check below cannot pass by arming nothing.
    touch_both();
    clear();
    {
        apogee::platform::ChildCommand plain;
        plain.program = "git";
        for (const std::vector<std::string>& arguments : {std::vector<std::string>{"status"},
                                                          {"diff"},
                                                          {"diff", "--no-ext-diff"},
                                                          {"log", "-1", "signed"}}) {
            plain.arguments = {"-C", root.string()};
            plain.arguments.insert(plain.arguments.end(), arguments.begin(), arguments.end());
            (void)apogee::tools::run_to_completion(plain, std::chrono::seconds{30});
        }
    }
    const std::vector<std::string> armed = ran();
    INFO("plain git ran: " << nlohmann::json(armed).dump());
    for (const char* expected : {"EXTDIFF_RAN", "FILTER_RAN", "FSMONITOR_RAN", "GPG_RAN",
                                 "HOOK_RAN", "SUBFILTER_RAN", "TEXTCONV_RAN"}) {
        CHECK(std::ranges::find(armed, expected) != armed.end());
    }

    // The tools: each runs and reads, and nothing configured runs.
    touch_both();
    clear();
    ToolRegistry registry;
    apogee::tools::GitOptions options;
    options.working_directory = root;
    options.fs_root = root;
    apogee::tools::register_git_tools(registry, options);
    for (const auto& [tool, arguments] :
         std::vector<std::pair<std::string, std::string>>{{"git_status", "{}"},
                                                          {"git_diff", "{}"},
                                                          {"git_diff", R"({"target":"1"})"},
                                                          {"git_log", R"({"branch":"signed"})"},
                                                          {"git_show", R"({"ref":"signed"})"},
                                                          {"git_show", "{}"}}) {
        INFO(tool << " " << arguments);
        const ToolOutcome outcome = registry.find(tool)->run(arguments);
        CHECK_FALSE(outcome.is_error);
        INFO(outcome.content);
        CHECK(ran().empty());
    }
    CHECK(registry.find("git_status")->run("{}").content.find("f.txt") != std::string::npos);
    CHECK(registry.find("git_diff")->run("{}").content.find("+b") != std::string::npos);
}

TEST_CASE("only the repository's own filter drivers are switched off, each by name",
          "[tools][git][57o]") {
    std::string refusal;
    const std::vector<std::string> options = apogee::tools::filter_neutralising_options(
        "global\tfilter.lfs.clean\nglobal\tfilter.lfs.smudge\nlocal\tfilter.x.clean\n"
        "local\tfilter.x.required\nlocal\tfilter.a b.clean\nworktree\tfilter.Up.Case.smudge\r\n"
        "command\tfilter.cli.clean\n",
        refusal);
    CHECK(refusal.empty());
    const auto has = [&options](const std::string& value) {
        return std::ranges::find(options, value) != options.end();
    };
    for (const std::string driver : {"x", "a b", "Up.Case"}) {
        INFO(driver);
        CHECK(has("filter." + driver + ".clean="));
        CHECK(has("filter." + driver + ".smudge="));
        CHECK(has("filter." + driver + ".process="));
        CHECK(has("filter." + driver + ".required=false"));
    }
    // The user's own -- a global git-lfs, a `-c` -- are left alone.
    CHECK_FALSE(has("filter.lfs.clean="));
    CHECK_FALSE(has("filter.cli.clean="));
    CHECK(options.size() == 3 * 8);
    CHECK(apogee::tools::filter_neutralising_options("", refusal).empty());
    // A name `-c` cannot carry refuses the call rather than leaving it armed.
    CHECK(apogee::tools::filter_neutralising_options("local\tfilter.a=b.clean\n", refusal).empty());
    CHECK(refusal.find("'a=b'") != std::string::npos);
    // Every call is hardened ahead of its command.
    const auto hardening = apogee::tools::git_hardening();
    CHECK(std::ranges::find(hardening, "core.fsmonitor=false") != hardening.end());
    CHECK(std::ranges::find(hardening, "log.showSignature=false") != hardening.end());
    CHECK(std::ranges::find(hardening, "status.submoduleSummary=false") != hardening.end());
    CHECK(std::ranges::find(hardening, "core.hooksPath=/dev/null") != hardening.end());
    CHECK(std::ranges::find(hardening, "diff.autoRefreshIndex=false") != hardening.end());
    CHECK(std::ranges::find(hardening, "--no-optional-locks") != hardening.end());
}

namespace {

/// The git tools over `root`, the working folder and the file tools' root
/// at once, as a launch there registers them.
struct Confined {
    ToolRegistry registry;

    explicit Confined(const std::filesystem::path& root) {
        apogee::tools::GitOptions options;
        options.working_directory = root;
        options.fs_root = root;
        apogee::tools::register_git_tools(registry, options);
    }

    [[nodiscard]] ToolOutcome run(std::string_view tool, const nlohmann::json& arguments) const {
        return registry.find(tool)->run(arguments.dump());
    }
};

}  // namespace

TEST_CASE(
    "what git opens must be inside the root: a .git file, link or setting that leads out "
    "is refused",
    "[tools][git][sandbox][57o]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    // The review's probe: `write_file{path:'x/.git', content:'gitdir:
    // <outrepo>/.git'}` -- one write, which accept-edits or a config `allow`
    // approves unasked -- and `git_diff{repo:'x'}` printed the outside
    // repository's file, `git_log{repo:'x'}` its `outside commit`. The folder
    // named was inside the root; the repository git opened there was not.
    const apogee::testing::TempDir temp{"tools-git-open-" + std::to_string(std::random_device{}())};
    const std::filesystem::path root = temp.path() / "proj";
    const std::filesystem::path outside = temp.path() / "outrepo";
    make_repo(outside, "s.txt", "secret\n", "outside commit");
    std::ofstream{outside / "s.txt", std::ios::binary} << "secret changed\n";
    const std::string outside_head = git(outside, {"rev-parse", "HEAD"}).substr(0, 40);
    std::filesystem::create_directories(root);
    const Confined tools{root};

    const auto refused_everywhere = [&](const nlohmann::json& arguments) {
        for (const char* tool : {"git_status", "git_log", "git_diff", "git_show"}) {
            INFO(tool << " " << arguments.dump());
            const ToolOutcome outcome = tools.run(tool, arguments);
            INFO(outcome.content);
            CHECK(outcome.is_error);
            CHECK(outcome.content.find("outside the allowed root") != std::string::npos);
            CHECK(outcome.content.find("outside commit") == std::string::npos);
            CHECK(outcome.content.find("secret") == std::string::npos);
            CHECK(outcome.content.find("s.txt") == std::string::npos);
        }
    };

    SECTION("a .git file the model writes, naming another repository") {
        std::filesystem::create_directories(root / "x");
        std::ofstream{root / "x" / ".git", std::ios::binary}
            << "gitdir: " << (outside / ".git").generic_string() << "\n";
        // The control: plain git follows it out.
        CHECK(git(root / "x", {"log", "--format=%s", "-1"}).find("outside commit") !=
              std::string::npos);
        refused_everywhere({{"repo", "x"}});
        CHECK(tools.run("git_log", {{"repo", "x"}}).content.find("by way of") != std::string::npos);
        // Named by its absolute path, or from inside it, the same.
        refused_everywhere({{"repo", (root / "x").string()}});
    }
    SECTION("a .git link to another repository") {
        std::filesystem::create_directories(root / "y");
        std::error_code code;
        std::filesystem::create_directory_symlink(outside / ".git", root / "y" / ".git", code);
        if (code) {
            SKIP("a directory link cannot be made here: " << code.message());
        }
        CHECK(git(root / "y", {"log", "--format=%s", "-1"}).find("outside commit") !=
              std::string::npos);
        refused_everywhere({{"repo", "y"}});
    }
    SECTION("with no repo named: a .git file written at the root is not followed out") {
        std::ofstream{root / ".git", std::ios::binary}
            << "gitdir: " << (outside / ".git").generic_string() << "\n";
        refused_everywhere(nlohmann::json::object());
        refused_everywhere({{"repo", "."}});
        // Said as the repository found from the working folder, not as a
        // folder outside a root that is itself.
        const std::string walked = tools.run("git_log", nlohmann::json::object()).content;
        CHECK(walked.find("the repository git finds from ") != std::string::npos);
        CHECK(walked.find("the repository at '") == std::string::npos);
        CHECK(tools.run("git_log", {{"repo", "."}}).content.find("the repository at '.'") !=
              std::string::npos);
    }
    SECTION("a repository inside whose core.worktree is outside") {
        git(root, {"init", "-q", "-b", "main", "z"});
        git(root / "z", {"config", "core.worktree", outside.generic_string()});
        CHECK(git(root / "z", {"status", "--short"}).find("s.txt") != std::string::npos);
        refused_everywhere({{"repo", "z"}});
        refused_everywhere({{"repo", "z/.git"}});  // from inside its git directory too
    }
    SECTION("a git directory inside whose commondir is outside") {
        std::filesystem::create_directories(root / "c" / ".git");
        std::ofstream{root / "c" / ".git" / "HEAD", std::ios::binary} << "ref: refs/heads/main\n";
        std::ofstream{root / "c" / ".git" / "commondir", std::ios::binary}
            << (outside / ".git").generic_string() << "\n";
        CHECK(git(root / "c", {"log", "--format=%s", "-1"}).find("outside commit") !=
              std::string::npos);
        refused_everywhere({{"repo", "c"}});
    }
    SECTION("a repository inside that borrows another's objects") {
        git(root, {"init", "-q", "-b", "main", "a"});
        std::filesystem::create_directories(root / "a" / ".git" / "objects" / "info");
        std::ofstream{root / "a" / ".git" / "objects" / "info" / "alternates", std::ios::binary}
            << (outside / ".git" / "objects").generic_string() << "\n";
        // The control: an outside commit, shown through the borrowed store.
        CHECK(git(root / "a", {"show", "-s", "--format=%s", outside_head}).find("outside commit") !=
              std::string::npos);
        refused_everywhere({{"repo", "a"}});
        const ToolOutcome show = tools.run("git_show", {{"repo", "a"}, {"ref", outside_head}});
        CHECK(show.is_error);
        CHECK(show.content.find("objects borrowed from") != std::string::npos);
        CHECK(show.content.find("outside commit") == std::string::npos);
    }
    SECTION("a repository inside whose object store is a link out") {
        git(root, {"init", "-q", "-b", "main", "l"});
        std::filesystem::remove_all(root / "l" / ".git" / "objects");
        std::error_code code;
        std::filesystem::create_directory_symlink(outside / ".git" / "objects",
                                                  root / "l" / ".git" / "objects", code);
        if (code) {
            SKIP("a directory link cannot be made here: " << code.message());
        }
        CHECK(git(root / "l", {"show", "-s", "--format=%s", outside_head}).find("outside commit") !=
              std::string::npos);
        refused_everywhere({{"repo", "l"}});
    }
    SECTION("a .git file pointed at a worktree or a submodule registered elsewhere") {
        // Real git directories that vouch for a work tree -- but not this one.
        const std::filesystem::path main = temp.path() / "main";
        make_repo(main, "m.txt", "m\n", "main commit");
        git(main, {"worktree", "add", "-q", (temp.path() / "wt").string()});
        std::filesystem::create_directories(root / "w");
        std::ofstream{root / "w" / ".git", std::ios::binary}
            << "gitdir: " << (main / ".git" / "worktrees" / "wt").generic_string() << "\n";
        const ToolOutcome worktree = tools.run("git_log", {{"repo", "w"}});
        CHECK(worktree.is_error);
        CHECK(worktree.content.find("outside the allowed root") != std::string::npos);
        CHECK(worktree.content.find("main commit") == std::string::npos);
    }
}

TEST_CASE("a launch in a subfolder, a linked worktree or a submodule still reads its repository",
          "[tools][git][sandbox][57o]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    // The repository's git directory is outside the root in each, reached
    // only through what lies outside it or through a `.git` file its own git
    // directory vouches for -- so confining what git opens refuses none.
    const apogee::testing::TempDir temp{"tools-git-launch-" +
                                        std::to_string(std::random_device{}())};
    const auto reads = [](const std::filesystem::path& root, const nlohmann::json& arguments,
                          const std::string& expected) {
        const Confined tools{root};
        for (const char* tool : {"git_status", "git_log", "git_show"}) {
            INFO(tool << " in " << root.string() << " " << arguments.dump());
            const ToolOutcome outcome = tools.run(tool, arguments);
            INFO(outcome.content);
            CHECK_FALSE(outcome.is_error);
        }
        CHECK(tools.run("git_log", arguments).content.find(expected) != std::string::npos);
    };

    SECTION("a subfolder of a checkout") {
        const std::filesystem::path launch = temp.path() / "launch";
        make_repo(launch, "l.txt", "l\n", "launch commit");
        std::filesystem::create_directories(launch / "sub");
        reads(launch / "sub", nlohmann::json::object(), "launch commit");
        reads(launch / "sub", {{"repo", "."}}, "launch commit");
    }
    SECTION("a linked worktree") {
        const std::filesystem::path main = temp.path() / "main";
        make_repo(main, "m.txt", "m\n", "main commit");
        const std::filesystem::path worktree = temp.path() / "wt";
        git(main, {"worktree", "add", "-q", worktree.string()});
        reads(worktree, nlohmann::json::object(), "main commit");
        reads(worktree, {{"repo", "."}}, "main commit");
        std::filesystem::create_directories(worktree / "deeper");
        reads(worktree / "deeper", {{"repo", "."}}, "main commit");
    }
    SECTION("a repository inside whose hooks are linked from outside") {
        // Only what a read shows is held inside: a `hooks` folder linked to
        // the person's own runs nothing these tools start.
        const std::filesystem::path root = temp.path() / "hooked";
        make_repo(root, "h.txt", "h\n", "hooked commit");
        std::filesystem::create_directories(temp.path() / "my-hooks");
        std::filesystem::remove_all(root / ".git" / "hooks");
        std::error_code code;
        std::filesystem::create_directory_symlink(temp.path() / "my-hooks", root / ".git" / "hooks",
                                                  code);
        if (code) {
            SKIP("a directory link cannot be made here: " << code.message());
        }
        reads(root, nlohmann::json::object(), "hooked commit");
    }
    SECTION("a submodule's checkout") {
        const std::filesystem::path source = temp.path() / "subsrc";
        make_repo(source, "s.txt", "s\n", "submodule commit");
        const std::filesystem::path super = temp.path() / "super";
        make_repo(super, "p.txt", "p\n", "super commit");
        git(super,
            {"-c", "protocol.file.allow=always", "submodule", "add", "-q", source.string(), "sm"});
        reads(super / "sm", {{"repo", "."}}, "submodule commit");
        // The same git directory named from any other folder is refused.
        std::filesystem::create_directories(super / "sm" / "other");
        std::ofstream{super / "sm" / "other" / ".git", std::ios::binary}
            << "gitdir: " << (super / ".git" / "modules" / "sm").generic_string() << "\n";
        const ToolOutcome elsewhere = Confined{super / "sm"}.run("git_log", {{"repo", "other"}});
        CHECK(elsewhere.is_error);
        CHECK(elsewhere.content.find("outside the allowed root") != std::string::npos);
    }
}

namespace {

/// Every file directly in `marks`, sorted: the markers the armed commands made.
std::vector<std::string> markers(const std::filesystem::path& marks) {
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator{marks}) {
        names.push_back(entry.path().filename().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

/// Plain git in `repo`, its exit status ignored: the control a check needs to
/// show its fixture is armed.
void plain_git(const std::filesystem::path& repo, const std::vector<std::string>& arguments) {
    apogee::platform::ChildCommand plain;
    plain.program = "git";
    plain.arguments = {"-C", repo.string()};
    plain.arguments.insert(plain.arguments.end(), arguments.begin(), arguments.end());
    (void)apogee::tools::run_to_completion(plain, std::chrono::seconds{30});
}

}  // namespace

TEST_CASE("git_diff never diffs inside a submodule, so the submodule's own config runs nothing",
          "[tools][git][sandbox][57o]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    // The review's second probe: a superproject's `diff.submodule = diff` has
    // git start `git diff` inside the submodule, and git hands that child the
    // `-c` options but neither `--no-ext-diff` nor `--no-textconv`, so the
    // submodule's own `diff.external`, or a textconv its `info/attributes`
    // names, ran. Writes inside the root arm it in any repository whose
    // history adds a submodule.
    const apogee::testing::TempDir temp{"tools-git-subdiff-" +
                                        std::to_string(std::random_device{}())};
    const std::filesystem::path root = temp.path() / "proj";
    const std::filesystem::path marks = temp.path() / "marks";
    const std::filesystem::path scripts = temp.path() / "scripts";
    std::filesystem::create_directories(marks);
    std::filesystem::create_directories(scripts);
    const std::filesystem::path source = temp.path() / "subsrc";
    make_repo(source, "s.txt", "one\n", "first sub commit");
    const std::string first = git(source, {"rev-parse", "HEAD"}).substr(0, 40);
    std::ofstream{source / "s.txt", std::ios::binary} << "two\n";
    git(source, {"commit", "-q", "-a", "-m", "second sub commit"});
    make_repo(root, "p.txt", "p\n", "project commit");
    git(root,
        {"-c", "protocol.file.allow=always", "submodule", "add", "-q", source.string(), "sm"});
    git(root, {"commit", "-q", "-m", "add the submodule"});  // the last commit adds it

    // The superproject asks for submodules diffed in full; the submodule's
    // own config names an external diff, and a textconv for every file.
    git(root, {"config", "diff.submodule", "diff"});
    marker_script(scripts / "extdiff", marks / "EXTDIFF_RAN", "exit 0");
    marker_script(scripts / "textconv", marks / "TEXTCONV_RAN", "cat \"$1\"");
    git(root / "sm", {"config", "diff.external", (scripts / "extdiff").generic_string()});
    git(root / "sm", {"config", "diff.y.textconv", (scripts / "textconv").generic_string()});
    const std::filesystem::path module_dir = root / ".git" / "modules" / "sm";
    std::filesystem::create_directories(module_dir / "info");
    std::ofstream{module_dir / "info" / "attributes", std::ios::binary} << "* diff=y\n";
    git(root / "sm", {"checkout", "-q", first});  // moved, so the work tree's diff has it too

    const auto clear = [&marks] {
        for (const auto& entry : std::filesystem::directory_iterator{marks}) {
            std::filesystem::remove(entry.path());
        }
    };
    const Confined tools{root};
    const auto nothing_runs = [&] {
        for (const auto& [tool, arguments] : std::vector<std::pair<std::string, nlohmann::json>>{
                 {"git_diff", nlohmann::json::object()},
                 {"git_diff", {{"target", "1"}}},
                 {"git_status", nlohmann::json::object()},
                 {"git_show", nlohmann::json::object()}}) {
            clear();
            INFO(tool << " " << arguments.dump());
            const ToolOutcome outcome = tools.run(tool, arguments);
            INFO(outcome.content);
            CHECK_FALSE(outcome.is_error);
            CHECK(markers(marks).empty());
        }
        // Read all the same, the submodule as the commits it moved between.
        const std::string worktree = tools.run("git_diff", nlohmann::json::object()).content;
        CHECK(worktree.find("Subproject commit " + first) != std::string::npos);
        const std::string added = tools.run("git_diff", {{"target", "1"}}).content;
        CHECK(added.find("+Subproject commit") != std::string::npos);
        CHECK(added.find("+two") == std::string::npos);  // not the submodule's own file
    };

    // The control: plain git, with the old hardening's two flags, runs the
    // submodule's external diff -- for the moved submodule and the added one.
    for (const std::vector<std::string>& arguments :
         {std::vector<std::string>{"diff", "--no-ext-diff", "--no-textconv", "HEAD"},
          {"diff", "--no-ext-diff", "--no-textconv", "HEAD~1..HEAD"}}) {
        clear();
        plain_git(root, arguments);
        INFO("plain git " << nlohmann::json(arguments).dump());
        CHECK(markers(marks) == std::vector<std::string>{"EXTDIFF_RAN"});
    }
    nothing_runs();

    // With no external diff, the textconv: plain git runs it, the tools never.
    git(root / "sm", {"config", "--unset", "diff.external"});
    clear();
    plain_git(root, {"diff", "--no-ext-diff", "--no-textconv", "HEAD"});
    CHECK(markers(marks) == std::vector<std::string>{"TEXTCONV_RAN"});
    nothing_runs();
}

TEST_CASE("a submodule whose .git names a repository outside shows its commit ids, never more",
          "[tools][git][sandbox][57o]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    // A submodule inside the root whose `.git` file the model rewrote to name
    // the person's own clone of it elsewhere: what git opens is judged at the
    // superproject, so git reaches that clone for the commit the submodule is
    // at. The superproject's own config would show more of it --
    // `status.submoduleSummary` and `diff.submodule = log` the clone's commit
    // subjects, `diff.submodule = diff` its files.
    const apogee::testing::TempDir temp{"tools-git-subout-" +
                                        std::to_string(std::random_device{}())};
    const std::filesystem::path root = temp.path() / "proj";
    const std::filesystem::path source = temp.path() / "subsrc";
    const std::filesystem::path clone = temp.path() / "clone";
    make_repo(source, "s.txt", "one\n", "sub commit");
    make_repo(root, "p.txt", "p\n", "project commit");
    git(root,
        {"-c", "protocol.file.allow=always", "submodule", "add", "-q", source.string(), "sm"});
    git(root, {"commit", "-q", "-m", "add the submodule"});
    git(temp.path(), {"clone", "-q", source.string(), clone.string()});
    std::ofstream{clone / "secret.txt", std::ios::binary} << "outside file text\n";
    git(clone, {"add", "secret.txt"});
    git(clone, {"commit", "-q", "-m", "outside subject"});
    const std::string outside_head = git(clone, {"rev-parse", "HEAD"}).substr(0, 40);
    std::ofstream{root / "sm" / ".git", std::ios::binary}
        << "gitdir: " << (clone / ".git").generic_string() << "\n";
    git(root, {"config", "status.submoduleSummary", "true"});

    const Confined tools{root};
    for (const std::string mode : {"log", "diff"}) {
        git(root, {"config", "diff.submodule", mode});
        // The control: plain git with the commands' flags before this
        // hardening shows the clone's subject, and its file too.
        INFO("diff.submodule = " << mode);
        CHECK(git(root, {"status", "--ignore-submodules=dirty"}).find("outside subject") !=
              std::string::npos);
        CHECK(git(root,
                  {"diff", "--no-ext-diff", "--no-textconv", "--ignore-submodules=dirty", "HEAD"})
                  .find(mode == "log" ? "outside subject" : "outside file text") !=
              std::string::npos);
        for (const char* tool : {"git_status", "git_diff"}) {
            INFO(tool);
            const ToolOutcome outcome = tools.run(tool, nlohmann::json::object());
            INFO(outcome.content);
            CHECK_FALSE(outcome.is_error);
            CHECK(outcome.content.find("outside subject") == std::string::npos);
            CHECK(outcome.content.find("outside file text") == std::string::npos);
            CHECK(outcome.content.find("secret.txt") == std::string::npos);
        }
        CHECK(tools.run("git_diff", nlohmann::json::object())
                  .content.find("+Subproject commit " + outside_head) != std::string::npos);
    }
}

TEST_CASE("no git tool writes the index or runs a hook, where plain git diff does both",
          "[tools][git][sandbox][57o]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    // The third review's probe: a checkout whose tracked file was written
    // again with the same bytes, as extracting an archive leaves one. `git
    // diff HEAD` -- `git_diff{}` on the default branch -- finds it unchanged
    // and writes the refreshed index back whatever `--no-optional-locks`
    // says, and writing the index runs `post-index-change`: from
    // `.git/hooks`, or from the folder the repository's own `core.hooksPath`
    // names.
    const apogee::testing::TempDir temp{"tools-git-hook-" + std::to_string(std::random_device{}())};
    const std::filesystem::path root = temp.path() / "proj";
    const std::filesystem::path marks = temp.path() / "marks";
    const std::filesystem::path hooks = temp.path() / "hooks";
    std::filesystem::create_directories(marks);
    std::filesystem::create_directories(hooks);
    make_repo(root, "f.txt", "same\n", "one");
    marker_script(root / ".git" / "hooks" / "post-index-change", marks / "GIT_DIR_HOOK_RAN",
                  "exit 0");
    marker_script(hooks / "post-index-change", marks / "HOOKS_PATH_RAN", "exit 0");
    marker_script(hooks / "reference-transaction", marks / "REF_HOOK_RAN", "exit 0");

    const auto clear = [&marks] {
        for (const auto& entry : std::filesystem::directory_iterator{marks}) {
            std::filesystem::remove(entry.path());
        }
    };
    const auto index = [&root] {
        std::ifstream in{root / ".git" / "index", std::ios::binary};
        return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    };
    // The same bytes, a later mtime each time: the file's stat no longer
    // matches the index, its content still does.
    int later = 0;
    const auto rewrite = [&] {
        std::ofstream{root / "f.txt", std::ios::binary} << "same\n";
        later += 5;
        std::filesystem::last_write_time(
            root / "f.txt",
            std::filesystem::file_time_type::clock::now() + std::chrono::seconds{later});
        clear();
    };
    // `git_diff{}`'s command with every flag the tools passed before this fix.
    const std::vector<std::string> before_fix{"-c",
                                              "core.fsmonitor=false",
                                              "-c",
                                              "log.showSignature=false",
                                              "-c",
                                              "status.submoduleSummary=false",
                                              "--no-optional-locks",
                                              "diff",
                                              "--no-ext-diff",
                                              "--no-textconv",
                                              "--ignore-submodules=dirty",
                                              "--submodule=short",
                                              "HEAD"};
    const Confined tools{root};
    for (const bool through_config : {false, true}) {
        if (through_config) {
            git(root, {"config", "core.hooksPath", hooks.generic_string()});
        }
        const std::string hook = through_config ? "HOOKS_PATH_RAN" : "GIT_DIR_HOOK_RAN";
        INFO(hook);
        // The control: that git shows no difference, yet writes the index
        // and runs the hook.
        rewrite();
        std::string was = index();
        CHECK(git(root, before_fix).empty());
        CHECK(markers(marks) == std::vector<std::string>{hook});
        const bool control_wrote_index = index() != was;
        CHECK(control_wrote_index);
        // The tools read the same work tree, and write and run nothing.
        for (const char* tool : {"git_diff", "git_status", "git_log", "git_show"}) {
            rewrite();
            was = index();
            INFO(tool);
            const ToolOutcome outcome = tools.run(tool, nlohmann::json::object());
            INFO(outcome.content);
            CHECK_FALSE(outcome.is_error);
            CHECK(markers(marks).empty());
            const bool index_untouched = index() == was;
            CHECK(index_untouched);
        }
        CHECK(tools.run("git_diff", nlohmann::json::object()).content.find("f.txt") ==
              std::string::npos);
    }

    // The review form's fetch writes refs by design, and runs no hook
    // either. The control: a plain fetch runs `reference-transaction`.
    const std::filesystem::path origin = temp.path() / "origin.git";
    git(temp.path(), {"clone", "-q", "--bare", root.string(), origin.string()});
    git(root, {"remote", "add", "origin", origin.string()});
    git(origin, {"branch", "one", "main"});
    git(origin, {"branch", "two", "main"});
    clear();
    git(root, {"fetch", "-q", "origin", "one"});
    CHECK(markers(marks) == std::vector<std::string>{"REF_HOOK_RAN"});
    clear();
    const ToolOutcome fetched =
        tools.run("git_diff", {{"head", "two"}, {"base", "main"}, {"fetch", "always"}});
    INFO(fetched.content);
    CHECK_FALSE(fetched.is_error);
    CHECK(fetched.content.find("...origin/two") != std::string::npos);
    CHECK(markers(marks).empty());
}
