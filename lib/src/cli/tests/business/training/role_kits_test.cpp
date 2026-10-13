#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <random>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "agent/tool.h"
#include "agentloop/query_rewrite.h"
#include "agentloop/structured.h"
#include "agentloop/tool_summary.h"
#include "contracts/assets.h"
#include "contracts/config.h"
#include "graph/extract.h"
#include "knowledge/clerk.h"
#include "support/env_guard.h"
#include "symphony/definition.h"
#include "tools/fs.h"
#include "tools/git.h"
#include "tools/shell.h"
#include "training/datasets.h"
#include "training/kit.h"
#include "training/synth.h"

/// The role-chore kits (42b) mirror the product: every prompt begins with a
/// prefix that is byte for byte the product's own words for the request --
/// read here from the product's functions, never from a copy -- and what
/// follows it rebuilds through the product's own request builder into the
/// very prompt the kit holds. A product change the kit did not follow fails
/// here; so does a kit edited away from the product.
namespace {

namespace fs = std::filesystem;

/// The root the tool-calling kit's declarations name: the product's
/// descriptions carry their sandbox's path, and the kit's say this one.
constexpr std::string_view kKitRoot = "/work/project";

[[nodiscard]] apogee::training::Kit bundled_kit(std::string_view name) {
    for (const apogee::harness::BundledKit& kit : apogee::harness::bundled_kits()) {
        if (kit.name == name) {
            apogee::training::Kit parsed = apogee::training::parse_kit(kit.text, std::string{name});
            parsed.name = std::string{name};
            return parsed;
        }
    }
    FAIL("no bundled kit " << name);
    return {};
}

[[nodiscard]] std::string rtrim(std::string text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) {
        text.pop_back();
    }
    return text;
}

// --- the product's own words, from the product ----------------------------

/// What `rewrite_request` puts before the conversation's first line.
[[nodiscard]] std::string rewrite_head() {
    const std::vector<apogee::harness::ChatMessage> history{
        apogee::harness::ChatMessage::user("QQQ1"), apogee::harness::ChatMessage::assistant("AAA1"),
        apogee::harness::ChatMessage::user("QQQ2")};
    const std::string text = apogee::agentloop::rewrite_request("u", history, "QQQ2")
                                 .messages.front()
                                 .content.plain_text();
    return text.substr(0, text.find("User: QQQ1"));
}

/// What `summary_request` puts before the tool's name.
[[nodiscard]] std::string summary_head() {
    apogee::harness::ToolCall call;
    call.name = "TTT";
    call.arguments = "{}";
    const std::string text =
        apogee::agentloop::summary_request("u", call, "OOO").messages.front().content.plain_text();
    return text.substr(0, text.find("TTT"));
}

/// A request of a system prompt and a user turn, carried as one prompt: the
/// system text, a blank line, the user's (a kit holds one turn).
[[nodiscard]] std::string system_head(const std::string& system) {
    return rtrim(system) + "\n\n";
}

/// The shipped symphony stage's prompt, up to its variable.
struct StageHead {
    std::string prompt;
    std::string variable;
    std::string schema;
};

[[nodiscard]] StageHead stage_head(std::string_view symphony, std::string_view stage) {
    const apogee::harness::BundledSymphony* bundled =
        apogee::harness::find_bundled_symphony(symphony);
    REQUIRE(bundled != nullptr);
    const apogee::harness::SymphonySpec spec =
        apogee::harness::parse_symphony_spec(bundled->text, "shipped", std::string{symphony});
    for (const apogee::harness::SymphonyStage& candidate : spec.stages) {
        if (candidate.name == stage) {
            const std::vector<std::string> variables =
                apogee::symphony::template_variables(candidate.prompt);
            REQUIRE(variables.size() == 1);
            return StageHead{candidate.prompt, variables.front(), candidate.schema};
        }
    }
    FAIL("no stage " << stage << " in " << symphony);
    return {};
}

/// The stage each extraction prefix is: its symphony and stage.
[[nodiscard]] std::optional<StageHead> stage_of(std::string_view prefix) {
    if (prefix == "extract-facts") {
        return stage_head("extract-facts", "extract");
    }
    if (prefix == "extract-actions") {
        return stage_head("extract-actions", "extract");
    }
    if (prefix == "digest-card") {
        return stage_head("digest", "card");
    }
    return std::nullopt;
}

/// The product's tools, each as the declaration a template is handed --
/// `{"name", "description", "parameters"}` -- with the sandbox's path said
/// as the kit's root.
[[nodiscard]] const std::map<std::string, nlohmann::json>& product_tools() {
    static const std::map<std::string, nlohmann::json> tools = [] {
        const apogee::testing::TempDir root{"role-kits-tools-" +
                                            std::to_string(std::random_device{}())};
        apogee::agent::ToolRegistry registry;
        apogee::tools::register_fs_tools(registry, root.path());
        apogee::tools::register_shell_tool(registry, root.path(), std::chrono::seconds{30});
        apogee::tools::GitOptions git;
        git.working_directory = root.path();
        apogee::tools::register_git_tools(registry, git);
        std::vector<std::string> roots{fs::weakly_canonical(root.path()).string(),
                                       root.path().string()};
        std::map<std::string, nlohmann::json> out;
        for (const std::string& name : registry.names()) {
            const apogee::agent::Tool* tool = registry.find(name);
            std::string description = tool->description;
            for (const std::string& path : roots) {
                for (std::size_t at = description.find(path); at != std::string::npos;
                     at = description.find(path, at)) {
                    description.replace(at, path.size(), kKitRoot);
                    at += kKitRoot.size();
                }
            }
            out[name] =
                nlohmann::json{{"name", name},
                               {"description", description},
                               {"parameters", nlohmann::json::parse(tool->parameters_schema)}};
        }
        return out;
    }();
    return tools;
}

/// The kit's own head of a tool-calling prefix, before its declarations.
constexpr std::string_view kCallHead =
    "You can call the tools below, each declared on one line as it is declared to you. Choose "
    "the one call that does what the request asks next, and reply with that call alone, as "
    "JSON: {\"name\": \"<tool>\", \"arguments\": {<its arguments, as its parameters declare "
    "them>}}\n\nTools:\n";
constexpr std::string_view kCallTail = "\n\nRequest: ";

/// The tools a tool-calling prefix declares, each as written; empty with
/// `problem` said when the prefix is not in the kit's shape.
[[nodiscard]] std::vector<nlohmann::json> declared_tools(std::string_view prefix,
                                                         std::string& problem) {
    if (!prefix.starts_with(kCallHead) || !prefix.ends_with(kCallTail)) {
        problem = "not the tool-calling head and tail";
        return {};
    }
    std::string_view lines =
        prefix.substr(kCallHead.size(), prefix.size() - kCallHead.size() - kCallTail.size());
    std::vector<nlohmann::json> out;
    while (!lines.empty()) {
        const std::size_t end = lines.find('\n');
        const nlohmann::json tool = nlohmann::json::parse(lines.substr(0, end), nullptr, false);
        if (tool.is_discarded()) {
            problem = "a declaration that is not JSON";
            return {};
        }
        out.push_back(tool);
        lines = end == std::string_view::npos ? std::string_view{} : lines.substr(end + 1);
    }
    return out;
}

/// Why `prefix`, as `kit` holds it, is not the product's own text; empty
/// when it is.
[[nodiscard]] std::string prefix_problem(std::string_view kit,
                                         const apogee::training::KitPrefix& prefix) {
    std::string expected;
    if (kit == "utility-chores" && prefix.name == "rewrite") {
        expected = rewrite_head();
    } else if (kit == "utility-chores" && prefix.name == "tool-summary") {
        expected = summary_head();
    } else if (kit == "extraction-fidelity" && prefix.name == "graph-extract") {
        expected = system_head(apogee::graph::extract_system_prompt());
    } else if (kit == "knowledge-capture" && prefix.name == "capture") {
        expected = system_head(apogee::knowledge::capture_system_prompt());
    } else if (kit == "extraction-fidelity") {
        const std::optional<StageHead> stage = stage_of(prefix.name);
        if (!stage.has_value()) {
            return "no such extraction prefix";
        }
        expected = stage->prompt.substr(0, stage->prompt.find("{{" + stage->variable + "}}"));
    } else if (kit == "tool-calling") {
        std::string problem;
        const std::vector<nlohmann::json> tools = declared_tools(prefix.text, problem);
        if (!problem.empty()) {
            return problem;
        }
        if (tools.empty()) {
            return "declares no tools";
        }
        for (const nlohmann::json& tool : tools) {
            const std::string name = tool.value("name", "");
            const auto product = product_tools().find(name);
            if (product == product_tools().end()) {
                return "declares '" + name + "', which the product has no tool by";
            }
            if (tool != product->second) {
                return "declares '" + name + "' otherwise than the product does";
            }
        }
        return {};
    } else {
        return "a prefix no product request has";
    }
    return prefix.text == expected ? std::string{} : "differs from the product's own text";
}

/// Why `prompt`, beginning with `prefix`, is not the product's request
/// rebuilt; empty when it is. Trailing whitespace is not compared: the synth
/// core trims what a teacher wrote.
[[nodiscard]] std::string prompt_problem(std::string_view kit,
                                         const apogee::training::KitPrefix& prefix,
                                         const std::string& prompt) {
    if (!prompt.starts_with(prefix.text)) {
        return "does not begin with its prefix";
    }
    const std::string rest = prompt.substr(prefix.text.size());
    if (rtrim(rest).empty()) {
        return "nothing after its prefix";
    }
    if (prefix.name == "rewrite") {
        constexpr std::string_view kLast = "\n\nLast question: ";
        const std::size_t at = rest.find(kLast);
        if (at == std::string::npos) {
            return "no 'Last question: ' after the conversation";
        }
        std::vector<apogee::harness::ChatMessage> history;
        std::string_view lines = std::string_view{rest}.substr(0, at);
        while (!lines.empty()) {
            const std::size_t end = lines.find('\n');
            const std::string_view line = lines.substr(0, end);
            if (line.starts_with("User: ")) {
                history.push_back(apogee::harness::ChatMessage::user(std::string{line.substr(6)}));
            } else if (line.starts_with("Assistant: ")) {
                history.push_back(
                    apogee::harness::ChatMessage::assistant(std::string{line.substr(11)}));
            } else {
                return "a conversation line that is neither 'User: ' nor 'Assistant: '";
            }
            lines = end == std::string_view::npos ? std::string_view{} : lines.substr(end + 1);
        }
        if (history.empty()) {
            return "no conversation before the question";
        }
        const std::string question = rest.substr(at + kLast.size());
        history.push_back(apogee::harness::ChatMessage::user(question));
        const std::string rebuilt = apogee::agentloop::rewrite_request("u", history, question)
                                        .messages.front()
                                        .content.plain_text();
        return rtrim(rebuilt) == rtrim(prompt) ? std::string{}
                                               : "rewrite_request does not rebuild it";
    }
    if (prefix.name == "tool-summary") {
        constexpr std::string_view kArguments = "\nArguments: ";
        constexpr std::string_view kOutput = "\n\nOutput:\n";
        const std::size_t name_end = rest.find(kArguments);
        const std::size_t output_at = rest.find(kOutput);
        if (name_end == std::string::npos || output_at == std::string::npos ||
            output_at < name_end) {
            return "not a tool's name, its arguments, then its output";
        }
        apogee::harness::ToolCall call;
        call.name = rest.substr(0, name_end);
        call.arguments =
            rest.substr(name_end + kArguments.size(), output_at - name_end - kArguments.size());
        if (!product_tools().contains(call.name)) {
            return "summarises '" + call.name + "', which the product has no tool by";
        }
        const std::string rebuilt =
            apogee::agentloop::summary_request("u", call, rest.substr(output_at + kOutput.size()))
                .messages.front()
                .content.plain_text();
        return rtrim(rebuilt) == rtrim(prompt) ? std::string{}
                                               : "summary_request does not rebuild it";
    }
    if (const std::optional<StageHead> stage = stage_of(prefix.name); stage.has_value()) {
        const std::string rebuilt =
            apogee::symphony::render_template(stage->prompt, {{stage->variable, rtrim(rest)}});
        return rtrim(rebuilt) == rtrim(prompt) ? std::string{}
                                               : "the stage's template does not render it";
    }
    if (kit == "tool-calling") {
        return rest.find('\n') == std::string::npos ? std::string{}
                                                    : "a request of more than one line";
    }
    return {};  // a system turn carried whole: the user's turn is free text
}

/// Why `completion`, answering a prompt that begins with `prefix`, is not
/// what the product accepts; empty when it is.
[[nodiscard]] std::string completion_problem(std::string_view kit,
                                             const apogee::training::KitPrefix& prefix,
                                             const std::string& completion) {
    const auto conforms = [&completion](const nlohmann::json& schema) -> std::string {
        const std::optional<nlohmann::json> json = apogee::agentloop::extract_json(completion);
        if (!json.has_value()) {
            return "not JSON";
        }
        const apogee::agentloop::ValidationResult result =
            apogee::agentloop::validate_against(schema, *json);
        return result.ok ? std::string{} : "off its schema: " + result.errors.front();
    };
    if (prefix.name == "rewrite") {
        // The product reads the first line, and refuses a query past 400 bytes.
        return completion.find('\n') == std::string::npos && completion.size() <= 400
                   ? std::string{}
                   : "not one line of a query";
    }
    if (prefix.name == "tool-summary") {
        return rtrim(completion).empty() ? "empty" : "";
    }
    if (prefix.name == "graph-extract") {
        return conforms(apogee::graph::extract_schema());
    }
    if (prefix.name == "capture") {
        return conforms(apogee::knowledge::capture_schema());
    }
    if (const std::optional<StageHead> stage = stage_of(prefix.name); stage.has_value()) {
        return conforms(nlohmann::json::parse(stage->schema));
    }
    if (kit == "tool-calling") {
        const nlohmann::json call = nlohmann::json::parse(completion, nullptr, false);
        if (call.is_discarded() || !call.is_object() || call.size() != 2 ||
            !call.contains("name") || !call.contains("arguments")) {
            return "not {\"name\", \"arguments\"}";
        }
        std::string problem;
        for (const nlohmann::json& tool : declared_tools(prefix.text, problem)) {
            if (tool["name"] == call["name"]) {
                const apogee::agentloop::ValidationResult result =
                    apogee::agentloop::validate_against(tool["parameters"], call["arguments"]);
                return result.ok ? std::string{}
                                 : "arguments off the tool's parameters: " + result.errors.front();
            }
        }
        return "calls a tool its prompt does not declare";
    }
    return "a prefix no product request has";
}

[[nodiscard]] const apogee::training::KitPrefix* prefix_of(const apogee::training::Kit& kit,
                                                           const std::string& prompt) {
    const apogee::training::KitPrefix* found = nullptr;
    for (const apogee::training::KitPrefix& prefix : kit.prefixes) {
        if (prompt.starts_with(prefix.text) &&
            (found == nullptr || prefix.text.size() > found->text.size())) {
            found = &prefix;
        }
    }
    return found;
}

const std::vector<std::string> kRoleKits{"utility-chores", "extraction-fidelity", "tool-calling",
                                         "knowledge-capture"};

/// One example of each of a kit's prefixes, in the product's format, as a
/// teacher would write it -- and one in no format of the kit's.
[[nodiscard]] nlohmann::json scripted_examples(std::string_view kit) {
    nlohmann::json rows = nlohmann::json::array();
    const auto row = [&rows](std::string prefix, std::string prompt, std::string completion) {
        rows.push_back({{"prefix", std::move(prefix)},
                        {"prompt", std::move(prompt)},
                        {"completion", std::move(completion)}});
    };
    if (kit == "utility-chores") {
        row("rewrite",
            "User: Where is Lake Baikal?\nAssistant: In southern Siberia; it is the deepest lake "
            "on Earth.\n\nLast question: How deep is it?",
            "How deep is Lake Baikal?");
        row("tool-summary",
            "run_command\nArguments: {\"command\":\"ls build\"}\n\nOutput:\napp\napp.o\n[exit 0]",
            "Two entries in build: the app binary and its object file app.o; the command "
            "succeeded (exit 0).");
    } else if (kit == "extraction-fidelity") {
        row("graph-extract",
            "Ada Lovelace wrote the first published algorithm for Babbage's engine.",
            R"({"entities": [{"name": "Ada Lovelace", "type": "person", "description": "Wrote the first published algorithm."}, {"name": "Analytical Engine", "type": "artifact", "description": ""}], "relations": [{"source": "Ada Lovelace", "target": "Analytical Engine", "relation": "wrote an algorithm for", "description": ""}]})");
        row("extract-facts", "The Great Fire of London began on 2 September 1666.",
            R"({"topic": "The Great Fire of London", "people": [], "places": ["London"], "dates": ["2 September 1666"], "facts": ["The Great Fire of London began on 2 September 1666."]})");
        row("extract-actions", "Jo will fix the login bug. Someone should update the docs.",
            R"({"actions": [{"owner": "Jo", "action": "Fix the login bug", "due": "not stated"}, {"owner": "unassigned", "action": "Update the docs", "due": "not stated"}]})");
        row("digest-card", "The town replaced its streetlights with LEDs, halving its power bill.",
            R"({"title": "LED streetlights halve the bill", "tags": ["energy", "lighting", "towns"], "one_line": "The town's switch to LED streetlights halved its power bill."})");
    } else if (kit == "tool-calling") {
        row("git-tools", "Show me the last 3 commits.",
            R"({"name": "git_log", "arguments": {"n": 3}})");
        row("edit-tools", "Create notes.md saying hello.",
            R"({"name": "write_file", "arguments": {"path": "notes.md", "content": "hello\n"}})");
    } else if (kit == "knowledge-capture") {
        row("capture",
            "Ola: we keep the nightly backups at 02:00, since daytime runs slowed the API for "
            "everyone. Tracked in OPS-9.",
            R"({"intent": "daytime runs slowed the API for everyone", "decision": "Keep the nightly backups at 02:00", "status": "shipped", "discipline": "eng", "downstream_link": "OPS-9", "provenance": {"source": "chat", "attribution": "Ola"}})");
    }
    // In no format of the kit's: a prefix it does not declare.
    row("no-such-prefix", "Say hello.", "hello");
    return rows;
}

}  // namespace

TEST_CASE("every prefix of the role-chore kits is the product's own text, byte for byte",
          "[training][kits][role-kits]") {
    for (const std::string& name : kRoleKits) {
        const apogee::training::Kit kit = bundled_kit(name);
        INFO(name);
        REQUIRE_FALSE(kit.prefixes.empty());
        for (const apogee::training::KitPrefix& prefix : kit.prefixes) {
            INFO(prefix.name);
            CHECK(prefix_problem(name, prefix).empty());
        }
    }
    // Every product tool the kit trains toward is declared somewhere in it.
    const apogee::training::Kit calling = bundled_kit("tool-calling");
    std::set<std::string> declared;
    for (const apogee::training::KitPrefix& prefix : calling.prefixes) {
        std::string problem;
        for (const nlohmann::json& tool : declared_tools(prefix.text, problem)) {
            declared.insert(tool["name"].get<std::string>());
        }
    }
    CHECK(declared == std::set<std::string>{"read_file", "list_directory", "search_files",
                                            "grep_files", "write_file", "edit_file", "delete_file",
                                            "run_command", "git_status", "git_log", "git_diff",
                                            "git_show"});
}

TEST_CASE(
    "every eval prompt of the role-chore kits rebuilds through the product's own request, "
    "and fits the trainers' window",
    "[training][kits][role-kits]") {
    for (const std::string& name : kRoleKits) {
        const apogee::training::Kit kit = bundled_kit(name);
        INFO(name);
        CHECK(kit.eval.size() >= 24);
        std::set<std::string> used;
        for (const apogee::training::EvalItem& item : kit.eval) {
            INFO(item.prompt.substr(0, 160));
            const apogee::training::KitPrefix* prefix = prefix_of(kit, item.prompt);
            REQUIRE(prefix != nullptr);
            used.insert(prefix->name);
            CHECK(prompt_problem(name, *prefix, item.prompt).empty());
            // The drivers train and infer within 2,048 tokens; a prompt this
            // size leaves the answer room at any tokenizer's rate.
            CHECK(item.prompt.size() <= 6000);
            CHECK_FALSE(item.expected.empty());
            if (name == "tool-calling") {
                // The call's name, in the form the prompt asks for.
                std::string problem;
                bool declared = false;
                for (const nlohmann::json& tool : declared_tools(prefix->text, problem)) {
                    declared = declared || item.expected == "\"name\": " + tool["name"].dump();
                }
                CHECK(declared);
            }
        }
        // Every prefix gates something.
        CHECK(used.size() == kit.prefixes.size());
    }
}

TEST_CASE(
    "a synth round of the role-chore kits over a scripted teacher yields the product's "
    "requests and answers its contracts accept; an example in no format of the kit's is "
    "dropped",
    "[training][kits][role-kits][synth]") {
    for (const std::string& name : kRoleKits) {
        const apogee::training::Kit kit = bundled_kit(name);
        INFO(name);
        // The teacher is shown each prefix once, and asked to name one.
        const std::string system = apogee::training::synth_system_prompt(kit);
        for (const apogee::training::KitPrefix& prefix : kit.prefixes) {
            CHECK(system.find(prefix.text) != std::string::npos);
        }
        CHECK(system.find("\"prefix\"") != std::string::npos);

        const nlohmann::json rows = scripted_examples(name);
        apogee::training::SynthOptions options;
        options.count = static_cast<int>(rows.size()) - 1;
        const apogee::training::SynthResult result = apogee::training::synthesize(
            kit,
            [&rows](std::string_view, std::string_view, double, int,
                    const apogee::harness::CancellationToken&) {
                return apogee::training::GenerateOutcome{.ok = true, .text = rows.dump()};
            },
            options);
        REQUIRE(result.examples.size() == rows.size() - 1);
        for (const apogee::training::SynthExample& example : result.examples) {
            INFO(example.prefix);
            const apogee::training::KitPrefix* prefix =
                apogee::training::find_prefix(kit, example.prefix);
            REQUIRE(prefix != nullptr);
            CHECK(prompt_problem(name, *prefix, example.prompt).empty());
            CHECK(completion_problem(name, *prefix, example.completion).empty());
        }
        // The dataset line carries the whole prompt as the user's turn.
        const std::vector<std::string> lines = apogee::training::example_lines(result.examples);
        REQUIRE(lines.size() == result.examples.size());
        const nlohmann::json first = nlohmann::json::parse(lines.front());
        CHECK(first["messages"][0]["content"] == result.examples.front().prompt);
    }
}

TEST_CASE("a planted format drift fails the mirror: a prefix, a prompt, a tool, a completion",
          "[training][kits][role-kits]") {
    const apogee::training::Kit utility = bundled_kit("utility-chores");
    apogee::training::KitPrefix rewrite = *apogee::training::find_prefix(utility, "rewrite");
    REQUIRE(prefix_problem("utility-chores", rewrite).empty());
    // A word of the product's instruction changed in the kit.
    rewrite.text.replace(rewrite.text.find("standalone"), 10, "stand-alone");
    CHECK(prefix_problem("utility-chores", rewrite) == "differs from the product's own text");

    // A rewrite the product would not have sent: no question line.
    const apogee::training::KitPrefix& kept = *apogee::training::find_prefix(utility, "rewrite");
    CHECK_FALSE(prompt_problem("utility-chores", kept,
                               kept.text + "User: hi\nAssistant: hello\n\nQuestion: and then?")
                    .empty());
    // A conversation longer than the product shows is not its request.
    std::string long_conversation;
    for (int i = 0; i < 4; ++i) {
        long_conversation +=
            "User: q" + std::to_string(i) + "\nAssistant: a" + std::to_string(i) + "\n";
    }
    CHECK(prompt_problem("utility-chores", kept,
                         kept.text + long_conversation + "\nLast question: and then?") ==
          "rewrite_request does not rebuild it");

    // A tool declared with a parameter the product's tool does not have.
    const apogee::training::Kit calling = bundled_kit("tool-calling");
    apogee::training::KitPrefix git = *apogee::training::find_prefix(calling, "git-tools");
    git.text.replace(git.text.find("\"range\""), 7, "\"revs\"");
    CHECK(prefix_problem("tool-calling", git) ==
          "declares 'git_log' otherwise than the product does");
    // A call whose arguments the tool's schema refuses.
    CHECK_FALSE(completion_problem("tool-calling",
                                   *apogee::training::find_prefix(calling, "git-tools"),
                                   R"({"name": "git_log", "arguments": {"n": "three"}})")
                    .empty());

    // A capture record missing its status.
    const apogee::training::Kit capture = bundled_kit("knowledge-capture");
    CHECK_FALSE(
        completion_problem(
            "knowledge-capture", *apogee::training::find_prefix(capture, "capture"),
            R"({"intent": "x", "decision": "y", "discipline": "eng", "provenance": {"source": "chat"}})")
            .empty());
}

TEST_CASE("nothing the role-chore kits or their regimes say names a vendor or a model family",
          "[training][kits][role-kits]") {
    // The product's own words in a prefix are the product's, held by the
    // mirror; everything the kit says for itself is checked here.
    const std::regex vendor{
        R"(\b(openai|anthropic|claude|chatgpt|gpt(-?\d+)?|gemini|google|llama|qwen|mistral|mixtral|gemma|deepseek|phi-?\d|copilot|ollama|meta)\b)",
        std::regex::icase};
    const auto clean = [&vendor](std::string_view what, const std::string& text) {
        INFO(what << ": " << text.substr(0, 200));
        CHECK_FALSE(std::regex_search(text, vendor));
    };
    for (const std::string& name : kRoleKits) {
        const apogee::training::Kit kit = bundled_kit(name);
        INFO(name);
        clean("description", kit.description);
        clean("synth.system", kit.synth.system);
        for (const std::string& seed : kit.synth.seeds) {
            clean("seed", seed);
        }
        for (const apogee::training::EvalItem& item : kit.eval) {
            const apogee::training::KitPrefix* prefix = prefix_of(kit, item.prompt);
            REQUIRE(prefix != nullptr);
            clean("eval prompt", item.prompt.substr(prefix->text.size()));
            clean("eval expected", item.expected);
        }
    }
    for (const std::string_view regime : {"harness-helpers", "knowledge"}) {
        const apogee::harness::BundledRegime* bundled =
            apogee::harness::find_bundled_regime(regime);
        REQUIRE(bundled != nullptr);
        clean(regime, std::string{bundled->text});
    }
    // And the check bites.
    CHECK(std::regex_search(std::string{"distil it from Qwen first"}, vendor));
}
