#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "httpserver/admin_agents.h"
#include "httpserver/admin_config.h"
#include "logger/session.h"
#include "support/cli_home.h"

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

void save_chat(const std::string& id, const std::string& title, int turns) {
    apogee::logger::Session session;
    session.chat_id = id;
    session.title = title;
    session.turns = turns;
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
    save_chat("20261007-100500-bbbb", "second", 2);
    const nlohmann::json listed =
        document(read(home, {"chats", "list", "--output-format", "json"}));
    REQUIRE(listed.at("data").size() == 2);
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
                                          "chats list", "agents list", "mcp list", "check"}) {
        INFO(refused.err);
        CHECK(refused.err.find(reader) != std::string::npos);
    }
    // A read refuses a turn's format by name.
    const Read stream = read(home, {"chats", "list", "--output-format", "stream-json"});
    CHECK(stream.code != 0);
    CHECK(stream.err.find("expected 'text' or 'json'") != std::string::npos);
}
