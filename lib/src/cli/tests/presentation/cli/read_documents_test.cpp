#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "cli/complete_protocol.h"
#include "cli/models.h"
#include "cli/registry.h"
#include "cli/root.h"
#include "contracts/config.h"
#include "httpserver/admin_agents.h"
#include "httpserver/admin_config.h"
#include "logger/session.h"
#include "support/cli_home.h"
#include "support/gguf_builder.h"
#include "support/mlx_model.h"
#include "support/pipeline_model.h"
#include "support/voice_model.h"

/// The reads' machine face (28h): `--output-format json` on `models
/// list|info|status`, `chats list`, `agents list`, `mcp list` and `check`,
/// each one document of the same facts as its human view, on the real
/// command tree in a throwaway install.
namespace {

using apogee::testing::CliHome;

constexpr const char* kConfig = R"(backends:
  local:
    type: mock
    model: mock-1
  spare:
    type: mock
    model: mock-2

models:
  default: local

mcp_servers:
  quiet:
    command: /bin/true
    enabled: false
)";

struct Read {
    int code = -1;
    std::string out;
    std::string err;
};

[[nodiscard]] Read read(const CliHome& home, std::vector<std::string> args) {
    Read result;
    result.code = home.run(args, &result.out, &result.err);
    return result;
}

/// stdout is one JSON document on one line, and nothing else.
[[nodiscard]] nlohmann::json document(const Read& read) {
    INFO(read.out << read.err);
    REQUIRE(!read.out.empty());
    CHECK(read.out.back() == '\n');
    CHECK(std::ranges::count(read.out, '\n') == 1);
    const nlohmann::json parsed = nlohmann::json::parse(read.out, nullptr, false);
    REQUIRE_FALSE(parsed.is_discarded());
    return parsed;
}

[[nodiscard]] std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in{text};
    std::string line;
    while (std::getline(in, line)) {
        out.push_back(line);
    }
    return out;
}

void save_chat(const std::string& id, const std::string& title, int turns,
               const std::string& backend = "local", const std::string& model = {}) {
    apogee::logger::Session session;
    session.chat_id = id;
    session.title = title;
    session.turns = turns;
    session.backend = backend;
    session.model = model;
    session.started_at = "2026-10-07T10:00:00Z";
    session.updated_at = "2026-10-07T10:0" + std::to_string(turns) + ":00Z";
    apogee::logger::save(session);
}

}  // namespace

TEST_CASE("models list: the table's rows, one document", "[reads][json][models]") {
    const CliHome home{kConfig};
    const nlohmann::json listed =
        document(read(home, {"models", "list", "--output-format", "json"}));
    CHECK(listed.at("object") == "list");
    CHECK(listed.at("folded") == 0);
    std::set<std::string> json_backends;
    for (const nlohmann::json& row : listed.at("data")) {
        json_backends.insert(row.at("backend").get<std::string>());
        CHECK(row.contains("state"));
        CHECK(row.contains("verified"));
    }
    CHECK(json_backends == std::set<std::string>{"local", "spare"});
    // The rows the human table lists, the same.
    const Read table = read(home, {"models", "list", "--no-color"});
    std::set<std::string> text_backends;
    for (const std::string& line : lines(table.out)) {
        for (const std::string& name : {std::string{"local"}, std::string{"spare"}}) {
            if (line.starts_with(name + " ")) {
                text_backends.insert(name);
            }
        }
    }
    CHECK(text_backends == json_backends);
    // stream-json keeps its rows, a line each, as before.
    const Read stream = read(home, {"models", "list", "--output-format", "stream-json"});
    CHECK(lines(stream.out).size() == 2);
}

TEST_CASE("models info and status: their record as fields, label for label",
          "[reads][json][models]") {
    const CliHome home{kConfig};
    for (const std::vector<std::string>& command :
         {std::vector<std::string>{"models", "info", "local"},
          std::vector<std::string>{"models", "status"}}) {
        const Read text = read(home, command);
        std::vector<std::string> json_args = command;
        json_args.insert(json_args.end(), {"--output-format", "json"});
        const nlohmann::json record = document(read(home, json_args));
        const std::vector<std::string> said = lines(text.out);
        REQUIRE(record.at("fields").size() == said.size());
        for (std::size_t i = 0; i < said.size(); ++i) {
            const nlohmann::json& field = record.at("fields")[i];
            INFO(said[i]);
            CHECK(said[i].starts_with(field.at("field").get<std::string>() + ":"));
            CHECK(said[i].ends_with(field.at("value").get<std::string>()));
        }
    }
    CHECK(document(read(home, {"models", "info", "local", "--output-format", "json"})).at("name") ==
          "local");
}

TEST_CASE("chats list: every saved chat, newest first, the row's facts", "[reads][json][chats]") {
    const CliHome home{kConfig};
    CHECK(document(read(home, {"chats", "list", "--output-format", "json"})) ==
          nlohmann::json::parse(R"({"object":"list","data":[]})"));
    save_chat("20261007-100000-aaaa", "first", 1);
    save_chat("20261007-100500-bbbb", "second", 2, "claude", "claude-opus-5-5");
    const nlohmann::json listed =
        document(read(home, {"chats", "list", "--output-format", "json"}));
    REQUIRE(listed.at("data").size() == 2);
    // Each row's backend, and the roster model it pinned -- empty on the
    // backend's own -- as its session file records them (52a).
    nlohmann::json newest = listed.at("data")[0];
    newest.erase("updated");  // the save's own stamp
    CHECK(newest == nlohmann::json::parse(R"({"id":"20261007-100500-bbbb","turns":2,)"
                                          R"("name":"second","backend":"claude",)"
                                          R"("model":"claude-opus-5-5"})"));
    CHECK(listed.at("data")[1].at("backend") == "local");
    CHECK(listed.at("data")[1].at("model") == "");
    const Read text = read(home, {"chats", "list"});
    const std::vector<std::string> rows = lines(text.out);
    REQUIRE(rows.size() == 2);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const nlohmann::json& chat = listed.at("data")[i];
        CHECK(rows[i].starts_with(chat.at("id").get<std::string>()));
        CHECK(rows[i].ends_with(chat.at("name").get<std::string>()));
        CHECK(rows[i].find(std::to_string(chat.at("turns").get<int>()) + " turns") !=
              std::string::npos);
    }
}

TEST_CASE("agents list and mcp list: the control plane's own bodies", "[reads][json][parity]") {
    const CliHome home{kConfig};
    const apogee::httpserver::AdminConfigContext context{.config_path = home.config_path()};

    const nlohmann::json agents =
        document(read(home, {"agents", "list", "--output-format", "json"}));
    CHECK(agents == nlohmann::json::parse(apogee::httpserver::admin_list_agents(context).body));
    const Read agents_text = read(home, {"agents", "list"});
    for (const nlohmann::json& agent : agents.at("data")) {
        CHECK(agents_text.out.find(agent.at("name").get<std::string>()) != std::string::npos);
    }

    const nlohmann::json servers = document(read(home, {"mcp", "list", "--output-format", "json"}));
    REQUIRE(servers.at("data").size() == 1);
    const nlohmann::json& quiet = servers.at("data")[0];
    // The admin entry, field for field, and what connecting found.
    const nlohmann::json admin =
        nlohmann::json::parse(apogee::httpserver::admin_list_mcp_servers(context).body);
    for (const auto& [key, value] : admin.at("data")[0].items()) {
        CHECK(quiet.at(key) == value);
    }
    CHECK(quiet.at("state") == "disabled");
    const Read servers_text = read(home, {"mcp", "list"});
    CHECK(servers_text.out.find("quiet") != std::string::npos);
    CHECK(servers_text.out.find("disabled") != std::string::npos);
}

TEST_CASE("check: the rows and the verdict as data, the exit code unchanged",
          "[reads][json][check]") {
    SECTION("a healthy install") {
        const CliHome home{kConfig};
        REQUIRE(read(home, {"check", "--fix"}).code == 0);
        const Read text = read(home, {"check", "--no-color"});
        const Read json = read(home, {"check", "--output-format", "json"});
        CHECK(json.code == text.code);
        const nlohmann::json report = document(json);
        CHECK(report.at("ok") == true);
        CHECK(report.at("failures") == 0);
        CHECK_FALSE(report.contains("fixed"));
        // Every row the report shows is a row of the document, by name.
        for (const nlohmann::json& row : report.at("rows")) {
            const std::string status = row.at("status").get<std::string>();
            CHECK((status == "ok" || status == "warn" || status == "fail" || status == "skipped"));
            CHECK(text.out.find(row.at("name").get<std::string>()) != std::string::npos);
        }
    }
    SECTION("a broken install fails the same way in both") {
        const CliHome home{"backends: [this is not a mapping\n"};
        const Read text = read(home, {"check", "--no-color"});
        const Read json = read(home, {"check", "--output-format", "json"});
        CHECK(text.code != 0);
        CHECK(json.code == text.code);
        const nlohmann::json report = document(json);
        CHECK(report.at("ok") == false);
        CHECK(report.at("failures").get<int>() > 0);
    }
    SECTION("--fix says what it repaired inside the document, nothing beside it") {
        const CliHome home{kConfig};
        const nlohmann::json report =
            document(read(home, {"check", "--fix", "--output-format", "json"}));
        REQUIRE(report.contains("fixed"));
        CHECK_FALSE(report.at("fixed").empty());
    }
}

TEST_CASE("a JSON face asked of a command with none is refused, naming the readers",
          "[reads][json]") {
    const CliHome home{kConfig};
    const Read refused = read(home, {"version", "--output-format", "json"});
    CHECK(refused.code != 0);
    CHECK(refused.out.empty());
    for (const std::string_view reader : {"models list", "models info", "models status",
                                          "chats list", "agents list", "mcp list", "check",
                                          // M31: three reads that answered only in prose.
                                          "chats info", "chats recall", "graph dedupe"}) {
        INFO(refused.err);
        CHECK(refused.err.find(reader) != std::string::npos);
    }
    // ...and each of those three takes the flag rather than refusing it
    // (M31: each exited 109, "The following arguments were not expected").
    save_chat("20261007-100000-aaaa", "first", 1);
    for (const std::vector<std::string>& args :
         {std::vector<std::string>{"chats", "info", "20261007-100000-aaaa"},
          std::vector<std::string>{"chats", "recall"}}) {
        std::vector<std::string> json = args;
        json.insert(json.end(), {"--output-format", "json"});
        const Read taken = read(home, json);
        INFO(taken.err);
        CHECK(taken.code == 0);
        CHECK(taken.err.find("not expected") == std::string::npos);
        CHECK(document(taken).is_object());
    }
    // A read refuses a turn's format by name.
    const Read stream = read(home, {"chats", "list", "--output-format", "stream-json"});
    CHECK(stream.code != 0);
    CHECK(stream.err.find("expected 'text' or 'json'") != std::string::npos);
}

TEST_CASE("models info on a stored set carries a typed data beside its prose fields, unchanged",
          "[reads][json][models][typed]") {
    // 52e: the set's facts as data -- format, bytes, window, cache, lineage --
    // where the format's reader knows them, null where it does not; the
    // fields[] the text says stay label for label.
    const CliHome home{kConfig};
    const std::filesystem::path gguf =
        home.models() / "org--m" / "gguf" / "aaaaaaaaaaaa" / "m-Q4_K_M.gguf";
    std::filesystem::create_directories(gguf.parent_path());
    std::ofstream{gguf, std::ios::binary} << apogee::testing::quantizable_gguf();
    apogee::testing::write_mlx_model(home.models() / "org--x" / "mlx" / "bbbbbbbbbbbb");
    apogee::testing::write_pipeline_model(home.models() / "org--clip" / "pipeline" /
                                          "cccccccccccc");
    apogee::testing::write_voice_model(home.models() / "hexgrad--K" / "voice" / "dddddddddddd");

    std::map<std::string, nlohmann::json> data;
    for (const std::string handle :
         {"org--m/gguf/aaaaaaaaaaaa", "org--x/mlx/bbbbbbbbbbbb", "org--clip/pipeline/cccccccccccc",
          "hexgrad--K/voice/dddddddddddd"}) {
        INFO(handle);
        const Read text = read(home, {"models", "info", handle});
        REQUIRE(text.code == 0);
        const nlohmann::json record =
            document(read(home, {"models", "info", handle, "--output-format", "json"}));
        // The prose, byte for byte the record the text makes, as before.
        nlohmann::json prose = apogee::commands::render_record_document(text.out);
        prose["name"] = handle;
        nlohmann::json without = record;
        without.erase("data");
        CHECK(without == prose);
        const nlohmann::json& typed = record.at("data");
        CHECK(typed.at("handle") == handle);
        CHECK(typed.at("bytes").get<std::uintmax_t>() > 0);
        CHECK(typed.at("lineage").is_array());
        data[typed.at("format").get<std::string>()] = typed;
    }
    // A GGUF's header states its window and what its cache costs.
    CHECK(data.at("gguf").at("window").at("tokens").get<std::int64_t>() > 0);
    CHECK(data.at("gguf").at("window").at("source") == "default");
    CHECK(data.at("gguf").contains("cache"));
    // An MLX model's config.json states its trained window.
    CHECK(data.at("mlx").at("window").at("trained") == 131072);
    CHECK(data.at("mlx").at("architecture").is_string());
    // A pipeline and a voice model state neither: unknown stays unknown.
    for (const std::string format : {"pipeline", "voice"}) {
        INFO(format);
        CHECK(data.at(format).at("window").is_null());
        CHECK(data.at(format).at("cache").is_null());
    }
    // A backend's document carries no data: its typed facts are its config's.
    CHECK_FALSE(document(read(home, {"models", "info", "local", "--output-format", "json"}))
                    .contains("data"));
}

TEST_CASE("the reads that gained a JSON face offer --output-format at completion, and take it",
          "[reads][json][completion]") {
    // M31, ADR 0007: the flag is offered where it is new -- `chats info`,
    // `chats recall`, `graph dedupe` -- its words with it, and each word
    // offered is one the verb takes.
    const CliHome home{kConfig};
    save_chat("20261007-100000-aaaa", "first", 1);
    const apogee::commands::RootCommand root{apogee::commands::default_registry()};
    const apogee::commands::CommandSpec tree = apogee::commands::specs_from_app(root.app());
    const apogee::harness::Config config = apogee::harness::parse_config(kConfig, "test");
    const auto offered = [&](std::vector<std::string> words, std::string current) {
        return apogee::commands::complete_words(
                   {.words = std::move(words), .current = std::move(current)}, config, tree)
            .candidates;
    };
    for (const std::vector<std::string>& verb :
         {std::vector<std::string>{"chats", "info", "20261007-100000-aaaa"},
          std::vector<std::string>{"chats", "recall"},
          std::vector<std::string>{"graph", "dedupe", "notes"}}) {
        std::string line;
        for (const std::string& word : verb) {
            line += word + " ";
        }
        INFO("apogee " << line);
        const std::vector<std::string> flags = offered(verb, "--");
        CHECK(std::ranges::find(flags, "--output-format") != flags.end());
        std::vector<std::string> valued = verb;
        valued.emplace_back("--output-format");
        const std::vector<std::string> words = offered(valued, "");
        CHECK(words == std::vector<std::string>{"text", "json"});
    }
    // What it offers, it takes.
    for (const std::string format : {"text", "json"}) {
        INFO(format);
        CHECK(
            read(home, {"chats", "info", "20261007-100000-aaaa", "--output-format", format}).code ==
            0);
        CHECK(read(home, {"chats", "recall", "--output-format", format}).code == 0);
    }
}
