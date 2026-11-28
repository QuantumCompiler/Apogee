#include "cli/mcp_serve.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "agentloop/graph_context.h"
#include "cli/complete.h"
#include "contracts/config.h"
#include "contracts/layout.h"
#include "contracts/mcp_serve.h"
#include "contracts/paths.h"
#include "contracts/suggested_command.h"
#include "contracts/utf8.h"
#include "embedstore/store.h"
#include "knowledge/record.h"
#include "knowledge/store.h"
#include "modelstore/sidecar.h"
#include "modelstore/snapshot.h"
#include "operations/collections.h"
#include "secrets/store.h"
#include "support/cli_home.h"
#include "support/env_guard.h"
#include "support/gguf_builder.h"
#include "tools/toolsets.h"
#include "training/store.h"

/// `apogee mcp serve` (54a) through the real command tree in a throwaway
/// install: the handshake with the grown set, the hidden alias answering
/// the same, each status read's text byte for byte the document its
/// command prints with `--output-format json`, a read's refusals said with
/// the command's own reason, and the verb's help and completion.
namespace {

using apogee::testing::CliHome;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;

constexpr std::string_view kConfig = R"(backends:
  local:
    type: mock
    model: mock-1
  spare:
    type: mock
    model: mock-2

models:
  default: local
)";

/// What a server answered: its exit code and its frames, by id.
struct Served {
    int code = -1;
    std::vector<nlohmann::json> frames;
    std::string err;

    [[nodiscard]] const nlohmann::json& reply(int id) const {
        for (const nlohmann::json& frame : frames) {
            if (frame.value("id", -1) == id) {
                return frame;
            }
        }
        FAIL("no reply to request " << id);
        return frames.front();
    }
};

/// `apogee <spelling...>` run against `home` with `requests` on its stdin,
/// one JSON-RPC frame a line; stdout must carry frames and nothing else.
[[nodiscard]] Served serve(const CliHome& home, const std::vector<std::string>& spelling,
                           const std::vector<nlohmann::json>& requests) {
    std::string input;
    for (const nlohmann::json& request : requests) {
        input += request.dump() + "\n";
    }
    std::istringstream in{input};
    std::streambuf* old_in = std::cin.rdbuf(in.rdbuf());
    Served served;
    std::string out;
    try {
        served.code = home.run(spelling, &out, &served.err);
    } catch (...) {
        std::cin.rdbuf(old_in);
        throw;
    }
    std::cin.rdbuf(old_in);
    std::istringstream lines{out};
    std::string line;
    while (std::getline(lines, line)) {
        const nlohmann::json frame = nlohmann::json::parse(line, nullptr, false);
        INFO("stdout carried a line that is not JSON-RPC: " << line);
        REQUIRE(frame.is_object());
        served.frames.push_back(frame);
    }
    return served;
}

[[nodiscard]] nlohmann::json initialize() {
    return {{"jsonrpc", "2.0"},
            {"id", 1},
            {"method", "initialize"},
            {"params",
             {{"protocolVersion", "2025-03-26"}, {"capabilities", nlohmann::json::object()}}}};
}

[[nodiscard]] nlohmann::json list_tools() {
    return {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}};
}

[[nodiscard]] nlohmann::json call(int id, std::string_view tool,
                                  nlohmann::json arguments = nlohmann::json::object()) {
    return {{"jsonrpc", "2.0"},
            {"id", id},
            {"method", "tools/call"},
            {"params", {{"name", tool}, {"arguments", std::move(arguments)}}}};
}

/// A call's text, and whether it was an error.
struct Answer {
    std::string text;
    bool is_error = false;
};

[[nodiscard]] Answer answer_of(const Served& served, int id) {
    const nlohmann::json& reply = served.reply(id);
    INFO(reply.dump());
    REQUIRE(reply.contains("result"));
    return Answer{reply["result"]["content"][0]["text"].get<std::string>(),
                  reply["result"]["isError"].get<bool>()};
}

/// What the command printed on stdout with `--output-format json`.
[[nodiscard]] std::string printed(const CliHome& home, std::vector<std::string> args) {
    args.emplace_back("--output-format");
    args.emplace_back("json");
    std::string out;
    std::string err;
    (void)home.run(args, &out, &err);  // `check` exits 1 on a failure, its document printed
    INFO(err);
    REQUIRE_FALSE(out.empty());
    return out;
}

/// A promoted backend's ledger, written as promote writes it.
void plant_ledger(const CliHome& home) {
    apogee::training::VersionLedger ledger;
    ledger.backend = "local";
    ledger.active_version = 2;
    ledger.versions.push_back(apogee::training::VersionEntry{
        .version = 1,
        .run_id = "run-1",
        .gguf_path = (home.home() / "models" / "local" / "v1.gguf").string(),
        .promoted_at = "2026-11-01T09:00:00Z",
        .eval_score = 0.75,
        .eval_passed = false,
    });
    ledger.versions.push_back(apogee::training::VersionEntry{
        .version = 2,
        .run_id = "run-2",
        .gguf_path = (home.home() / "models" / "local" / "v2.gguf").string(),
        .promoted_at = "2026-11-01T10:00:00Z",
        .eval_score = 1.0,
        .eval_passed = true,
    });
    REQUIRE(apogee::training::save_ledger(home.home() / "training" / "versions", ledger).empty());
}

/// A SafeTensors snapshot a conversion consumed, and the GGUF made from it:
/// `models list` folds the snapshot unless `--all`, so the two documents
/// differ and `all` is seen to reach the read.
void plant_consumed_snapshot(const CliHome& home) {
    const std::filesystem::path snapshot =
        home.models() / "org--m" / "safetensors" / "aaaaaaaaaaaa";
    std::filesystem::create_directories(snapshot);
    std::ofstream{snapshot / "config.json"} << R"({"architectures": ["LlamaForCausalLM"]})";
    std::ofstream{snapshot / "model.safetensors"} << "weights";
    apogee::models::Snapshot record;
    record.ref = "org/m";
    record.revision = "main";
    record.source = "huggingface";
    record.files.push_back({.path = "model.safetensors", .size = 7, .sha256 = "abc"});
    REQUIRE(apogee::models::write_snapshot(snapshot, record));

    const std::filesystem::path f16 =
        home.models() / "org--m" / "gguf" / "111111111111" / "m-F16.gguf";
    std::filesystem::create_directories(f16.parent_path());
    std::ofstream{f16, std::ios::binary} << apogee::testing::minimal_gguf("llama");
    apogee::models::Sidecar converted;
    converted.source = "convert";
    converted.transform = "convert";
    converted.ref = "org--m/safetensors/aaaaaaaaaaaa";
    converted.transform_note = "--outtype f16";
    REQUIRE(apogee::models::write_sidecar(f16, converted));
}

}  // namespace

TEST_CASE("mcp serve answers the handshake with the native read tools, then the status reads",
          "[cli][mcp][serve]") {
    const CliHome home{std::string{kConfig}};
    const Served served = serve(home, {"mcp", "serve"}, {initialize(), list_tools()});
    INFO(served.err);
    REQUIRE(served.code == 0);
    CHECK(served.reply(1)["result"]["serverInfo"]["name"] == "apogee-tools");
    CHECK(served.reply(1)["result"]["capabilities"].contains("tools"));

    const nlohmann::json& tools = served.reply(2)["result"]["tools"];
    std::vector<std::string> names;
    for (const nlohmann::json& tool : tools) {
        names.push_back(tool["name"].get<std::string>());
        INFO(names.back());
        CHECK(tool["annotations"]["readOnlyHint"] == true);
        CHECK(tool["inputSchema"]["type"] == "object");
        CHECK_FALSE(tool["description"].get<std::string>().empty());
    }
    // The native read tools first (the spike's seventeen among them), the
    // status reads last, in their order: added, never renamed -- 54a's five,
    // then 58b's graph and knowledge reads after them. Where this build
    // cannot start a program (Windows, today) the four git reads are not
    // served, as they are not offered to a model: each could only fail (57k).
    const bool git = apogee::tools::ToolsetOptions{}.child_processes;
    REQUIRE(names.size() >= 11 + (git ? 17U : 13U));
    const std::vector<std::string> tail(names.end() - 11, names.end());
    CHECK(tail == std::vector<std::string>{"models_list", "models_status", "train_status",
                                           "train_versions", "check", "list_graphs", "graph_stats",
                                           "graph_report", "knowledge_list", "knowledge_info",
                                           "knowledge_query"});
    for (const char* native : {"read_file", "list_directory", "grep_files", "list_notes",
                               "search_documents", "graph_query"}) {
        INFO(native);
        CHECK(std::ranges::find(names, native) != names.end());
    }
    for (const char* reads : {"git_status", "git_log", "git_diff", "git_show"}) {
        INFO(reads);
        CHECK((std::ranges::find(names, reads) != names.end()) == git);
    }
    for (const char* writer : {"write_file", "edit_file", "write_note", "run_command"}) {
        INFO(writer);
        CHECK(std::ranges::find(names, writer) == names.end());
    }
    CHECK(std::set<std::string>(names.begin(), names.end()).size() == names.size());

    // The hidden alias serves the very same set: existing registrations keep
    // working, and gain the reads.
    const Served alias = serve(home, {"__mcp-tools"}, {initialize(), list_tools()});
    REQUIRE(alias.code == 0);
    CHECK(alias.reply(2)["result"]["tools"] == tools);
    CHECK(alias.reply(1)["result"]["serverInfo"] == served.reply(1)["result"]["serverInfo"]);
}

TEST_CASE("each status read answers with its command's own document, byte for byte",
          "[cli][mcp][serve][parity]") {
    const CliHome home{std::string{kConfig}};
    plant_ledger(home);
    plant_consumed_snapshot(home);
    const Served served =
        serve(home, {"mcp", "serve"},
              {initialize(), call(10, "models_list"), call(11, "models_list", {{"all", true}}),
               call(12, "models_status"), call(13, "models_status", {{"suite", "off"}}),
               call(14, "train_status"), call(15, "train_versions"),
               call(16, "train_versions", {{"backend", "local"}}), call(17, "check")});
    INFO(served.err);
    REQUIRE(served.code == 0);

    const auto same = [&](int id, const std::vector<std::string>& args) {
        const Answer answer = answer_of(served, id);
        INFO("tool " << id << " against apogee " << args.front());
        CHECK_FALSE(answer.is_error);
        // The document, without the newline that ends the command's stdout.
        CHECK(answer.text + "\n" == printed(home, args));
    };
    same(10, {"models", "list"});
    same(11, {"models", "list", "--all"});
    same(12, {"models", "status"});
    same(13, {"models", "status", "--suite", "off"});
    same(14, {"train", "status"});
    same(15, {"train", "versions"});
    same(16, {"train", "versions", "local"});
    same(17, {"check"});

    // Not vacuous: each is the document a host renders.
    CHECK(nlohmann::json::parse(answer_of(served, 10).text)["object"] == "list");
    // `all` reaches the read: the consumed snapshot folded, then listed.
    CHECK(nlohmann::json::parse(answer_of(served, 10).text)["folded"] == 1);
    CHECK(answer_of(served, 10).text != answer_of(served, 11).text);
    CHECK_THAT(answer_of(served, 11).text, ContainsSubstring("org--m/safetensors/aaaaaaaaaaaa"));
    CHECK_THAT(answer_of(served, 10).text, ContainsSubstring("\"local\""));
    CHECK(nlohmann::json::parse(answer_of(served, 16).text)["active_version"] == 2);
    CHECK(nlohmann::json::parse(answer_of(served, 17).text).contains("rows"));
    CHECK(nlohmann::json::parse(answer_of(served, 14).text).contains("versions"));
}

TEST_CASE("a status read reads at its call: a promotion after the server started is seen",
          "[cli][mcp][serve]") {
    // One set of reads, built once as the server builds them at its start,
    // asked before and after a promotion lands: each opens what it reads
    // when asked, as the command does when it runs.
    const CliHome home{std::string{kConfig}};
    apogee::commands::RootContext context;
    context.config_path = home.config_path().string();
    const std::vector<apogee::mcp::ServedRead> reads = apogee::commands::status_reads(context);
    const auto versions =
        std::ranges::find(reads, "train_versions", &apogee::mcp::ServedRead::name);
    REQUIRE(versions != reads.end());
    const apogee::mcp::ToolCallResult before = versions->answer(nlohmann::json::object());
    CHECK(nlohmann::json::parse(before.text)["data"].empty());
    plant_ledger(home);
    const apogee::mcp::ToolCallResult after = versions->answer(nlohmann::json::object());
    CHECK(nlohmann::json::parse(after.text)["data"].size() == 1);
    CHECK(after.text + "\n" == printed(home, {"train", "versions"}));
}

TEST_CASE("a status read that cannot answer says the command's own reason, and the server goes on",
          "[cli][mcp][serve]") {
    const CliHome home{std::string{kConfig}};
    const Served served = serve(
        home, {"mcp", "serve"},
        {call(1, "train_versions", {{"backend", "nope"}}),
         call(2, "models_status", {{"suite", "nope"}}), call(3, "models_list", {{"all", "maybe"}}),
         call(4, "train_versions", {{"backend", 5}}), call(5, "models_list", {{"all", "true"}}),
         call(6, "list_notes")});
    REQUIRE(served.code == 0);

    std::string out;
    std::string err;
    CHECK(home.run({"train", "versions", "nope", "--output-format", "json"}, &out, &err) != 0);
    CHECK(out.empty());
    const Answer missing = answer_of(served, 1);
    CHECK(missing.is_error);
    CHECK(missing.text == "Error: no version history for backend 'nope'");
    CHECK_THAT(err, ContainsSubstring("no version history for backend 'nope'"));

    CHECK(home.run({"models", "status", "--suite", "nope", "--output-format", "json"}, &out,
                   &err) != 0);
    const Answer suite = answer_of(served, 2);
    CHECK(suite.is_error);
    CHECK_THAT(suite.text, StartsWith("Error: suite: "));
    // The refusal's own words, as the command says them after `--suite: `.
    CHECK_THAT(err, ContainsSubstring(suite.text.substr(std::string{"Error: suite: "}.size())));

    CHECK(answer_of(served, 3).is_error);
    CHECK(answer_of(served, 3).text == "Error: all must be true or false");
    CHECK(answer_of(served, 4).is_error);
    CHECK(answer_of(served, 4).text == "Error: backend must be a string");
    // A string a host's model sends for a boolean is read, as the graph
    // tools read one.
    CHECK_FALSE(answer_of(served, 5).is_error);
    CHECK(answer_of(served, 5).text + "\n" == printed(home, {"models", "list", "--all"}));
    CHECK_FALSE(answer_of(served, 6).is_error);
}

TEST_CASE("a config that will not load: the reads say why, as their commands do",
          "[cli][mcp][serve]") {
    const CliHome home{"backends: [this is not\n"};
    const Served served = serve(home, {"mcp", "serve"},
                                {list_tools(), call(10, "models_list"), call(11, "models_status")});
    REQUIRE(served.code == 0);
    CHECK_FALSE(served.reply(2)["result"]["tools"].empty());

    std::string out;
    std::string err;
    CHECK(home.run({"models", "list", "--output-format", "json"}, &out, &err) != 0);
    CHECK(out.empty());
    for (const int id : {10, 11}) {
        const Answer answer = answer_of(served, id);
        INFO(answer.text);
        CHECK(answer.is_error);
        CHECK_THAT(answer.text, StartsWith("Error: "));
        // The same reason the command gives, after its own prefix.
        CHECK_THAT(err, ContainsSubstring(answer.text.substr(std::string{"Error: "}.size())));
    }
}

TEST_CASE("mcp serve is a public verb: in help and completion, the alias in neither",
          "[cli][mcp][serve][completion]") {
    const CliHome home{std::string{kConfig}};
    std::string out;
    std::string err;
    REQUIRE(home.run({"mcp", "--help"}, &out, &err) == 0);
    CHECK_THAT(out, ContainsSubstring("serve"));
    CHECK_THAT(out, ContainsSubstring("MCP server on stdio"));
    REQUIRE(home.run({"mcp", "serve", "--help"}, &out, &err) == 0);
    CHECK_THAT(out, ContainsSubstring("apogee mcp presets"));
    CHECK_THAT(out, ContainsSubstring("models_list, models_status, train_status, train_versions, "
                                      "check"));
    // 58b's reads, named where 54a's are.
    CHECK_THAT(out, ContainsSubstring("list_graphs, graph_stats, graph_report, knowledge_list, "
                                      "knowledge_info, knowledge_query"));

    REQUIRE(home.run({"__complete", "mcp", ""}, &out, &err) == 0);
    CHECK_THAT(out, ContainsSubstring("serve\n"));
    REQUIRE(home.run({"__complete", "mcp", "se"}, &out, &err) == 0);
    CHECK(out == "serve\n");
    REQUIRE(home.run({"__complete", ""}, &out, &err) == 0);
    CHECK_THAT(out, !ContainsSubstring("__mcp-tools"));
    REQUIRE(home.run({"--help"}, &out, &err) == 0);
    CHECK_THAT(out, !ContainsSubstring("__mcp-tools"));
}

TEST_CASE("the status reads are named once, in order", "[cli][mcp][serve]") {
    const std::vector<std::string_view> names{apogee::commands::status_read_names().begin(),
                                              apogee::commands::status_read_names().end()};
    CHECK(names == std::vector<std::string_view>{"models_list", "models_status", "train_status",
                                                 "train_versions", "check", "list_graphs",
                                                 "graph_stats", "graph_report", "knowledge_list",
                                                 "knowledge_info", "knowledge_query"});
    const apogee::commands::RootContext context;
    const std::vector<apogee::mcp::ServedRead> reads = apogee::commands::status_reads(context);
    REQUIRE(reads.size() == names.size());
    for (std::size_t i = 0; i < reads.size(); ++i) {
        CHECK(reads[i].name == names[i]);
        CHECK(reads[i].input_schema["type"] == "object");
        CHECK_THAT(reads[i].description, ContainsSubstring("byte for byte"));
    }
    // The reads that cannot answer without an argument say so in their
    // schema (58b); the rest require nothing.
    for (const apogee::mcp::ServedRead& read : reads) {
        INFO(read.name);
        const nlohmann::json required =
            read.input_schema.value("required", nlohmann::json::array());
        if (read.name == "graph_stats") {
            CHECK(required == nlohmann::json::array({"graph"}));
        } else if (read.name == "knowledge_info") {
            CHECK(required == nlohmann::json::array({"id"}));
        } else if (read.name == "knowledge_query") {
            CHECK(required == nlohmann::json::array({"question"}));
        } else {
            CHECK(required.empty());
        }
        for (const auto& [key, property] : read.input_schema["properties"].items()) {
            INFO(key);
            CHECK_FALSE(property.value("description", std::string{}).empty());
        }
    }
}

namespace {

/// Mock scripts outside the install, and a config naming them, for the
/// callable set (54b): `echo` answers with the bound its request carried and
/// the prompt it was asked; `paid` says each call is billed.
struct CallableFiles {
    apogee::testing::TempDir dir{"mcp-serve-calls-" + std::to_string(std::random_device{}())};
    std::filesystem::path echo = dir.path() / "echo.json";
    std::filesystem::path paid = dir.path() / "paid.json";

    CallableFiles() {
        std::ofstream{echo, std::ios::binary}
            << R"({"turns": [{"text": "max_tokens={{max_tokens}} asked={{last_user}}"}]})";
        std::ofstream{paid, std::ios::binary}
            << R"({"metered": true, "turns": [{"text": "paid"}]})";
    }

    /// `local` and `short` answer from `echo` (`short` with its own lower
    /// max_tokens), `spare` is a plain mock and `paid` bills; `listed` is
    /// written into the entries named as a hand edit would.
    [[nodiscard]] std::string config(const std::vector<std::string>& listed = {}) const {
        const auto serve = [&listed](const std::string& name) {
            return std::ranges::find(listed, name) != listed.end()
                       ? std::string{"    mcp_serve: [complete]\n"}
                       : std::string{};
        };
        return "backends:\n  local:\n    type: mock\n    model_path: " + echo.string() + "\n" +
               serve("local") + "  short:\n    type: mock\n    model_path: " + echo.string() +
               "\n    max_tokens: 256\n" + serve("short") +
               "  spare:\n    type: mock\n    model: mock-2\n" + serve("spare") +
               "  paid:\n    type: mock\n    model_path: " + paid.string() + "\n" + serve("paid") +
               "models:\n  default: local\n";
    }
};

[[nodiscard]] std::vector<std::string> names_in(const nlohmann::json& tools) {
    std::vector<std::string> names;
    for (const nlohmann::json& tool : tools) {
        names.push_back(tool["name"].get<std::string>());
    }
    return names;
}

}  // namespace

TEST_CASE("with nothing listed, the served set is the read set and complete is absent",
          "[cli][mcp][serve][callable]") {
    // 54b: consent is config; a call no entry lists is not there to refuse.
    const CallableFiles files;
    const CliHome home{files.config()};
    const Served served =
        serve(home, {"mcp", "serve"},
              {list_tools(), call(10, "complete", {{"prompt", "hi"}, {"backend", "local"}})});
    REQUIRE(served.code == 0);
    const nlohmann::json& tools = served.reply(2)["result"]["tools"];
    const std::vector<std::string> names = names_in(tools);
    CHECK(std::ranges::find(names, "complete") == names.end());
    for (const nlohmann::json& tool : tools) {
        INFO(tool["name"]);
        CHECK(tool["annotations"]["readOnlyHint"] == true);
    }
    CHECK(names.back() == "knowledge_query");
    const Answer absent = answer_of(served, 10);
    CHECK(absent.is_error);
    CHECK(absent.text == "Error: no such tool 'complete'");
    // Nothing was built or asked, so nothing is said.
    CHECK(served.err.empty());
}

TEST_CASE("a listed complete is the command's own one-shot, bounded, its description honest",
          "[cli][mcp][serve][callable]") {
    const CallableFiles files;
    const CliHome home{files.config()};
    std::string out;
    std::string err;
    // Listed through the one editor, as a person lists it.
    REQUIRE(home.run({"config", "set-backend", "local", "--mcp-serve", "complete"}, &out, &err) ==
            0);
    REQUIRE(home.run({"config", "set-backend", "short", "--mcp-serve", "complete"}, &out, &err) ==
            0);
    const Served served = serve(
        home, {"mcp", "serve"},
        {initialize(), list_tools(),
         call(10, "complete", {{"prompt", "hello there"}, {"backend", "local"}}),
         call(11, "complete", {{"prompt", "hi"}, {"backend", "short"}}),
         call(12, "complete", {{"prompt", "hi"}, {"backend", "local"}, {"max_tokens", 64}}),
         call(13, "complete", {{"prompt", "hi"}, {"backend", "local"}, {"max_tokens", "32"}})});
    INFO(served.err);
    REQUIRE(served.code == 0);

    const nlohmann::json& tools = served.reply(2)["result"]["tools"];
    const std::vector<std::string> names = names_in(tools);
    // Last, after the reads: the set only grows, and every tool before it is
    // read-only.
    REQUIRE(names.back() == "complete");
    CHECK(names[names.size() - 2] == "knowledge_query");
    const nlohmann::json& complete = tools.back();
    CHECK(complete["annotations"]["readOnlyHint"] == false);
    for (std::size_t i = 0; i + 1 < tools.size(); ++i) {
        CHECK(tools[i]["annotations"]["readOnlyHint"] == true);
    }
    // The description golden: what a call is, the backends and their
    // bounds, that the spend is this machine's, and why it is offered.
    CHECK(complete["description"] ==
          "Hand a prompt to one of Apogee's local models and answer with its reply. One call is "
          "the one-shot `apogee complete -m <backend> \"<prompt>\"` runs: the backend's own "
          "system prompt and sampling, no tools, no retrieval. backend may name: local (at most "
          "1024 tokens), short (at most 256 tokens). Each runs on this machine and is unmetered "
          "by its own provider's word, so a call spends this machine's time, never money. "
          "max_tokens asks for fewer tokens than the backend's bound, never more, and a reply "
          "that reaches its bound ends there. Offered because Apogee's config lists complete "
          "under these backends' mcp_serve.");
    const nlohmann::json& schema = complete["inputSchema"];
    CHECK(schema["properties"]["backend"]["enum"] == nlohmann::json::array({"local", "short"}));
    CHECK(schema["properties"]["max_tokens"]["maximum"] == 1024);
    CHECK(schema["properties"]["max_tokens"]["minimum"] == 1);
    CHECK(schema["required"] == nlohmann::json::array({"prompt", "backend"}));
    CHECK(schema["properties"]["prompt"]["type"] == "string");

    // The call is the command's own answer, at the served bound.
    const Answer local = answer_of(served, 10);
    CHECK_FALSE(local.is_error);
    CHECK(local.text == "max_tokens=1024 asked=hello there");
    REQUIRE(home.run({"complete", "-m", "local", "-n", "1024", "hello there"}, &out, &err) == 0);
    CHECK(local.text + "\n" == out);
    // A backend's own lower max_tokens is its bound; a host asks for fewer.
    CHECK(answer_of(served, 11).text == "max_tokens=256 asked=hi");
    CHECK(answer_of(served, 12).text == "max_tokens=64 asked=hi");
    CHECK(answer_of(served, 13).text == "max_tokens=32 asked=hi");
    // Narrated on stderr, the stream a host captures; stdout held frames only.
    CHECK_THAT(served.err, ContainsSubstring("apogee mcp serve: complete on 'local', at most "
                                             "1024 tokens\napogee mcp serve: complete on "
                                             "'local' answered\n"));
    CHECK_THAT(served.err, ContainsSubstring("complete on 'short', at most 256 tokens"));
}

TEST_CASE("a served complete refuses what it cannot run, in words, and the server goes on",
          "[cli][mcp][serve][callable]") {
    const CallableFiles files;
    const CliHome home{files.config({"local"})};
    const Served served = serve(
        home, {"mcp", "serve"},
        {call(10, "complete", {{"prompt", "hi"}, {"backend", "spare"}}),
         call(11, "complete", {{"prompt", ""}, {"backend", "local"}}),
         call(12, "complete", {{"prompt", "hi"}}),
         call(13, "complete", {{"prompt", "hi"}, {"backend", "local"}, {"max_tokens", 2048}}),
         call(14, "complete", {{"prompt", "hi"}, {"backend", "local"}, {"max_tokens", 0}}),
         call(15, "complete", {{"prompt", "hi"}, {"backend", "local"}, {"max_tokens", "lots"}}),
         call(16, "complete", {{"prompt", 5}, {"backend", "local"}}),
         call(17, "complete", {{"prompt", "still here"}, {"backend", "local"}}),
         // Past a 64-bit integer either way: read as the range's end, refused
         // by the bound -- never cast undefined.
         call(18, "complete", {{"prompt", "hi"}, {"backend", "local"}, {"max_tokens", 1e300}}),
         call(19, "complete", {{"prompt", "hi"}, {"backend", "local"}, {"max_tokens", -1e300}}),
         call(20, "complete",
              {{"prompt", "hi"},
               {"backend", "local"},
               {"max_tokens", std::numeric_limits<std::uint64_t>::max()}}),
         call(21, "complete", {{"prompt", "hi"}, {"backend", "local"}, {"max_tokens", 2.5}})});
    REQUIRE(served.code == 0);
    const auto refused = [&served](int id, std::string_view text) {
        const Answer answer = answer_of(served, id);
        INFO(id);
        CHECK(answer.is_error);
        CHECK(answer.text == text);
    };
    refused(10, "Error: 'spare' is not a backend complete runs on here -- it may name local");
    refused(11, "Error: prompt is required: what the model is asked");
    refused(12, "Error: backend is required: one of local");
    refused(13, "Error: max_tokens must be from 1 to 1024, local's bound");
    refused(14, "Error: max_tokens must be from 1 to 1024, local's bound");
    refused(15, "Error: max_tokens must be a whole number");
    refused(16, "Error: prompt must be a string");
    CHECK(answer_of(served, 17).text == "max_tokens=1024 asked=still here");
    refused(18, "Error: max_tokens must be from 1 to 1024, local's bound");
    refused(19, "Error: max_tokens must be from 1 to 1024, local's bound");
    refused(20, "Error: max_tokens must be from 1 to 1024, local's bound");
    refused(21, "Error: max_tokens must be a whole number");
    // Nothing ran for a refusal: one call narrated.
    CHECK(served.err ==
          "apogee mcp serve: complete on 'local', at most 1024 tokens\n"
          "apogee mcp serve: complete on 'local' answered\n");
}

TEST_CASE("a listing the editor would refuse is left out at the server's start, and said",
          "[cli][mcp][serve][callable]") {
    // A hand edit can write what the editor refuses: the server asks the
    // provider's word again, and a billed backend is never offered.
    const CallableFiles files;
    {
        const CliHome home{files.config({"paid"})};
        const Served served = serve(home, {"mcp", "serve"}, {list_tools()});
        REQUIRE(served.code == 0);
        const std::vector<std::string> names = names_in(served.reply(2)["result"]["tools"]);
        CHECK(std::ranges::find(names, "complete") == names.end());
        CHECK(served.err ==
              "apogee mcp serve: 'paid' cannot serve complete: its provider bills each call -- a "
              "served call runs on another host's initiative, so only a backend local and "
              "unmetered by its own provider's word may serve one; it is not served\n");
    }
    const CliHome home{files.config({"local", "paid"})};
    const Served served =
        serve(home, {"mcp", "serve"},
              {list_tools(), call(10, "complete", {{"prompt", "hi"}, {"backend", "paid"}})});
    REQUIRE(served.code == 0);
    const nlohmann::json& complete = served.reply(2)["result"]["tools"].back();
    REQUIRE(complete["name"] == "complete");
    CHECK(complete["inputSchema"]["properties"]["backend"]["enum"] ==
          nlohmann::json::array({"local"}));
    CHECK(answer_of(served, 10).text ==
          "Error: 'paid' is not a backend complete runs on here -- it may name local");
    CHECK_THAT(served.err, ContainsSubstring("'paid' cannot serve complete"));
}

TEST_CASE("consent is read again at each call: a listing taken back is not run",
          "[cli][mcp][serve][callable]") {
    const CallableFiles files;
    const CliHome home{files.config({"local"})};
    apogee::commands::RootContext context;
    context.config_path = home.config_path().string();
    const apogee::harness::Config config = apogee::harness::load_config(home.config_path());
    apogee::commands::ServedModels models{config, home.config_path()};
    std::ostringstream narration;
    const apogee::mcp::ServedCall complete = apogee::commands::served_complete(
        context, models, {{.name = "local", .max_tokens = 1024}}, narration);
    const nlohmann::json args{{"prompt", "hi"}, {"backend", "local"}};
    const apogee::mcp::ToolCallResult first = complete.answer(args);
    CHECK_FALSE(first.is_error);
    CHECK(first.text == "max_tokens=1024 asked=hi");
    // The providers are built once, for the server's life.
    const apogee::backends::BuildResult* built = &models.ready();
    CHECK(&models.ready() == built);

    std::string out;
    std::string err;
    REQUIRE(home.run({"config", "set-backend", "local", "--mcp-serve", ""}, &out, &err) == 0);
    const apogee::mcp::ToolCallResult second = complete.answer(args);
    CHECK(second.is_error);
    CHECK(second.text ==
          "Error: 'local' no longer lists complete under mcp_serve -- the config changed since "
          "this server started, and nothing was run");
    CHECK(narration.str() ==
          "apogee mcp serve: complete on 'local', at most 1024 tokens\n"
          "apogee mcp serve: complete on 'local' answered\n");
}

TEST_CASE("complete_backends takes the provider's word for each listed backend, in name order",
          "[cli][mcp][serve][callable]") {
    const CallableFiles files;
    const apogee::harness::Config config =
        apogee::harness::parse_config(files.config({"short", "local", "paid"}), "<test>");
    std::vector<std::string> asked;
    const apogee::commands::MeteredProbe probe = [&asked](const apogee::harness::Config&,
                                                          std::string_view backend) {
        asked.emplace_back(backend);
        return backend == "paid" ? apogee::commands::MeteredAnswer{.metered = true}
                                 : apogee::commands::MeteredAnswer{.metered = false};
    };
    const apogee::commands::CallableBackends callable =
        apogee::commands::complete_backends(config, probe);
    // Only the listed are asked; spare is not.
    CHECK(asked == std::vector<std::string>{"local", "paid", "short"});
    REQUIRE(callable.backends.size() == 2);
    CHECK(callable.backends[0].name == "local");
    CHECK(callable.backends[0].max_tokens == 1024);
    CHECK(callable.backends[1].name == "short");
    CHECK(callable.backends[1].max_tokens == 256);
    REQUIRE(callable.left_out.size() == 1);
    CHECK_THAT(callable.left_out.front(), StartsWith("'paid' cannot serve complete: its provider "
                                                     "bills each call"));
    // No probe tells nothing, and unknown is metered: none is served.
    const apogee::commands::CallableBackends blind =
        apogee::commands::complete_backends(config, {});
    CHECK(blind.backends.empty());
    CHECK(blind.left_out.size() == 3);
}

TEST_CASE("the one-shot core resolves a request as apogee complete does, the utility off on ask",
          "[cli][complete][one-shot]") {
    const apogee::harness::Config config = apogee::harness::parse_config(
        "backends:\n  local:\n    type: mock\n    temperature: 0.4\n    max_tokens: 300\n"
        "    system_prompt: Be brief.\n    thinking: auto\n  helper:\n    type: mock\n"
        "models:\n  default: local\n  default_utility: helper\n",
        "<test>");
    const apogee::commands::OneShot entry =
        apogee::commands::one_shot(config, "local", "why?", apogee::commands::OneShotSettings{});
    CHECK(entry.options.model == "local");
    CHECK(entry.options.temperature == 0.4);
    CHECK(entry.options.max_tokens == 300);
    CHECK(entry.options.thinking.mode == apogee::harness::ThinkingMode::Auto);
    CHECK(entry.options.thinking_judge == "helper");
    CHECK(entry.options.summary_model == "helper");
    REQUIRE(entry.messages.size() == 2);
    CHECK(entry.messages.front().content.plain_text() == "Be brief.");
    CHECK(entry.messages.back().content.plain_text() == "why?");

    // The flags win, as on the command line; the served call turns the
    // utility model off, so `auto` is decided by rule.
    const apogee::commands::OneShot served = apogee::commands::one_shot(
        config, "local", "why?",
        apogee::commands::OneShotSettings{
            .system_prompt = "Be exact.", .temperature = 0.0, .max_tokens = 64, .utility = false});
    CHECK(served.options.temperature == 0.0);
    CHECK(served.options.max_tokens == 64);
    CHECK(served.options.thinking_judge.empty());
    CHECK(served.options.summary_model.empty());
    CHECK(served.messages.front().content.plain_text() == "Be exact.");
}

TEST_CASE("search_documents searches with the collection's embedder, as chat's tool does",
          "[cli][mcp][serve][rag]") {
    // 54b's verification: the served search was chunk-only -- no embedder
    // reached it, so `vector` was refused and auto ran lexical over a
    // vectorised collection. It resolves through the one resolver now, with
    // the providers the server builds at its first search.
    const CliHome home{std::string{R"(backends:
  local:
    type: mock
    model: mock-1
  embed:
    type: mock
    embedding_model: mock-space

models:
  default: local
  default_embedding: embed
)"}};
    const std::filesystem::path docs = home.home() / "docs";
    std::filesystem::create_directories(docs);
    std::ofstream{docs / "paint.md"} << "The painter mixed ultramarine pigment with oil.\n";
    std::ofstream{docs / "rust.md"} << "Rust ownership moves values between bindings.\n";
    std::string out;
    std::string err;
    REQUIRE(home.run({"embed", "ingest", "notes", docs.string(), "--retriever", "vector"}, &out,
                     &err) == 0);
    const Served served =
        serve(home, {"mcp", "serve"},
              {call(10, "search_documents",
                    {{"query", "pigment"}, {"collection", "notes"}, {"retriever", "vector"}}),
               call(11, "search_documents", {{"query", "pigment"}, {"collection", "notes"}}),
               call(12, "search_documents",
                    {{"query", "pigment"}, {"collection", "notes"}, {"retriever", "lexical"}})});
    INFO(served.err);
    REQUIRE(served.code == 0);
    const Answer vector = answer_of(served, 10);
    INFO(vector.text);
    CHECK_FALSE(vector.is_error);
    CHECK_THAT(vector.text, StartsWith("collection: notes  retriever: vector\n"));
    CHECK_THAT(answer_of(served, 11).text, StartsWith("collection: notes  retriever: vector\n"));
    CHECK_THAT(answer_of(served, 12).text, StartsWith("collection: notes  retriever: lexical\n"));
}

namespace {

void write_file(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << text;
}

/// The spike's sandbox (58b): a two-file Python project, its README
/// ingested as the collection `notes`, and -- when `built` -- the model-free
/// code graph `shop` over the project.
void plant_shop(const CliHome& home, bool built = true) {
    const std::filesystem::path project = home.home() / "project";
    write_file(project / "store.py",
               "class Store:\n    def charge(self, amount):\n        return amount\n");
    write_file(project / "app.py",
               "from store import Store\n\n\ndef checkout():\n    return Store().charge(5)\n");
    write_file(home.home() / "readme" / "README.md",
               "The shop charges at checkout through the store.\n");
    std::string out;
    std::string err;
    INFO(out << err);
    REQUIRE(home.run({"embed", "ingest", "notes", (home.home() / "readme").string()}, &out, &err) ==
            0);
    if (built) {
        REQUIRE(home.run({"graph", "build", "--source", project.string(), "--graph", "shop", "-q"},
                         &out, &err) == 0);
    }
}

/// What a command printed on stdout as it is, for a read served as text.
[[nodiscard]] std::string printed_text(const CliHome& home, const std::vector<std::string>& args) {
    std::string out;
    std::string err;
    INFO(err);
    REQUIRE(home.run(args, &out, &err) == 0);
    REQUIRE_FALSE(out.empty());
    return out;
}

/// Two records in the default collection, one shipped and one rejected.
void plant_records() {
    apogee::knowledge::Store store{apogee::harness::embeddings_dir() / "knowledge.db",
                                   apogee::harness::knowledge_raw_dir()};
    apogee::knowledge::Record shipped;
    shipped.id = "kr-20260913T120010Z-00000a";
    shipped.intent = "the cancel button was removed after testing";
    shipped.decision = "remove it";
    shipped.status = "shipped";
    shipped.discipline = "ux";
    shipped.timestamp = "2026-09-13T12:00:10.000000Z";
    store.put(shipped, {}, "Ada: drop the cancel button? Bob: yes");
    apogee::knowledge::Record rejected;
    rejected.id = "kr-20260913T120020Z-00000b";
    rejected.intent = "keep the cancel button for undo";
    rejected.status = "rejected";
    rejected.discipline = "eng";
    rejected.timestamp = "2026-09-13T12:00:20.000000Z";
    store.put(rejected, {}, "raw");
}

/// The providers a served read asks for, counted: built once, as the
/// server's own are (`ServedModels`).
struct CountedProviders {
    explicit CountedProviders(const CliHome& home)
        : models{apogee::harness::load_config(home.config_path()), home.config_path()} {}

    [[nodiscard]] apogee::commands::ServedProviders source() {
        return [this]() -> const apogee::harness::Harness& {
            ++asked;
            (void)models.ready();
            return models.harness();
        };
    }

    apogee::commands::ServedModels models;
    int asked = 0;
};

[[nodiscard]] const apogee::mcp::ServedRead& read_named(
    const std::vector<apogee::mcp::ServedRead>& reads, std::string_view name) {
    const auto found = std::ranges::find(reads, name, &apogee::mcp::ServedRead::name);
    REQUIRE(found != reads.end());
    return *found;
}

}  // namespace

TEST_CASE("the graph reads answer as their commands do, over the spike's shop and notes",
          "[cli][mcp][serve][graph][58b]") {
    // 58b's W2: list_collections named `notes` and never `shop`, and no
    // served tool listed graphs; graph stats and graph report were unserved.
    const CliHome home{std::string{kConfig}};
    plant_shop(home);
    const Served served =
        serve(home, {"mcp", "serve"},
              {call(10, "list_graphs"), call(11, "graph_stats", {{"graph", "shop"}}),
               call(12, "graph_report", {{"graph", "shop"}}), call(13, "graph_report"),
               call(14, "graph_stats", {{"graph", "notes"}})});
    INFO(served.err);
    REQUIRE(served.code == 0);

    const Answer listed = answer_of(served, 10);
    CHECK_FALSE(listed.is_error);
    CHECK(listed.text + "\n" == printed(home, {"graph", "list"}));
    const nlohmann::json graphs = nlohmann::json::parse(listed.text);
    // `shop` named, with its kind; `notes` holds no graph, so is not one.
    REQUIRE(graphs["data"].size() == 1);
    CHECK(graphs["data"][0]["name"] == "shop");
    CHECK(graphs["data"][0]["kind"] == "named");
    CHECK(graphs["data"][0]["built"] == true);

    const Answer stats = answer_of(served, 11);
    CHECK_FALSE(stats.is_error);
    CHECK(stats.text + "\n" == printed(home, {"graph", "stats", "shop"}));
    CHECK(nlohmann::json::parse(stats.text)["nodes"] == graphs["data"][0]["nodes"]);

    // The report is the command's own Markdown, served as text.
    const Answer report = answer_of(served, 12);
    CHECK_FALSE(report.is_error);
    const std::string markdown = printed_text(home, {"graph", "report", "--graph", "shop"});
    CHECK(report.text + "\n" == markdown);
    CHECK(report.text.starts_with("#"));
    CHECK_THROWS(nlohmann::json::parse(report.text));
    // With no graph named, the one graph built, as the command takes it.
    CHECK(answer_of(served, 13).text + "\n" == printed_text(home, {"graph", "report"}));

    // A collection with no graph built is a read, not a refusal.
    const Answer unbuilt = answer_of(served, 14);
    CHECK_FALSE(unbuilt.is_error);
    CHECK(unbuilt.text + "\n" == printed(home, {"graph", "stats", "notes"}));
    CHECK(nlohmann::json::parse(unbuilt.text)["built"] == false);
}

TEST_CASE("a graph built after the server started is seen by the next call",
          "[cli][mcp][serve][graph][58b]") {
    const CliHome home{std::string{kConfig}};
    plant_shop(home, /*built=*/false);
    apogee::commands::RootContext context;
    context.config_path = home.config_path().string();
    const std::vector<apogee::mcp::ServedRead> reads = apogee::commands::status_reads(context);
    const apogee::mcp::ServedRead& list = read_named(reads, "list_graphs");
    const apogee::mcp::ServedRead& stats = read_named(reads, "graph_stats");
    CHECK(nlohmann::json::parse(list.answer(nlohmann::json::object()).text)["data"].empty());
    const apogee::mcp::ToolCallResult before = stats.answer({{"graph", "shop"}});
    CHECK(before.is_error);

    std::string out;
    std::string err;
    REQUIRE(home.run({"graph", "build", "--source", (home.home() / "project").string(), "--graph",
                      "shop", "-q"},
                     &out, &err) == 0);
    const apogee::mcp::ToolCallResult after = list.answer(nlohmann::json::object());
    CHECK(after.text + "\n" == printed(home, {"graph", "list"}));
    CHECK(nlohmann::json::parse(after.text)["data"][0]["name"] == "shop");
    CHECK(stats.answer({{"graph", "shop"}}).text + "\n" ==
          printed(home, {"graph", "stats", "shop"}));
}

TEST_CASE("the knowledge reads answer as knowledge list, info and query do",
          "[cli][mcp][serve][knowledge][58b]") {
    // 58b's W3: the records, the project's why, had no MCP tool at all.
    const CliHome home{std::string{kConfig}};
    plant_records();
    const Served served =
        serve(home, {"mcp", "serve"},
              {call(10, "knowledge_list"), call(11, "knowledge_list", {{"status", "rejected"}}),
               call(12, "knowledge_list", {{"discipline", "ux"}}),
               call(13, "knowledge_info", {{"id", "kr-20260913T120010Z-00000a"}}),
               call(14, "knowledge_query", {{"question", "cancel button"}}),
               call(15, "knowledge_query", {{"question", "cancel button"}, {"top_k", 1}}),
               call(16, "knowledge_query", {{"question", "cancel button"}, {"top_k", "1"}})});
    INFO(served.err);
    REQUIRE(served.code == 0);
    const auto same = [&](int id, const std::vector<std::string>& args) {
        const Answer answer = answer_of(served, id);
        INFO("tool " << id);
        CHECK_FALSE(answer.is_error);
        CHECK(answer.text + "\n" == printed(home, args));
    };
    same(10, {"knowledge", "list"});
    same(11, {"knowledge", "list", "--status", "rejected"});
    same(12, {"knowledge", "list", "--discipline", "ux"});
    same(13, {"knowledge", "info", "kr-20260913T120010Z-00000a"});
    same(14, {"knowledge", "query", "cancel button"});
    same(15, {"knowledge", "query", "cancel button", "-n", "1"});
    same(16, {"knowledge", "query", "cancel button", "-n", "1"});

    // Not vacuous: the filters reach the reads, and the record is whole.
    CHECK(nlohmann::json::parse(answer_of(served, 10).text)["data"].size() == 2);
    CHECK(nlohmann::json::parse(answer_of(served, 11).text)["data"].size() == 1);
    CHECK(nlohmann::json::parse(answer_of(served, 12).text)["data"][0]["discipline"] == "ux");
    CHECK(nlohmann::json::parse(answer_of(served, 13).text)["decision"] == "remove it");
    // The private archive is never served.
    CHECK_FALSE(nlohmann::json::parse(answer_of(served, 13).text).contains("raw"));
    const nlohmann::json found = nlohmann::json::parse(answer_of(served, 14).text);
    CHECK(found["retriever"] == "lexical");
    // query keeps to shipped records, as the command does by default.
    REQUIRE(found["records"].size() == 1);
    CHECK(found["records"][0]["record"]["id"] == "kr-20260913T120010Z-00000a");
}

TEST_CASE("knowledge_query spends nothing on the lexical path and one embedding by vector",
          "[cli][mcp][serve][knowledge][spend][58b]") {
    // Nothing spends but a query's embedding (58b, the 2026-09-13 rule): a
    // lexical search asks for no provider, a vector one asks the server's
    // own -- built once -- and no query reranks, whatever the collection pins.
    const apogee::testing::TempDir scripts{"mcp-serve-knowledge-" +
                                           std::to_string(std::random_device{}())};
    write_file(
        scripts.path() / "clerk.json",
        nlohmann::json{
            {"turns",
             {{{"text",
                R"({"intent": "We dropped the cancel button because testers kept mistaking it for back.", "decision": "Remove the cancel button.", "status": "shipped", "discipline": "ux", "downstream_link": "", "provenance": {"source": "meeting", "attribution": "Ada"}})"}}}}}
            .dump());
    write_file(scripts.path() / "judge.json",
               nlohmann::json{{"turns", {{{"text", "[2, 1]"}}}}}.dump());

    SECTION("a collection pinned lexical, and a rerank judge it pins, ask for nothing") {
        const CliHome home{
            "backends:\n  local:\n    type: mock\n  judge:\n    type: mock\n"
            "    model_path: " +
            (scripts.path() / "judge.json").string() +
            "\nmodels:\n  default: local\nembeddings:\n  knowledge:\n"
            "    retriever: lexical\n    rerank: judge\n"};
        plant_records();
        {
            // A second shipped match, so the judge has an order to give.
            apogee::knowledge::Store store{apogee::harness::embeddings_dir() / "knowledge.db",
                                           apogee::harness::knowledge_raw_dir()};
            apogee::knowledge::Record also;
            also.id = "kr-20260913T120030Z-00000c";
            also.intent = "the cancel button moved to the footer";
            also.status = "shipped";
            also.timestamp = "2026-09-13T12:00:30.000000Z";
            store.put(also, {}, "raw");
        }
        CountedProviders providers{home};
        apogee::commands::RootContext context;
        context.config_path = home.config_path().string();
        const std::vector<apogee::mcp::ServedRead> reads =
            apogee::commands::status_reads(context, providers.source());
        const apogee::mcp::ToolCallResult answer =
            read_named(reads, "knowledge_query")
                .answer({{"question", "cancel button"}, {"top_k", 2}});
        INFO(answer.text);
        CHECK_FALSE(answer.is_error);
        CHECK(providers.asked == 0);
        const nlohmann::json document = nlohmann::json::parse(answer.text);
        CHECK(document["retriever"] == "lexical");
        CHECK(document["reranked"] == false);
        // The command's own answer with the judge off -- and the command
        // left alone does spend a judge's call, which the served read never
        // makes.
        CHECK(answer.text + "\n" ==
              printed(home, {"knowledge", "query", "cancel button", "-n", "2", "--rerank", "off"}));
        const nlohmann::json judged = nlohmann::json::parse(
            printed(home, {"knowledge", "query", "cancel button", "-n", "2"}));
        CHECK(judged["reranked"] == true);
    }

    SECTION("a vectorised collection searches by vector through the server's own providers") {
        const CliHome home{"backends:\n  clerk:\n    type: mock\n    model_path: " +
                           (scripts.path() / "clerk.json").string() +
                           "\n  embed:\n    type: mock\n    embedding_model: mock-space\n"
                           "models:\n  default: clerk\n  default_embedding: embed\n"};
        std::string out;
        std::string err;
        REQUIRE(
            home.run({"knowledge", "capture", "--retriever", "vector", "Ada: drop it? Bob: yes"},
                     &out, &err) == 0);
        CountedProviders providers{home};
        apogee::commands::RootContext context;
        context.config_path = home.config_path().string();
        const std::vector<apogee::mcp::ServedRead> reads =
            apogee::commands::status_reads(context, providers.source());
        const apogee::mcp::ServedRead& query = read_named(reads, "knowledge_query");
        const apogee::mcp::ToolCallResult first = query.answer({{"question", "cancel button"}});
        INFO(first.text);
        CHECK_FALSE(first.is_error);
        CHECK(nlohmann::json::parse(first.text)["retriever"] == "vector");
        CHECK(providers.asked == 1);
        CHECK(first.text + "\n" == printed(home, {"knowledge", "query", "cancel button"}));
        // The providers are the server's, built once for its life.
        const apogee::backends::BuildResult* built = &providers.models.ready();
        (void)query.answer({{"question", "cancel button"}});
        CHECK(providers.asked == 2);
        CHECK(&providers.models.ready() == built);
    }
}

TEST_CASE("a graph or knowledge read that cannot answer says why, and the server goes on",
          "[cli][mcp][serve][graph][knowledge][58b]") {
    const CliHome home{std::string{kConfig}};
    // A graph and a records store that are not databases.
    std::string out;
    std::string err;
    REQUIRE(home.run({"config", "add-graph", "broken", "--collections", "notes"}, &out, &err) == 0);
    write_file(apogee::agentloop::graph_db_path("broken"), "this is not a database");
    write_file(apogee::harness::embeddings_dir() / "knowledge.db", "this is not a database");
    const Served served = serve(
        home, {"mcp", "serve"},
        {call(1, "graph_stats"), call(2, "graph_stats", {{"graph", 5}}),
         call(3, "graph_stats", {{"graph", "nope"}}), call(4, "graph_stats", {{"graph", "broken"}}),
         call(5, "graph_report", {{"graph", "nope"}}), call(6, "knowledge_list"),
         call(7, "knowledge_info"), call(8, "knowledge_query"),
         call(9, "knowledge_query", {{"question", "x"}, {"top_k", "lots"}}),
         call(10, "knowledge_query", {{"question", "x"}, {"top_k", 0}}),
         call(11, "knowledge_list", {{"status", true}}), call(12, "list_graphs"),
         call(13, "list_notes")});
    REQUIRE(served.code == 0);
    const auto refused = [&served](int id) {
        const Answer answer = answer_of(served, id);
        INFO(id << ": " << answer.text);
        CHECK(answer.is_error);
        CHECK_THAT(answer.text, StartsWith("Error: "));
        return answer.text.substr(std::string{"Error: "}.size());
    };
    CHECK(refused(1) ==
          "graph is required: a named graph or a collection, as list_graphs names "
          "them");
    CHECK(refused(2) == "graph must be a string");
    // The command's own words, as it says them after its prefix.
    CHECK(home.run({"graph", "stats", "nope"}, &out, &err) == 1);
    CHECK(err == "apogee graph: " + refused(3) + "\n");
    (void)refused(4);
    CHECK(home.run({"graph", "report", "--graph", "nope"}, &out, &err) == 1);
    CHECK(err == "apogee graph: " + refused(5) + "\n");
    (void)refused(6);
    CHECK(refused(7) == "id is required: a record id, as knowledge_list names them");
    CHECK(refused(8) == "question is required: what the records are asked");
    CHECK(refused(9) == "top_k must be a whole number");
    CHECK(refused(10) == "top_k must be at least 1");
    CHECK(refused(11) == "status must be a string");
    // The list still answers, the broken graph's row saying why.
    const Answer listed = answer_of(served, 12);
    CHECK_FALSE(listed.is_error);
    CHECK(nlohmann::json::parse(listed.text)["data"][0].contains("error"));
    CHECK_FALSE(answer_of(served, 13).is_error);
}

// ---- the introduction (58a) -------------------------------------------------

namespace {

/// What `initialize` told the host.
[[nodiscard]] std::string instructions_of(const Served& served) {
    const nlohmann::json& result = served.reply(1)["result"];
    INFO(result.dump());
    REQUIRE(result.contains("instructions"));
    REQUIRE(result["instructions"].is_string());
    return result["instructions"].get<std::string>();
}

/// Every served name, as a full server lists them: the when-to-use lines
/// name tools only from here.
[[nodiscard]] std::vector<std::string> every_served_name() {
    return {"read_file",       "list_directory", "search_files",     "grep_files",
            "git_status",      "git_log",        "git_diff",         "git_show",
            "list_notes",      "read_note",      "search_documents", "list_collections",
            "collection_info", "graph_query",    "graph_neighbors",  "graph_path",
            "graph_explain",   "models_list",    "models_status",    "train_status",
            "train_versions",  "check",          "list_graphs",      "graph_stats",
            "graph_report",    "knowledge_list", "knowledge_info",   "knowledge_query"};
}

/// The paragraph every introduction opens with: what Apogee is and its two
/// kinds of tool -- the git reads named only where they are served.
[[nodiscard]] std::string introduction_head(bool git, bool complete) {
    return std::string{
               "Apogee is the person's local AI harness: their models, and the material "
               "they have indexed with it -- document collections, knowledge graphs and "
               "decision records -- on this machine. This server is Apogee's read-only "
               "face, with two kinds of tool: native reads (files, "} +
           (git ? "git, " : "") +
           "notes, document search, graph navigation) and Apogee's own reads (models, training, "
           "the install check, graphs, knowledge records), each answering with what its command "
           "prints. Nothing it serves writes." +
           (complete ? " One call runs a model: complete, on a local model the person's config "
                       "lists."
                     : "");
}

/// The when-to-use lines every full server ends with.
[[nodiscard]] std::string introduction_uses(bool complete) {
    return std::string{
               "When to reach for them -- for the person's own material, not in place of "
               "the host's own tools:\n"
               "- their indexed documents and decisions: search_documents, "
               "knowledge_query\n"
               "- how code in an indexed project connects: graph_query, graph_path\n"
               "- what models exist and where they stand: models_status"} +
           (complete ? "\n- a bounded side task for a local model: complete" : "");
}

}  // namespace

TEST_CASE(
    "mcp serve introduces itself from the setup: the collections, the graph, the records "
    "and the listed complete",
    "[cli][mcp][serve][58a]") {
    // 58a's W1: the spike's sandbox read `"instructions": null`. Its notes
    // and shop, two records, and complete listed on `local` and, by a hand
    // edit, on the billed `paid`; `short` and `spare` list nothing.
    const CallableFiles files;
    const CliHome home{files.config({"local", "paid"})};
    plant_shop(home);
    plant_records();
    const Served served = serve(home, {"mcp", "serve"}, {initialize(), list_tools()});
    INFO(served.err);
    REQUIRE(served.code == 0);
    const std::string text = instructions_of(served);
    INFO(text);
    const bool git = apogee::tools::ToolsetOptions{}.child_processes;

    // The golden pins the shape: the paragraph, the inventory, the uses.
    CHECK(text == introduction_head(git, true) +
                      "\n\n"
                      "This setup, as the server started (the reads answer live):\n"
                      "- collections: knowledge (2 chunks, lexical only), notes (1 chunk, "
                      "lexical only)\n"
                      "- graphs: shop (named, 6 nodes, 6 edges)\n"
                      "- knowledge records: 2, 1 shipped\n"
                      "- local models for complete: local (at most 1024 tokens)\n"
                      "\n" +
                      introduction_uses(true));
    CHECK(text.size() <= apogee::commands::kInstructionsBound);
    // The listed one with its bound; neither the unlisted nor the billed one.
    for (const char* never : {"short", "spare", "paid"}) {
        INFO(never);
        CHECK_THAT(text, !ContainsSubstring(never));
    }
    // Each tool it names is one the server lists.
    const std::vector<std::string> names = names_in(served.reply(2)["result"]["tools"]);
    for (const char* named : {"search_documents", "knowledge_query", "graph_query", "graph_path",
                              "models_status", "complete"}) {
        INFO(named);
        CHECK(std::ranges::find(names, named) != names.end());
    }
    // The hidden alias introduces itself the same.
    const Served alias = serve(home, {"__mcp-tools"}, {initialize()});
    CHECK(instructions_of(alias) == text);
}

TEST_CASE("an empty home's introduction says it holds nothing yet, and offers no complete",
          "[cli][mcp][serve][58a]") {
    const CliHome home{std::string{kConfig}};
    const Served served = serve(home, {"mcp", "serve"}, {initialize()});
    REQUIRE(served.code == 0);
    const bool git = apogee::tools::ToolsetOptions{}.child_processes;
    CHECK(instructions_of(served) == introduction_head(git, false) +
                                         "\n\n"
                                         "This setup, as the server started (the reads answer "
                                         "live):\n"
                                         "- collections: none yet\n"
                                         "- graphs: none yet\n"
                                         "- knowledge records: none yet\n"
                                         "\n" +
                                         introduction_uses(false));
}

TEST_CASE(
    "with no config the introduction says Apogee is not set up and names check; with a "
    "broken one, that it does not load",
    "[cli][mcp][serve][58a]") {
    const bool git = apogee::tools::ToolsetOptions{}.child_processes;
    const std::string check = apogee::harness::render({.verbs = {"check"}});
    {
        const CliHome home{std::string{kConfig}};
        std::filesystem::remove(home.config_path());
        const Served served =
            serve(home, {"mcp", "serve"}, {initialize(), call(10, "models_status")});
        REQUIRE(served.code == 0);
        const std::string text = instructions_of(served);
        CHECK(text == introduction_head(git, false) + "\n\nApogee is not set up here yet: `" +
                          check +
                          "` says what is missing, and each read answers with its own "
                          "reason.\n\n" +
                          introduction_uses(false));
        // The reads still answer their own reasons.
        CHECK(answer_of(served, 10).is_error);
        CHECK_THAT(text, !ContainsSubstring(home.home().string()));
    }
    const CliHome home{"backends: [this is not\n"};
    const Served served = serve(home, {"mcp", "serve"}, {initialize()});
    REQUIRE(served.code == 0);
    const std::string text = instructions_of(served);
    CHECK(text == introduction_head(git, false) + "\n\nApogee's config does not load here: `" +
                      check + "` says why, and each read answers with its own reason.\n\n" +
                      introduction_uses(false));
    // The config's reason names its path: the introduction never says it.
    CHECK_THAT(text, !ContainsSubstring(home.home().string()));
}

TEST_CASE("no key, token or path reaches the introduction", "[cli][mcp][serve][58a][secrets]") {
    // A config holding a literal key and absolute paths, a stored key, a
    // graph over an absolute source tree, and a collection whose embedder a
    // store recorded by its absolute path: names and counts reach the text,
    // never one of those.
    constexpr std::string_view kLiteralKey = "sk-ant-planted-58a-literal-key";
    constexpr std::string_view kStoredKey = "sk-planted-58a-stored-key";
    const CallableFiles files;
    std::string config = files.config({"local"});
    config.insert(config.find("models:\n"),
                  "  cloud:\n    type: anthropic\n    model: claude-x\n"
                  "    api_key: " +
                      std::string{kLiteralKey} + "\n");
    const CliHome home{config};
    apogee::secrets::CredentialStore store{apogee::secrets::credentials_path(home.config_path())};
    store.put("openai", kStoredKey);
    plant_shop(home);
    const std::filesystem::path embedder = home.home() / "models" / "embed-58a.Q4_K_M.gguf";
    {
        apogee::embedstore::Store notes{apogee::commands::collection_path("notes")};
        notes.set_embedding_model(embedder.string(), 4);
    }
    const Served served = serve(home, {"mcp", "serve"}, {initialize()});
    REQUIRE(served.code == 0);
    const std::string text = instructions_of(served);
    INFO(text);
    // Named by its file, never located.
    CHECK_THAT(text, ContainsSubstring("notes (1 chunk, embedder embed-58a.Q4_K_M.gguf)"));
    CHECK_THAT(text, ContainsSubstring("shop (named, 6 nodes, 6 edges)"));
    for (const std::string& never :
         {std::string{kLiteralKey}, std::string{kStoredKey}, home.home().string(),
          files.dir.path().string(), (home.home() / "project").string(), embedder.string(),
          std::string{"secrets"}, std::string{"credentials"}}) {
        INFO(never);
        CHECK_THAT(text, !ContainsSubstring(never));
    }
    // No path of any shape: nothing in the text is a path.
    CHECK(text.find('/') == std::string::npos);
    CHECK(text.find('\\') == std::string::npos);
}

TEST_CASE(
    "each list in the introduction shows eight names, then the count and the tool that "
    "lists them whole",
    "[cli][mcp][serve][58a]") {
    apogee::commands::ServerInventory inventory;
    inventory.served = every_served_name();
    inventory.served.emplace_back("complete");
    for (int i = 1; i <= 14; ++i) {
        inventory.graphs.push_back({.name = "g" + std::to_string(i),
                                    .kind = "named",
                                    .built = true,
                                    .nodes = i,
                                    .edges = 1});
    }
    for (int i = 1; i <= 8; ++i) {
        inventory.collections.push_back({.name = "c" + std::to_string(i), .chunks = 3});
    }
    for (int i = 1; i <= 9; ++i) {
        inventory.backends.push_back({.name = "b" + std::to_string(i), .max_tokens = 512});
    }
    inventory.records = 1;
    inventory.shipped_records = 0;
    const std::string text = apogee::commands::server_instructions(inventory);
    INFO(text);
    // Eight shown, the rest counted with the tool that lists them whole.
    CHECK_THAT(text, ContainsSubstring("- graphs: g1 (named, 1 node, 1 edge), g2 (named, 2 nodes, "
                                       "1 edge), "));
    CHECK_THAT(text, ContainsSubstring("g8 (named, 8 nodes, 1 edge), … and 6 more: list_graphs\n"));
    CHECK_THAT(text, !ContainsSubstring("g9 "));
    // Exactly eight is no cut.
    CHECK_THAT(text, ContainsSubstring("c8 (3 chunks, lexical only)\n"));
    CHECK_THAT(text, !ContainsSubstring("more: list_collections"));
    CHECK_THAT(text, ContainsSubstring("b8 (at most 512 tokens), … and 1 more: complete\n"));
    CHECK_THAT(text, ContainsSubstring("- knowledge records: 1, 0 shipped\n"));
    CHECK(text.size() <= apogee::commands::kInstructionsBound);
}

TEST_CASE("the introduction never passes its bound: names are cut until it fits",
          "[cli][mcp][serve][58a]") {
    apogee::commands::ServerInventory inventory;
    inventory.served = every_served_name();
    inventory.served.emplace_back("complete");
    const std::string tail(120, 'x');
    for (int i = 0; i < 40; ++i) {
        inventory.collections.push_back(
            {.name = "c" + std::to_string(i) + tail, .chunks = 100000, .embedder = tail});
        inventory.graphs.push_back({.name = "g" + std::to_string(i) + tail, .kind = "named"});
        inventory.backends.push_back({.name = "b" + std::to_string(i) + tail, .max_tokens = 1024});
    }
    const std::string text = apogee::commands::server_instructions(inventory);
    INFO(text);
    CHECK(text.size() <= apogee::commands::kInstructionsBound);
    // Fewer than eight a list, every list cut with its count and its tool --
    // the same number shown in each -- and the uses whole.
    CHECK_THAT(text, ContainsSubstring("more: list_collections\n"));
    CHECK_THAT(text, ContainsSubstring("more: list_graphs\n"));
    CHECK_THAT(text, ContainsSubstring("more: complete\n"));
    CHECK_THAT(text, !ContainsSubstring("c7" + tail));
    CHECK_THAT(text, ContainsSubstring(introduction_uses(true)));

    // At its fewest every list is a count alone, and that always fits.
    for (auto& collection : inventory.collections) {
        collection.name = std::string(apogee::commands::kInstructionsBound, 'n');
    }
    const std::string fewest = apogee::commands::server_instructions(inventory);
    INFO(fewest);
    CHECK(fewest.size() <= apogee::commands::kInstructionsBound);
    CHECK_THAT(fewest, ContainsSubstring("- collections: … and 40 more: list_collections\n"));
}

TEST_CASE("a when-to-use line names only the tools served, and the introduction only the kinds",
          "[cli][mcp][serve][58a]") {
    // `tools.disabled` can switch a toolset off: the introduction says only
    // what this server lists.
    apogee::commands::ServerInventory inventory;
    for (const std::string& name : every_served_name()) {
        if (!name.starts_with("graph_") && !name.starts_with("git_") && name != "read_file" &&
            name != "search_documents") {
            inventory.served.push_back(name);
        }
    }
    const std::string text = apogee::commands::server_instructions(inventory);
    INFO(text);
    CHECK_THAT(text, ContainsSubstring("native reads (notes) and Apogee's own reads"));
    CHECK_THAT(text,
               ContainsSubstring("- their indexed documents and decisions: knowledge_query\n"));
    CHECK_THAT(text, !ContainsSubstring("how code in an indexed project connects"));
    CHECK_THAT(text, !ContainsSubstring("graph_query"));
    CHECK_THAT(text, !ContainsSubstring("search_documents"));
    CHECK_THAT(text, !ContainsSubstring("complete"));
    CHECK_THAT(text, ContainsSubstring("- what models exist and where they stand: models_status"));
}

TEST_CASE("names are mended to UTF-8, and an embedder named by an absolute path by its file",
          "[cli][mcp][serve][58a][utf8]") {
    apogee::commands::ServerInventory inventory;
    inventory.served = every_served_name();
    inventory.collections.push_back(
        {.name = "caf\xE9", .chunks = 1, .embedder = "/Users/someone/models/e5.gguf"});
    inventory.collections.push_back(
        {.name = "win", .chunks = 2, .embedder = "C:\\models\\bge-small.gguf + adapters ab12"});
    inventory.collections.push_back({.name = "hf", .chunks = 2, .embedder = "nomic-ai/nomic-v1"});
    inventory.collections.push_back({.name = "both", .chunks = 2, .mixed = true});
    inventory.collections.push_back({.name = "gone", .unreadable = true});
    inventory.graphs.push_back({.name = "idle", .kind = "named"});
    inventory.graphs.push_back({.name = "broken", .kind = "named", .unreadable = true});
    const std::string text = apogee::commands::server_instructions(inventory);
    INFO(text);
    CHECK(apogee::harness::is_valid_utf8(text));
    CHECK_THAT(text,
               ContainsSubstring("- collections: caf\xEF\xBF\xBD (1 chunk, embedder e5.gguf), "
                                 "win (2 chunks, embedder bge-small.gguf + adapters ab12), "
                                 "hf (2 chunks, embedder nomic-ai/nomic-v1), both (2 chunks, "
                                 "mixed embedders), gone (unreadable)\n"));
    CHECK_THAT(
        text, ContainsSubstring("- graphs: idle (named, not built), broken (named, unreadable)\n"));
}

TEST_CASE("under a custom root the no-config introduction names check without the root's path",
          "[cli][mcp][serve][58a]") {
    // `--custom <root>/config/<file>` puts its path in every printed command;
    // the introduction never says a path, so it names the verb in words.
    const apogee::testing::TempDir root{"mcp-serve-custom-" +
                                        std::to_string(std::random_device{}())};
    const std::filesystem::path config = root.path() / "config" / "config.json";
    const apogee::harness::RootFlagScope scope{
        apogee::harness::RootFlag{.channel = std::nullopt, .custom_config = config}};
    REQUIRE_THAT(apogee::harness::render({.verbs = {"check"}}),
                 ContainsSubstring(root.path().string()));
    apogee::commands::ServerInventory inventory;
    inventory.setup = apogee::commands::ServedSetup::Absent;
    inventory.served = every_served_name();
    const std::string text = apogee::commands::server_instructions(inventory);
    INFO(text);
    CHECK_THAT(text, ContainsSubstring("Apogee is not set up here yet: its check command says what "
                                       "is missing, and each read answers with its own reason."));
    CHECK_THAT(text, !ContainsSubstring(root.path().string()));
    CHECK(text.find('/') == std::string::npos);
}
