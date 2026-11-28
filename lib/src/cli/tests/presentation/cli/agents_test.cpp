#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "cli/agents_cmd.h"
#include "cli/registry.h"
#include "cli/root.h"
#include "contracts/config.h"
#include "support/env_guard.h"

/// `apogee agents` in-process: create scaffolds a runnable agent, list sees
/// it, delete removes it -- with the comment-preserving guarantee held.
namespace {

struct Home {
    apogee::testing::TempDir root{"agents-cmd-" + std::to_string(std::random_device{}())};
    std::filesystem::path config = root.path() / "config" / "config.yaml";

    Home() {
        std::filesystem::create_directories(config.parent_path());
        std::ofstream{config, std::ios::binary}
            << "# PRESERVE-ME: agents are workflows run with apogee analyze --agent\n"
               "backends:\n  mock:\n    type: mock\nmodels:\n  default: mock\n"
               "agents:\n  existing:\n    description: \"An existing agent.\"\n"
               "    prompts: [prompts/existing.txt]\n    tools: none\n";
    }

    [[nodiscard]] int run(const std::vector<std::string>& args) const {
        apogee::commands::RootCommand command{apogee::commands::default_registry()};
        std::vector<std::string> full{"--config", config.string()};
        full.insert(full.end(), args.begin(), args.end());
        std::vector<const char*> argv{"apogee"};
        for (const std::string& arg : full) {
            argv.push_back(arg.c_str());
        }
        return command.run(static_cast<int>(argv.size()), argv.data());
    }

    [[nodiscard]] std::string bytes() const {
        std::ifstream in{config, std::ios::binary};
        std::ostringstream out;
        out << in.rdbuf();
        return out.str();
    }
};

/// stdout and stderr captured for one run, restored however it ends.
struct Captured {
    std::ostringstream out;
    std::ostringstream err;
    std::streambuf* old_out = std::cout.rdbuf(out.rdbuf());
    std::streambuf* old_err = std::cerr.rdbuf(err.rdbuf());

    Captured() = default;
    Captured(const Captured&) = delete;
    Captured& operator=(const Captured&) = delete;
    Captured(Captured&&) = delete;
    Captured& operator=(Captured&&) = delete;

    ~Captured() {
        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);
    }
};

}  // namespace

TEST_CASE("create scaffolds the files and appends an entry, preserving comments and neighbours",
          "[commands][agents]") {
    const Home home;
    REQUIRE(home.run({"agents", "create", "reviewer", "--description", "Reviews things", "--tools",
                      "all", "--questions"}) == 0);
    CHECK(std::filesystem::exists(home.root.path() / "prompts" / "reviewer.txt"));
    CHECK(std::filesystem::exists(home.root.path() / "schemas" / "reviewer-output.json"));
    const std::string config = home.bytes();
    CHECK(config.find("PRESERVE-ME") != std::string::npos);
    CHECK(config.find("  existing:\n") != std::string::npos);
    CHECK(config.find("  reviewer:\n    description: Reviews things\n") != std::string::npos);
    CHECK(config.find("    questions: true\n") != std::string::npos);
    CHECK(config.find("    tools: all\n") != std::string::npos);
    // Runnable with no further editing: the loader sees it.
    const apogee::harness::Config loaded = apogee::harness::load_config(home.config);
    REQUIRE(loaded.find_agent("reviewer") != nullptr);
    CHECK(loaded.find_agent("reviewer")->tools == apogee::harness::AgentToolPolicy::All);

    // A duplicate is refused without --force; --no-schema writes no schema.
    CHECK(home.run({"agents", "create", "reviewer", "--description", "y"}) != 0);
    REQUIRE(home.run({"agents", "create", "prose", "--no-schema"}) == 0);
    CHECK_FALSE(std::filesystem::exists(home.root.path() / "schemas" / "prose-output.json"));
    CHECK(home.bytes().find("  prose:\n    prompts: [prompts/prose.txt]\n    save_subdir: prose\n"
                            "    tools: read-only\n") != std::string::npos);
    CHECK(home.run({"agents", "list"}) == 0);
    CHECK(home.run({"analyze", "--list"}) == 0);
}

TEST_CASE("delete removes the entry; --purge removes the files, --keep-files keeps them",
          "[commands][agents]") {
    const Home home;
    REQUIRE(home.run({"agents", "create", "temp"}) == 0);
    REQUIRE(home.run({"agents", "create", "keep"}) == 0);
    REQUIRE(home.run({"agents", "delete", "temp", "--purge"}) == 0);
    CHECK(home.bytes().find("  temp:") == std::string::npos);
    CHECK_FALSE(std::filesystem::exists(home.root.path() / "prompts" / "temp.txt"));
    REQUIRE(home.run({"agents", "delete", "keep", "--keep-files"}) == 0);
    CHECK(home.bytes().find("  keep:") == std::string::npos);
    CHECK(std::filesystem::exists(home.root.path() / "prompts" / "keep.txt"));
    CHECK(home.bytes().find("  existing:\n") != std::string::npos);  // the neighbour survives
    CHECK(home.run({"agents", "delete", "ghost"}) != 0);
    CHECK(home.run({"agents", "delete", "security-review"}) != 0);  // bundled: no entry
    CHECK(home.run({"agents", "delete", "existing", "--purge", "--keep-files"}) != 0);
}

TEST_CASE("a refused name writes nothing, and every rerun gives the same refusal",
          "[commands][agents][M52]") {
    // CLI-M52: `agents create -- -x` wrote prompts/-x.txt and
    // schemas/-x-output.json before the config refused the name; the rerun
    // said "prompt file already exists", and `--force` wrote them again.
    const Home home;
    const std::string before = home.bytes();
    for (const std::vector<std::string>& args :
         {std::vector<std::string>{"agents", "create", "--", "-x"},
          std::vector<std::string>{"agents", "create", "--", "-x"},
          std::vector<std::string>{"agents", "create", "--force", "--", "-x"},
          std::vector<std::string>{"agents", "create", "--", ".x"}}) {
        INFO(args.back() << " " << args[2]);
        std::string err;
        std::string out;
        int code = 0;
        {
            const Captured captured;
            code = home.run(args);
            err = captured.err.str();
            out = captured.out.str();
        }
        CHECK(code == 1);
        CHECK(out.empty());
        CHECK(err.starts_with("apogee agents: agent name '" + args.back() + "'"));
        CHECK(err.find("is not one both config formats can edit") != std::string::npos);
        CHECK(err.find('\n') == err.size() - 1);
        CHECK(err.find("already exists") == std::string::npos);
        CHECK_FALSE(std::filesystem::exists(home.root.path() / "prompts" / "-x.txt"));
        CHECK_FALSE(std::filesystem::exists(home.root.path() / "schemas" / "-x-output.json"));
        CHECK(home.bytes() == before);
    }
}

TEST_CASE("a purged file that cannot be removed is a note on stderr, and the delete still succeeds",
          "[commands][agents][notes][M58]") {
    // CLI-M58: `  (could not remove <file>: <reason>)` was a stdout line
    // between the two `removed` lines (c-ag2), with stderr empty.
#if defined(_WIN32)
    SKIP("no POSIX directory modes on this platform");
#else
    const Home home;
    REQUIRE(home.run({"agents", "create", "probe"}) == 0);
    const std::filesystem::path prompts = home.root.path() / "prompts";
    const std::filesystem::path prompt = prompts / "probe.txt";
    const std::filesystem::path schema = home.root.path() / "schemas" / "probe-output.json";
    REQUIRE(std::filesystem::exists(prompt));
    REQUIRE(std::filesystem::exists(schema));
    std::filesystem::permissions(
        prompts,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec |
            std::filesystem::perms::group_read | std::filesystem::perms::group_exec |
            std::filesystem::perms::others_read | std::filesystem::perms::others_exec,
        std::filesystem::perm_options::replace);

    // Restored on every path out, so the temp directory can be cleaned up.
    class Unlock {
    public:
        explicit Unlock(std::filesystem::path path) : path_{std::move(path)} {}

        Unlock(const Unlock&) = delete;
        Unlock& operator=(const Unlock&) = delete;
        Unlock(Unlock&&) = delete;
        Unlock& operator=(Unlock&&) = delete;

        ~Unlock() {
            std::error_code code;
            std::filesystem::permissions(path_, std::filesystem::perms::owner_all,
                                         std::filesystem::perm_options::replace, code);
        }

    private:
        std::filesystem::path path_;
    };

    const Unlock unlock{prompts};
    {
        // A user the mode does not bind (root) cannot fail this way.
        const std::filesystem::path probe = prompts / "probe-write";
        if (std::ofstream{probe}.good()) {
            std::error_code code;
            std::filesystem::remove(probe, code);
            SKIP("this user can write a read-only directory");
        }
    }
    std::string out;
    std::string err;
    int code = -1;
    {
        const Captured captured;
        code = home.run({"agents", "delete", "probe", "--purge"});
        out = captured.out.str();
        err = captured.err.str();
    }
    INFO(out << err);
    CHECK(code == 0);
    CHECK(out == "removed agent 'probe' from " + home.config.string() + "\n  removed " +
                     schema.string() + "\n");
    CHECK(err.starts_with("apogee agents: could not remove " + prompt.string() + ": "));
    CHECK(err.find('\n') == err.size() - 1);
    CHECK(err.find('(') == std::string::npos);
    CHECK(std::filesystem::exists(prompt));
    CHECK_FALSE(std::filesystem::exists(schema));
#endif
}
