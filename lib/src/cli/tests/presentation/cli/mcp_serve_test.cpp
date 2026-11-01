#include "cli/mcp_serve.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "modelstore/sidecar.h"
#include "modelstore/snapshot.h"
#include "support/cli_home.h"
#include "support/gguf_builder.h"
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
    // five status reads last, in their order: added, never renamed.
    REQUIRE(names.size() >= 5 + 17);
    const std::vector<std::string> tail(names.end() - 5, names.end());
    CHECK(tail == std::vector<std::string>{"models_list", "models_status", "train_status",
                                           "train_versions", "check"});
    for (const char* native : {"read_file", "list_directory", "grep_files", "git_status",
                               "list_notes", "search_documents", "graph_query"}) {
        INFO(native);
        CHECK(std::ranges::find(names, native) != names.end());
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
                                                 "train_versions", "check"});
    const apogee::commands::RootContext context;
    const std::vector<apogee::mcp::ServedRead> reads = apogee::commands::status_reads(context);
    REQUIRE(reads.size() == names.size());
    for (std::size_t i = 0; i < reads.size(); ++i) {
        CHECK(reads[i].name == names[i]);
        CHECK(reads[i].input_schema["type"] == "object");
        CHECK_THAT(reads[i].description, ContainsSubstring("byte for byte"));
    }
}
