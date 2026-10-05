#include "cli/reset.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "cli/complete_protocol.h"
#include "cli/registry.h"
#include "cli/root.h"
#include "contracts/config.h"
#include "contracts/layout.h"
#include "modelstore/sidecar.h"
#include "modelstore/store.h"
#include "secrets/store.h"
#include "support/cli_home.h"
#include "support/env_guard.h"
#include "support/gguf_builder.h"

/// `apogee reset` (M9): the plan, the keep set, the skeleton it leaves, the
/// confirmation, the failure it reports, and its completion -- every case over
/// a sandboxed temp `APOGEE_HOME`.
///
/// No case here lists the layout's rows. What a reset plans is enumerated from
/// `layout.h` exactly as the command enumerates it; the one golden that spells
/// rows out spells FIXTURE rows, handed to the planner in place of the layout,
/// so it pins the wording and cannot go stale against the real declaration.
namespace {

using apogee::commands::PlannedRow;
using apogee::commands::ResetPlan;
using apogee::harness::LayoutEntry;

constexpr std::string_view kModelFile = "models/org--m/gguf/0123456789ab/m.gguf";

void write(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out{path, std::ios::binary};
    out << content;
}

[[nodiscard]] std::string read(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

[[nodiscard]] bool contains(std::string_view haystack, std::string_view needle) {
    return haystack.find(needle) != std::string_view::npos;
}

/// Every entry under `root`, by relative path: its type, its mode, and a
/// file's bytes. Two trees compare equal only when they are the same tree.
[[nodiscard]] std::map<std::string, std::string> tree_of(const std::filesystem::path& root) {
    std::map<std::string, std::string> tree;
    for (const auto& item : std::filesystem::recursive_directory_iterator(root)) {
        const std::filesystem::file_status status = item.symlink_status();
        const auto mode =
            static_cast<unsigned>(status.permissions() & std::filesystem::perms::mask);
        std::string entry = std::to_string(mode) + " ";
        if (std::filesystem::is_directory(status)) {
            entry += "dir";
        } else {
            entry += "file " + read(item.path());
        }
        tree[item.path().lexically_relative(root).generic_string()] = std::move(entry);
    }
    return tree;
}

/// `tree` with or without everything under `row/`.
[[nodiscard]] std::map<std::string, std::string> under(
    const std::map<std::string, std::string>& tree, std::string_view row, bool inside) {
    std::map<std::string, std::string> out;
    for (const auto& [path, entry] : tree) {
        const bool in_row = path == row || path.starts_with(std::string{row} + "/");
        if (in_row == inside) {
            out.emplace(path, entry);
        }
    }
    return out;
}

/// The credential store's place in a home, where the product puts it.
[[nodiscard]] std::filesystem::path store_in(const std::filesystem::path& home) {
    return apogee::secrets::credentials_path(home / "config" / "config.yaml");
}

/// A used install: a stored model with its record, a chat, a summary, stored
/// keys, a training run, a note, and the chat editor's history at the root.
void populate(const std::filesystem::path& home) {
    REQUIRE(apogee::harness::seed_data_directory(home).ok());
    const std::filesystem::path model = home / kModelFile;
    write(model, apogee::testing::minimal_gguf("llama"));
    apogee::models::Sidecar record;
    record.ref = "org/m";
    record.source = "huggingface";
    record.file = model.filename().string();
    REQUIRE(apogee::models::write_sidecar(model, record));
    write(home / "sessions" / "chat-1.json", R"({"id":"chat-1"})");
    write(home / "memory" / "chats.db", "summaries");
    write(store_in(home), R"({"anthropic":{"api_key":"sk-reset-test"}})");
    write(home / "training" / "runs" / "run-1" / "manifest.json", "{}");
    write(home / "notes" / "note.md", "remember");
    write(home / "chat_history", "what the user typed\n");
}

/// A home of the test's own, seeded the way an installer seeds one.
struct Home {
    apogee::testing::TempDir dir{"reset-" + std::to_string(std::random_device{}())};
    std::filesystem::path path = dir.path() / "apogee";
};

/// A user home of the test's own for every case that runs the command: the
/// reset never reaches outside the data directory, and the doctor it ends with
/// only reads -- but a test exists to prove that, so it does not trust it with
/// the developer's real home.
struct UserHome {
    apogee::testing::TempDir dir{"reset-user-home-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard home{"HOME", dir.path().string()};
    apogee::testing::EnvGuard profile{"USERPROFILE", dir.path().string()};
};

/// The layout's rows plus one a contributor might add tomorrow.
[[nodiscard]] std::vector<LayoutEntry> layout_with_fixture_row() {
    std::vector<LayoutEntry> rows{apogee::harness::data_directories().begin(),
                                  apogee::harness::data_directories().end()};
    rows.push_back({"fixture-row", "a row added to the layout after reset shipped", false, true});
    return rows;
}

/// Gives a directory its owner's full mode back on scope exit, so a test that
/// locked it can still have its temp directory removed.
class Unlock {
public:
    explicit Unlock(std::filesystem::path path) : path_{std::move(path)} {}

    ~Unlock() {
        std::error_code code;
        std::filesystem::permissions(path_, std::filesystem::perms::owner_all,
                                     std::filesystem::perm_options::replace, code);
    }

    Unlock(const Unlock&) = delete;
    Unlock& operator=(const Unlock&) = delete;
    Unlock(Unlock&&) = delete;
    Unlock& operator=(Unlock&&) = delete;

private:
    std::filesystem::path path_;
};

/// Runs `apogee <args>` against `home` in process, with std::cin a pipe
/// carrying `stdin_text` -- whatever runs the suite, the command sees a pipe.
int run_piped(const apogee::testing::CliHome& home, const std::vector<std::string>& args,
              const std::string& stdin_text, std::string* out) {
    std::istringstream piped{stdin_text};
    std::streambuf* original = std::cin.rdbuf(piped.rdbuf());
    int code = -1;
    try {
        code = home.run(args, out);
    } catch (...) {
        std::cin.rdbuf(original);
        throw;
    }
    std::cin.rdbuf(original);
    return code;
}

[[nodiscard]] std::vector<std::string> complete(std::vector<std::string> words,
                                                std::string current = "") {
    static const apogee::commands::CommandSpec kTree = [] {
        const apogee::commands::RootCommand root{apogee::commands::default_registry()};
        return apogee::commands::specs_from_app(root.app());
    }();
    apogee::commands::CompletionRequest request;
    request.words = std::move(words);
    request.current = std::move(current);
    return apogee::commands::completion_candidates(request, apogee::harness::Config{}, kTree);
}

}  // namespace

TEST_CASE("the reset plan, word for word", "[commands][reset][plan]") {
    // The golden, over FIXTURE rows handed in place of the layout -- so it pins
    // the prompt's wording without becoming a second list of the real rows.
    const std::array<LayoutEntry, 6> rows{{
        {"vault", "secrets", true, true},
        {"chats", "conversations", true, true},
        {"weights", "models", false, true},
        {"scratch", "safe to delete", false, false},
        {"empty", "nothing yet", false, true},
        {"gone", "never created", false, true},
    }};
    const Home home;
    write(home.path / "vault" / "credentials.json", "{}");
    write(home.path / "chats" / "c.json", "{}");
    write(home.path / "weights" / "w.bin", "w");
    write(home.path / "scratch" / "tmp", "t");
    std::filesystem::create_directories(home.path / "empty");
    write(home.path / "chat_history", "typed");

    const ResetPlan plan = apogee::commands::plan_reset(
        home.path, {"weights"}, home.path / "vault" / "credentials.json", rows);

    CHECK(apogee::commands::describe_plan(plan) ==
          "This will reset " + home.path.string() +
              " to a fresh install:\n"
              "  remove  vault/\n"
              "  remove  chats/\n"
              "  keep    weights/\n"
              "  remove  scratch/\n"
              "  remove  empty/\n"
              "  absent  gone/\n"
              "  remove  chat_history   (not in the layout)\n"
              "\n"
              "Including YOUR OWN DATA in:\n"
              "  vault/   the secrets store (credentials.json): your stored API keys\n"
              "  chats/\n"
              "  chat_history\n"
              "\n"
              "This cannot be undone.\n"
              "\n"
              "A kept row is left exactly as it is. Everything else is then recreated as a "
              "fresh install has it ('apogee check --fix'), and checked.\n");
}

TEST_CASE("the plan covers every row the layout declares, in its order",
          "[commands][reset][plan]") {
    const Home home;
    populate(home.path);

    const ResetPlan plan = apogee::commands::plan_reset(home.path, {}, store_in(home.path));

    std::vector<std::string> expected;
    for (const LayoutEntry& entry : apogee::harness::data_directories()) {
        expected.emplace_back(entry.relative_path);
    }
    std::vector<std::string> planned;
    for (const PlannedRow& row : plan.rows) {
        planned.push_back(row.name);
        CHECK(row.present);
        CHECK_FALSE(row.kept);  // bare reset keeps nothing
    }
    CHECK(planned == expected);
    CHECK(planned.size() >= 5);  // not vacuous
    CHECK(plan.unlisted == std::vector<std::string>{"chat_history"});
}

TEST_CASE("the plan names every user-data row it removes, and the secrets store by name",
          "[commands][reset][plan]") {
    const Home home;
    populate(home.path);

    const ResetPlan plan = apogee::commands::plan_reset(home.path, {"models"}, store_in(home.path));
    const std::string described = apogee::commands::describe_plan(plan);

    // The secrets store is in a row being removed, and is said to be.
    const std::string store_row =
        store_in(home.path).lexically_relative(home.path).begin()->string();
    CHECK(plan.secrets_row == store_row);
    CHECK(contains(described, store_row + "/   the secrets store (" +
                                  store_in(home.path).filename().string() +
                                  "): your stored API keys"));
    // Every removed row holding the user's data is warned by name; the kept
    // models are not, since nothing of theirs is at risk.
    for (const PlannedRow& row : plan.rows) {
        INFO(row.name);
        if (row.kept) {
            CHECK(contains(described, "  keep    " + row.name + "/\n"));
            CHECK_FALSE(contains(described, "\n  " + row.name + "/\n"));
        } else if (row.user_data) {
            CHECK(contains(described, "\n  " + row.name + "/"));
        }
    }
    const auto sessions = std::ranges::find(plan.rows, "sessions", &PlannedRow::name);
    REQUIRE(sessions != plan.rows.end());
    CHECK(sessions->user_data);  // chats are warned
    CHECK(contains(described, "YOUR OWN DATA"));
    CHECK(contains(described, "This cannot be undone."));

    // Keep the row the store lives in, and the store is not mentioned at all.
    const ResetPlan keeping_keys =
        apogee::commands::plan_reset(home.path, {store_row}, store_in(home.path));
    CHECK(keeping_keys.secrets_row.empty());
    CHECK_FALSE(contains(apogee::commands::describe_plan(keeping_keys), "secrets store"));
}

TEST_CASE("a fresh install's own files raise no user-data warning", "[commands][reset][plan]") {
    const Home home;
    REQUIRE(apogee::harness::seed_data_directory(home.path).ok());

    const ResetPlan plan = apogee::commands::plan_reset(home.path, {}, store_in(home.path));

    CHECK(std::ranges::none_of(plan.rows, &PlannedRow::user_data));
    CHECK_FALSE(contains(apogee::commands::describe_plan(plan), "YOUR OWN DATA"));
}

TEST_CASE("a row added to the layout reaches the reset with no change to the command",
          "[commands][reset][plan]") {
    // The no-second-list pin. The planner is handed the layout's rows plus a
    // new one, through the very parameter the command fills from layout.h: it
    // is planned, removed, warned, keepable and kept, with nothing taught.
    const std::vector<LayoutEntry> rows = layout_with_fixture_row();
    const Home home;
    populate(home.path);
    write(home.path / "fixture-row" / "data", "x");

    const ResetPlan removing =
        apogee::commands::plan_reset(home.path, {}, store_in(home.path), rows);
    const auto row = std::ranges::find(removing.rows, "fixture-row", &PlannedRow::name);
    REQUIRE(row != removing.rows.end());
    CHECK(row->present);
    CHECK(row->user_data);
    CHECK(contains(apogee::commands::describe_plan(removing), "  remove  fixture-row/\n"));
    CHECK(removing.unlisted == std::vector<std::string>{"chat_history"});

    const std::vector<std::string> keepable = apogee::commands::keepable_rows(rows);
    CHECK(std::ranges::find(keepable, "fixture-row") != keepable.end());
    const ResetPlan keeping =
        apogee::commands::plan_reset(home.path, {"fixture-row"}, store_in(home.path), rows);
    CHECK(contains(apogee::commands::describe_plan(keeping), "  keep    fixture-row/\n"));

    // And against the real layout, which has no such row, the same directory
    // is something the layout does not declare -- removed, and said so.
    const ResetPlan real = apogee::commands::plan_reset(home.path, {}, store_in(home.path));
    CHECK(std::ranges::find(real.unlisted, "fixture-row") != real.unlisted.end());
}

TEST_CASE("a keep that names no row is refused, never ignored", "[commands][reset][plan]") {
    const Home home;
    REQUIRE(apogee::harness::seed_data_directory(home.path).ok());
    CHECK_THROWS_AS(apogee::commands::plan_reset(home.path, {"not-a-row"}, store_in(home.path)),
                    std::invalid_argument);
}

TEST_CASE("reset --keep models keeps the store byte-identical and leaves the rest fresh",
          "[commands][reset][run]") {
    // The asked-for case end to end, in process: a used install reset with its
    // models kept. models/ is the same tree after -- records included, so every
    // stored model is still listed -- and every other row is exactly what a
    // fresh install seeds, which `check` then passes.
    const UserHome user_home;
    const apogee::testing::CliHome home{"backends:\n  local:\n    type: mock\n"};
    populate(home.home());
    const std::map<std::string, std::string> models_before =
        under(tree_of(home.home()), "models", true);
    const auto stored_before =
        apogee::models::list_store_ggufs(apogee::models::StoreRoots::at(home.models()));
    REQUIRE(stored_before.size() == 1);

    std::string out;
    const int code = home.run({"reset", "--keep", "models", "--yes"}, &out);

    INFO(out);
    CHECK(code == 0);
    CHECK(contains(out, "  keep    models/\n"));
    CHECK(contains(out, "removed " + (home.home() / "sessions").string()));
    CHECK(contains(out, "fixed: created "));
    CHECK_FALSE(contains(out, "FAIL"));

    const std::map<std::string, std::string> after = tree_of(home.home());
    CHECK(under(after, "models", true) == models_before);
    const auto stored_after =
        apogee::models::list_store_ggufs(apogee::models::StoreRoots::at(home.models()));
    REQUIRE(stored_after.size() == 1);
    CHECK(stored_after.front().file == stored_before.front().file);

    const Home fresh;
    REQUIRE(apogee::harness::seed_data_directory(fresh.path).ok());
    CHECK(under(after, "models", false) == under(tree_of(fresh.path), "models", false));
    CHECK_FALSE(std::filesystem::exists(store_in(home.home())));
    CHECK_FALSE(std::filesystem::exists(home.home() / "chat_history"));

    // A backend registered again on the kept file resolves -- the config is
    // fresh, the path it names is the one the store kept.
    write(home.config_path(), "backends:\n  kept-model:\n    type: llamacpp\n    model_path: " +
                                  stored_after.front().file.generic_string() + "\n");
    std::string listing;
    CHECK(home.run({"models", "list"}, &listing) == 0);
    INFO(listing);
    CHECK(contains(listing, "kept-model"));
}

TEST_CASE("a bare reset leaves exactly the first-run skeleton", "[commands][reset][run]") {
    const UserHome user_home;
    const apogee::testing::CliHome home{"backends:\n  local:\n    type: mock\n"};
    populate(home.home());

    std::string out;
    const int code = home.run({"reset", "--yes"}, &out);

    INFO(out);
    CHECK(code == 0);
    CHECK(contains(out, "  remove  models/\n"));
    const Home fresh;
    REQUIRE(apogee::harness::seed_data_directory(fresh.path).ok());
    CHECK(tree_of(home.home()) == tree_of(fresh.path));
}

TEST_CASE("a piped reset without --yes refuses, and removes nothing",
          "[commands][reset][confirm]") {
    const UserHome user_home;
    const apogee::testing::CliHome home{"backends:\n  local:\n    type: mock\n"};
    populate(home.home());
    const std::map<std::string, std::string> before = tree_of(home.home());

    // Even a 'yes' on the pipe: a piped reset must not decide for the user.
    std::string out;
    const int code = run_piped(home, {"reset", "--keep", "models"}, "yes\n", &out);

    INFO(out);
    CHECK(code != 0);
    CHECK(contains(out, "YOUR OWN DATA"));  // the plan is still shown
    CHECK(contains(out,
                   "apogee reset: not a terminal -- rerun with --yes to confirm "
                   "non-interactively"));
    CHECK(tree_of(home.home()) == before);
}

TEST_CASE("a keep the layout does not declare is refused before anything is removed",
          "[commands][reset][confirm]") {
    const UserHome user_home;
    const apogee::testing::CliHome home{"backends:\n  local:\n    type: mock\n"};
    populate(home.home());
    const std::map<std::string, std::string> before = tree_of(home.home());

    std::string out;
    CHECK(home.run({"reset", "--keep", "not-a-row", "--yes"}, &out) != 0);
    INFO(out);
    CHECK(contains(out, "not-a-row"));
    CHECK(tree_of(home.home()) == before);
}

TEST_CASE("the confirmation is uninstall's, word for word", "[commands][reset][confirm]") {
    using apogee::commands::confirm_removal;
    using apogee::commands::Confirmation;
    const auto ask = [](bool yes, bool interactive, const std::string& typed, std::string* said) {
        std::istringstream in{typed};
        std::ostringstream out;
        const Confirmation answer = confirm_removal("reset", yes, interactive, in, out, out);
        *said = out.str();
        return answer;
    };
    std::string said;
    CHECK(ask(true, false, "", &said) == Confirmation::Proceed);
    CHECK(said.empty());
    CHECK(ask(false, true, "yes\n", &said) == Confirmation::Proceed);
    CHECK(said == "\nType 'yes' to proceed: ");
    CHECK(ask(false, true, "y\n", &said) == Confirmation::Cancelled);
    CHECK(said == "\nType 'yes' to proceed: cancelled\n");
    CHECK(ask(false, false, "yes\n", &said) == Confirmation::Refused);
    CHECK(said ==
          "\napogee reset: not a terminal -- rerun with --yes to confirm "
          "non-interactively\n");
}

TEST_CASE("a row that cannot be removed is named, and the rest still goes",
          "[commands][reset][failure]") {
    // The partial failure, reported rather than hidden: a directory inside
    // sessions/ that this user may not change makes sessions/ unremovable.
#if defined(_WIN32)
    SKIP("no POSIX directory modes on this platform");
#else
    const UserHome user_home;
    const apogee::testing::CliHome home{"backends:\n  local:\n    type: mock\n"};
    populate(home.home());
    const std::filesystem::path locked = home.home() / "sessions" / "locked";
    write(locked / "chat.json", "{}");
    std::filesystem::permissions(
        locked, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec,
        std::filesystem::perm_options::replace);
    // Restored on every path out, so the temp directory can be cleaned up.
    const Unlock unlock{locked};
    {
        // A user the mode does not bind (root) cannot fail this way.
        std::error_code code;
        std::ofstream probe{locked / "probe"};
        if (probe.good()) {
            probe.close();
            std::filesystem::remove(locked / "probe", code);
            SKIP("this user can write a read-only directory");
        }
    }
    const std::map<std::string, std::string> models_before =
        under(tree_of(home.home()), "models", true);

    std::string out;
    const int code = home.run({"reset", "--keep", "models", "--yes"}, &out);

    INFO(out);
    CHECK(code != 0);
    CHECK(contains(out, "could not remove " + (home.home() / "sessions").string()));
    CHECK(contains(out, "path(s) could not be removed"));
    // What could not be removed is still there -- the truth the report is of.
    CHECK(std::filesystem::exists(locked / "chat.json"));
    // Everything after the failure still went, and the kept row is untouched.
    CHECK_FALSE(std::filesystem::exists(store_in(home.home())));
    CHECK_FALSE(std::filesystem::exists(home.home() / "chat_history"));
    CHECK_FALSE(std::filesystem::exists(home.home() / "notes" / "note.md"));
    CHECK(under(tree_of(home.home()), "models", true) == models_before);
    // And the check ran after it: the skeleton is back around the leftover.
    CHECK(contains(out, "Filesystem"));
#endif
}

TEST_CASE("reset --keep completes to exactly the layout's rows", "[commands][reset][completion]") {
    // The completion golden, enumerated from the declaration rather than
    // restated: what TAB offers IS the layout, in its order.
    std::vector<std::string> rows;
    for (const LayoutEntry& entry : apogee::harness::data_directories()) {
        rows.emplace_back(entry.relative_path);
    }
    const std::vector<std::string> offered = complete({"reset", "--keep"});
    CHECK(offered == rows);
    CHECK(offered.size() >= 5);
    CHECK(complete({"reset", "--keep"}, "mod") == std::vector<std::string>{"models"});
    // Repeatable: a second --keep offers the rows again.
    CHECK(complete({"reset", "--keep", "models", "--keep"}) == rows);
    // And the verb's own flags.
    const std::vector<std::string> flags = complete({"reset"}, "--");
    CHECK(std::ranges::find(flags, "--keep") != flags.end());
    CHECK(std::ranges::find(flags, "--yes") != flags.end());
    const std::vector<std::string> verbs = complete({});
    CHECK(std::ranges::find(verbs, "reset") != verbs.end());

    // The offer is a contract: every row offered is one the verb keeps.
    const Home home;
    REQUIRE(apogee::harness::seed_data_directory(home.path).ok());
    for (const std::string& row : offered) {
        INFO(row);
        const ResetPlan plan = apogee::commands::plan_reset(home.path, {row}, store_in(home.path));
        CHECK(std::ranges::count_if(plan.rows, &PlannedRow::kept) == 1);
        const auto kept = std::ranges::find(plan.rows, row, &PlannedRow::name);
        REQUIRE(kept != plan.rows.end());
        CHECK(kept->kept);
    }
}
