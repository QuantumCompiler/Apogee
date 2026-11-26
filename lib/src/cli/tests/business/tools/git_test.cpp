#include "tools/git.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "agent/tool.h"
#include "platform/child_process.h"
#include "support/env_guard.h"
#include "support/file_link.h"
#include "support/git_http_server.h"
#include "tools/fs.h"
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

/// Runs `tool` as dispatch does: through `run_gated` with `gate` when the tool
/// takes one -- the review fetch's question (57x) -- and through `run`
/// otherwise. The default gate is nobody's: every host refused.
ToolOutcome run_tool(const apogee::agent::Tool& tool, std::string_view arguments,
                     const apogee::agent::TargetGate& gate = {}) {
    if (tool.run_gated) {
        return tool.run_gated(
            arguments, gate ? gate : [](std::string_view, std::string_view) { return false; });
    }
    return tool.run(arguments);
}

/// A repository with `main` (README) and two feature branches off it, each
/// adding one file, left checked out on `main` -- plus a bare clone acting as
/// `origin` that holds a branch the working clone has never seen. Both are
/// inside the root the tools read (57x: a remote's folder outside it is
/// refused).
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
        options.fs_root = temp.path();
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
        return run_tool(*registry.find(tool), arguments);
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
    const ToolOutcome defaulted = run_tool(*diff, "{}");
    REQUIRE_FALSE(defaulted.is_error);
    CHECK(defaulted.content.find("Review: main...feature-a") != std::string::npos);
    CHECK(defaulted.content.find("a.txt") != std::string::npos);
    // The model naming refs explicitly wins -- plain parameters, no side channel.
    CHECK(run_tool(*diff, R"({"head":"feature-b","base":"main"})")
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
    const ToolOutcome logged = run_tool(*log, "{}");
    REQUIRE_FALSE(logged.is_error);
    CHECK(logged.content.find("Range: main..feature-a") != std::string::npos);
    CHECK(logged.content.find("add a.txt") != std::string::npos);
    CHECK(logged.content.find("add b.txt") == std::string::npos);
    CHECK(logged.content.find("init") == std::string::npos);
    // An explicit range from the model still wins.
    CHECK(run_tool(*log, R"({"range":"main..feature-b"})").content.find("add b.txt") !=
          std::string::npos);

    // Re-pointing the shared value moves both tools without re-registering:
    // what `/branch` does mid-session.
    review->head = "feature-b";
    CHECK(run_tool(*diff, "{}").content.find("Review: main...feature-b") != std::string::npos);
    CHECK(run_tool(*log, "{}").content.find("add b.txt") != std::string::npos);
    CHECK(run_tool(*log, "{}").content.find("add a.txt") == std::string::npos);
    // Switched off: back to the checked-out branch's own log.
    review->head.clear();
    review->base.clear();
    CHECK(run_tool(*log, "{}").content.find("Branch: main") != std::string::npos);
    CHECK(run_tool(*log, "{}").content.find("init") != std::string::npos);
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
                run_tool(*registry.find(tool), nlohmann::json{{"repo", repo}}.dump());
            CHECK(outcome.is_error);
            CHECK(outcome.content.find("outside the allowed root") != std::string::npos);
            CHECK(outcome.content.find("outside commit") == std::string::npos);
            CHECK(outcome.content.find("s.txt") == std::string::npos);
        }
    }
    // Inside the root, by any spelling, it reads.
    CHECK(
        run_tool(*registry.find("git_show"), R"({"repo":"inner"})").content.find("inner commit") !=
        std::string::npos);
    CHECK(run_tool(*registry.find("git_log"), nlohmann::json{{"repo", root.string()}}.dump())
              .content.find("project commit") != std::string::npos);
    // With no repo, the walk from the working folder, as before.
    CHECK(run_tool(*registry.find("git_show"), "{}").content.find("project commit") !=
          std::string::npos);
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
        const ToolOutcome outcome = run_tool(*registry.find(tool), arguments);
        CHECK_FALSE(outcome.is_error);
        INFO(outcome.content);
        CHECK(ran().empty());
    }
    CHECK(run_tool(*registry.find("git_status"), "{}").content.find("f.txt") != std::string::npos);
    CHECK(run_tool(*registry.find("git_diff"), "{}").content.find("+b") != std::string::npos);
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
        return run_tool(*registry.find(tool), arguments.dump());
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

/// Plain git in `repo` under the test's own environment -- the person's
/// `GIT_CONFIG_GLOBAL` included, which `git()` sets aside for a fixture -- its
/// exit status ignored and its stdout returned: the control a check needs to
/// show its fixture is armed, or what the person's own git reads.
std::string plain_git(const std::filesystem::path& repo,
                      const std::vector<std::string>& arguments) {
    apogee::platform::ChildCommand plain;
    plain.program = "git";
    plain.arguments = {"-C", repo.string()};
    plain.arguments.insert(plain.arguments.end(), arguments.begin(), arguments.end());
    return apogee::tools::run_to_completion(plain, std::chrono::seconds{30}).out.text();
}

/// Runs git in `repo` with `input` on its stdin -- `pack-objects` reads the
/// object names to pack there (57x) -- and returns its stdout.
std::string git_with_stdin(const std::filesystem::path& repo, std::vector<std::string> arguments,
                           std::string_view input) {
    apogee::platform::ChildCommand command;
    command.program = "git";
    command.arguments = {"-C", repo.string()};
    for (std::string& argument : arguments) {
        command.arguments.push_back(std::move(argument));
    }
    command.extra_environment = {
        {"GIT_CONFIG_GLOBAL", "/dev/null"}, {"GIT_CONFIG_SYSTEM", "/dev/null"},
        {"GIT_AUTHOR_NAME", "t"},           {"GIT_AUTHOR_EMAIL", "t@example.com"},
        {"GIT_COMMITTER_NAME", "t"},        {"GIT_COMMITTER_EMAIL", "t@example.com"}};
    std::string error;
    auto child = apogee::platform::start_child(command, error);
    REQUIRE(child != nullptr);
    REQUIRE(child->write_stdin(input));
    child->close_stdin();
    std::string out;
    for (;;) {
        std::string piece;
        const apogee::platform::ReadStatus status =
            child->read_stdout(piece, std::chrono::milliseconds{200});
        if (status == apogee::platform::ReadStatus::Data) {
            out += piece;
        } else if (status != apogee::platform::ReadStatus::Timeout) {
            break;
        }
    }
    (void)child->wait_for_exit(std::chrono::seconds{30});
    return out;
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

TEST_CASE("a submodule whose .git names a repository outside is refused, its ids and log unshown",
          "[tools][git][sandbox][57o][57z]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    // A submodule inside the root whose `.git` file the model rewrote to name
    // the person's own clone of it elsewhere: what git opens is judged at the
    // superproject, so git reaches that clone for the commit the submodule is
    // at. The superproject's own config would show more of it --
    // `status.submoduleSummary` and `diff.submodule = log` the clone's commit
    // subjects, `diff.submodule = diff` its files. 57o hid those and showed
    // the commit ids; since 57z the submodule is confined as the repository
    // is, so the call is refused naming it and not even an id is shown.
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
        CHECK(git(root, {"diff", "--no-ext-diff", "--ignore-submodules=dirty", "--submodule=short",
                         "HEAD"})
                  .find("+Subproject commit " + outside_head) != std::string::npos);
        for (const char* tool : {"git_status", "git_diff"}) {
            INFO(tool);
            const ToolOutcome outcome = tools.run(tool, nlohmann::json::object());
            INFO(outcome.content);
            CHECK(outcome.is_error);
            CHECK(outcome.content.find("the submodule at 'sm' of ") != std::string::npos);
            CHECK(outcome.content.find("outside the allowed root") != std::string::npos);
            CHECK(outcome.content.find("outside subject") == std::string::npos);
            CHECK(outcome.content.find("outside file text") == std::string::npos);
            CHECK(outcome.content.find("secret.txt") == std::string::npos);
            CHECK(outcome.content.find(outside_head) == std::string::npos);
        }
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
    // either. The control: a plain fetch runs `reference-transaction`. The
    // remote is a folder inside the root, which a review fetches from with
    // no question (57x).
    const std::filesystem::path origin = root / "origin.git";
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

// ---------------------------------------------------------------------------
// The review fetch stays in bounds (57x): the probe's table. Each case arms a
// repository the way the model can -- files written into the root -- shows
// plain git running what is armed (the control, so no check passes by arming
// nothing), and then the tool reaching nothing it should not.
// ---------------------------------------------------------------------------

namespace {

/// What the review fetch's gate was asked, and its one answer.
struct ScriptedGate {
    bool allow = false;
    std::vector<std::string> hosts{};
    std::vector<std::string> details{};

    [[nodiscard]] apogee::agent::TargetGate gate() {
        return [this](std::string_view host, std::string_view detail) {
            hosts.emplace_back(host);
            details.emplace_back(detail);
            return allow;
        };
    }
};

/// A config as the probe planted one.
std::string config_with(const std::string& body) {
    return "[core]\n\trepositoryformatversion = 0\n\tbare = false\n" + body;
}

/// A repository planted as the model plants one, one `write_file` each: a
/// `HEAD` naming a branch it lacks, `config` as given, empty `objects` and
/// `refs/heads`.
void plant_repo(const std::filesystem::path& dir, const std::string& config) {
    std::filesystem::create_directories(dir / ".git" / "objects");
    std::filesystem::create_directories(dir / ".git" / "refs" / "heads");
    std::ofstream{dir / ".git" / "HEAD", std::ios::binary} << "ref: refs/heads/other\n";
    std::ofstream{dir / ".git" / "config", std::ios::binary} << config;
}

/// Whether `repo`'s object store holds `object`.
bool holds(const std::filesystem::path& repo, const std::string& object) {
    apogee::platform::ChildCommand command;
    command.program = "git";
    command.arguments = {"-C", repo.string(), "cat-file", "-e", object};
    return apogee::tools::run_to_completion(command, std::chrono::seconds{30})
               .exit_code.value_or(1) == 0;
}

std::string first_line(const std::string& text) {
    return text.substr(0, text.find('\n'));
}

/// `text` as a quoted git config value, a backslash or double quote escaped,
/// so a Windows path is read as written.
std::string config_quoted(const std::string& text) {
    std::string quoted = "\"";
    for (const char c : text) {
        if (c == '\\' || c == '"') {
            quoted += '\\';
        }
        quoted += c;
    }
    return quoted + "\"";
}

/// The probe's world: the launch repository (`proj`, the root), an outside
/// repository holding a secret, a folder for markers, and the git tools over
/// the root with a scripted gate. Git's global and system config are the
/// test's own, and it never prompts. `under`, when given, is a folder the
/// root sits in, so the root's own path holds what it names.
struct FetchWorld {
    apogee::testing::EnvGuard no_global{"GIT_CONFIG_GLOBAL", "/dev/null"};
    apogee::testing::EnvGuard no_system{"GIT_CONFIG_SYSTEM", "/dev/null"};
    apogee::testing::EnvGuard no_prompt{"GIT_TERMINAL_PROMPT", "0"};
    apogee::testing::TempDir temp{"tools-git-fetch-" + std::to_string(std::random_device{}())};
    std::filesystem::path root = temp.path() / "proj";
    std::filesystem::path outside = temp.path() / "outside";
    std::filesystem::path marks = temp.path() / "marks";
    std::string outside_head;
    ToolRegistry registry;
    ScriptedGate gate;

    explicit FetchWorld(apogee::tools::ReviewDefaults review = {}, const std::string& under = {}) {
        if (!under.empty()) {
            root = temp.path() / under / "proj";
        }
        std::filesystem::create_directories(marks);
        make_repo(root, "README.md", "launch\n", "launch");
        make_repo(outside, "secret.txt", "OUTSIDE SECRET TEXT\n", "OUTSIDE SUBJECT");
        outside_head = first_line(git(outside, {"rev-parse", "HEAD"}));
        apogee::tools::GitOptions options;
        options.working_directory = root;
        options.fs_root = root;
        options.review = std::move(review);
        apogee::tools::register_git_tools(registry, options);
    }

    [[nodiscard]] ToolOutcome run(std::string_view tool, const nlohmann::json& arguments) {
        return run_tool(*registry.find(tool), arguments.dump(), gate.gate());
    }

    [[nodiscard]] std::string mark(const std::string& name) const {
        return (marks / name).generic_string();
    }

    void clear_marks() const {
        for (const auto& entry : std::filesystem::directory_iterator{marks}) {
            std::filesystem::remove(entry.path());
        }
    }

    /// A bare repository inside the root sharing the launch repository's
    /// history, plus a `feature` branch with its own file: a remote a review
    /// may fetch from with no question.
    [[nodiscard]] std::filesystem::path inside_remote(const std::string& name = "up.git") const {
        const std::filesystem::path source = temp.path() / ("src-" + name);
        git(temp.path(), {"clone", "-q", root.string(), source.string()});
        git(source, {"checkout", "-q", "-b", "feature"});
        std::ofstream{source / "feature.txt", std::ios::binary} << "FEATURE TEXT\n";
        git(source, {"add", "."});
        git(source, {"commit", "-q", "-m", "FEATURE SUBJECT"});
        git(source, {"checkout", "-q", "main"});
        const std::filesystem::path bare = root / name;
        git(temp.path(), {"clone", "-q", "--bare", source.string(), bare.string()});
        return bare;
    }
};

}  // namespace

TEST_CASE(
    "a planted remote's own settings never run, and a folder outside the root is never "
    "fetched from",
    "[tools][git][review][sandbox][57x]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    // The probe's C1, C5, C6 and C7: a repository planted inside the root,
    // its remote `o` armed, and a review asking for `main` -- absent, so
    // `fetch: auto` fetches.
    FetchWorld world;
    const std::filesystem::path proxy = world.temp.path() / "proxy.sh";
    marker_script(proxy, world.marks / "GITPROXY_RAN", "exit 1");

    struct Case {
        std::string name;
        std::string config;
        std::string marker;
        std::string said;
    };

    const std::vector<Case> cases{
        {"C1 uploadpack",
         "[remote \"o\"]\n\turl = " + world.outside.generic_string() + "\n\tuploadpack = touch " +
             world.mark("UPLOADPACK_RAN") + "; git-upload-pack\n",
         "UPLOADPACK_RAN", "outside the allowed root"},
        {"C5 ext transport",
         "[protocol \"ext\"]\n\tallow = always\n[remote \"o\"]\n\turl = ext::sh -c touch% " +
             world.mark("EXT_RAN") + "\n",
         "EXT_RAN", "ext::"},
        {"C6 core.sshCommand",
         "[core]\n\tsshCommand = touch " + world.mark("SSH_RAN") +
             "; false\n[remote \"o\"]\n\turl = ssh://example.invalid/r.git\n",
         "SSH_RAN", "core.sshCommand"},
        {"C7 core.gitProxy",
         "[core]\n\tgitProxy = " + proxy.generic_string() +
             "\n[remote \"o\"]\n\turl = git://example.invalid/r.git\n",
         "GITPROXY_RAN", "core.gitProxy"},
    };
    int at = 0;
    for (const Case& armed : cases) {
        INFO(armed.name);
        world.clear_marks();
        const std::filesystem::path x = world.root / ("x" + std::to_string(at++));
        plant_repo(x, config_with(armed.config));
        const std::string repo = x.filename().string();
        for (const bool allow : {false, true}) {
            world.gate.allow = allow;
            const ToolOutcome outcome =
                world.run("git_diff", {{"repo", repo}, {"head", "main"}, {"remote", "o"}});
            INFO(outcome.content);
            CHECK(outcome.is_error);
            CHECK(outcome.content.find(armed.said) != std::string::npos);
            CHECK(markers(world.marks).empty());
            CHECK_FALSE(holds(x, world.outside_head));
        }
        // Nobody is asked about a fetch that may not go ahead.
        CHECK(world.gate.hosts.empty());
        // The control: plain git, as the review fetch ran it before.
        plain_git(x, {"-c", "core.hooksPath=/dev/null", "fetch", "o", "main"});
        CHECK(markers(world.marks) == std::vector<std::string>{armed.marker});
    }
}

TEST_CASE(
    "a remote whose folder is outside the root is refused in the sandbox's words, however "
    "it is spelled",
    "[tools][git][review][sandbox][57x]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    // The probe's C2: the remote's URL an outside folder, `fetch: always`,
    // then `git_show` of what it would have fetched.
    FetchWorld world;
    world.gate.allow = true;
    std::filesystem::create_directories(world.root / "a");
    std::error_code linked;
    std::filesystem::create_directory_symlink(world.outside, world.root / "link", linked);
    std::vector<std::string> spellings{
        world.outside.generic_string(), "../../outside", "file://" + world.outside.generic_string(),
        "file://" + (world.root / "a").generic_string() + "/../../outside",
        "File://" + world.outside.generic_string()};
    if (!linked) {
        spellings.push_back((world.root / "link").generic_string());
    }
    int at = 0;
    for (const std::string& url : spellings) {
        INFO(url);
        const std::filesystem::path x = world.root / ("c2-" + std::to_string(at++));
        plant_repo(x, config_with("[remote \"o\"]\n\turl = " + url + "\n"));
        const std::string repo = x.filename().string();
        const ToolOutcome diff = world.run("git_diff", {{"repo", repo},
                                                        {"head", "main"},
                                                        {"base", "main"},
                                                        {"remote", "o"},
                                                        {"fetch", "always"}});
        INFO(diff.content);
        CHECK(diff.is_error);
        CHECK(diff.content.find("outside the allowed root") != std::string::npos);
        const ToolOutcome show = world.run("git_show", {{"repo", repo}, {"ref", "o/main"}});
        CHECK(show.is_error);
        CHECK(show.content.find("OUTSIDE SUBJECT") == std::string::npos);
        CHECK_FALSE(holds(x, world.outside_head));
    }
    CHECK(world.gate.hosts.empty());
    // The control: plain git copies the outside history in.
    const std::filesystem::path control = world.root / "c2-0";
    plain_git(control, {"fetch", "o"});
    CHECK(holds(control, world.outside_head));
}

TEST_CASE(
    "remote must be a remote the repository has: a path, a URL or an unknown name is "
    "refused, with or without anyone to ask",
    "[tools][git][review][sandbox][57x]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    // The probe's C3 and C3c: no config planted at all. `remote` names the
    // outside repository by a path that walks up out of a folder in the root.
    FetchWorld world;
    world.gate.allow = true;
    std::filesystem::create_directories(world.root / "a");
    std::filesystem::create_directories(world.root / "src");
    std::ofstream{world.root / "src" / "a.txt", std::ios::binary} << "code\n";
    for (const std::string& remote :
         {std::string{"a/../../outside"}, std::string{"src/../../outside"},
          world.outside.generic_string(), std::string{"https://example.invalid/r.git"},
          std::string{"upstream"}}) {
        INFO(remote);
        const ToolOutcome outcome = world.run(
            "git_diff",
            {{"head", "main"}, {"base", "main"}, {"remote", remote}, {"fetch", "always"}});
        INFO(outcome.content);
        CHECK(outcome.is_error);
        CHECK(outcome.content.find("is not a remote this repository has") != std::string::npos);
        CHECK(outcome.content.find("OUTSIDE") == std::string::npos);
        CHECK_FALSE(holds(world.root, world.outside_head));
    }
    CHECK(world.gate.hosts.empty());

    // C3c through dispatch, with no approval and nobody to ask: no gated
    // tool runs, and the read tool is refused all the same.
    const apogee::harness::ToolCall call{"c3c", "git_diff",
                                         nlohmann::json{{"head", "main"},
                                                        {"base", "main"},
                                                        {"remote", "src/../../outside"},
                                                        {"fetch", "always"}}
                                             .dump()};
    const ToolOutcome dispatched =
        apogee::agent::dispatch(world.registry, call, apogee::agent::DispatchContext{});
    INFO(dispatched.content);
    CHECK(dispatched.is_error);
    CHECK(dispatched.content.find("is not a remote this repository has") != std::string::npos);
    const ToolOutcome after = world.run("git_diff", {{"target", world.outside_head}});
    CHECK(after.content.find("OUTSIDE SECRET TEXT") == std::string::npos);
    CHECK_FALSE(holds(world.root, world.outside_head));

    // The control: plain git fetches the outside repository by that path.
    plain_git(world.root, {"fetch", "src/../../outside", "main"});
    CHECK(holds(world.root, world.outside_head));
}

TEST_CASE(
    "the launch repository's own config is no door either, and git_log's review form "
    "fetches the same way",
    "[tools][git][review][sandbox][57x]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    // The probe's C4: the launch repository's own `.git/config` gains an
    // `uploadpack` on its `origin`.
    {
        FetchWorld world;
        git(world.root, {"remote", "add", "origin", world.outside.generic_string()});
        git(world.root, {"config", "remote.origin.uploadpack",
                         "touch " + world.mark("LAUNCH_UPLOADPACK_RAN") + "; git-upload-pack"});
        const ToolOutcome outcome =
            world.run("git_diff", {{"head", "feature"}, {"remote", "origin"}});
        INFO(outcome.content);
        CHECK(outcome.is_error);
        CHECK(outcome.content.find("outside the allowed root") != std::string::npos);
        CHECK(markers(world.marks).empty());
        CHECK_FALSE(holds(world.root, world.outside_head));
        plain_git(world.root, {"-c", "core.hooksPath=/dev/null", "fetch", "origin", "feature"});
        CHECK(markers(world.marks) == std::vector<std::string>{"LAUNCH_UPLOADPACK_RAN"});
    }
    // The probe's C9: `git_log` under the person's own `--branch feature`,
    // in a repository the model planted with an armed `origin`.
    {
        apogee::tools::ReviewDefaults review;
        review.head = "feature";
        FetchWorld world{review};
        const std::filesystem::path x = world.root / "x";
        plant_repo(x, config_with("[remote \"origin\"]\n\turl = " + world.outside.generic_string() +
                                  "\n\tuploadpack = touch " + world.mark("LOG_UPLOADPACK_RAN") +
                                  "; git-upload-pack\n"));
        const ToolOutcome outcome = world.run("git_log", {{"repo", "x"}});
        INFO(outcome.content);
        CHECK(outcome.is_error);
        CHECK(markers(world.marks).empty());
        CHECK_FALSE(holds(x, world.outside_head));
        plain_git(x, {"-c", "core.hooksPath=/dev/null", "fetch", "origin", "feature"});
        CHECK(markers(world.marks) == std::vector<std::string>{"LOG_UPLOADPACK_RAN"});
    }
}

TEST_CASE("a remote inside the root still fetches, unasked, and its own settings never run",
          "[tools][git][review][sandbox][57x]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    // A fetch that may go ahead still fetches: the remote's folder is in the
    // root, so nothing leaves the machine and nobody is asked. Its
    // `uploadpack` is a key of the remote's name, which a fetch by URL never
    // reads -- and git_diff and git_log both resolve the fetched branch.
    FetchWorld world;
    const std::filesystem::path up = world.inside_remote();
    git(world.root, {"remote", "add", "o", up.generic_string()});
    git(world.root, {"config", "remote.o.uploadpack",
                     "touch " + world.mark("INSIDE_UPLOADPACK_RAN") + "; git-upload-pack"});
    // And a submodule fetch.recurseSubmodules would fetch inside, its own
    // remote armed: never fetched into.
    const std::filesystem::path sub_source = world.temp.path() / "subsrc";
    make_repo(sub_source, "s.txt", "s\n", "sub");
    git(world.root,
        {"-c", "protocol.file.allow=always", "submodule", "add", "-q", sub_source.string(), "sm"});
    git(world.root, {"commit", "-q", "-m", "submodule"});
    git(world.root / "sm", {"config", "remote.origin.uploadpack",
                            "touch " + world.mark("SUBMODULE_FETCHED") + "; git-upload-pack"});
    git(world.root, {"config", "fetch.recurseSubmodules", "yes"});

    const ToolOutcome diff = world.run("git_diff", {{"head", "feature"}, {"remote", "o"}});
    INFO(diff.content);
    CHECK_FALSE(diff.is_error);
    CHECK(diff.content.find("Review: main...o/feature") != std::string::npos);
    CHECK(diff.content.find("FEATURE TEXT") != std::string::npos);
    CHECK(markers(world.marks).empty());
    CHECK(world.gate.hosts.empty());
    // A relative URL is read as git reads it -- against the work tree's top,
    // where its fetch runs -- though the call names a folder inside it.
    git(world.root, {"remote", "add", "rel", "up.git"});
    std::filesystem::create_directories(world.root / "deep");
    const ToolOutcome relative =
        world.run("git_diff", {{"repo", "deep"}, {"head", "feature"}, {"remote", "rel"}});
    INFO(relative.content);
    CHECK(relative.content.find("Review: main...rel/feature") != std::string::npos);

    apogee::tools::ReviewDefaults review;
    review.head = "o/feature";
    review.remote = "o";
    review.fetch = "always";
    ToolRegistry logging;
    apogee::tools::GitOptions options;
    options.working_directory = world.root;
    options.fs_root = world.root;
    options.review = review;
    apogee::tools::register_git_tools(logging, options);
    const ToolOutcome log = run_tool(*logging.find("git_log"), "{}", world.gate.gate());
    INFO(log.content);
    CHECK_FALSE(log.is_error);
    CHECK(log.content.find("FEATURE SUBJECT") != std::string::npos);
    CHECK(markers(world.marks).empty());
    CHECK(world.gate.hosts.empty());

    // The control: plain git, by the remote's name, runs both (the file
    // transport allowed, as a submodule over the network would be).
    plain_git(world.root, {"-c", "protocol.file.allow=always", "-c", "core.hooksPath=/dev/null",
                           "fetch", "o", "feature"});
    CHECK(markers(world.marks) ==
          std::vector<std::string>{"INSIDE_UPLOADPACK_RAN", "SUBMODULE_FETCHED"});
}

TEST_CASE(
    "the repository's own settings a fetch would run or follow refuse it by name; the "
    "person's own are honored",
    "[tools][git][review][sandbox][57x]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    FetchWorld world;
    world.gate.allow = true;
    const std::filesystem::path up = world.inside_remote();

    struct Case {
        std::string key;
        std::string value;
        std::string said;
    };

    const std::vector<Case> cases{
        {"credential.helper", "!touch " + world.mark("CRED_RAN") + "; false", "credential.helper"},
        {"credential.https://example.invalid.helper", "!true",
         "credential.https://example.invalid.helper"},
        {"core.askPass", world.mark("ASKPASS"), "core.askPass"},
        {"core.sshCommand", "true", "core.sshCommand"},
        {"core.gitProxy", "true", "core.gitProxy"},
        {"core.alternateRefsCommand", "touch " + world.mark("ALTREFS_RAN") + "; true",
         "core.alternateRefsCommand"},
        {"url." + world.outside.generic_string() + ".insteadOf", up.generic_string(), "insteadOf"},
        {"http.proxy", "http://proxy.invalid:1", "http.proxy"},
        {"http.https://example.invalid.proxy", "http://proxy.invalid:1",
         "http.https://example.invalid.proxy"},
        {"http.curloptResolve", "example.invalid:443:127.0.0.1", "http.curloptResolve"},
        {"http.followRedirects", "true", "http.followRedirects"},
        // A cookie file the fetch would overwrite -- any file the person can
        // write, the server's chosen bytes in it -- in the plain and the
        // per-URL form alike.
        {"http.cookieFile", world.mark("COOKIE_JAR"), "http.cookieFile"},
        {"http.saveCookies", "true", "http.saveCookies"},
        {"http.https://example.invalid.cookieFile", world.mark("COOKIE_JAR"),
         "http.https://example.invalid.cookieFile"},
        {"fetch.bundleURI", "file://" + world.outside.generic_string() + "/x.bundle",
         "fetch.bundleURI"},
    };
    int at = 0;
    for (const Case& setting : cases) {
        INFO(setting.key);
        const std::filesystem::path x = world.root / ("k" + std::to_string(at++));
        make_repo(x, "x.txt", "x\n", "x");
        git(x, {"remote", "add", "o", up.generic_string()});
        git(x, {"config", setting.key, setting.value});
        const ToolOutcome outcome = world.run(
            "git_diff", {{"repo", x.filename().string()}, {"head", "feature"}, {"remote", "o"}});
        INFO(outcome.content);
        CHECK(outcome.is_error);
        CHECK(outcome.content.find(setting.said) != std::string::npos);
        CHECK(outcome.content.find("the repository's own config sets") != std::string::npos);
        CHECK_FALSE(holds(x, first_line(git(up, {"rev-parse", "feature"}))));
    }
    CHECK(markers(world.marks).empty());

    // Pulled in from another file by `include.path` is the repository's own
    // too: git reports it in local scope.
    const std::filesystem::path included = world.root / "inc";
    make_repo(included, "x.txt", "x\n", "x");
    git(included, {"remote", "add", "o", up.generic_string()});
    std::ofstream{world.root / "extra.cfg", std::ios::binary}
        << "[credential]\n\thelper = !touch " << world.mark("INCLUDED_RAN") << "; false\n";
    git(included, {"config", "include.path", (world.root / "extra.cfg").generic_string()});
    const ToolOutcome through_include =
        world.run("git_diff", {{"repo", "inc"}, {"head", "feature"}, {"remote", "o"}});
    INFO(through_include.content);
    CHECK(through_include.is_error);
    CHECK(through_include.content.find("credential.helper") != std::string::npos);

    // The controls: a repository insteadOf sends the fetch outside; a
    // repository alternateRefsCommand runs on a fetch from inside.
    const std::filesystem::path rewritten = world.root / "k6";
    plain_git(rewritten, {"fetch", "o", "main"});
    CHECK(holds(rewritten, world.outside_head));
    const std::filesystem::path alternates = world.root / "k5";
    std::ofstream{alternates / ".git" / "objects" / "info" / "alternates", std::ios::binary}
        << (up / "objects").generic_string() << "\n";
    plain_git(alternates, {"fetch", "o", "feature"});
    CHECK(markers(world.marks) == std::vector<std::string>{"ALTREFS_RAN"});
    world.clear_marks();

    // The person's own: a global insteadOf that rewrites a URL to the folder
    // inside the root is followed, and the fetch runs unasked.
    const std::filesystem::path global = world.temp.path() / "global.cfg";
    std::ofstream{global, std::ios::binary} << "[url \"" << up.generic_string()
                                            << "\"]\n\tinsteadOf = https://example.invalid/up\n";
    const apogee::testing::EnvGuard person{"GIT_CONFIG_GLOBAL", global.string()};
    const std::filesystem::path theirs = world.root / "theirs";
    git(world.temp.path(), {"clone", "-q", world.root.string(), theirs.string()});
    git(theirs, {"remote", "add", "o", "https://example.invalid/up"});
    world.gate.hosts.clear();
    const ToolOutcome honored =
        world.run("git_diff", {{"repo", "theirs"}, {"head", "feature"}, {"remote", "o"}});
    INFO(honored.content);
    CHECK_FALSE(honored.is_error);
    CHECK(honored.content.find("FEATURE TEXT") != std::string::npos);
    CHECK(world.gate.hosts.empty());
}

TEST_CASE(
    "a folder URL the person's own rewrites would change a second time is refused, so git "
    "never fetches from outside the root",
    "[tools][git][review][sandbox][57x]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    // The round 7 review's finding. `git remote get-url` applies one of the
    // person's `url.<base>.insteadOf` rewrites, and `git fetch <url>` applies
    // another to the URL the tool hands it, so a chain of two has the tool
    // judge one address while git reaches the next. Here the first rewrite
    // names a repository inside the root and the second passes it on to one
    // outside: the C2 class again, through the person's own config. A URL git
    // would rewrite again is refused before git runs.
    FetchWorld world;
    world.gate.allow = true;
    const std::filesystem::path up = world.inside_remote();
    // The path the tool hands git for that folder: resolved inside the root,
    // as `judge_folder` resolves it.
    std::string why;
    const std::string handed =
        apogee::tools::resolve_in_root(world.root, up.string(), why).value_or("").string();
    INFO(why);
    REQUIRE_FALSE(handed.empty());
    const std::filesystem::path global = world.temp.path() / "global.cfg";
    std::ofstream{global, std::ios::binary}
        << "[url \"" << up.generic_string() << "\"]\n\tinsteadOf = https://first.invalid/up\n"
        << "[url \"" << world.outside.generic_string()
        << "\"]\n\tinsteadOf = " << config_quoted(handed) << "\n";
    const apogee::testing::EnvGuard person{"GIT_CONFIG_GLOBAL", global.string()};
    git(world.root, {"remote", "add", "o", "https://first.invalid/up"});
    // One rewrite applied, the remote is the folder inside the root.
    REQUIRE(first_line(plain_git(world.root, {"remote", "get-url", "o"})) == up.generic_string());

    const ToolOutcome diff = world.run(
        "git_diff", {{"head", "main"}, {"base", "main"}, {"remote", "o"}, {"fetch", "always"}});
    INFO(diff.content);
    // Not fetched, so the review is left to what is already here, saying why.
    CHECK(diff.content.find("Not fetched: the remote 'o' is not fetched from: git would rewrite "
                            "its URL again") != std::string::npos);
    const ToolOutcome show = world.run("git_show", {{"ref", "o/main"}});
    CHECK(show.is_error);
    CHECK(show.content.find("OUTSIDE SUBJECT") == std::string::npos);
    CHECK_FALSE(holds(world.root, world.outside_head));
    CHECK(world.gate.hosts.empty());
    // The control: plain git, handed the path the tool would hand it, rewrites
    // it again and copies the outside history in.
    plain_git(world.root, {"fetch", "--no-tags", "--end-of-options", handed,
                           "+refs/heads/main:refs/remotes/o/main"});
    CHECK(holds(world.root, world.outside_head));
}

TEST_CASE(
    "a network URL the person's own rewrites would change a second time is refused, so no "
    "host nobody was asked about is reached",
    "[tools][git][review][sandbox][57x]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    // The same chain over the network: the first rewrite names 127.0.0.1, the
    // host a person would allow; the second passes it on to `localhost`, a
    // different host to `contracts/host.h`, which nobody is asked about.
    FetchWorld world;
    world.gate.allow = true;
    const apogee::testing::GitHttpServer asked{world.temp.path(), world.temp.path() / "asked.log"};
    const apogee::testing::GitHttpServer unasked{world.temp.path(),
                                                 world.temp.path() / "unasked.log"};
    if (!asked.running() || !unasked.running()) {
        SKIP("python3 cannot serve here");
    }
    const std::string elsewhere = "http://localhost:" + std::to_string(unasked.port()) + "/";
    const std::filesystem::path global = world.temp.path() / "global.cfg";
    std::ofstream{global, std::ios::binary}
        << "[url \"" << asked.url("") << "\"]\n\tinsteadOf = https://first.invalid/\n"
        << "[url \"" << elsewhere << "\"]\n\tinsteadOf = " << asked.url("") << "\n";
    const apogee::testing::EnvGuard person{"GIT_CONFIG_GLOBAL", global.string()};
    git(world.root, {"remote", "add", "o", "https://first.invalid/r.git"});
    // One rewrite applied, the remote is on 127.0.0.1.
    const std::string judged = first_line(plain_git(world.root, {"remote", "get-url", "o"}));
    REQUIRE(judged == asked.url("r.git"));

    const ToolOutcome diff = world.run(
        "git_diff", {{"head", "main"}, {"base", "main"}, {"remote", "o"}, {"fetch", "always"}});
    INFO(diff.content);
    // Not fetched, so the review is left to what is already here, saying why.
    CHECK(diff.content.find("Not fetched: the remote 'o' is not fetched from: git would rewrite "
                            "its URL again") != std::string::npos);
    CHECK(world.gate.hosts.empty());
    CHECK(asked.requests().empty());
    CHECK(unasked.requests().empty());
    // The control: plain git, handed the URL the tool judges, reaches
    // `localhost` and never 127.0.0.1.
    plain_git(world.root, {"-c", "http.followRedirects=false", "fetch", "--no-tags",
                           "--end-of-options", judged, "+refs/heads/main:refs/remotes/o/main"});
    CHECK_FALSE(unasked.requests().empty());
    CHECK(asked.requests().empty());
}

TEST_CASE("a network remote is asked about by its host, once, before anything is sent",
          "[tools][git][review][sandbox][57x]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    // The probe's C8: an http remote on a host the model chose, a
    // repository credential.helper, a listener answering 401.
    FetchWorld world;
    const apogee::testing::GitHttpServer server{world.temp.path(), world.temp.path() / "c8.log",
                                                apogee::testing::GitHttpServer::Mode::Unauthorized};
    if (!server.running()) {
        SKIP("python3 cannot serve here");
    }
    const std::filesystem::path x = world.root / "x";
    plant_repo(x, config_with("[credential]\n\thelper = !touch " + world.mark("CREDHELPER_RAN") +
                              "; false\n[remote \"o\"]\n\turl = " +
                              server.url("LEAKED-DATA-README/r.git") + "\n"));
    world.gate.allow = true;
    const ToolOutcome armed =
        world.run("git_diff", {{"repo", "x"}, {"head", "main"}, {"remote", "o"}});
    INFO(armed.content);
    CHECK(armed.is_error);
    CHECK(armed.content.find("credential.helper") != std::string::npos);
    CHECK(server.requests().empty());
    CHECK(markers(world.marks).empty());
    CHECK(world.gate.hosts.empty());
    // The control: plain git sends the path out and runs the helper on the 401.
    plain_git(x, {"fetch", "o", "main"});
    REQUIRE_FALSE(server.requests().empty());
    CHECK(server.requests().front().find("LEAKED-DATA-README") != std::string::npos);
    CHECK(markers(world.marks) == std::vector<std::string>{"CREDHELPER_RAN"});
    world.clear_marks();

    // Without the helper the fetch is a question about 127.0.0.1 -- the URL's
    // host, never the remote's name -- asked once though both refs need it.
    const std::size_t sent = server.requests().size();
    plain_git(x, {"config", "--unset", "credential.helper"});
    world.gate.allow = false;
    const ToolOutcome denied = world.run(
        "git_diff", {{"repo", "x"}, {"head", "main"}, {"base", "develop"}, {"remote", "o"}});
    INFO(denied.content);
    CHECK(denied.is_error);
    CHECK(denied.content.find("permission to reach 127.0.0.1 was not given") != std::string::npos);
    CHECK(denied.content.find("--allow-host 127.0.0.1") != std::string::npos);
    CHECK(denied.content.find("config add-allowed-host 127.0.0.1") != std::string::npos);
    CHECK(world.gate.hosts == std::vector<std::string>{"127.0.0.1"});
    CHECK(world.gate.details == std::vector<std::string>{server.url("LEAKED-DATA-README/r.git")});
    CHECK(server.requests().size() == sent);

    world.gate.hosts.clear();
    world.gate.allow = true;
    const ToolOutcome allowed = world.run(
        "git_diff", {{"repo", "x"}, {"head", "main"}, {"base", "develop"}, {"remote", "o"}});
    CHECK(world.gate.hosts == std::vector<std::string>{"127.0.0.1"});
    CHECK(server.requests().size() > sent);
    CHECK(markers(world.marks).empty());

    // A password in the URL is the person's: the question shows it masked.
    plain_git(x, {"remote", "set-url", "o",
                  "http://reviewer:hunter2@127.0.0.1:" + std::to_string(server.port()) + "/r.git"});
    world.gate.hosts.clear();
    world.gate.details.clear();
    world.gate.allow = false;
    (void)world.run("git_diff", {{"repo", "x"}, {"head", "main"}, {"remote", "o"}});
    CHECK(world.gate.hosts == std::vector<std::string>{"127.0.0.1"});
    REQUIRE(world.gate.details.size() == 1);
    CHECK(world.gate.details.front().find("hunter2") == std::string::npos);
    CHECK(world.gate.details.front().find("reviewer:***@127.0.0.1") != std::string::npos);
}

TEST_CASE(
    "an allowed network fetch reaches its host and no other: it fetches, and a redirect is "
    "never followed",
    "[tools][git][review][sandbox][57x]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    FetchWorld world;
    // A bare repository served over git's dumb HTTP protocol, holding a
    // branch the launch repository has never seen.
    const std::filesystem::path served = world.temp.path() / "served";
    std::filesystem::create_directories(served);
    git(world.root, {"checkout", "-q", "-b", "remote-only"});
    std::ofstream{world.root / "r.txt", std::ios::binary} << "REMOTE ONLY TEXT\n";
    git(world.root, {"add", "r.txt"});
    git(world.root, {"commit", "-q", "-m", "remote only"});
    git(world.root, {"checkout", "-q", "main"});
    git(world.temp.path(),
        {"clone", "-q", "--bare", world.root.string(), (served / "r.git").string()});
    git(world.root, {"branch", "-D", "remote-only"});
    git(served / "r.git", {"update-server-info"});
    const apogee::testing::GitHttpServer server{served, world.temp.path() / "served.log"};
    if (!server.running()) {
        SKIP("python3 cannot serve here");
    }
    git(world.root, {"remote", "add", "origin", server.url("r.git")});

    // Refused: nothing is sent.
    const ToolOutcome refused = world.run("git_diff", {{"head", "remote-only"}, {"base", "main"}});
    CHECK(refused.is_error);
    CHECK(world.gate.hosts == std::vector<std::string>{"127.0.0.1"});
    CHECK(server.requests().empty());
    // Allowed: fetched, and diffed as before.
    world.gate.allow = true;
    world.gate.hosts.clear();
    const ToolOutcome fetched = world.run("git_diff", {{"head", "remote-only"}, {"base", "main"}});
    INFO(fetched.content);
    CHECK_FALSE(fetched.is_error);
    CHECK(fetched.content.find("Review: main...origin/remote-only") != std::string::npos);
    CHECK(fetched.content.find("REMOTE ONLY TEXT") != std::string::npos);
    CHECK(world.gate.hosts == std::vector<std::string>{"127.0.0.1"});
    CHECK_FALSE(server.requests().empty());
    // A review that needs no fetch asks nothing: `never`, and `auto` with
    // both refs here.
    world.gate.hosts.clear();
    CHECK_FALSE(
        world
            .run("git_diff", {{"head", "origin/remote-only"}, {"base", "main"}, {"fetch", "never"}})
            .is_error);
    CHECK_FALSE(world.run("git_diff", {{"head", "origin/remote-only"}, {"base", "main"}}).is_error);
    CHECK(world.gate.hosts.empty());

    // A redirect is never followed: the host asked about is the host reached.
    const apogee::testing::GitHttpServer redirecting{
        served, world.temp.path() / "redirect.log", apogee::testing::GitHttpServer::Mode::Redirect};
    REQUIRE(redirecting.running());
    const std::filesystem::path x = world.root / "x";
    make_repo(x, "x.txt", "x\n", "x");
    git(x, {"remote", "add", "o", redirecting.url("r.git")});
    const ToolOutcome redirected =
        world.run("git_diff", {{"repo", "x"}, {"head", "remote-only"}, {"remote", "o"}});
    INFO(redirected.content);
    CHECK(redirected.is_error);
    REQUIRE_FALSE(redirecting.requests().empty());
    for (const std::string& path : redirecting.requests()) {
        CHECK_FALSE(path.starts_with("/elsewhere"));
    }
    // The control: plain git follows the first redirect.
    plain_git(x, {"fetch", "o", "remote-only"});
    const std::vector<std::string> followed = redirecting.requests();
    CHECK(std::ranges::any_of(
        followed, [](const std::string& path) { return path.starts_with("/elsewhere"); }));
}

TEST_CASE(
    "a repository that asks for packfile URIs cannot reroute the review fetch to another host",
    "[tools][git][review][sandbox][57x]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    // The second review's finding: `fetch.uriProtocols` turns on protocol-v2
    // packfile URIs, so a server on an allowed host can answer a fetch with a
    // pack named on another host, which `git http-fetch` then downloads under
    // the judged transport's `protocol.http.allow=always` -- a host the gate
    // was never asked about. `-c fetch.uriProtocols=` on the fetch's `-c`
    // list switches the capability off (git advertises none, the blob stays
    // in the inline pack), beside the redirect and bundle-URI switches.
    FetchWorld world;
    // A served repository holding one blob, offered as a packfile URI pointing
    // at a second listener -- `localhost`, a different host from the remote's
    // `127.0.0.1` to `contracts/host.h`, and one never put to the gate.
    const std::filesystem::path served = world.temp.path() / "served";
    std::filesystem::create_directories(served);
    const std::filesystem::path work = world.temp.path() / "srvwork";
    make_repo(work, "my-blob", "A BLOB DELIVERED BY URI\n", "served");
    git(world.temp.path(), {"clone", "-q", "--bare", work.string(), (served / "r.git").string()});
    const std::string blob = first_line(git(work, {"rev-parse", "HEAD:my-blob"}));
    const std::filesystem::path packs = world.temp.path() / "packs";
    std::filesystem::create_directories(packs);
    const std::string pack_hash = first_line(git_with_stdin(
        served / "r.git", {"pack-objects", (packs / "mypack").string()}, blob + "\n"));
    REQUIRE(pack_hash.size() == blob.size());

    const apogee::testing::GitHttpServer repo{served, world.temp.path() / "smart.log",
                                              apogee::testing::GitHttpServer::Mode::Smart};
    const apogee::testing::GitHttpServer elsewhere{packs, world.temp.path() / "elsewhere.log"};
    if (!repo.running() || !elsewhere.running()) {
        SKIP("python3 cannot serve here");
    }
    const std::string pack_url =
        "http://localhost:" + std::to_string(elsewhere.port()) + "/mypack-" + pack_hash + ".pack";
    git(served / "r.git", {"config", "uploadpack.allowsidebandall", "true"});
    git(served / "r.git",
        {"config", "--add", "uploadpack.blobpackfileuri", blob + " " + pack_hash + " " + pack_url});

    // The control: a fresh repository whose own config turns the capability on,
    // fetched with the review fetch's `-c` list but WITHOUT `-c
    // fetch.uriProtocols=`, requests the packfile URI and reaches `localhost`.
    const std::filesystem::path control = world.root / "control";
    git(world.temp.path(), {"init", "-q", "-b", "main", control.string()});
    git(control, {"config", "fetch.uriProtocols", "http"});
    plain_git(control,
              {"-c", "protocol.allow=never", "-c", "protocol.http.allow=always", "-c",
               "http.followRedirects=false", "-c", "transfer.bundleURI=false", "fetch",
               "--no-recurse-submodules", "--no-tags", "--no-prune", "--no-auto-maintenance",
               "--end-of-options", repo.url("r.git"), "+refs/heads/main:refs/remotes/o/main"});
    REQUIRE_FALSE(elsewhere.requests().empty());
    const std::size_t reached = elsewhere.requests().size();

    // The tool: a planted repository with the capability on and its remote on
    // the allowed host. The remote's host is the only one asked about, and the
    // second listener is never reached -- no packfile URI was requested.
    const std::filesystem::path x = world.root / "x";
    plant_repo(x, config_with("[fetch]\n\turiProtocols = http\n[remote \"o\"]\n\turl = " +
                              repo.url("r.git") + "\n"));
    world.gate.allow = true;
    const ToolOutcome fetched =
        world.run("git_diff", {{"repo", "x"}, {"head", "main"}, {"base", "main"}, {"remote", "o"}});
    INFO(fetched.content);
    CHECK(world.gate.hosts == std::vector<std::string>{"127.0.0.1"});
    CHECK(elsewhere.requests().size() == reached);
    // The fetch still ran: the branch and its blob arrived in the inline pack.
    CHECK(holds(x, "refs/remotes/o/main"));
    CHECK(holds(x, blob));
}

TEST_CASE(
    "nothing a remote's URL says reaches git as an option, and a remote named like a URL "
    "is not git's to read",
    "[tools][git][review][sandbox][57x]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    FetchWorld world;
    world.gate.allow = true;
    int at = 0;
    for (const std::string& url :
         {"--upload-pack=touch " + world.mark("OPTION_RAN") + ";git-upload-pack",
          std::string{"ssh://-oProxyCommand=true/r.git"}, std::string{"-oProxyCommand=true:r.git"},
          std::string{"ssh://-x@example.invalid/r.git"}}) {
        INFO(url);
        const std::filesystem::path x = world.root / ("o" + std::to_string(at++));
        make_repo(x, "x.txt", "x\n", "x");
        std::ofstream{x / ".git" / "config", std::ios::binary | std::ios::app}
            << "[remote \"o\"]\n\turl = \"" << url << "\"\n";
        const ToolOutcome outcome = world.run(
            "git_diff", {{"repo", x.filename().string()}, {"head", "feature"}, {"remote", "o"}});
        INFO(outcome.content);
        CHECK(outcome.is_error);
        CHECK(outcome.content.find("'-'") != std::string::npos);
    }
    CHECK(markers(world.marks).empty());
    CHECK(world.gate.hosts.empty());

    // A remote named exactly as the URL another remote names: git would read
    // that remote's settings for a fetch by the URL.
    const std::filesystem::path x = world.root / "named";
    make_repo(x, "x.txt", "x\n", "x");
    git(x, {"config", "remote.o.url", "ssh://example.invalid/r.git"});
    git(x, {"config", "remote.ssh://example.invalid/r.git.url", world.outside.generic_string()});
    const ToolOutcome named =
        world.run("git_diff", {{"repo", "named"}, {"head", "feature"}, {"remote", "o"}});
    INFO(named.content);
    CHECK(named.is_error);
    CHECK(named.content.find("is named") != std::string::npos);
    CHECK_FALSE(holds(x, world.outside_head));
    CHECK(world.gate.hosts.empty());
    plain_git(x, {"fetch", "ssh://example.invalid/r.git", "main"});
    CHECK(holds(x, world.outside_head));
}

TEST_CASE(
    "an ssh or git URL whose path or user part git would read back into the host is refused "
    "before anyone is asked",
    "[tools][git][review][sandbox][57x]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    // git's parse_connect_url scans the whole ssh:// URL for `@[` (and
    // percent-decodes it first), so a path holding `@[other]` names a host
    // the gate was never asked about; git:// looks up the bare `host/x@other`
    // name, which ends in the other host's zone. And ssh:// and git:// alike
    // read a user part that starts with `[` as a bracketed host, dropping the
    // rest: `[other]@good` reaches `other`. Each is refused before the host
    // is judged, so nothing is sent and nobody is asked (57x).
    FetchWorld world;
    world.gate.allow = true;
    // The person's own ssh and git proxy (their environment, honoured): each
    // records the host it was handed, and never dials.
    const std::filesystem::path handed = world.temp.path() / "handed.txt";
    const std::string record =
        std::string{R"(printf '%s\n' "$@" >> ')"} + handed.generic_string() + "'\nexit 1";
    const std::filesystem::path ssh = world.temp.path() / "ssh.sh";
    const std::filesystem::path proxy = world.temp.path() / "proxy.sh";
    marker_script(ssh, world.marks / "SSH_RAN", record);
    marker_script(proxy, world.marks / "PROXY_RAN", record);
    const apogee::testing::EnvGuard person_ssh{"GIT_SSH_COMMAND", ssh.generic_string()};
    const apogee::testing::EnvGuard person_proxy{"GIT_PROXY_COMMAND", proxy.generic_string()};
    const std::vector<std::string> urls{
        "ssh://good.invalid/x@[evil.invalid]/y",     "ssh://good.invalid/x%40%5bevil.invalid%5d/y",
        "ssh://u@good.invalid/a@%5Bevil.invalid]/y", "git://good.invalid/x@evil.invalid",
        "ssh://[evil.invalid]@good.invalid/y",       "ssh://[evil.invalid]:x@good.invalid/y",
        "git://[evil.invalid]@good.invalid/y"};
    int at = 0;
    for (const std::string& url : urls) {
        INFO(url);
        const std::filesystem::path x = world.root / ("h" + std::to_string(at++));
        make_repo(x, "x.txt", "x\n", "x");
        git(x, {"config", "remote.o.url", url});
        const ToolOutcome outcome = world.run(
            "git_diff", {{"repo", x.filename().string()}, {"head", "feature"}, {"remote", "o"}});
        INFO(outcome.content);
        CHECK(outcome.is_error);
        CHECK(outcome.content.find("read back into the host") != std::string::npos);
        CHECK(outcome.content.find("good.invalid") == std::string::npos);
    }
    // The host the gate would have been asked about is never asked, and
    // neither the person's ssh nor their proxy is handed any host.
    CHECK(world.gate.hosts.empty());
    CHECK(markers(world.marks).empty());
    CHECK_FALSE(std::filesystem::exists(handed));

    // Through dispatch, with nobody to ask, the read tool is refused all the
    // same -- never asked about good.invalid, never sent to evil.invalid.
    for (const std::string& url : {std::string{"ssh://good.invalid/x@[evil.invalid]/y"},
                                   std::string{"ssh://[evil.invalid]@good.invalid/y"}}) {
        INFO(url);
        const std::filesystem::path d = world.root / ("dispatch" + std::to_string(at++));
        make_repo(d, "x.txt", "x\n", "x");
        git(d, {"config", "remote.o.url", url});
        const apogee::harness::ToolCall call{
            "h", "git_diff",
            nlohmann::json{{"repo", d.filename().string()}, {"head", "feature"}, {"remote", "o"}}
                .dump()};
        const ToolOutcome dispatched =
            apogee::agent::dispatch(world.registry, call, apogee::agent::DispatchContext{});
        INFO(dispatched.content);
        CHECK(dispatched.is_error);
        CHECK(dispatched.content.find("read back into the host") != std::string::npos);
    }
    CHECK(markers(world.marks).empty());

    // The controls: git's own parser reads the host back out of the path and
    // out of the user part, so plain git would reach a host the gate never
    // saw -- it hands the person's ssh, and their proxy, `evil.invalid`.
    for (const auto& [url, read_as] : std::vector<std::pair<std::string, std::string>>{
             {"ssh://good.invalid/x@[evil.invalid]/y", "good.invalid/x@evil.invalid"},
             {"ssh://[evil.invalid]@good.invalid/y", "evil.invalid"},
             {"ssh://[evil.invalid]:x@good.invalid/y", "evil.invalid"}}) {
        INFO(url);
        const std::string diag = git(world.root, {"fetch-pack", "--diag-url", url});
        const std::size_t line = diag.find("userandhost=");
        REQUIRE(line != std::string::npos);
        std::string user_and_host =
            first_line(diag.substr(line + std::string_view{"userandhost="}.size()));
        if (user_and_host.ends_with('\r')) {
            user_and_host.pop_back();
        }
        CHECK(user_and_host == read_as);
    }
    world.clear_marks();
    std::filesystem::remove(handed);
    plain_git(world.root / "h4", {"fetch", "o", "main"});
    CHECK(markers(world.marks) == std::vector<std::string>{"SSH_RAN"});
    world.clear_marks();
    plain_git(world.root / "h6", {"fetch", "o", "main"});
    CHECK(markers(world.marks) == std::vector<std::string>{"PROXY_RAN"});
    std::ifstream read{handed, std::ios::binary};
    const std::string hosts{std::istreambuf_iterator<char>{read}, std::istreambuf_iterator<char>{}};
    INFO(hosts);
    CHECK(hosts.find("evil.invalid") != std::string::npos);
    CHECK(hosts.find("good.invalid") == std::string::npos);
}

TEST_CASE(
    "a folder remote is a repository only at the path judged, never one git reaches by a "
    "'.git' suffix",
    "[tools][git][review][sandbox][57x]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    // The review fetch must fetch from exactly the repository it confined. A
    // plain folder `sub` inside the root is confined -- git's discovery walks
    // up to the launch repository -- but git's local upload-pack, entering
    // `sub` non-strictly, tries `sub/.git`, `sub`, `sub.git/.git`, `sub.git`,
    // and reads a `.git` the model planted at a suffix as a gitfile that
    // points outside the root (57x).
    for (const std::string& suffix : {std::string{"sub.git"}, std::string{"sub.git/.git"}}) {
        INFO(suffix);
        FetchWorld world;
        world.gate.allow = true;
        std::filesystem::create_directories(world.root / "sub");
        std::ofstream{world.root / "sub" / ".keep", std::ios::binary} << "";
        std::filesystem::create_directories((world.root / suffix).parent_path());
        std::ofstream{world.root / suffix, std::ios::binary}
            << "gitdir: " << (world.outside / ".git").generic_string() << "\n";
        const std::filesystem::path x = world.root / "x";
        plant_repo(
            x,
            config_with("[remote \"o\"]\n\turl = " + (world.root / "sub").generic_string() + "\n"));
        const ToolOutcome diff = world.run("git_diff", {{"repo", "x"},
                                                        {"head", "main"},
                                                        {"base", "main"},
                                                        {"remote", "o"},
                                                        {"fetch", "always"}});
        INFO(diff.content);
        CHECK(diff.is_error);
        CHECK(diff.content.find("OUTSIDE SUBJECT") == std::string::npos);
        const ToolOutcome show = world.run("git_show", {{"repo", "x"}, {"ref", "o/main"}});
        CHECK(show.is_error);
        CHECK(show.content.find("OUTSIDE SUBJECT") == std::string::npos);
        CHECK_FALSE(holds(x, world.outside_head));
        CHECK(world.gate.hosts.empty());
        // The control: plain git, handed the same folder, follows the suffix
        // out of the root.
        plain_git(x, {"fetch", (world.root / "sub").generic_string(), "main"});
        CHECK(holds(x, world.outside_head));
    }
}

TEST_CASE(
    "a folder remote git would enter by a different path -- its '@' bracketed-host split -- is "
    "refused, in its relative, absolute and file:// spellings",
    "[tools][git][review][sandbox][57x]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    // git's parse_connect_url runs host_end() on a LOCAL or file:// path too:
    // it splits the path where an "@[" names a bracketed host and reads the
    // path after the matching "]" instead. So a remote whose URL the judgment
    // confines at `x/q@[y]/z` -- a bona fide repository inside the root -- is
    // entered by git at `x/]/z`, a `.git` gitfile a `write_file` planted there
    // naming a repository outside the root. The review fetch must reach
    // exactly the path judged, so a path git would read differently from the
    // one judged is refused before git runs (57x, round 5).
    for (const std::string& form : {std::string{"rel"}, std::string{"abs"}, std::string{"file"}}) {
        INFO(form);
        FetchWorld world;
        world.gate.allow = true;
        // The folder the judgment confines: a real repository inside the root.
        const std::filesystem::path judged = world.root / "x" / "q@[y]" / "z";
        std::filesystem::create_directories(judged);
        plant_repo(judged, config_with(""));
        // The folder git actually enters once host_end splits the path: a
        // gitfile naming the outside repository.
        std::filesystem::create_directories(world.root / "x" / "]" / "z");
        std::ofstream{world.root / "x" / "]" / "z" / ".git", std::ios::binary}
            << "gitdir: " << (world.outside / ".git").generic_string() << "\n";
        const std::filesystem::path x = world.root / "x";
        // The remote the tool judges, and the path plain git actually follows
        // out of the root (the tool strips a file:// URL to this bare path
        // before handing it to git, so the control is that same path).
        const std::string rel_url = "q@[y]/z";
        const std::string abs_url = judged.generic_string();
        std::string url = form == "rel" ? rel_url : (form == "abs" ? abs_url : "file://" + abs_url);
        const std::string control = form == "rel" ? rel_url : abs_url;
        plant_repo(x, config_with("[remote \"o\"]\n\turl = " + url + "\n"));
        const ToolOutcome diff = world.run("git_diff", {{"repo", "x"},
                                                        {"head", "main"},
                                                        {"base", "main"},
                                                        {"remote", "o"},
                                                        {"fetch", "always"}});
        INFO(diff.content);
        CHECK(diff.is_error);
        CHECK(diff.content.find("OUTSIDE SUBJECT") == std::string::npos);
        const ToolOutcome show = world.run("git_show", {{"repo", "x"}, {"ref", "o/main"}});
        CHECK(show.is_error);
        CHECK(show.content.find("OUTSIDE SUBJECT") == std::string::npos);
        CHECK_FALSE(holds(x, world.outside_head));
        CHECK(world.gate.hosts.empty());
        // The control: plain git, handed the folder path the URL names, splits
        // it at "@[" and follows the planted gitfile out of the root.
        plain_git(x, {"fetch", control, "main"});
        CHECK(holds(x, world.outside_head));
    }
}

TEST_CASE(
    "a folder remote is refused when the path git is handed holds an '@' bracketed host though "
    "its URL does not -- through a link inside the root, or the root's own path",
    "[tools][git][review][sandbox][57x]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    // The backstop behind parse_fetch_url (57x, round 6). The URL `link/z`
    // holds no "@[", but resolve_in_root follows links, so where `x/link` is
    // a link to `x/q@[y]` -- as a checked-out repository can carry -- the
    // path judge_folder hands git is `x/q@[y]/z`, and git's host_end() enters
    // `x/]/z` instead. A root whose own path holds "@[" does the same to every
    // folder remote under it: `z` resolves to `<p@[r]>/proj/x/z`, which git
    // enters as `x/]/proj/x/z`. A `.git` gitfile planted at the split path
    // names a repository outside the root, so the resolved path is held to
    // the URL's rule before git runs.
    for (const std::string& route : {std::string{"link"}, std::string{"root"}}) {
        INFO(route);
        FetchWorld world{apogee::tools::ReviewDefaults{}, route == "root" ? "p@[r]" : ""};
        world.gate.allow = true;
        const std::filesystem::path x = world.root / "x";
        const std::string url = route == "link" ? "link/z" : "z";
        // The repository the judgment confines, and the folder git enters.
        const std::filesystem::path judged = route == "link" ? x / "q@[y]" / "z" : x / "z";
        const std::filesystem::path split =
            route == "link" ? x / "]" / "z" : x / "]" / "proj" / "x" / "z";
        std::filesystem::create_directories(judged);
        plant_repo(judged, config_with(""));
        std::filesystem::create_directories(split);
        std::ofstream{split / ".git", std::ios::binary}
            << "gitdir: " << (world.outside / ".git").generic_string() << "\n";
        plant_repo(x, config_with("[remote \"o\"]\n\turl = " + url + "\n"));
        if (route == "link") {
            const apogee::testing::LinkMade link =
                apogee::testing::make_directory_link(x / "q@[y]", x / "link");
            INFO(link.why);
            REQUIRE(link.made);
        }
        // The URL alone passes: the refusal is the resolved path's.
        std::string why;
        CHECK(apogee::tools::parse_fetch_url(url, why).has_value());
        const ToolOutcome diff = world.run("git_diff", {{"repo", "x"},
                                                        {"head", "main"},
                                                        {"base", "main"},
                                                        {"remote", "o"},
                                                        {"fetch", "always"}});
        INFO(diff.content);
        CHECK(diff.is_error);
        CHECK(diff.content.find("its path holds '@['") != std::string::npos);
        CHECK(diff.content.find("OUTSIDE SUBJECT") == std::string::npos);
        const ToolOutcome show = world.run("git_show", {{"repo", "x"}, {"ref", "o/main"}});
        CHECK(show.is_error);
        CHECK(show.content.find("OUTSIDE SUBJECT") == std::string::npos);
        CHECK_FALSE(holds(x, world.outside_head));
        CHECK(world.gate.hosts.empty());
        // The control: plain git, handed the path the tool resolves -- links
        // followed -- splits it at "@[" and follows the planted gitfile out.
        plain_git(x, {"fetch", std::filesystem::canonical(judged).generic_string(), "main"});
        CHECK(holds(x, world.outside_head));
    }
}

TEST_CASE("no git tool fetches a missing object from a promisor remote by its name",
          "[tools][git][sandbox][57x]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    // A partial clone's missing object is fetched lazily from its promisor
    // remote, by name -- that remote's `uploadpack` and URL included -- by
    // any read that needs it: `git_show --stat` reads every blob it counts.
    FetchWorld world;
    const std::filesystem::path up = world.inside_remote();
    git(up, {"config", "uploadpack.allowFilter", "true"});
    const std::filesystem::path partial = world.root / "partial";
    git(world.temp.path(), {"clone", "-q", "--no-local", "--filter=blob:none", "--no-checkout",
                            "file://" + up.generic_string(), partial.string()});
    git(partial, {"config", "remote.origin.uploadpack",
                  "touch " + world.mark("LAZY_FETCH_RAN") + "; git-upload-pack"});
    // A git that predates GIT_NO_LAZY_FETCH (2.44) fetches whatever it is
    // told, so the check is made only where the variable is read.
    {
        const apogee::testing::EnvGuard told{"GIT_NO_LAZY_FETCH", "1"};
        plain_git(partial, {"show", "--stat", "origin/feature"});
    }
    if (!markers(world.marks).empty()) {
        WARN("this git does not read GIT_NO_LAZY_FETCH (before 2.44): not checked here");
        return;
    }
    const ToolOutcome shown =
        world.run("git_show", {{"repo", "partial"}, {"ref", "origin/feature"}});
    INFO(shown.content);
    CHECK(markers(world.marks).empty());
    // The control: plain git fetches it, running the planted command.
    plain_git(partial, {"show", "--stat", "origin/feature"});
    CHECK(markers(world.marks) == std::vector<std::string>{"LAZY_FETCH_RAN"});
}

TEST_CASE("the person's GIT_NO_LAZY_FETCH=0 brings no lazy fetch back to a git tool",
          "[tools][git][sandbox][57x]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    // Round 8's follow-up review: the tools' own GIT_NO_LAZY_FETCH=1 was added
    // after the inherited entries, and git reads the first of two, so the
    // person's 0 -- git's documented way to allow lazy fetches again --
    // brought back the fetch by the promisor remote's name, its planted
    // `uploadpack` and its URL from the repository's own config.
    FetchWorld world;
    const std::filesystem::path up = world.inside_remote();
    git(up, {"config", "uploadpack.allowFilter", "true"});
    const std::filesystem::path partial = world.root / "partial";
    git(world.temp.path(), {"clone", "-q", "--no-local", "--filter=blob:none", "--no-checkout",
                            "file://" + up.generic_string(), partial.string()});
    git(partial, {"config", "remote.origin.uploadpack",
                  "touch " + world.mark("LAZY_FETCH_RAN") + "; git-upload-pack"});
    const std::string blob = first_line(git(up, {"rev-parse", "feature:feature.txt"}));
    // Whether the clone holds the blob, asked without fetching it.
    const auto present = [&partial, &blob] {
        const apogee::testing::EnvGuard told{"GIT_NO_LAZY_FETCH", "1"};
        return holds(partial, blob);
    };
    {
        const apogee::testing::EnvGuard told{"GIT_NO_LAZY_FETCH", "1"};
        plain_git(partial, {"show", "--stat", "origin/feature"});
    }
    if (!markers(world.marks).empty()) {
        WARN("this git does not read GIT_NO_LAZY_FETCH (before 2.44): not checked here");
        return;
    }
    REQUIRE_FALSE(present());
    const apogee::testing::EnvGuard person{"GIT_NO_LAZY_FETCH", "0"};
    const ToolOutcome shown =
        world.run("git_show", {{"repo", "partial"}, {"ref", "origin/feature"}});
    INFO(shown.content);
    CHECK(markers(world.marks).empty());
    CHECK(shown.is_error);
    CHECK_FALSE(present());
    // The control: plain git under the same variable fetches the blob,
    // running the planted command.
    plain_git(partial, {"show", "--stat", "origin/feature"});
    CHECK(markers(world.marks) == std::vector<std::string>{"LAZY_FETCH_RAN"});
    CHECK(present());
}

TEST_CASE("a remote's URL is judged as git would read it, in any spelling, before git runs",
          "[tools][git][review][57x]") {
    using apogee::tools::FetchUrl;
    using apogee::tools::parse_fetch_url;

    struct Read {
        std::string url;
        FetchUrl::Kind kind;
        std::string transport;
        std::string host;
        std::string rebuilt;
        std::string shown;
    };

    const auto network = FetchUrl::Kind::Network;
    const auto local = FetchUrl::Kind::Local;
    const std::vector<Read> accepted{
        {"https://github.com/acme/widget.git", network, "https", "github.com",
         "https://github.com/acme/widget.git", "https://github.com/acme/widget.git"},
        // A scheme in any case is that scheme, rebuilt in lower case, and the
        // host canonical: git is handed what was judged.
        {"HTTPS://GitHub.COM/acme/widget.git", network, "https", "github.com",
         "https://github.com/acme/widget.git", "https://github.com/acme/widget.git"},
        {"http://127.0.0.1:8080/r.git", network, "http", "127.0.0.1", "http://127.0.0.1:8080/r.git",
         "http://127.0.0.1:8080/r.git"},
        // Credentials are the person's: the host is the part after the `@`,
        // and a password is never shown.
        {"https://ci:hunter2@example.com/r.git", network, "https", "example.com",
         "https://ci:hunter2@example.com/r.git", "https://ci:***@example.com/r.git"},
        {"https://token@example.com/r.git", network, "https", "example.com",
         "https://token@example.com/r.git", "https://token@example.com/r.git"},
        {"ssh://git@example.com:2222/acme/widget.git", network, "ssh", "example.com",
         "ssh://git@example.com:2222/acme/widget.git",
         "ssh://git@example.com:2222/acme/widget.git"},
        {"git://example.com/r.git", network, "git", "example.com", "git://example.com/r.git",
         "git://example.com/r.git"},
        {"https://[::1]:8443/r.git", network, "https", "::1", "https://[::1]:8443/r.git",
         "https://[::1]:8443/r.git"},
        // scp-like: ssh, the host before the `:`, an IPv6 literal in brackets.
        {"git@github.com:acme/widget.git", network, "ssh", "github.com",
         "git@github.com:acme/widget.git", "git@github.com:acme/widget.git"},
        {"git@[::1]:repo/x.git", network, "ssh", "::1", "git@[::1]:repo/x.git",
         "git@[::1]:repo/x.git"},
        {"[0:0::1]:repo/x.git", network, "ssh", "0:0::1", "[0:0::1]:repo/x.git",
         "[0:0::1]:repo/x.git"},
        // Folders: judged later, against the root.
        {"/abs/path/repo", local, "file", "", "/abs/path/repo", "/abs/path/repo"},
        {"../sibling", local, "file", "", "../sibling", "../sibling"},
        {"repo", local, "file", "", "repo", "repo"},
        {"./a:b", local, "file", "", "./a:b", "./a:b"},
        {"file:///abs/path", local, "file", "", "/abs/path", "file:///abs/path"},
        {"File:///abs/path", local, "file", "", "/abs/path", "File:///abs/path"},
    };
    for (const Read& read : accepted) {
        INFO(read.url);
        std::string refusal;
        const std::optional<FetchUrl> parsed = parse_fetch_url(read.url, refusal);
        REQUIRE(parsed.has_value());
        // The early return is unreachable past REQUIRE; it is the guard
        // clang-tidy's dataflow reads, since it cannot see REQUIRE abort.
        if (!parsed.has_value()) {
            continue;
        }
        CHECK(refusal.empty());
        CHECK(parsed->kind == read.kind);
        CHECK(parsed->transport == read.transport);
        CHECK(parsed->host == read.host);
        CHECK(parsed->url == read.rebuilt);
        CHECK(parsed->shown == read.shown);
    }

    const std::vector<std::pair<std::string, std::string>> refused{
        {"", "no URL"},
        {"ext::sh -c touch% /tmp/x", "ext::"},
        {"fd::3", "fd::"},
        {"hg::https://example.com/r", "hg::"},
        {"ftp://example.com/r.git", "ftp://"},
        {"git+ssh://example.com/r.git", "git+ssh://"},
        // Never an option to git or to ssh.
        {"--upload-pack=touch x;git-upload-pack", "'-'"},
        {"-oProxyCommand=true:r/x.git", "'-'"},
        {"ssh://-oProxyCommand=true/r.git", "'-'"},
        {"ssh://-u@example.com/r.git", "'-'"},
        {"git@-oProxyCommand=true:r/x.git", "'-'"},
        // An ssh:// or git:// path that git would read back into the host --
        // its `@[`, raw or percent-escaped (ssh decodes first), or a bare
        // `@` git:// looks a host up through -- is refused, so the host asked
        // about is the host reached.
        {"ssh://good.example/x@[evil.example]/y", "read back into the host"},
        {"ssh://good.example/x%40%5bevil.example%5d/y", "read back into the host"},
        {"ssh://u@good.example/a@%5Bevil.example]/y", "read back into the host"},
        {"ssh://good.example/x@[evil.example:2222]/y", "read back into the host"},
        {"git://good.example/x@evil.example", "read back into the host"},
        // A user part holding a bracket: ssh:// and git:// read a leading
        // `[...]` as the host and drop the rest, so `[evil]@good` reaches
        // evil. No user part may hold one, so none is judged by the part
        // after its `@`.
        {"ssh://[evil.example]@good.example/y", "user part"},
        {"ssh://[evil.example]:x@good.example/y", "user part"},
        {"git://[evil.example]@good.example/y", "user part"},
        {"ssh://[evil.example]x@good.example/y", "user part"},
        {"ssh://u]@good.example/y", "user part"},
        {"https://[evil.example]@good.example/r.git", "user part"},
        {"a[evil.example]@good.example:r/x.git", "user part"},
        // A LOCAL or file:// path git's parse_connect_url would split at an
        // "@[" (or a leading "[") -- host_end reads the folder after the
        // matching "]", not the one judged -- is refused before git runs, in
        // its relative, absolute and file:// spellings (57x, round 5).
        {"q@[evil]/z", "folder other than the one judged"},
        {"/abs/x/q@[evil]/z", "folder other than the one judged"},
        {"[evil]/z", "folder other than the one judged"},
        {"file:///abs/x/q@[evil]/z", "folder other than the one judged"},
        // A host that cannot be judged is never guessed at.
        {"https://a@b@evil.example/r.git", "more than one '@'"},
        {"https://evil.example%2f@good.example/", "holds a character"},
        {"https://good.example\\@evil.example/", "holds a character"},
        {"https://exa mple.com/r.git", "holds a character"},
        {"https://example.com:99999/r.git", "port"},
        {"https://example.com:/r.git", "port"},
        {"https://*.example.com/r.git", "not a host name"},
        {"file://host/x", "names a host"},
        {"file:///a/%2e%2e/b", "percent escape"},
        {"host:repo.git", "no '/'"},
        {std::string{"https://example.com/r\n.git"}, "control character"},
    };
    for (const auto& [url, said] : refused) {
        INFO(url);
        std::string refusal;
        CHECK_FALSE(parse_fetch_url(url, refusal).has_value());
        CHECK(refusal.find(said) != std::string::npos);
    }
}

TEST_CASE(
    "the person's GIT_CONFIG hides no setting of the repository's own from the review fetch, so "
    "a planted sshCommand is still refused",
    "[tools][git][review][sandbox][57x]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    // Round 8's review: `git config` reads only the file GIT_CONFIG names,
    // while `git fetch` ignores the variable and reads the repository's own
    // config. Under it the settings listing came back empty, and the fetch
    // ran a planted core.sshCommand for a host the person had allowed.
    FetchWorld world;
    world.gate.allow = true;
    const std::filesystem::path theirs = world.temp.path() / "person.gitconfig";
    std::ofstream{theirs, std::ios::binary} << "[user]\n\tname = person\n";
    const std::filesystem::path x = world.root / "x";
    plant_repo(x, config_with("[core]\n\tsshCommand = touch " + world.mark("SSH_RAN") +
                              "; false\n[remote \"o\"]\n\turl = ssh://example.invalid/r.git\n"));
    const std::vector<std::string> listing{"config", "--show-scope", "--name-only", "--get-regexp",
                                           "^core\\.ssh"};
    CHECK(first_line(plain_git(x, listing)) == "local\tcore.sshcommand");
    const apogee::testing::EnvGuard person{"GIT_CONFIG", theirs.string()};
    // The control: under it, plain git's listing misses the key, and plain
    // git's fetch runs it.
    CHECK(plain_git(x, listing).empty());
    plain_git(x, {"-c", "core.hooksPath=/dev/null", "fetch", "o", "main"});
    REQUIRE(markers(world.marks) == std::vector<std::string>{"SSH_RAN"});
    world.clear_marks();

    const ToolOutcome outcome =
        world.run("git_diff", {{"repo", "x"}, {"head", "main"}, {"remote", "o"}});
    INFO(outcome.content);
    CHECK(outcome.is_error);
    CHECK(outcome.content.find("the repository's own config sets core.sshCommand") !=
          std::string::npos);
    CHECK(markers(world.marks).empty());
    // Refused before anyone is asked about the host.
    CHECK(world.gate.hosts.empty());
}

TEST_CASE(
    "the person's GIT_CONFIG hides no filter driver of the repository's own, so git_diff still "
    "switches it off",
    "[tools][git][sandbox][57x]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    // The same blind spot in 57o's filter listing, closed by the same change:
    // under GIT_CONFIG the listing found no driver, and git_diff of a changed
    // file ran the repository's clean command.
    FetchWorld world;
    const std::filesystem::path theirs = world.temp.path() / "person.gitconfig";
    std::ofstream{theirs, std::ios::binary} << "[user]\n\tname = person\n";
    const std::filesystem::path x = world.root / "x";
    make_repo(x, "x.txt", "x\n", "x");
    std::ofstream{x / ".git" / "config", std::ios::binary | std::ios::app}
        << "[filter \"evil\"]\n\tclean = touch " << world.mark("FILTER_RAN") << "; cat\n";
    std::ofstream{x / ".gitattributes", std::ios::binary} << "*.txt filter=evil\n";
    std::ofstream{x / "x.txt", std::ios::binary} << "changed\n";
    const std::vector<std::string> listing{"config", "--show-scope", "--name-only", "--get-regexp",
                                           "^filter\\."};
    CHECK(first_line(plain_git(x, listing)) == "local\tfilter.evil.clean");
    const apogee::testing::EnvGuard person{"GIT_CONFIG", theirs.string()};
    CHECK(plain_git(x, listing).empty());

    const ToolOutcome outcome = world.run("git_diff", {{"repo", "x"}});
    INFO(outcome.content);
    CHECK_FALSE(outcome.is_error);
    CHECK(outcome.content.find("+changed") != std::string::npos);
    CHECK(markers(world.marks).empty());
    // The control: plain git's diff runs the driver under the same variable.
    plain_git(x, {"-c", "core.hooksPath=/dev/null", "diff"});
    CHECK(markers(world.marks) == std::vector<std::string>{"FILTER_RAN"});
}

TEST_CASE("the repository's own fetch settings are named, the person's left alone",
          "[tools][git][review][57x]") {
    using apogee::tools::fetch_setting_refusal;
    const std::vector<std::pair<std::string, std::string>> named{
        {"local\tcore.sshcommand", "core.sshCommand, a command git would run to fetch"},
        {"local\tcore.gitproxy", "core.gitProxy"},
        {"local\tcore.askpass", "core.askPass"},
        {"local\tcore.alternaterefscommand", "core.alternateRefsCommand"},
        {"local\tcredential.helper", "credential.helper"},
        {"worktree\tcredential.https://H.example.helper", "credential.https://H.example.helper"},
        {"local\turl.https://Mirror.example/.insteadof", "url.https://Mirror.example/.insteadOf"},
        // An empty base is a subsection all the same: git honours `[url ""]`
        // and lists it with two dots.
        {"local\turl..insteadof", "url..insteadOf"},
        {"local\thttp.proxy", "http.proxy, a proxy"},
        {"local\thttp.https://x.example.proxy", "http.https://x.example.proxy"},
        {"local\thttps.proxy", "https.proxy"},
        {"local\thttp.curloptresolve", "http.curloptResolve"},
        {"local\thttp.followredirects", "http.followRedirects"},
        {"local\thttp.cookiefile", "http.cookieFile"},
        {"local\thttp.savecookies", "http.saveCookies"},
        {"worktree\thttp.https://x.example.cookiefile", "http.https://x.example.cookieFile"},
        {"local\tfetch.bundleuri", "fetch.bundleURI"},
    };
    for (const auto& [listed, said] : named) {
        INFO(listed);
        const std::string refusal = fetch_setting_refusal("local\tcore.bare\n" + listed + "\r\n");
        CHECK(refusal.find("the repository's own config sets " + said) != std::string::npos);
        CHECK(refusal.find("fetch it yourself, or remove the setting") != std::string::npos);
    }
    // The person's own, and the keys a fetch neither runs nor follows.
    CHECK(fetch_setting_refusal("global\tcore.sshcommand\nsystem\tcredential.helper\ncommand\t"
                                "url.x.insteadof\nglobal\thttp.proxy\n")
              .empty());
    CHECK(fetch_setting_refusal("local\tcore.bare\nlocal\turl.x.pushinsteadof\nlocal\tcredential."
                                "username\nlocal\thttp.sslverify\nlocal\tfetch.prune\nlocal\tcore."
                                "sshcommandx\nlocal\turl.insteadof\nlocal\tcore.x.sshcommand\n")
              .empty());
    // A setting git reads with no subsection is not read with an empty one:
    // `[core ""] sshCommand` runs nothing.
    CHECK(fetch_setting_refusal("local\tcore..sshcommand\nlocal\tfetch..bundleuri\n").empty());
    CHECK(fetch_setting_refusal("").empty());
}

TEST_CASE("a repository's own rewrite with an empty base is refused by name, as one with a base is",
          "[tools][git][review][sandbox][57x]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    // Round 8's follow-up review: git honours `[url ""] insteadOf = <prefix>`,
    // listing it as `url..insteadof`, and the refusal read that empty
    // subsection as none, so the planted rewrite was followed. Here it turns
    // the remote's ssh URL into the folder `sub`, a repository inside the root.
    FetchWorld world;
    world.gate.allow = true;
    const std::filesystem::path x = world.root / "x";
    make_repo(x, "x.txt", "x\n", "x");
    const std::filesystem::path sub = x / "sub";
    git(world.temp.path(), {"clone", "-q", x.string(), sub.string()});
    git(sub, {"checkout", "-q", "-b", "feature"});
    std::ofstream{sub / "f.txt", std::ios::binary} << "VIA EMPTY BASE\n";
    git(sub, {"add", "f.txt"});
    git(sub, {"commit", "-q", "-m", "feature"});
    std::ofstream{x / ".git" / "config", std::ios::binary | std::ios::app}
        << "[remote \"o\"]\n\turl = ssh://example.invalid/sub\n[url \"\"]\n\tinsteadOf = "
           "ssh://example.invalid/\n";
    // Armed: plain git lists the key with an empty subsection and applies it.
    CHECK(first_line(plain_git(x, {"config", "--show-scope", "--name-only", "--get-regexp",
                                   "^url\\."})) == "local\turl..insteadof");
    CHECK(first_line(plain_git(x, {"remote", "get-url", "o"})) == "sub");

    const ToolOutcome outcome = world.run("git_diff", {{"repo", "x"},
                                                       {"head", "feature"},
                                                       {"base", "main"},
                                                       {"remote", "o"},
                                                       {"fetch", "always"}});
    INFO(outcome.content);
    CHECK(outcome.is_error);
    CHECK(outcome.content.find("the repository's own config sets url..insteadOf") !=
          std::string::npos);
    CHECK(outcome.content.find("VIA EMPTY BASE") == std::string::npos);
    CHECK_FALSE(holds(x, "refs/remotes/o/feature"));
    CHECK(world.gate.hosts.empty());
    // The control: plain git's fetch follows the rewrite into `sub`.
    plain_git(x, {"fetch", "o", "feature:refs/remotes/o/feature"});
    CHECK(holds(x, "refs/remotes/o/feature"));
}

TEST_CASE("a repository's own header settings are refused when they could name another site",
          "[tools][git][review][57x]") {
    using apogee::tools::fetch_header_refusal;
    // `git config -z --show-scope --get-regexp` output: the scope, a NUL, the
    // key, a newline and the value, a NUL.
    const auto entry = [](std::string_view scope, std::string_view key, std::string_view value) {
        std::string out{scope};
        out += '\0';
        out += key;
        out += '\n';
        out += value;
        out += '\0';
        return out;
    };

    struct Row {
        std::string listed;
        std::string said;
    };

    const std::vector<Row> refused{
        {entry("local", "http.extraheader", "Host: other.example"),
         "http.extraHeader to a Host header"},
        {entry("local", "http.extraheader", "host: other.example"), "a Host header"},
        {entry("local", "http.extraheader", "  HOST : other.example"), "a Host header"},
        // An empty Host has git's library send none of its own.
        {entry("local", "http.extraheader", "Host:"), "a Host header"},
        {entry("local", "http.extraheader", "Host;"), "a Host header"},
        {entry("worktree", "http.https://x.example/.extraheader", "Host: y.example"),
         "http.https://x.example/.extraHeader to a Host header"},
        // A line break starts a header line of its own, a Host among them.
        {entry("local", "http.extraheader", "X-A: 1\nHost: other.example"), "control character"},
        {entry("local", "http.extraheader", "X-A: 1\r"), "control character"},
        {entry("local", "http.extraheader", "\tHost: other.example"), "control character"},
        {entry("local", "http.useragent", "ua\r\nHost: other.example"),
         "http.userAgent to a value holding a control character"},
        {entry("local", "http.https://x.example/.useragent", "ua\n"),
         "http.https://x.example/.userAgent"},
        // Behind one the fetch sends as it is.
        {entry("local", "http.extraheader", "X-Trace: 1") +
             entry("local", "http.extraheader", "Host: other.example"),
         "a Host header"},
    };
    for (const Row& row : refused) {
        INFO(row.listed);
        const std::string refusal = fetch_header_refusal(row.listed);
        CHECK(refusal.find("the repository's own config sets ") == 0);
        CHECK(refusal.find(row.said) != std::string::npos);
        CHECK(refusal.find("fetch it yourself, or remove the setting") != std::string::npos);
        // A header's value may be a credential: it is never repeated.
        CHECK(refusal.find("other.example") == std::string::npos);
    }
    // The header actions/checkout writes into a repository's own config, a
    // header naming no site, a reset, a plain user agent, the person's own
    // settings, and a key set with no value.
    std::string kept = entry("local", "http.https://github.com/.extraheader",
                             "AUTHORIZATION: basic eC1hY2Nlc3MtdG9rZW46Kioq");
    kept += entry("local", "http.extraheader", "Hostname: other.example");
    kept += entry("local", "http.extraheader", "");
    kept += entry("local", "http.useragent", "git/2.53.0 (ci)");
    kept += entry("global", "http.extraheader", "Host: other.example");
    kept += entry("command", "http.useragent", "ua\nHost: other.example");
    kept += std::string{"local"} + '\0' + "http.extraheader" + '\0';
    CHECK(fetch_header_refusal(kept).empty());
    CHECK(fetch_header_refusal("").empty());
}

TEST_CASE(
    "a repository's own header that would name another site is refused by name, and a "
    "credential header still fetches",
    "[tools][git][review][sandbox][57x]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    // Round 8's follow-up review: a front that picks its site by the Host
    // header -- a shared host, a CDN edge, a reverse proxy -- serves the site
    // a request names, so a repository's own `http.extraHeader = Host: <other>`
    // had the fetch read a site nobody was asked about from the asked host's
    // address. A line break in a header setting slips a second Host in.
    FetchWorld world;
    const std::filesystem::path served = world.temp.path() / "served";
    std::filesystem::create_directories(served);
    git(world.temp.path(),
        {"clone", "-q", "--bare", world.root.string(), (served / "r.git").string()});
    git(served / "r.git", {"update-server-info"});
    const apogee::testing::GitHttpServer server{served, world.temp.path() / "served.log"};
    if (!server.running()) {
        SKIP("python3 cannot serve here");
    }
    const std::string asked = "127.0.0.1:" + std::to_string(server.port());
    const std::string other = "unasked.localhost:" + std::to_string(server.port());
    const std::string url = server.url("r.git");
    world.gate.allow = true;

    struct Planted {
        std::string name;
        std::string config;
        std::string said;
    };

    const std::filesystem::path included = world.root / "included.config";
    std::ofstream{included, std::ios::binary} << "[http]\n\textraHeader = Host: " << other << "\n";
    const std::vector<Planted> planted{
        {"bare", "[http]\n\textraHeader = Host: " + other + "\n",
         "http.extraHeader to a Host header"},
        {"for the URL", "[http \"" + server.url("") + "\"]\n\textraHeader = Host: " + other + "\n",
         "http." + server.url("") + ".extraHeader to a Host header"},
        {"included", "[include]\n\tpath = " + config_quoted(included.generic_string()) + "\n",
         "http.extraHeader to a Host header"},
        {"a second line", "[http]\n\textraHeader = \"X-A: 1\r\\nHost: " + other + "\"\n",
         "http.extraHeader to a value holding a control character"},
        {"in the user agent", "[http]\n\tuserAgent = \"ua\r\\nHost: " + other + "\"\n",
         "http.userAgent to a value holding a control character"},
    };
    const std::vector<std::string> fetch_switches{"-c",
                                                  "protocol.allow=never",
                                                  "-c",
                                                  "protocol.http.allow=always",
                                                  "-c",
                                                  "http.followRedirects=false",
                                                  "-c",
                                                  "transfer.bundleURI=false",
                                                  "-c",
                                                  "fetch.uriProtocols=",
                                                  "fetch",
                                                  "--no-recurse-submodules",
                                                  "--no-tags",
                                                  "--no-prune",
                                                  "--no-auto-maintenance",
                                                  "--end-of-options",
                                                  url,
                                                  "+refs/heads/main:refs/remotes/o/main"};
    int at = 0;
    for (const Planted& plant : planted) {
        INFO(plant.name);
        const std::filesystem::path x = world.root / ("h" + std::to_string(at++));
        plant_repo(x, config_with(plant.config + "[remote \"o\"]\n\turl = " + url + "\n"));
        const std::size_t sent = server.requests().size();
        const ToolOutcome outcome = world.run("git_diff", {{"repo", x.filename().string()},
                                                           {"head", "main"},
                                                           {"base", "main"},
                                                           {"remote", "o"},
                                                           {"fetch", "always"}});
        INFO(outcome.content);
        CHECK(outcome.content.find("the repository's own config sets " + plant.said) !=
              std::string::npos);
        CHECK(server.requests().size() == sent);
        CHECK_FALSE(holds(x, "refs/remotes/o/main"));
        CHECK(world.gate.hosts.empty());
        // The control: plain git with the fetch's own switches sends the
        // planted Host to the asked host's address.
        plain_git(x, fetch_switches);
        const std::vector<std::string> named = server.hosts();
        REQUIRE(named.size() > sent);
        CHECK(std::any_of(
            named.begin() + static_cast<std::ptrdiff_t>(sent), named.end(),
            [&other](const std::string& hosts) { return hosts.find(other) != std::string::npos; }));
    }

    // The header actions/checkout leaves in a repository's own config -- a
    // credential for the host asked about -- is sent there, and the fetch runs.
    const std::filesystem::path ci = world.root / "ci";
    plant_repo(ci, config_with("[http \"" + server.url("") +
                               "\"]\n\textraHeader = AUTHORIZATION: basic eC1hY2Nlc3M=\n[remote "
                               "\"o\"]\n\turl = " +
                               url + "\n"));
    const std::size_t before = server.requests().size();
    world.gate.hosts.clear();
    const ToolOutcome fetched = world.run(
        "git_diff",
        {{"repo", "ci"}, {"head", "main"}, {"base", "main"}, {"remote", "o"}, {"fetch", "always"}});
    INFO(fetched.content);
    CHECK_FALSE(fetched.is_error);
    CHECK(fetched.content.find("Not fetched") == std::string::npos);
    CHECK(world.gate.hosts == std::vector<std::string>{"127.0.0.1"});
    CHECK(holds(ci, "refs/remotes/o/main"));
    const std::vector<std::string> named = server.hosts();
    REQUIRE(named.size() > before);
    for (auto host = named.begin() + static_cast<std::ptrdiff_t>(before); host != named.end();
         ++host) {
        CHECK(*host == asked);
    }
}

namespace {

/// Appends to `config` one section holding `count` harmless variables with
/// long names, so a `git config` listing of that section runs past the 256 KiB
/// the tools keep of a child's output.
void pad_config(const std::filesystem::path& config, const std::string& section,
                std::size_t count) {
    std::ofstream out{config, std::ios::binary | std::ios::app};
    out << "[" << section << "]\n";
    const std::string tail(120, 'x');
    for (std::size_t at = 0; at < count; ++at) {
        out << "\tpad" << at << tail << " = 1\n";
    }
}

/// Plain git's `config --show-scope --name-only --get-regexp <pattern>` in
/// `repo`, read whole -- `plain_git` keeps only the last 256 KiB too.
std::string whole_listing(const std::filesystem::path& repo, const std::string& pattern) {
    apogee::platform::ChildCommand plain;
    plain.program = "git";
    plain.arguments = {"-C",          repo.string(),  "config", "--show-scope",
                       "--name-only", "--get-regexp", pattern};
    return apogee::tools::run_to_completion(plain, std::chrono::seconds{30},
                                            apogee::tools::OutputLimit{.head = 4U << 20U})
        .out.text();
}

}  // namespace

TEST_CASE("a repository's own settings are read whole, so a long config hides no planted command",
          "[tools][git][review][sandbox][57x]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    // Found while fixing round 8's follow-up: a child's output is kept to its
    // last 256 KiB, so a config listing more than that lost its start, and a
    // planted command set before the padding went unseen -- the fetch's
    // core.sshCommand, and 57o's filter driver git_diff switches off.
    FetchWorld world;
    world.gate.allow = true;
    const std::filesystem::path x = world.root / "x";
    plant_repo(x, config_with("[core]\n\tsshCommand = touch " + world.mark("SSH_RAN") +
                              "; false\n[remote \"o\"]\n\turl = ssh://example.invalid/r.git\n"));
    pad_config(x / ".git" / "config", "core", 2500);
    const std::string names = whole_listing(x, "^(core|credential)\\.");
    // Armed: the listing runs past what the tools keep, the key first in it.
    CHECK(names.size() > std::size_t{256} * 1024);
    CHECK(names.find("local\tcore.sshcommand\n") != std::string::npos);
    const ToolOutcome fetched =
        world.run("git_diff", {{"repo", "x"}, {"head", "main"}, {"remote", "o"}});
    INFO(fetched.content);
    CHECK(fetched.is_error);
    CHECK(fetched.content.find("lists more than the tools read whole") != std::string::npos);
    CHECK(markers(world.marks).empty());
    CHECK(world.gate.hosts.empty());
    // The control: plain git's fetch runs it.
    plain_git(x, {"-c", "core.hooksPath=/dev/null", "fetch", "o", "main"});
    CHECK(markers(world.marks) == std::vector<std::string>{"SSH_RAN"});
    world.clear_marks();

    // 57o's filter listing, the same way: a driver git_diff must switch off.
    const std::filesystem::path y = world.root / "y";
    make_repo(y, "y.txt", "y\n", "y");
    std::ofstream{y / ".git" / "config", std::ios::binary | std::ios::app}
        << "[filter \"evil\"]\n\tclean = touch " << world.mark("FILTER_RAN") << "; cat\n";
    pad_config(y / ".git" / "config", "filter \"pad\"", 2500);
    std::ofstream{y / ".gitattributes", std::ios::binary} << "*.txt filter=evil\n";
    std::ofstream{y / "y.txt", std::ios::binary} << "changed\n";
    const std::string drivers = whole_listing(y, "^filter\\.");
    CHECK(drivers.size() > std::size_t{256} * 1024);
    CHECK(drivers.find("local\tfilter.evil.clean\n") != std::string::npos);
    const ToolOutcome diffed = world.run("git_diff", {{"repo", "y"}});
    INFO(diffed.content);
    CHECK(diffed.is_error);
    CHECK(diffed.content.find("lists more than the tools read whole") != std::string::npos);
    CHECK(markers(world.marks).empty());
    // The control: plain git's diff runs the driver.
    plain_git(y, {"-c", "core.hooksPath=/dev/null", "diff"});
    CHECK(markers(world.marks) == std::vector<std::string>{"FILTER_RAN"});
}

TEST_CASE("the review fetch's protocol fence names every transport the installed git ships",
          "[tools][git][review][57x]") {
    if (!have_git()) {
        SKIP("git is not available");
    }
    // A repository's own `protocol.<name>.allow` outranks the fetch's bare
    // `-c protocol.allow=never`, so the fence must name each transport git
    // can use: its built-in file, git and ssh, and every `git-remote-*`
    // helper in its exec path (git 2.53 ships ext, fd, ftp, ftps, http and
    // https). A helper the fence missed would stay the repository's to allow.
    std::set<std::string> shipped{"file", "git", "ssh"};
    const std::filesystem::path exec_path{first_line(plain_git(".", {"--exec-path"}))};
    INFO("git's exec path: " << exec_path.string());
    std::size_t helpers = 0;
    std::error_code code;
    for (const auto& entry : std::filesystem::directory_iterator{exec_path, code}) {
        std::string name = entry.path().filename().string();
        if (name.ends_with(".exe")) {
            name.resize(name.size() - 4);
        }
        constexpr std::string_view prefix = "git-remote-";
        if (name.starts_with(prefix)) {
            shipped.insert(name.substr(prefix.size()));
            ++helpers;
        }
    }
    // Not vacuous: the exec path was read, and it holds git's helpers.
    REQUIRE(helpers > 0);
    const std::span<const std::string_view> fenced = apogee::tools::fetch_fence_transports();
    for (const std::string& transport : shipped) {
        INFO("shipped transport: " << transport);
        CHECK(std::ranges::find(fenced, std::string_view{transport}) != fenced.end());
    }
}

// ---------------------------------------------------------------------------
// 57y: the whole git folder is judged, so no link anywhere git reads leads the
// tools out of the root. Each case plants what the probe planted (2026-11-15),
// shows plain git reading through it, then the tools refusing.
// ---------------------------------------------------------------------------

namespace {

/// Plain git's stdout and stderr together, its exit status ignored, under
/// the test's own environment: the control showing git reading through a
/// planted link, its errors included -- `fatal: bad shallow line` is one.
std::string plain_git_said(const std::filesystem::path& repo,
                           const std::vector<std::string>& arguments) {
    apogee::platform::ChildCommand plain;
    plain.program = "git";
    plain.arguments = {"-C", repo.string()};
    plain.arguments.insert(plain.arguments.end(), arguments.begin(), arguments.end());
    const apogee::tools::ProcessOutcome outcome =
        apogee::tools::run_to_completion(plain, std::chrono::seconds{30});
    return outcome.out.text() + outcome.err.text();
}

/// The probe's world (57y): the launch repository `proj` (the root), a
/// repository `x` inside it, an outside repository whose one commit is
/// `OUTSIDE SUBJECT` with the file `OUTSIDE SECRET TEXT`, its objects loose,
/// and a plain outside file. Git's global and system config are the test's
/// own.
struct LinkWorld {
    apogee::testing::EnvGuard no_global{"GIT_CONFIG_GLOBAL", "/dev/null"};
    apogee::testing::EnvGuard no_system{"GIT_CONFIG_SYSTEM", "/dev/null"};
    apogee::testing::TempDir temp{"tools-git-links-" + std::to_string(std::random_device{}())};
    std::filesystem::path root = temp.path() / "proj";
    std::filesystem::path x = root / "x";
    std::filesystem::path outside = temp.path() / "outside";
    std::filesystem::path plain_file = temp.path() / "outside.txt";
    std::string commit;
    std::string tree;
    std::string blob;

    LinkWorld() {
        make_repo(root, "README.md", "launch\n", "launch");
        make_repo(x, "x.txt", "x\n", "x commit");
        make_repo(outside, "secret.txt", "OUTSIDE SECRET TEXT\n", "OUTSIDE SUBJECT");
        commit = first_line(git(outside, {"rev-parse", "HEAD"}));
        tree = first_line(git(outside, {"rev-parse", "HEAD^{tree}"}));
        blob = first_line(git(outside, {"rev-parse", "HEAD:secret.txt"}));
        std::ofstream{plain_file, std::ios::binary} << "OUTSIDE LINE ONE\nOUTSIDE LINE TWO\n";
    }

    /// The outside object's loose file.
    [[nodiscard]] std::filesystem::path outside_object(const std::string& object) const {
        return outside / ".git" / "objects" / object.substr(0, 2) / object.substr(2);
    }

    /// Links the outside `object`'s loose file into `store` under its own id,
    /// as the probe did; returns the link.
    std::filesystem::path link_object(const std::filesystem::path& store,
                                      const std::string& object) const {
        const std::filesystem::path link = store / object.substr(0, 2) / object.substr(2);
        std::filesystem::create_directories(link.parent_path());
        const apogee::testing::LinkMade made =
            apogee::testing::make_file_link(outside_object(object), link);
        INFO(made.why);
        REQUIRE(made.made);
        return link;
    }

    /// Makes `link` a link to the file `target`.
    static void link_file(const std::filesystem::path& target, const std::filesystem::path& link) {
        std::filesystem::create_directories(link.parent_path());
        const apogee::testing::LinkMade made = apogee::testing::make_file_link(target, link);
        INFO(made.why);
        REQUIRE(made.made);
    }

    /// Makes `link` a link to the folder `target`.
    static void link_folder(const std::filesystem::path& target,
                            const std::filesystem::path& link) {
        std::filesystem::create_directories(link.parent_path());
        const apogee::testing::LinkMade made = apogee::testing::make_directory_link(target, link);
        INFO(made.why);
        REQUIRE(made.made);
    }
};

/// Every tool in `tools_run` refuses `arguments` in 57o's words, naming the
/// link `named`, and no answer holds any of `hidden`.
void refused_naming(const Confined& tools, const nlohmann::json& arguments,
                    const std::string& named, const std::vector<std::string>& hidden,
                    const std::vector<std::string>& tools_run = {"git_status", "git_log",
                                                                 "git_diff", "git_show"}) {
    for (const std::string& tool : tools_run) {
        INFO(tool << " " << arguments.dump());
        const ToolOutcome outcome = tools.run(tool, arguments);
        INFO(outcome.content);
        CHECK(outcome.is_error);
        CHECK(outcome.content.find("outside the allowed root") != std::string::npos);
        CHECK(outcome.content.find(named + ", a link that leads out of it") != std::string::npos);
        for (const std::string& text : hidden) {
            CHECK(outcome.content.find(text) == std::string::npos);
        }
    }
}

/// Every tool refuses `arguments` in 57o's words with `reason`, and no
/// answer holds any of `hidden`.
void refused_with(const Confined& tools, const nlohmann::json& arguments, const std::string& reason,
                  const std::vector<std::string>& hidden) {
    for (const char* tool : {"git_status", "git_log", "git_diff", "git_show"}) {
        INFO(tool << " " << arguments.dump());
        const ToolOutcome outcome = tools.run(tool, arguments);
        INFO(outcome.content);
        CHECK(outcome.is_error);
        CHECK(outcome.content.find("outside the allowed root") != std::string::npos);
        CHECK(outcome.content.find(reason) != std::string::npos);
        for (const std::string& text : hidden) {
            CHECK(outcome.content.find(text) == std::string::npos);
        }
    }
}

/// The name a refusal gives a loose object's link: its folder and file.
std::string object_link_name(const std::string& object) {
    return object.substr(0, 2) + "/" + object.substr(2);
}

}  // namespace

TEST_CASE("a loose object linked out of the root is refused, wherever the store holds it",
          "[tools][git][sandbox][57y]") {
    if (!have_git()) {
        SKIP("git is not available, or this platform starts no child process to run it");
    }
    // The probe's loose-object finding: the outside commit, tree and blob
    // linked under their own ids, and `git_show{ref:<commit>}` printed
    // `OUTSIDE SUBJECT`, `git_diff{target:<commit>}` the outside text.
    const LinkWorld world;
    const Confined tools{world.root};
    const std::vector<std::string> hidden{"OUTSIDE SUBJECT", "OUTSIDE SECRET TEXT"};

    SECTION("in a repository the model names") {
        const std::filesystem::path store = world.x / ".git" / "objects";
        world.link_object(store, world.commit);
        world.link_object(store, world.tree);
        world.link_object(store, world.blob);
        // The control: plain git reads the outside commit and file through them.
        CHECK(plain_git_said(world.x, {"show", "-s", "--format=%s", world.commit})
                  .find("OUTSIDE SUBJECT") != std::string::npos);
        CHECK(plain_git_said(world.x, {"show", world.blob}).find("OUTSIDE SECRET TEXT") !=
              std::string::npos);
        refused_naming(tools, {{"repo", "x"}}, "", hidden);
        refused_naming(tools, {{"repo", "x"}, {"ref", world.commit}}, "", hidden, {"git_show"});
        refused_naming(tools, {{"repo", "x"}, {"target", world.commit}}, "", hidden, {"git_diff"});
    }
    SECTION("one linked blob is enough, and it is named") {
        world.link_object(world.x / ".git" / "objects", world.blob);
        CHECK(plain_git_said(world.x, {"show", world.blob}).find("OUTSIDE SECRET TEXT") !=
              std::string::npos);
        refused_naming(tools, {{"repo", "x"}, {"ref", world.blob}}, object_link_name(world.blob),
                       hidden, {"git_show"});
    }
    SECTION("in the launch repository, with no repo named") {
        world.link_object(world.root / ".git" / "objects", world.commit);
        CHECK(
            plain_git_said(world.root, {"cat-file", "-p", world.commit}).find("OUTSIDE SUBJECT") !=
            std::string::npos);
        refused_naming(tools, nlohmann::json::object(), object_link_name(world.commit), hidden);
    }
}

TEST_CASE("a file git_status reads, linked to any outside file, is refused and never shown",
          "[tools][git][sandbox][57y]") {
    if (!have_git()) {
        SKIP("git is not available, or this platform starts no child process to run it");
    }
    // The probe's state-file finding: no object or id needed, any file the
    // person can read. `BISECT_START` linked out made `git_status` answer
    // `started from branch '<the file>'`, the file whole.
    const LinkWorld world;
    const Confined tools{world.root};
    const std::filesystem::path dot_git = world.x / ".git";
    const std::vector<std::string> hidden{"OUTSIDE LINE"};

    SECTION("BISECT_START") {
        LinkWorld::link_file(world.plain_file, dot_git / "BISECT_START");
        std::ofstream{dot_git / "BISECT_LOG", std::ios::binary} << "";
        CHECK(plain_git_said(world.x, {"status"}).find("OUTSIDE LINE TWO") != std::string::npos);
        refused_naming(tools, {{"repo", "x"}}, "BISECT_START", hidden);
    }
    SECTION("rebase-merge/head-name") {
        LinkWorld::link_file(world.plain_file, dot_git / "rebase-merge" / "head-name");
        std::ofstream{dot_git / "rebase-merge" / "onto", std::ios::binary}
            << git(world.x, {"rev-parse", "HEAD"});
        CHECK(plain_git_said(world.x, {"status"}).find("OUTSIDE LINE TWO") != std::string::npos);
        refused_naming(tools, {{"repo", "x"}}, "head-name", hidden);
    }
    SECTION("shallow, which every tool reads") {
        LinkWorld::link_file(world.plain_file, dot_git / "shallow");
        CHECK(plain_git_said(world.x, {"log"}).find("bad shallow line: OUTSIDE LINE ONE") !=
              std::string::npos);
        refused_naming(tools, {{"repo", "x"}}, "shallow", hidden);
    }
    SECTION("config") {
        const std::filesystem::path config = world.temp.path() / "outside-config";
        std::ofstream{config, std::ios::binary}
            << "[core]\n\trepositoryformatversion = 0\n\tbare = false\n"
               "[user]\n\tname = OUTSIDE NAME\n";
        std::filesystem::remove(dot_git / "config");
        LinkWorld::link_file(config, dot_git / "config");
        CHECK(plain_git_said(world.x, {"config", "user.name"}).find("OUTSIDE NAME") !=
              std::string::npos);
        refused_naming(tools, {{"repo", "x"}}, "config", {"OUTSIDE NAME"});
    }
}

TEST_CASE("a ref linked out of the root is refused, and no outside id is shown",
          "[tools][git][sandbox][57y]") {
    if (!have_git()) {
        SKIP("git is not available, or this platform starts no child process to run it");
    }
    // The probe's pseudo-ref finding: `CHERRY_PICK_HEAD` linked to an outside
    // ref file made `git_status` print the outside commit's id, and the
    // tools' own errors then named the tree and blob to link next.
    const LinkWorld world;
    const Confined tools{world.root};
    const std::filesystem::path outside_ref = world.outside / ".git" / "refs" / "heads" / "main";
    const std::string shown_id = world.commit.substr(0, 7);

    SECTION("CHERRY_PICK_HEAD") {
        LinkWorld::link_file(outside_ref, world.x / ".git" / "CHERRY_PICK_HEAD");
        CHECK(plain_git_said(world.x, {"status"}).find(shown_id) != std::string::npos);
        refused_naming(tools, {{"repo", "x"}}, "CHERRY_PICK_HEAD", {shown_id});
    }
    SECTION("a branch under refs/heads") {
        LinkWorld::link_file(outside_ref, world.x / ".git" / "refs" / "heads" / "stolen");
        CHECK(plain_git_said(world.x, {"rev-parse", "refs/heads/stolen"}).find(world.commit) !=
              std::string::npos);
        refused_naming(tools, {{"repo", "x"}}, "stolen", {shown_id});
    }
    // Two reftable repositories, `t` in the root and one outside holding
    // `outside-branch`; false where this git makes none (before 2.45 it knows
    // no --ref-format, and says so as an unknown option, not a fatal error).
    const std::filesystem::path table = world.root / "t";
    const std::filesystem::path outside_table = world.temp.path() / "outside-table";
    const auto make_tables = [&]() {
        std::filesystem::create_directories(table);
        std::filesystem::create_directories(outside_table);
        apogee::platform::ChildCommand init;
        init.program = "git";
        init.arguments = {"-C", table.string(), "init", "-q", "--ref-format=reftable",
                          "-b", "main"};
        if (apogee::tools::run_to_completion(init, std::chrono::seconds{30})
                .exit_code.value_or(1) != 0) {
            return false;
        }
        git(outside_table, {"init", "-q", "--ref-format=reftable", "-b", "outside-branch"});
        std::ofstream{outside_table / "o.txt", std::ios::binary} << "o\n";
        git(outside_table, {"add", "."});
        git(outside_table, {"commit", "-q", "-m", "outside table commit"});
        std::ofstream{table / "t.txt", std::ios::binary} << "t\n";
        git(table, {"add", "."});
        git(table, {"commit", "-q", "-m", "table commit"});
        return true;
    };

    SECTION("reftable, where this git makes one") {
        if (!make_tables()) {
            SKIP("this git makes no reftable repository");
        }
        std::filesystem::remove_all(table / ".git" / "reftable");
        LinkWorld::link_folder(outside_table / ".git" / "reftable", table / ".git" / "reftable");
        CHECK(plain_git_said(table, {"branch"}).find("outside-branch") != std::string::npos);
        refused_naming(tools, {{"repo", "t"}}, "reftable", {"outside-branch"});
    }
    SECTION("a reftable stack naming a table outside its folder") {
        // The second review's probe: `tables.list` lines are opened in the
        // folder as written, so `../../..` reached an outside repository's
        // table, as an alternates line would its objects.
        if (!make_tables()) {
            SKIP("this git makes no reftable repository");
        }
        std::string first_table;
        {
            std::ifstream in{outside_table / ".git" / "reftable" / "tables.list", std::ios::binary};
            std::getline(in, first_table);
        }
        REQUIRE_FALSE(first_table.empty());
        const std::filesystem::path stack = table / ".git" / "reftable" / "tables.list";
        std::string own;
        {
            std::ifstream in{stack, std::ios::binary};
            own.assign(std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{});
        }
        const std::string outward =
            std::filesystem::relative(outside_table / ".git" / "reftable" / first_table,
                                      table / ".git" / "reftable")
                .generic_string();
        std::ofstream{stack, std::ios::binary} << outward << "\n" << own;
        CHECK(plain_git_said(table, {"branch"}).find("outside-branch") != std::string::npos);
        refused_with(tools, {{"repo", "t"}}, "tables.list that leaves its folder",
                     {"outside-branch"});
    }
}

TEST_CASE("a store reached inside the root is walked too: an alternate's, a folder link's",
          "[tools][git][sandbox][57y]") {
    if (!have_git()) {
        SKIP("git is not available, or this platform starts no child process to run it");
    }
    // The probe's finding around the one-level scan: a link that lands
    // inside the root was followed, and nothing looked in where it landed.
    const LinkWorld world;
    const Confined tools{world.root};
    const std::vector<std::string> hidden{"OUTSIDE SUBJECT", "OUTSIDE SECRET TEXT"};

    SECTION("an alternate store inside the root holding a loose link out") {
        const std::filesystem::path shared = world.root / "shared-store";
        world.link_object(shared, world.commit);
        std::filesystem::create_directories(world.x / ".git" / "objects" / "info");
        std::ofstream{world.x / ".git" / "objects" / "info" / "alternates", std::ios::binary}
            << shared.generic_string() << "\n";
        CHECK(plain_git_said(world.x, {"cat-file", "-p", world.commit}).find("OUTSIDE SUBJECT") !=
              std::string::npos);
        refused_naming(tools, {{"repo", "x"}}, object_link_name(world.commit), hidden);
    }
    SECTION("an object folder linked to a folder inside the root holding a loose link out") {
        // The folder the link lands on holds what x's own folder of that
        // name held, plus the outside commit.
        const std::string prefix = world.commit.substr(0, 2);
        const std::filesystem::path own = world.x / ".git" / "objects" / prefix;
        const std::filesystem::path landing = world.root / "landing";
        std::filesystem::create_directories(landing);
        if (std::filesystem::exists(own)) {
            for (const auto& entry : std::filesystem::directory_iterator{own}) {
                std::filesystem::rename(entry.path(), landing / entry.path().filename());
            }
            std::filesystem::remove(own);
        }
        LinkWorld::link_file(world.outside_object(world.commit), landing / world.commit.substr(2));
        LinkWorld::link_folder(landing, own);
        CHECK(plain_git_said(world.x, {"cat-file", "-p", world.commit}).find("OUTSIDE SUBJECT") !=
              std::string::npos);
        refused_naming(tools, {{"repo", "x"}}, world.commit.substr(2), hidden);
    }
}

TEST_CASE("the review fetch from a folder remote whose store links out copies nothing in",
          "[tools][git][review][sandbox][57y]") {
    if (!have_git()) {
        SKIP("git is not available, or this platform starts no child process to run it");
    }
    // The probe's laundering: a folder remote inside the root, its loose
    // objects linked out and a branch naming the outside commit, and the
    // review fetch copied the commit, tree and blob in as real files, which
    // outlived the links.
    FetchWorld world;
    const std::filesystem::path up = world.inside_remote("r.git");
    const std::string tree = first_line(git(world.outside, {"rev-parse", "HEAD^{tree}"}));
    const std::string blob = first_line(git(world.outside, {"rev-parse", "HEAD:secret.txt"}));
    for (const std::string& object : {world.outside_head, tree, blob}) {
        const std::filesystem::path link = up / "objects" / object.substr(0, 2) / object.substr(2);
        std::filesystem::create_directories(link.parent_path());
        const apogee::testing::LinkMade made = apogee::testing::make_file_link(
            world.outside / ".git" / "objects" / object.substr(0, 2) / object.substr(2), link);
        INFO(made.why);
        REQUIRE(made.made);
    }
    std::filesystem::create_directories(up / "refs" / "heads");
    std::ofstream{up / "refs" / "heads" / "evil", std::ios::binary} << world.outside_head << "\n";
    git(world.root, {"remote", "add", "o", up.generic_string()});

    const ToolOutcome diff = world.run(
        "git_diff", {{"head", "evil"}, {"base", "main"}, {"remote", "o"}, {"fetch", "always"}});
    INFO(diff.content);
    // The branch is the remote's alone, so the review stops at the fetch's
    // refusal, which names the link.
    CHECK(diff.is_error);
    CHECK(diff.content.find("it was not fetched from 'o'") != std::string::npos);
    CHECK(diff.content.find(", a link that leads out of it") != std::string::npos);
    CHECK(diff.content.find("OUTSIDE SECRET TEXT") == std::string::npos);
    for (const std::string& object : {world.outside_head, tree, blob}) {
        INFO(object);
        CHECK_FALSE(holds(world.root, object));
    }
    CHECK(world.gate.hosts.empty());
    // The control: plain git's fetch copies all three in.
    plain_git(world.root, {"-c", "protocol.file.allow=always", "fetch", "o", "evil"});
    CHECK(holds(world.root, world.outside_head));
    CHECK(holds(world.root, blob));
}

TEST_CASE("hooks, lfs and annex linked out, links landing inside and a loop of them still read",
          "[tools][git][sandbox][57y]") {
    if (!have_git()) {
        SKIP("git is not available, or this platform starts no child process to run it");
    }
    // What the walk leaves alone: the three folders the tools never read,
    // which people link to their own hooks or a store on another disk, and
    // every link that lands inside the root -- walked where it lands, each
    // folder once, so a loop of them ends.
    const LinkWorld world;
    const Confined tools{world.root};
    const auto reads = [&](const nlohmann::json& arguments, const std::string& expected) {
        for (const char* tool : {"git_status", "git_log", "git_diff", "git_show"}) {
            INFO(tool << " " << arguments.dump());
            const ToolOutcome outcome = tools.run(tool, arguments);
            INFO(outcome.content);
            CHECK_FALSE(outcome.is_error);
        }
        CHECK(tools.run("git_log", arguments).content.find(expected) != std::string::npos);
    };
    const std::filesystem::path dot_git = world.x / ".git";

    SECTION("hooks, lfs and annex linked out") {
        for (const char* name : {"hooks", "lfs", "annex"}) {
            const std::filesystem::path theirs = world.temp.path() / ("their-" + std::string{name});
            std::filesystem::create_directories(theirs);
            std::ofstream{theirs / "kept", std::ios::binary} << "kept\n";
            std::filesystem::remove_all(dot_git / name);
            LinkWorld::link_folder(theirs, dot_git / name);
        }
        reads({{"repo", "x"}}, "x commit");
    }
    SECTION("links that land inside the root") {
        // The refs moved to a folder inside the root and linked back, and the
        // description a link to a file inside it.
        const std::filesystem::path moved = world.root / "moved-refs";
        std::filesystem::rename(dot_git / "refs", moved);
        LinkWorld::link_folder(moved, dot_git / "refs");
        std::filesystem::remove(dot_git / "description");
        LinkWorld::link_file(world.root / "README.md", dot_git / "description");
        reads({{"repo", "x"}}, "x commit");
    }
    SECTION("a loop of folder links inside the root ends, and the repository reads") {
        std::filesystem::create_directories(dot_git / "extra");
        LinkWorld::link_folder(dot_git / "extra", dot_git / "extra" / "again");
        LinkWorld::link_folder(dot_git, dot_git / "extra" / "up");
        LinkWorld::link_folder(dot_git / "objects", dot_git / "extra" / "store");
        reads({{"repo", "x"}}, "x commit");
    }
}

namespace {

/// Takes the read permission off `folder` (0311) for its life -- git can
/// still enter it and open a file in it by name, as a listing cannot -- and
/// gives it back after, so the temporary folder can be removed.
class Unlistable {
public:
    explicit Unlistable(std::filesystem::path folder) : folder_{std::move(folder)} {
        using std::filesystem::perms;
        std::filesystem::permissions(
            folder_,
            perms::owner_write | perms::owner_exec | perms::group_exec | perms::others_exec,
            std::filesystem::perm_options::replace);
    }

    ~Unlistable() {
        using std::filesystem::perms;
        std::error_code code;
        std::filesystem::permissions(folder_,
                                     perms::owner_all | perms::group_read | perms::group_exec |
                                         perms::others_read | perms::others_exec,
                                     std::filesystem::perm_options::replace, code);
    }

    Unlistable(const Unlistable&) = delete;
    Unlistable& operator=(const Unlistable&) = delete;
    Unlistable(Unlistable&&) = delete;
    Unlistable& operator=(Unlistable&&) = delete;

    /// Whether the folder really cannot be listed: a superuser lists anyway.
    [[nodiscard]] bool holds() const {
        std::error_code code;
        const std::filesystem::directory_iterator it{folder_, code};
        return static_cast<bool>(code);
    }

private:
    std::filesystem::path folder_;
};

}  // namespace

TEST_CASE("a folder in the git folder that cannot be listed is refused, as git still opens it",
          "[tools][git][sandbox][57y]") {
    if (!have_git()) {
        SKIP("git is not available, or this platform starts no child process to run it");
    }
    // The review's finding: a folder git can enter but nobody can list (0311)
    // listed as empty, so the walk passed what git then opened by name --
    // 57o's own refusal of objects linked out came back as a read.
    const LinkWorld world;
    const Confined tools{world.root};
    const std::vector<std::string> hidden{"OUTSIDE SUBJECT", "OUTSIDE SECRET TEXT"};

    SECTION("the git folder itself, its objects linked out") {
        std::filesystem::remove_all(world.x / ".git" / "objects");
        LinkWorld::link_folder(world.outside / ".git" / "objects", world.x / ".git" / "objects");
        const Unlistable shut{world.x / ".git"};
        if (!shut.holds()) {
            SKIP("this user lists a folder with no read permission (a superuser)");
        }
        CHECK(plain_git_said(world.x, {"cat-file", "-p", world.commit}).find("OUTSIDE SUBJECT") !=
              std::string::npos);
        refused_with(tools, {{"repo", "x"}}, ".git, a folder that cannot be listed", hidden);
    }
    SECTION("an object folder holding a loose link out") {
        const std::filesystem::path link =
            world.link_object(world.x / ".git" / "objects", world.blob);
        const Unlistable shut{link.parent_path()};
        if (!shut.holds()) {
            SKIP("this user lists a folder with no read permission (a superuser)");
        }
        CHECK(plain_git_said(world.x, {"show", world.blob}).find("OUTSIDE SECRET TEXT") !=
              std::string::npos);
        refused_with(tools, {{"repo", "x"}},
                     world.blob.substr(0, 2) + ", a folder that cannot be listed", hidden);
    }
}

TEST_CASE(
    "a path git opens is judged as written: a trailing space or carriage return borrows nothing",
    "[tools][git][sandbox][57y]") {
    if (!have_git()) {
        SKIP("git is not available, or this platform starts no child process to run it");
    }
    // The review's finding: the file tools' resolver trims what a model
    // types, so `<root>/st ` was judged as `<root>/st` -- a missing sibling,
    // inside -- while git opened the link `st ` to an outside store.
    const LinkWorld world;
    const Confined tools{world.root};
    const std::vector<std::string> hidden{"OUTSIDE SUBJECT", "OUTSIDE SECRET TEXT"};
    const std::filesystem::path alternates = world.x / ".git" / "objects" / "info" / "alternates";
    std::filesystem::create_directories(alternates.parent_path());
    const std::filesystem::path outside_store = world.outside / ".git" / "objects";

    SECTION("an alternates line naming a link that ends in a space") {
        LinkWorld::link_folder(outside_store, world.root / "st ");
        std::ofstream{alternates, std::ios::binary} << (world.root / "st ").generic_string()
                                                    << "\n";
        CHECK(plain_git_said(world.x, {"cat-file", "-p", world.commit}).find("OUTSIDE SUBJECT") !=
              std::string::npos);
        refused_with(tools, {{"repo", "x"}}, "objects borrowed from", hidden);
    }
    SECTION("a link in the git folder whose name ends in a space") {
        LinkWorld::link_folder(outside_store, alternates.parent_path() / "st ");
        std::ofstream{alternates, std::ios::binary} << "info/st \n";
        CHECK(plain_git_said(world.x, {"cat-file", "-p", world.commit}).find("OUTSIDE SUBJECT") !=
              std::string::npos);
        refused_with(tools, {{"repo", "x"}}, "st , a link that leads out of it", hidden);
    }
    SECTION("an alternates line ending in a carriage return") {
        LinkWorld::link_folder(outside_store, world.root / "st\r");
        std::ofstream{alternates, std::ios::binary} << (world.root / "st").generic_string()
                                                    << "\r\n";
        CHECK(plain_git_said(world.x, {"cat-file", "-p", world.commit}).find("OUTSIDE SUBJECT") !=
              std::string::npos);
        refused_with(tools, {{"repo", "x"}}, "objects borrowed from", hidden);
        // Said with the carriage return escaped, so the refusal never
        // rewrites the line it is shown on.
        const std::string said = tools.run("git_log", {{"repo", "x"}}).content;
        CHECK(said.find("st\\r") != std::string::npos);
        CHECK(said.find('\r') == std::string::npos);
    }
}

TEST_CASE("what the walk leaves out at the top is judged when git reads it by another name",
          "[tools][git][sandbox][57y]") {
    if (!have_git()) {
        SKIP("git is not available, or this platform starts no child process to run it");
    }
    const LinkWorld world;
    const Confined tools{world.root};
    const std::filesystem::path dot_git = world.x / ".git";
    const std::filesystem::path outside_ref = world.outside / ".git" / "refs" / "heads" / "main";

    SECTION("a link landing on the git folder reaches lfs as refs/x/lfs") {
        // The review's probe: `refs/x -> ..` and `lfs` linked to an outside
        // refs folder, and a CHERRY_PICK_HEAD naming `refs/x/lfs/heads/main`
        // had git_status print the outside commit's whole id.
        LinkWorld::link_folder(dot_git, dot_git / "refs" / "x");
        LinkWorld::link_folder(world.outside / ".git" / "refs", dot_git / "lfs");
        std::ofstream{dot_git / "CHERRY_PICK_HEAD", std::ios::binary}
            << "ref: refs/x/lfs/heads/main\n";
        git(world.x, {"config", "core.abbrev", "40"});
        CHECK(plain_git_said(world.x, {"status"}).find(world.commit) != std::string::npos);
        refused_with(tools, {{"repo", "x"}}, "lfs, a link that leads out of it", {world.commit});
    }
    SECTION("a file named lfs, a root ref on a case-insensitive disk") {
        LinkWorld::link_file(outside_ref, dot_git / "lfs");
        // The control holds where the disk ignores case (git reads `LFS`
        // there); elsewhere git reads no such ref, and the refusal stands.
        if (plain_git_said(world.x, {"rev-parse", "LFS"}).find(world.commit) == std::string::npos) {
            WARN("this disk tells LFS from lfs, so git reads no root ref through the link here");
        }
        refused_with(tools, {{"repo", "x"}}, "lfs, a link that leads out of it", {world.commit});
    }
    SECTION("a link named lfs whose target cannot be resolved, though the system opens it") {
        // The second review's probe: a relative link through a chain of
        // folders longer than realpath takes (macOS's 1,024 bytes) was taken
        // for a link to nothing and left out, while the system followed it and
        // git read the root ref `LFS`. Elsewhere it resolves, and is a link to
        // an outside file like the case above.
        // Made relative to `x`, as a person's shell makes it: an absolute
        // path that long is refused by the very limit probed.
        std::string chain;
        for (int step = 0; step < 85; ++step) {
            chain += (step == 0 ? "" : "/") + std::string{"dddddddddd"};
        }
        apogee::platform::ChildCommand make;
        make.program = "/bin/sh";
        make.arguments = {
            "-c", "mkdir -p \"$1\" && ln -s \"$2\" \"$1/leaf\" && ln -s \"../$1/leaf\" .git/lfs",
            "sh", chain, outside_ref.string()};
        make.working_directory = world.x.string();
        const apogee::tools::ProcessOutcome made =
            apogee::tools::run_to_completion(make, std::chrono::seconds{30});
        INFO(made.err.text());
        REQUIRE(made.exit_code.value_or(1) == 0);
        std::ofstream{dot_git / "CHERRY_PICK_HEAD", std::ios::binary} << "ref: LFS\n";
        if (plain_git_said(world.x, {"status"}).find(world.commit.substr(0, 7)) ==
            std::string::npos) {
            WARN("this disk tells LFS from lfs, so git reads no root ref through the link here");
        }
        refused_with(tools, {{"repo", "x"}}, "lfs, a link that leads out of it",
                     {world.commit.substr(0, 7)});
    }
}

TEST_CASE(
    "a submodule's git folder is walked as the superproject's is: hooks left out, refs judged",
    "[tools][git][sandbox][57y]") {
    if (!have_git()) {
        SKIP("git is not available, or this platform starts no child process to run it");
    }
    // `modules/` is walked (the item's decision: its refs linked out showed an
    // outside id through the superproject's git_diff), and each submodule's
    // own git folder leaves out what the superproject's does.
    const LinkWorld world;
    const Confined tools{world.root};
    const std::filesystem::path super = world.root / "super";
    make_repo(super, "p.txt", "p\n", "super commit");
    const std::filesystem::path source = world.temp.path() / "subsrc";
    make_repo(source, "s.txt", "s\n", "submodule commit");
    git(super,
        {"-c", "protocol.file.allow=always", "submodule", "add", "-q", source.string(), "sub"});
    git(super, {"commit", "-q", "-m", "add the submodule"});
    const std::filesystem::path module = super / ".git" / "modules" / "sub";
    REQUIRE(std::filesystem::is_regular_file(module / "HEAD"));

    SECTION("its hooks, lfs and annex linked out still read") {
        for (const char* name : {"hooks", "lfs", "annex"}) {
            const std::filesystem::path theirs =
                world.temp.path() / ("their-sub-" + std::string{name});
            std::filesystem::create_directories(theirs);
            std::filesystem::remove_all(module / name);
            LinkWorld::link_folder(theirs, module / name);
        }
        for (const char* tool : {"git_status", "git_log", "git_diff", "git_show"}) {
            INFO(tool);
            const ToolOutcome outcome = tools.run(tool, {{"repo", "super"}});
            INFO(outcome.content);
            CHECK_FALSE(outcome.is_error);
        }
    }
    SECTION("a submodule at a nested path, its hooks linked out, still reads") {
        // The second review's finding: a submodule's git folder is named
        // after its path, so `libs/foo` lives at `modules/libs/foo`.
        git(super, {"-c", "protocol.file.allow=always", "submodule", "add", "-q", source.string(),
                    "libs/foo"});
        git(super, {"commit", "-q", "-m", "add a nested submodule"});
        const std::filesystem::path nested = super / ".git" / "modules" / "libs" / "foo";
        REQUIRE(std::filesystem::is_regular_file(nested / "HEAD"));
        const std::filesystem::path theirs = world.temp.path() / "their-nested-hooks";
        std::filesystem::create_directories(theirs);
        std::filesystem::remove_all(nested / "hooks");
        LinkWorld::link_folder(theirs, nested / "hooks");
        for (const char* tool : {"git_status", "git_log", "git_diff", "git_show"}) {
            INFO(tool);
            const ToolOutcome outcome = tools.run(tool, {{"repo", "super"}});
            INFO(outcome.content);
            CHECK_FALSE(outcome.is_error);
        }
    }
    SECTION("its branch linked out is refused") {
        std::filesystem::remove(module / "refs" / "heads" / "main");
        LinkWorld::link_file(world.outside / ".git" / "refs" / "heads" / "main",
                             module / "refs" / "heads" / "main");
        // The control, with the tools' own submodule flags (a status inside
        // the submodule would fail on the missing object first).
        CHECK(plain_git_said(super,
                             {"diff", "HEAD", "--submodule=short", "--ignore-submodules=dirty"})
                  .find(world.commit) != std::string::npos);
        refused_with(tools, {{"repo", "super"}}, "main, a link that leads out of it",
                     {world.commit});
    }
}

// ---------------------------------------------------------------------------
// 57z: the git tools' last read routes out of the root -- a submodule git
// reads through the work tree, and a file the repository's own config names.
// Each case plants what the probe planted (2026-11-15) and shows plain git
// reading outside before the tools refuse.
// ---------------------------------------------------------------------------

namespace {

/// The superproject `super` in the root, its commit, and the submodule
/// helpers 57z's cases share.
struct SubmoduleWorld {
    LinkWorld world;
    std::filesystem::path super = world.root / "super";

    SubmoduleWorld() {
        make_repo(super, "p.txt", "p\n", "super commit");
    }

    /// An embedded repository at `super/<name>` added as a gitlink, its git
    /// folder kept in the work tree (no `.gitmodules` entry): what `git add`
    /// of a nested repository leaves.
    [[nodiscard]] std::filesystem::path embed(const std::string& name) const {
        const std::filesystem::path sub = super / name;
        make_repo(sub, "s.txt", "s\n", name + " commit");
        git(super, {"add", name});
        git(super, {"commit", "-q", "-m", "add " + name});
        return sub;
    }

    /// Plain git's diff with the tools' own submodule flags: the control that
    /// shows git reading the submodule's HEAD.
    [[nodiscard]] std::string plain_diff() const {
        return plain_git_said(
            super, {"diff", "--no-ext-diff", "--ignore-submodules=dirty", "--submodule=short"});
    }
};

}  // namespace

TEST_CASE("a submodule git reads through the work tree is confined as the repository is",
          "[tools][git][sandbox][57z]") {
    if (!have_git()) {
        SKIP("git is not available, or this platform starts no child process to run it");
    }
    // The probe's route 1: for a work-tree diff and for status, git reads
    // each populated submodule's HEAD through `<path>/.git`, which nothing
    // judged, so git_diff printed `+Subproject commit <the outside id>`.
    const SubmoduleWorld box;
    const LinkWorld& world = box.world;
    const Confined tools{world.root};
    const std::filesystem::path outside_ref = world.outside / ".git" / "refs" / "heads" / "main";
    const auto refused_both = [&](const nlohmann::json& arguments, const Confined& on) {
        for (const char* tool : {"git_diff", "git_status"}) {
            INFO(tool << " " << arguments.dump());
            const ToolOutcome outcome = on.run(tool, arguments);
            INFO(outcome.content);
            CHECK(outcome.is_error);
            CHECK(outcome.content.find("the submodule at 'sub' of ") != std::string::npos);
            CHECK(outcome.content.find("outside the allowed root") != std::string::npos);
            CHECK(outcome.content.find(world.commit) == std::string::npos);
            CHECK(outcome.content.find(world.commit.substr(0, 7)) == std::string::npos);
        }
    };

    SECTION("an embedded git folder whose branch is linked out") {
        const std::filesystem::path sub = box.embed("sub");
        std::filesystem::remove(sub / ".git" / "refs" / "heads" / "main");
        LinkWorld::link_file(outside_ref, sub / ".git" / "refs" / "heads" / "main");
        CHECK(box.plain_diff().find("+Subproject commit " + world.commit) != std::string::npos);
        refused_both({{"repo", "super"}}, tools);
        // Launched in the superproject with no repo named, the same; and
        // named by a folder below its top, where git lists only that part.
        refused_both(nlohmann::json::object(), Confined{box.super});
        std::filesystem::create_directories(box.super / "d");
        std::ofstream{box.super / "d" / "k.txt", std::ios::binary} << "k\n";
        git(box.super, {"add", "d"});
        git(box.super, {"commit", "-q", "-m", "a folder"});
        refused_both({{"repo", "super/d"}}, tools);
    }
    SECTION("a split index, its gitlink in the shared index file") {
        // The index read for gitlinks before the listing runs: a split index
        // keeps its entries in a shared file beside it, so it is listed.
        const std::filesystem::path sub = box.embed("sub");
        git(box.super, {"update-index", "--split-index"});
        // Not vacuous: the gitlink's bytes are in a shared file, not the index.
        const auto holds_mode = [](const std::filesystem::path& file) {
            std::ifstream in{file, std::ios::binary};
            const std::string bytes{std::istreambuf_iterator<char>{in}, {}};
            return bytes.find(std::string{"\0\0\xe0\0", 4}) != std::string::npos;
        };
        bool shared_holds = false;
        for (const auto& entry : std::filesystem::directory_iterator{box.super / ".git"}) {
            if (entry.path().filename().string().starts_with("sharedindex.")) {
                shared_holds = shared_holds || holds_mode(entry.path());
            }
        }
        REQUIRE(shared_holds);
        REQUIRE_FALSE(holds_mode(box.super / ".git" / "index"));
        std::filesystem::remove(sub / ".git" / "refs" / "heads" / "main");
        LinkWorld::link_file(outside_ref, sub / ".git" / "refs" / "heads" / "main");
        CHECK(box.plain_diff().find("+Subproject commit " + world.commit) != std::string::npos);
        refused_both({{"repo", "super"}}, tools);
    }
    SECTION("a .git file naming an outside repository") {
        const std::filesystem::path sub = box.embed("sub");
        std::filesystem::remove_all(sub / ".git");
        std::ofstream{sub / ".git", std::ios::binary}
            << "gitdir: " << (world.outside / ".git").generic_string() << "\n";
        CHECK(box.plain_diff().find("+Subproject commit " + world.commit) != std::string::npos);
        refused_both({{"repo", "super"}}, tools);
    }
    SECTION("a .git link to an outside repository") {
        const std::filesystem::path sub = box.embed("sub");
        std::filesystem::remove_all(sub / ".git");
        LinkWorld::link_folder(world.outside / ".git", sub / ".git");
        CHECK(box.plain_diff().find("+Subproject commit " + world.commit) != std::string::npos);
        refused_both({{"repo", "super"}}, tools);
    }
    SECTION("its own config including an outside file") {
        // Git follows a submodule's own include: an outside file that does
        // not parse fails plain git (the control), named in its error. Up
        // to git 2.55 the superproject's work-tree diff reads the
        // submodule's whole config and fails on it. Git 2.56 opens a
        // submodule as a repository of its own and no longer loads that
        // config for the diff (CI's macOS runner, git 2.56.1, 2026-11-26),
        // so there the control is git run in the submodule, which loads it
        // on every version. One that parses is refused by the tools before
        // git runs, whichever git it is.
        const std::filesystem::path sub = box.embed("sub");
        const std::filesystem::path broken = world.temp.path() / "broken.cfg";
        std::ofstream{broken, std::ios::binary} << "[broken\n";
        git(sub, {"config", "include.path", broken.generic_string()});
        if (box.plain_diff().find("broken.cfg") == std::string::npos) {
            WARN("this git reads no submodule config for a work-tree diff: " << plain_git_said(
                     box.super, {"version"}));
            CHECK(plain_git_said(sub, {"status"}).find("broken.cfg") != std::string::npos);
        }
        const std::filesystem::path outside_cfg = world.temp.path() / "sub-extra.cfg";
        std::ofstream{outside_cfg, std::ios::binary} << "[core]\n\tabbrev = 12\n";
        // Edited as a file: reading the config would follow the broken include.
        git(world.temp.path(), {"config", "--file", (sub / ".git" / "config").string(),
                                "--replace-all", "include.path", outside_cfg.generic_string()});
        refused_both({{"repo", "super"}}, tools);
    }
    SECTION("the reads that never open a submodule's work tree still answer") {
        const std::filesystem::path sub = box.embed("sub");
        std::filesystem::remove(sub / ".git" / "refs" / "heads" / "main");
        LinkWorld::link_file(outside_ref, sub / ".git" / "refs" / "heads" / "main");
        const std::string first =
            first_line(git(box.super, {"rev-list", "--max-parents=0", "HEAD"}));
        for (const auto& [tool, arguments] : std::vector<std::pair<std::string, nlohmann::json>>{
                 {"git_log", {{"repo", "super"}}},
                 {"git_show", {{"repo", "super"}}},
                 {"git_diff", {{"repo", "super"}, {"target", first}}}}) {
            INFO(tool << " " << arguments.dump());
            const ToolOutcome outcome = tools.run(tool, arguments);
            INFO(outcome.content);
            CHECK_FALSE(outcome.is_error);
            CHECK(outcome.content.find(world.commit) == std::string::npos);
        }
    }
}

TEST_CASE(
    "a submodule whose git folder is inside the root, or vouched for from outside it, still reads",
    "[tools][git][sandbox][57z]") {
    if (!have_git()) {
        SKIP("git is not available, or this platform starts no child process to run it");
    }
    const SubmoduleWorld box;
    const LinkWorld& world = box.world;
    const std::filesystem::path source = world.temp.path() / "subsrc";
    make_repo(source, "s.txt", "s\n", "submodule commit");
    const auto reads = [](const Confined& on, const nlohmann::json& arguments) {
        for (const char* tool : {"git_status", "git_diff", "git_log", "git_show"}) {
            INFO(tool << " " << arguments.dump());
            const ToolOutcome outcome = on.run(tool, arguments);
            INFO(outcome.content);
            CHECK_FALSE(outcome.is_error);
        }
    };

    SECTION("an embedded submodule with new commits, and an absorbed one") {
        const std::filesystem::path sub = box.embed("sub");
        std::ofstream{sub / "s.txt", std::ios::binary} << "moved\n";
        git(sub, {"commit", "-q", "-am", "moved on"});
        git(box.super, {"-c", "protocol.file.allow=always", "submodule", "add", "-q",
                        source.string(), "absorbed"});
        git(box.super, {"commit", "-q", "-m", "add absorbed"});
        const Confined tools{world.root};
        reads(tools, {{"repo", "super"}});
        CHECK(tools.run("git_diff", {{"repo", "super"}}).content.find("+Subproject commit") !=
              std::string::npos);
    }
    SECTION("a submodule whose .git opens no repository reads as git reads it, unpopulated") {
        const std::filesystem::path sub = box.embed("sub");
        std::filesystem::remove_all(sub / ".git");
        std::ofstream{sub / ".git", std::ios::binary}
            << "gitdir: " << (box.super / "nowhere").generic_string() << "\n";
        CHECK(plain_git_said(box.super, {"status", "--ignore-submodules=dirty"}).find("fatal") ==
              std::string::npos);
        reads(Confined{world.root}, {{"repo", "super"}});
    }
    SECTION("a submodule outside the root, the launch in a folder beside it") {
        // The person's repository above the folder Apogee was started in: a
        // submodule outside that folder is theirs, whatever its .git names.
        const std::filesystem::path sub = box.embed("sub");
        std::filesystem::remove_all(sub / ".git");
        std::ofstream{sub / ".git", std::ios::binary}
            << "gitdir: " << (world.outside / ".git").generic_string() << "\n";
        std::filesystem::create_directories(box.super / "app");
        std::ofstream{box.super / "app" / "a.txt", std::ios::binary} << "a\n";
        git(box.super, {"add", "app"});
        git(box.super, {"commit", "-q", "-m", "app"});
        reads(Confined{box.super / "app"}, nlohmann::json::object());
    }
    SECTION(
        "a linked worktree's submodule, its git folder in the main repository outside the root") {
        // The main repository lives outside the root; the worktree is the
        // root, and its submodule's git folder sits in the main repository's
        // `worktrees/<id>/modules/`, vouched for by its `core.worktree`.
        const std::filesystem::path main = world.temp.path() / "main";
        make_repo(main, "m.txt", "m\n", "main commit");
        git(main,
            {"-c", "protocol.file.allow=always", "submodule", "add", "-q", source.string(), "sm"});
        git(main, {"commit", "-q", "-m", "add sm"});
        const std::filesystem::path worktree = world.temp.path() / "wt";
        git(main, {"worktree", "add", "-q", worktree.string()});
        git(worktree, {"-c", "protocol.file.allow=always", "submodule", "update", "--init", "-q"});
        REQUIRE(std::filesystem::is_regular_file(worktree / "sm" / ".git"));
        reads(Confined{worktree}, nlohmann::json::object());
    }
}

TEST_CASE(
    "a file the repository's own config names for git to open is judged like an alternates line",
    "[tools][git][sandbox][57z]") {
    if (!have_git()) {
        SKIP("git is not available, or this platform starts no child process to run it");
    }
    // The probe's route 2: the repository's own config named an outside
    // file -- an include, a mailmap, an excludes or attributes file -- and
    // git read it, `git_status` printing the outside config's value.
    const LinkWorld world;
    const Confined tools{world.root};
    const std::filesystem::path cfg = world.temp.path() / "cfgdir" / "config";
    std::filesystem::create_directories(cfg.parent_path());
    std::ofstream{cfg, std::ios::binary}
        << "[branch \"main\"]\n\tremote = origin\n\tmerge = refs/heads/OUTSIDE-SECRET-BRANCH\n";
    git(world.x, {"remote", "add", "origin", "https://example.invalid/r.git"});
    const std::vector<std::string> hidden{"OUTSIDE-SECRET-BRANCH"};
    const auto included = [&]() {
        CHECK(plain_git_said(world.x, {"status"}).find("OUTSIDE-SECRET-BRANCH") !=
              std::string::npos);
    };

    SECTION("include.path, absolute") {
        git(world.x, {"config", "include.path", cfg.generic_string()});
        included();
        refused_with(tools, {{"repo", "x"}}, "include.path", hidden);
    }
    SECTION("include.path, relative to the including file") {
        git(world.x, {"config", "include.path", "../../../cfgdir/config"});
        included();
        refused_with(tools, {{"repo", "x"}}, "include.path", hidden);
    }
    SECTION("includeIf on the git folder") {
        // git matches the real path of the git folder, links resolved.
        git(world.x, {"config",
                      "includeIf.gitdir:" +
                          std::filesystem::canonical(world.root).generic_string() + "/.path",
                      cfg.generic_string()});
        included();
        refused_with(tools, {{"repo", "x"}}, "includeif.gitdir:", hidden);
    }
    SECTION("includeIf on the branch") {
        git(world.x, {"config", "includeIf.onbranch:main.path", cfg.generic_string()});
        included();
        refused_with(tools, {{"repo", "x"}}, "includeif.onbranch:main.path", hidden);
    }
    SECTION("an include of a file inside the root that is a link out") {
        LinkWorld::link_file(cfg, world.root / "inroot.cfg");
        git(world.x, {"config", "include.path", "../../inroot.cfg"});
        included();
        refused_with(tools, {{"repo", "x"}}, "include.path", hidden);
    }
    SECTION("a nested include: a file inside the root including the outside one") {
        std::ofstream{world.x / "nested.cfg", std::ios::binary}
            << "[include]\n\tpath = " << cfg.generic_string() << "\n";
        git(world.x, {"config", "include.path", "../nested.cfg"});
        included();
        refused_with(tools, {{"repo", "x"}}, "nested.cfg", hidden);
    }
    SECTION("a relative include, the repository named by a folder below its top") {
        // git prints the origin relative to the work tree's top, wherever it
        // was started: judged from `x/sub/dir`, it would land inside.
        git(world.x, {"config", "include.path", "../../../cfgdir/config"});
        std::filesystem::create_directories(world.x / "sub" / "dir");
        std::ofstream{world.x / "sub" / "dir" / "k.txt", std::ios::binary} << "k\n";
        git(world.x, {"add", "sub"});
        git(world.x, {"commit", "-q", "-m", "sub"});
        CHECK(plain_git_said(world.x / "sub" / "dir", {"status"}).find("OUTSIDE-SECRET-BRANCH") !=
              std::string::npos);
        refused_with(tools, {{"repo", "x/sub/dir"}}, "include.path", hidden);
    }
    SECTION("a ~/ path under an empty HOME, which git reads as the filesystem's root") {
        const apogee::testing::EnvGuard empty_home{"HOME", ""};
        git(world.x, {"config", "include.path", "~" + cfg.generic_string()});
        included();
        refused_with(tools, {{"repo", "x"}}, "include.path", hidden);
    }
    SECTION("a path git expands at its own install, which cannot be judged") {
        git(world.x, {"config", "include.path", "%(prefix)/etc/gitconfig"});
        refused_with(tools, {{"repo", "x"}}, "include.path", hidden);
    }
    SECTION("the three named files behind :(optional), which git strips") {
        const std::filesystem::path mailmap = world.temp.path() / "mailmap";
        std::ofstream{mailmap, std::ios::binary} << "OUTSIDE NAME <t@example.com>\n";
        git(world.x, {"config", "mailmap.file", ":(optional)" + mailmap.generic_string()});
        CHECK(plain_git_said(world.x, {"show", "-s"}).find("OUTSIDE NAME") != std::string::npos);
        refused_with(tools, {{"repo", "x"}}, "mailmap.file", {"OUTSIDE NAME"});
        git(world.x, {"config", "--unset", "mailmap.file"});
        for (const char* key : {"core.excludesFile", "core.attributesFile"}) {
            INFO(key);
            git(world.x, {"config", key, ":(optional)" + mailmap.generic_string()});
            refused_with(tools, {{"repo", "x"}}, "core.", {});
            git(world.x, {"config", "--unset", key});
        }
    }
    SECTION("a relative core.excludesFile, read against the work tree's top") {
        // `../../excludes` from x is outside the root; from x/.git it would
        // land inside it, so this pins the rule git reads it by.
        std::ofstream{world.temp.path() / "excludes", std::ios::binary} << "untracked-secret.txt\n";
        std::ofstream{world.x / "untracked-secret.txt", std::ios::binary} << "u\n";
        git(world.x, {"config", "core.excludesFile", "../../excludes"});
        CHECK(plain_git_said(world.x, {"status", "--porcelain"}).find("untracked-secret") ==
              std::string::npos);
        refused_with(tools, {{"repo", "x"}}, "core.excludesfile", {});
    }
    SECTION("mailmap.file") {
        const std::filesystem::path mailmap = world.temp.path() / "mailmap";
        std::ofstream{mailmap, std::ios::binary} << "OUTSIDE NAME <t@example.com>\n";
        git(world.x, {"config", "mailmap.file", mailmap.generic_string()});
        CHECK(plain_git_said(world.x, {"show", "-s"}).find("OUTSIDE NAME") != std::string::npos);
        refused_with(tools, {{"repo", "x"}}, "mailmap.file", {"OUTSIDE NAME"});
    }
    SECTION("core.excludesFile") {
        const std::filesystem::path excludes = world.temp.path() / "excludes";
        std::ofstream{excludes, std::ios::binary} << "untracked-secret.txt\n";
        std::ofstream{world.x / "untracked-secret.txt", std::ios::binary} << "u\n";
        git(world.x, {"config", "core.excludesFile", excludes.generic_string()});
        CHECK(plain_git_said(world.x, {"status", "--porcelain"}).find("untracked-secret") ==
              std::string::npos);
        refused_with(tools, {{"repo", "x"}}, "core.excludesfile", {});
    }
    SECTION("core.attributesFile") {
        const std::filesystem::path attributes = world.temp.path() / "attributes";
        std::ofstream{attributes, std::ios::binary} << "* -diff\n";
        std::ofstream{world.x / "x.txt", std::ios::binary} << "changed\n";
        git(world.x, {"config", "core.attributesFile", attributes.generic_string()});
        CHECK(plain_git_said(world.x, {"diff"}).find("Binary files") != std::string::npos);
        refused_with(tools, {{"repo", "x"}}, "core.attributesfile", {});
    }
}

TEST_CASE("a named file inside the root, and the person's own settings outside it, still read",
          "[tools][git][sandbox][57z]") {
    if (!have_git()) {
        SKIP("git is not available, or this platform starts no child process to run it");
    }
    const LinkWorld world;
    const std::filesystem::path cfg = world.temp.path() / "cfgdir" / "config";
    std::filesystem::create_directories(cfg.parent_path());
    std::ofstream{cfg, std::ios::binary}
        << "[branch \"main\"]\n\tremote = origin\n\tmerge = refs/heads/THEIR-OWN-BRANCH\n";
    git(world.x, {"remote", "add", "origin", "https://example.invalid/r.git"});
    const auto reads = [](const Confined& on, const nlohmann::json& arguments,
                          const std::string& expected) {
        for (const char* tool : {"git_status", "git_diff", "git_log", "git_show"}) {
            INFO(tool << " " << arguments.dump());
            const ToolOutcome outcome = on.run(tool, arguments);
            INFO(outcome.content);
            CHECK_FALSE(outcome.is_error);
        }
        CHECK(on.run("git_status", arguments).content.find(expected) != std::string::npos);
    };

    SECTION("an include and named files inside the root") {
        std::ofstream{world.x / "inside.cfg", std::ios::binary}
            << "[branch \"main\"]\n\tremote = origin\n\tmerge = refs/heads/INSIDE-BRANCH\n";
        git(world.x, {"config", "include.path", "../inside.cfg"});
        std::ofstream{world.x / "mailmap", std::ios::binary} << "Inside Name <t@example.com>\n";
        git(world.x, {"config", "mailmap.file", "mailmap"});
        std::ofstream{world.root / "excludes", std::ios::binary} << "nothing-here\n";
        git(world.x, {"config", "core.excludesFile", (world.root / "excludes").generic_string()});
        reads(Confined{world.root}, {{"repo", "x"}}, "INSIDE-BRANCH");
    }
    SECTION("the person's global config including a file outside the root") {
        const std::filesystem::path global = world.temp.path() / "global.gitconfig";
        std::ofstream{global, std::ios::binary} << "[include]\n\tpath = " << cfg.generic_string()
                                                << "\n";
        const apogee::testing::EnvGuard theirs{"GIT_CONFIG_GLOBAL", global.string()};
        reads(Confined{world.root}, {{"repo", "x"}}, "THEIR-OWN-BRANCH");
    }
    SECTION("a repository whose own config sits outside the root, the launch in its subfolder") {
        // The person's repository above the folder Apogee was started in: its
        // `.git/config` is theirs, and so is what it includes.
        git(world.x, {"config", "include.path", cfg.generic_string()});
        std::filesystem::create_directories(world.x / "deeper");
        reads(Confined{world.x / "deeper"}, nlohmann::json::object(), "THEIR-OWN-BRANCH");
        reads(Confined{world.x / "deeper"}, {{"repo", "."}}, "THEIR-OWN-BRANCH");
    }
}
