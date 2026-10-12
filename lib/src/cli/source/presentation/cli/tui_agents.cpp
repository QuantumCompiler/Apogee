#include "cli/tui_agents.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cli/line_tokens.h"
#include "cli/mcp_cmd.h"
#include "cli/tui_child.h"
#include "cli/tui_common.h"
#include "contracts/config.h"
#include "operations/credential_views.h"
#include "operations/read_views.h"
#include "secrets/resolve.h"

namespace apogee::commands {

namespace {

/// The rows the last read drew, by key, as their documents state them.
struct LastRead {
    std::mutex mutex;
    std::map<std::string, nlohmann::json> by_key;
};

/// A document's value as a card says it: a string as itself, a list joined,
/// a switch yes or no, nothing as `-`.
[[nodiscard]] std::string card_value(const nlohmann::json& value) {
    if (value.is_null()) {
        return "-";
    }
    if (value.is_string()) {
        return value.get<std::string>().empty() ? "-" : value.get<std::string>();
    }
    if (value.is_boolean()) {
        return value.get<bool>() ? "yes" : "no";
    }
    if (value.is_array()) {
        std::string out;
        for (const nlohmann::json& item : value) {
            out += (out.empty() ? "" : ", ") + card_value(item);
        }
        return out.empty() ? "-" : out;
    }
    return value.dump();
}

/// `fields` of `object`, one `field: value` line each, in that order.
[[nodiscard]] std::vector<std::string> card(const nlohmann::json& object,
                                            const std::vector<const char*>& fields) {
    std::size_t width = 0;
    for (const char* field_name : fields) {
        width = std::max(width, std::string{field_name}.size());
    }
    std::vector<std::string> lines;
    for (const char* field_name : fields) {
        if (!object.contains(field_name)) {
            continue;
        }
        std::string label = std::string{field_name} + ":";
        label.append(width + 2 - label.size(), ' ');
        lines.push_back(label + card_value(object.at(field_name)));
    }
    return lines;
}

[[nodiscard]] std::optional<nlohmann::json> remembered(const LastRead& last,
                                                       const std::string& key) {
    const auto found = last.by_key.find(key);
    return found == last.by_key.end() ? std::nullopt : std::optional<nlohmann::json>{found->second};
}

}  // namespace

tui::ListOptions agents_view_options(const RootContext& context, std::filesystem::path binary) {
    const auto last = std::make_shared<LastRead>();
    tui::ListOptions options;
    options.title = "Agents";
    options.columns = {"NAME", "MODEL", "TOOLS", "SOURCE"};
    options.load = [&context, last]() {
        // `agents list`'s own document: every agent, bundled and configured.
        const harness::Config config =
            config_if_any(config_file(context)).value_or(harness::Config{});
        const nlohmann::json document = operations::agents_document(config);
        std::vector<tui::ListRow> rows;
        std::map<std::string, nlohmann::json> by_key;
        for (const nlohmann::json& agent : document["data"]) {
            const std::string name = field(agent, "name");
            const std::string model = field(agent, "model");
            const bool bundled = agent.value("bundled", false);
            rows.push_back(tui::ListRow{
                .key = name,
                .cells = {name, model.empty() ? std::string{"(default)"} : model,
                          field(agent, "tools") +
                              (agent.value("questions", false) ? "+questions" : ""),
                          bundled ? "bundled" : "config"},
                .look = bundled ? tui::ListRow::Look::Dim : tui::ListRow::Look::Plain});
            by_key.emplace(name, agent);
        }
        {
            const std::lock_guard lock{last->mutex};
            last->by_key = std::move(by_key);
        }
        return std::pair{std::vector<std::string>{}, std::move(rows)};
    };
    options.enter_label = "definition";
    options.detail = [last](const tui::ListRow& row) {
        const std::lock_guard lock{last->mutex};
        const std::optional<nlohmann::json> agent = remembered(*last, row.key);
        if (!agent.has_value()) {
            return std::vector<std::string>{"read again -- the agents have changed"};
        }
        return card(*agent, {"name", "description", "model", "prompts", "schemas", "output_format",
                             "tools", "mcp", "questions", "collection", "save_dir", "save_filename",
                             "save_subdir", "bundled", "overrides_bundled"});
    };
    options.actions = {tui::ListAction{
        .key = "x",
        .label = "delete",
        .applies =
            [last](const tui::ListRow& row) {
                // A bundled agent with no entry has nothing to delete.
                const std::lock_guard lock{last->mutex};
                const std::optional<nlohmann::json> agent = remembered(*last, row.key);
                return agent.has_value() && (!agent->value("bundled", false) ||
                                             agent->value("overrides_bundled", false));
            },
        .confirm =
            [](const tui::ListRow& row) {
                return "Delete agent " + row.key + "'s entry ('apogee agents delete " + row.key +
                       "')? Its prompt and schema files stay";
            },
        .run =
            [&context, binary](const tui::ListRow& row) {
                return child_answer(context, binary, {"agents", "delete", row.key});
            }}};
    return options;
}

tui::ListOptions mcp_view_options(const RootContext& context, std::filesystem::path binary) {
    const auto last = std::make_shared<LastRead>();
    tui::ListOptions options;
    options.title = "MCP";
    options.columns = {"NAME", "STATE", "PROTOCOL", "TOOLS"};
    options.load = [&context, last]() {
        // `mcp list`'s own document: each server connected to, as the
        // command connects -- off the shell's thread, bounded.
        nlohmann::json document;
        try {
            document = mcp_list_document(context);
        } catch (const harness::ConfigError& e) {
            return std::pair{std::vector<std::string>{e.what()}, std::vector<tui::ListRow>{}};
        }
        std::vector<tui::ListRow> rows;
        std::map<std::string, nlohmann::json> by_key;
        for (const nlohmann::json& server : document["data"]) {
            const std::string name = field(server, "name");
            const std::string state = field(server, "state");
            const std::string protocol = field(server, "protocol_version");
            const std::string tools = server.contains("tools") ? card_value(server["tools"]) : "-";
            rows.push_back(
                tui::ListRow{.key = name,
                             .cells = {name, state.empty() ? std::string{"-"} : state,
                                       protocol.empty() ? std::string{"-"} : protocol, tools},
                             .look = state == "connected"       ? tui::ListRow::Look::Active
                                     : state == "not connected" ? tui::ListRow::Look::Attention
                                                                : tui::ListRow::Look::Dim});
            by_key.emplace(name, server);
        }
        std::vector<std::string> heading;
        if (rows.empty()) {
            heading.emplace_back(
                "No MCP servers configured. Create one with: apogee mcp create <name>");
        }
        {
            const std::lock_guard lock{last->mutex};
            last->by_key = std::move(by_key);
        }
        return std::pair{std::move(heading), std::move(rows)};
    };
    options.enter_label = "entry";
    options.detail = [last](const tui::ListRow& row) {
        const std::lock_guard lock{last->mutex};
        const std::optional<nlohmann::json> server = remembered(*last, row.key);
        if (!server.has_value()) {
            return std::vector<std::string>{"read again -- the servers have changed"};
        }
        return card(*server, {"name", "command", "args", "enabled", "env_set", "state",
                              "protocol_version", "tools", "error"});
    };
    // `t`: one tool on the selected server, as `mcp test` invokes it.
    options.asks = {
        tui::ListAsk{.key = "t",
                     .label = "test",
                     .ask =
                         [&context, binary](const tui::ListRow& /*row*/, const std::string& line) {
                             const LineTokens split = line_tokens(line);
                             if (!split.error.empty()) {
                                 return "not run: " + split.error;
                             }
                             std::vector<std::string> words{"mcp", "test"};
                             words.insert(words.end(), split.words.begin(), split.words.end());
                             return child_answer(context, binary, words);
                         },
                     .prefill = [](const tui::ListRow& row) { return row.key + " "; },
                     .needs_row = true}};
    const auto in_state = [last](bool enabled) {
        return [last, enabled](const tui::ListRow& row) {
            const std::lock_guard lock{last->mutex};
            const std::optional<nlohmann::json> server = remembered(*last, row.key);
            return server.has_value() && server->value("enabled", true) != enabled;
        };
    };
    options.actions = {
        tui::ListAction{.key = "e",
                        .label = "enable",
                        .applies = in_state(true),
                        .run =
                            [&context, binary](const tui::ListRow& row) {
                                return child_answer(context, binary, {"mcp", "enable", row.key});
                            }},
        tui::ListAction{.key = "d",
                        .label = "disable",
                        .applies = in_state(false),
                        .run = [&context, binary](const tui::ListRow& row) {
                            return child_answer(context, binary, {"mcp", "disable", row.key});
                        }}};
    return options;
}

tui::ListOptions auth_view_options(const RootContext& context) {
    tui::ListOptions options;
    options.title = "Auth";
    options.columns = {"PROVIDER", "STORED AT"};
    options.load = [&context]() {
        // `auth list`'s own document: metadata only -- there is no key field
        // to draw.
        const nlohmann::json document =
            operations::credentials_document(config_file(context), secrets::EnvSnapshot::process());
        std::vector<tui::ListRow> rows;
        for (const nlohmann::json& stored : document["data"]) {
            rows.push_back(
                tui::ListRow{.key = field(stored, "provider"),
                             .cells = {field(stored, "provider"), field(stored, "stored_at")}});
        }
        std::vector<std::string> heading;
        if (const std::string warning = field(document, "warning"); !warning.empty()) {
            heading.push_back(warning);
        }
        for (const nlohmann::json& backend : document["backends"]) {
            const std::string variable = field(backend, "variable");
            heading.push_back("backend " + field(backend, "name") + " (" + field(backend, "type") +
                              "): its key from " + field(backend, "source") +
                              (variable.empty() ? std::string{} : " (" + variable + ")"));
        }
        heading.emplace_back(
            "a key is stored at a real prompt -- 'apogee auth add <provider>' -- never in the "
            "shell");
        return std::pair{std::move(heading), std::move(rows)};
    };
    return options;
}

}  // namespace apogee::commands
