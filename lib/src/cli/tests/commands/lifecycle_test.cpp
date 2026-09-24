#include <CLI/CLI.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "commands/complete_protocol.h"
#include "commands/registry.h"
#include "commands/root.h"
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

[[nodiscard]] const apogee::commands::CommandSpec& commands() {
    // A small hand-built tree, for the walking rules alone. Anything about what
    // the real commands declare goes through `real_tree()` instead: this is a
    // second list, and could pass while the real registration was broken.
    using apogee::commands::CommandSpec;
    using apogee::commands::ValueKind;
    using apogee::commands::ValueSpec;
    static const ValueSpec kBackend{.kind = ValueKind::Backend};
    static const ValueSpec kText{.kind = ValueKind::Text, .hint = "a word"};
    static const ValueSpec kPath{.kind = ValueKind::Path};
    static const CommandSpec kRoot{
        // The root's own flags live on the root node, so `apogee --<TAB>`
        // needs no special case at the call site.
        .name = "",
        .flags = {"--config", "--help", "--version", "-V", "-h"},
        .values = {{"--config", kPath}},
        .subcommands =
            {
                CommandSpec{.name = "chat",
                            .flags = {"--help", "--model", "--search", "--tools", "-h", "-m"},
                            .values = {{"--model", kBackend}, {"-m", kBackend}}},
                CommandSpec{.name = "check", .flags = {"--fix", "--help", "--no-color", "-h"}},
                CommandSpec{.name = "chats", .flags = {"--help", "-h"}},
                CommandSpec{.name = "complete",
                            .flags = {"--help", "--image", "--model", "--tools", "-h", "-m"},
                            .values = {{"--image", kPath}, {"--model", kBackend}, {"-m", kBackend}},
                            .positionals = {kText}},
                CommandSpec{.name = "config", .flags = {"--help", "-h"}},
                CommandSpec{.name = "knowledge",
                            .flags = {"--db", "--help", "-h"},
                            .values = {{"--db", kText}},
                            .aliases = {"kn"},
                            .subcommands = {CommandSpec{.name = "list",
                                                        .flags = {"--help", "--status", "-h"},
                                                        .values = {{"--status", kText}}},
                                            CommandSpec{.name = "query",
                                                        .flags = {"--help", "-h"},
                                                        .positionals = {kText}}}},
                CommandSpec{.name = "uninstall",
                            .flags = {"--help", "--keep-data", "--yes", "-h", "-y"}},
                CommandSpec{.name = "version", .flags = {"--help", "-h"}},
            },
    };
    return kRoot;
}

/// The tree the real registry declares, read the way `apogee __complete`
/// reads it.
[[nodiscard]] const apogee::commands::CommandSpec& real_tree() {
    static const apogee::commands::CommandSpec kTree = [] {
        // The real root, so its own flags (`--config`) are in the tree too.
        const apogee::commands::RootCommand root{apogee::commands::default_registry()};
        return apogee::commands::specs_from_app(root.app());
    }();
    return kTree;
}

[[nodiscard]] std::vector<std::string> complete_line(std::vector<std::string> words,
                                                     std::string current = "") {
    CompletionRequest request;
    request.words = std::move(words);
    request.current = std::move(current);
    return completion_candidates(request, two_backends(), real_tree());
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
    // Through the real registry: what makes these complete is the type the
    // command declares on its positional, not a list in the protocol.
    for (const std::string_view verb :
         {"set-default", "set-default-embedding", "set-default-extraction", "delete-backend"}) {
        INFO(verb);
        const std::vector<std::string> candidates = complete_line({"config", std::string{verb}});
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

TEST_CASE("every registered command reaches completion with no per-shell edit",
          "[commands][completion]") {
    // The completion list is DERIVED from the real registry, never maintained
    // beside it. `RootContext::command_names` says why: a second list goes
    // stale the first time a command is added, and the symptom -- tab
    // completion quietly missing a command -- is one nobody files a bug about.
    //
    // Asserted against the actual registry rather than the hand-written
    // CommandSpec list above, because that list is itself a second list and
    // could pass while the real path was broken.
    CLI::App app{"apogee"};
    apogee::commands::RootContext context;
    apogee::commands::CommandRegistry registry = apogee::commands::default_registry();
    registry.bind_all(app, context);

    const apogee::commands::CommandSpec specs = apogee::commands::specs_from_app(app);

    CompletionRequest request;
    request.current = "";
    const std::vector<std::string> candidates =
        completion_candidates(request, two_backends(), specs);

    // Every command the registry knows about, offered -- including the four
    // shells' stubs, which only ever call back into `apogee __complete`.
    for (const std::string_view name : registry.names()) {
        if (name.starts_with("__")) {
            continue;  // protocol commands are hidden on purpose
        }
        INFO("command: " << name);
        CHECK(contains(candidates, std::string{name}));
    }

    // And the one this item added, named explicitly so the assertion cannot
    // pass by iterating an empty list.
    CHECK(contains(candidates, "models"));
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

namespace {

/// Every node of `node`'s tree with its path, parents before children.
void collect_paths(
    const apogee::commands::CommandSpec& node, std::vector<std::string> path,
    std::vector<std::pair<std::vector<std::string>, const apogee::commands::CommandSpec*>>& out) {
    out.emplace_back(path, &node);
    for (const apogee::commands::CommandSpec& child : node.subcommands) {
        std::vector<std::string> deeper = path;
        deeper.push_back(child.name);
        collect_paths(child, deeper, out);
    }
}

[[nodiscard]] std::string joined(const std::vector<std::string>& words) {
    std::string out;
    for (const std::string& word : words) {
        out += (out.empty() ? "" : " ") + word;
    }
    return out;
}

}  // namespace

TEST_CASE("every visible subcommand completes under its parent, at every depth",
          "[commands][completion][tree]") {
    // The report: `apogee models <TAB>` offered nothing. The protocol read the
    // top level of the parser and kept `config`'s verbs by hand -- four behind
    // by the time anyone looked. Now every parent in the real tree must offer
    // exactly its children, so a verb added anywhere is covered by this loop.
    std::vector<std::pair<std::vector<std::string>, const apogee::commands::CommandSpec*>> nodes;
    collect_paths(real_tree(), {}, nodes);

    std::size_t parents = 0;
    std::size_t deepest = 0;
    for (const auto& [path, node] : nodes) {
        if (node->subcommands.empty()) {
            continue;
        }
        ++parents;
        deepest = std::max(deepest, path.size());
        INFO("apogee " << joined(path) << " <TAB>");
        std::vector<std::string> expected;
        for (const apogee::commands::CommandSpec& child : node->subcommands) {
            expected.push_back(child.name);
        }
        CHECK(complete_line(path) == expected);
    }
    // Not vacuous: the tree has verbs two levels down (`train pipeline run`).
    CHECK(parents >= 10);
    CHECK(deepest >= 2);
    CHECK(contains(complete_line({"models"}), "pull"));
    CHECK(contains(complete_line({"train", "pipeline"}), "resume"));
    CHECK(contains(complete_line({"config"}), "add-graph"));
}

TEST_CASE("every command's own flags complete at its own depth", "[commands][completion][tree]") {
    std::vector<std::pair<std::vector<std::string>, const apogee::commands::CommandSpec*>> nodes;
    collect_paths(real_tree(), {}, nodes);
    for (const auto& [path, node] : nodes) {
        INFO("apogee " << joined(path) << " -<TAB>");
        CHECK(complete_line(path, "-") == node->flags);
    }
    CHECK(contains(complete_line({"models", "pull"}, "--"), "--safetensors"));
    // A parent's flags do not leak into its children, nor the other way.
    CHECK_FALSE(contains(complete_line({"models", "pull"}, "--"), "--config"));
    CHECK_FALSE(contains(complete_line({}, "--"), "--safetensors"));
}

TEST_CASE("hidden commands are never offered, at any depth", "[commands][completion][tree]") {
    const std::vector<std::string> top = complete_line({});
    CHECK_FALSE(contains(top, "__complete"));
    CHECK_FALSE(contains(top, "__mcp-tools"));
    for (const std::string& name : top) {
        CHECK_FALSE(name.starts_with("__"));
    }
}

TEST_CASE("an alias walks like the name it stands for", "[commands][completion][tree]") {
    // Recognised on the line, never offered: offering `kn` beside `knowledge`
    // would double what the user reads.
    CHECK(complete_line({"kn"}) == complete_line({"knowledge"}));
    CHECK_FALSE(complete_line({"knowledge"}).empty());
    CHECK_FALSE(contains(complete_line({}), "kn"));

    CompletionRequest request;
    request.words = {"kn"};
    const std::vector<std::string> candidates =
        completion_candidates(request, two_backends(), commands());
    CHECK(candidates == std::vector<std::string>{"list", "query"});
}

TEST_CASE("a flag's value is never mistaken for a subcommand", "[commands][completion][tree]") {
    // `knowledge --db list <TAB>`: `list` is the value of --db, so the verbs
    // are still on offer. Descending into `list` would offer nothing.
    CompletionRequest request;
    request.words = {"knowledge", "--db", "list"};
    request.current = "";
    CHECK(completion_candidates(request, two_backends(), commands()) ==
          std::vector<std::string>{"list", "query"});

    // `--db=list` carries its own value, so the next word is free again and
    // does descend: these are `list`'s flags, not `knowledge`'s.
    request.words = {"knowledge", "--db=x", "list"};
    request.current = "--";
    CHECK(completion_candidates(request, two_backends(), commands()) ==
          std::vector<std::string>{"--help", "--status"});
    request.words = {"knowledge", "--db=x"};
    request.current = "";
    CHECK(completion_candidates(request, two_backends(), commands()) ==
          std::vector<std::string>{"list", "query"});
}

TEST_CASE("a positional completes by what it declares, once", "[commands][completion][tree]") {
    // `models info <backend>`: declared BACKEND, so it completes; once given,
    // there is no second positional and nothing more is offered.
    CHECK(complete_line({"models", "info"}) == std::vector<std::string>{"claude", "local"});
    CHECK(complete_line({"models", "info"}, "lo") == std::vector<std::string>{"local"});
    // Every positional given: only a flag can follow, so flags are offered.
    CHECK(complete_line({"models", "info", "claude"}) == std::vector<std::string>{"--help", "-h"});
    CHECK(complete_line({"train", "rollback"}) == std::vector<std::string>{"claude", "local"});
    // A positional that is a file or free text offers nothing, which hands
    // the word back to the shell's own file completion.
    CHECK(complete_line({"models", "delete"}).empty());
    CHECK(complete_line({"embed", "ingest", "notes"}).empty());
}

TEST_CASE("backend-valued flags are the ones declared so, not the ones spelled so",
          "[commands][completion][tree]") {
    // The spelling rule this replaced offered backends for any `--model`, and
    // `config add-backend --model` takes a vendor model id (claude-sonnet-5),
    // while `--judge` and `--teacher` -- backend names -- offered nothing.
    CHECK(complete_line({"config", "add-backend", "x", "--model"}).empty());
    CHECK(complete_line({"train", "eval", "run-1", "--judge"}) ==
          std::vector<std::string>{"claude", "local"});
    CHECK(complete_line({"datasets", "synth", "d", "--teacher"}) ==
          std::vector<std::string>{"claude", "local"});
    CHECK(complete_line({"chat", "--rerank"}) == std::vector<std::string>{"claude", "local"});
    CHECK(complete_line({"graph", "build", "notes", "-m"}, "cl") ==
          std::vector<std::string>{"claude"});
    // A value flag that is not a backend hands the word to the shell.
    CHECK(complete_line({"chat", "--system"}).empty());
}

namespace {

[[nodiscard]] apogee::commands::Completion complete_full(std::vector<std::string> words,
                                                         std::string current = "") {
    CompletionRequest request;
    request.words = std::move(words);
    request.current = std::move(current);
    return apogee::commands::complete_words(request, two_backends(), real_tree());
}

}  // namespace

TEST_CASE("a fixed set of values completes from the parser's own validator",
          "[commands][completion][values]") {
    // `--type` is validated against CLI::IsMember; `--help` shows the set, and
    // completion reads the same set -- nothing restated here.
    const std::vector<std::string> types = complete_line({"config", "add-backend", "x", "--type"});
    CHECK(contains(types, "anthropic"));
    CHECK(contains(types, "llamacpp"));
    CHECK(contains(types, "ollama-cli"));
    CHECK(complete_line({"config", "add-backend", "x", "-t"}, "cl") ==
          std::vector<std::string>{"claude-cli"});
    CHECK(complete_line({"config", "set-permission", "write_file"}) ==
          std::vector<std::string>{"ask", "allow", "deny"});
    // models convert's precisions come from the converter's own list.
    const std::vector<std::string> precisions =
        complete_line({"models", "convert", "snap", "out.gguf", "--type"});
    CHECK(contains(precisions, "f16"));
    CHECK(contains(precisions, "q8_0"));
    CHECK(complete_full({"models", "convert"}).files);
}

TEST_CASE("free text says what it wants instead of offering file names",
          "[commands][completion][values]") {
    // The report: `config add-backend --model na<TAB>` did nothing, and a bare
    // `--model <TAB>` listed the working directory -- a model id is neither.
    const apogee::commands::Completion model =
        complete_full({"config", "add-backend", "--model"}, "na");
    CHECK(model.candidates.empty());
    CHECK_FALSE(model.files);
    CHECK(model.hint == "--model TEXT: Model name");

    const apogee::commands::Completion name = complete_full({"config", "add-backend"});
    CHECK(name.hint == "name TEXT: Name for the new backend");
    CHECK_FALSE(name.files);

    const apogee::commands::Completion size =
        complete_full({"config", "add-backend", "x", "--context-size"});
    CHECK(size.hint.starts_with("--context-size INT"));
}

TEST_CASE("a path, and only a path, completes files", "[commands][completion][values]") {
    CHECK(complete_full({"config", "add-backend", "x", "--model-path"}).files);
    CHECK(complete_full({"embed", "ingest", "notes"}).files);
    CHECK(complete_full({"models", "quantize"}).files);
    CHECK(complete_full({"--config"}).files);
    CHECK_FALSE(complete_full({"models", "delete"}).files);
    CHECK_FALSE(complete_full({"chat", "--system"}).files);
}

TEST_CASE("a backend word with no backends configured says so", "[commands][completion][values]") {
    CompletionRequest request;
    request.words = {"config", "set-default"};
    const apogee::commands::Completion completion =
        apogee::commands::complete_words(request, apogee::harness::Config{}, real_tree());
    CHECK(completion.candidates.empty());
    CHECK_FALSE(completion.files);
    CHECK(completion.hint.find("no backends configured") != std::string::npos);
}

TEST_CASE("every argument in the real tree can say what it is", "[commands][completion][values]") {
    // A hint is what TAB shows for free text, so an argument without one would
    // be the silent TAB this replaced. And a Choice with no members would
    // offer nothing while claiming a fixed set.
    std::vector<std::pair<std::vector<std::string>, const apogee::commands::CommandSpec*>> nodes;
    collect_paths(real_tree(), {}, nodes);
    std::size_t values = 0;
    for (const auto& [path, node] : nodes) {
        for (const auto& [spelling, value] : node->values) {
            INFO("apogee " << joined(path) << " " << spelling);
            CHECK_FALSE(value.hint.empty());
            if (value.kind == apogee::commands::ValueKind::Choice) {
                CHECK_FALSE(value.choices.empty());
            }
            ++values;
        }
        for (const apogee::commands::ValueSpec& value : node->positionals) {
            INFO("apogee " << joined(path) << " <positional>");
            CHECK_FALSE(value.hint.empty());
        }
    }
    CHECK(values > 100);
}

TEST_CASE("the directive line is sent only to a stub that asks for it",
          "[commands][completion][values]") {
    using apogee::commands::Completion;
    using apogee::commands::render_completion;
    const Completion values{.candidates = {"pull", "list"}};
    const Completion files{.files = true};
    const Completion hint{.hint = "--model TEXT: Model\nname"};

    // Bare: exactly the old contract, one candidate per line and nothing else,
    // so an older stub never offers a directive as a completion.
    CHECK(render_completion(values, false) == "pull\nlist\n");
    CHECK(render_completion(files, false).empty());
    CHECK(render_completion(hint, false).empty());

    CHECK(render_completion(values, true) == ":values\npull\nlist\n");
    CHECK(render_completion(files, true) == ":files\n");
    // One line, whatever the description held: the stubs read lines.
    CHECK(render_completion(hint, true) == ":hint --model TEXT: Model name\n");
    CHECK(render_completion(Completion{}, true) == ":values\n");
}
