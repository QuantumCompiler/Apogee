#include "cli/tui_workbench.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <utility>

#include "cli/chat_history.h"
#include "cli/config_cmd.h"
#include "cli/config_suites.h"
#include "cli/models.h"
#include "cli/tui_common.h"
#include "contracts/config.h"
#include "contracts/paths.h"
#include "httpserver/admin_config.h"
#include "logger/session.h"
#include "operations/backend_names.h"

namespace apogee::commands {

namespace {

/// The pointers every view shows above its table: where the config is, and
/// what the default backend and suite are.
[[nodiscard]] std::vector<std::string> config_heading(const std::filesystem::path& path,
                                                      const harness::Config& config) {
    const WrittenDefaults defaults = written_defaults(config);
    return {"config: " + path.string(),
            "models.default = " +
                (defaults.backend.empty() ? std::string{"(none)"} : defaults.backend) +
                "  ·  models.default_suite = " +
                (defaults.suite.empty() ? std::string{"(none)"} : defaults.suite)};
}

[[nodiscard]] tui::ListAction make_default(const RootContext& context) {
    return tui::ListAction{
        .key = "d",
        .label = "make default",
        .applies = [](const tui::ListRow& row) { return row.look != tui::ListRow::Look::Dim; },
        .run =
            [&context](const tui::ListRow& row) {
                return point_role(config_file(context), "default", row.key);
            }};
}

}  // namespace

tui::ListOptions models_view_options(const RootContext& context, const WorkbenchHooks& hooks) {
    tui::ListOptions options;
    options.title = "Models";
    options.columns = {"BACKEND", "TYPE", "MODEL", "ROLES", "STATE", "VERIFIED"};
    options.load = [&context]() {
        const std::filesystem::path path = config_file(context);
        const harness::Config config = harness::load_config(path);
        std::vector<tui::ListRow> rows;
        // `models list`'s own read, folded as its document folds it.
        for (const ModelRow& row : read_model_rows(config, path)) {
            if (row.consumed) {
                continue;
            }
            const nlohmann::json object = model_row_json(row);
            tui::ListRow shown{.key = row.backend.empty() ? row.model : row.backend,
                               .cells = {field(object, "backend"), field(object, "backend_type"),
                                         field(object, "model"), field(object, "roles"),
                                         field(object, "state"), field(object, "verified")}};
            if (row.attention) {
                shown.look = tui::ListRow::Look::Attention;
            } else if (!row.configured) {
                shown.look = tui::ListRow::Look::Dim;
            } else if (row.backend == written_defaults(config).backend) {
                shown.look = tui::ListRow::Look::Active;
            }
            rows.push_back(std::move(shown));
        }
        return std::pair{config_heading(path, config), std::move(rows)};
    };
    // Enter moves the conversation onto the row (35) -- a backend, or a model
    // one runs, named as `/model` takes it -- and i reads `models info`. A
    // row `/model` cannot take (stored weights no entry runs) stays put.
    options.enter_label = "use in the session";
    options.open = [&context, hooks](const tui::ListRow& row) {
        if (!hooks.use_model ||
            resolve_session_model(harness::load_config(config_file(context)), row.key)
                .backend.empty()) {
            return;
        }
        (void)hooks.use_model(row.key);
        if (hooks.show_session) {
            hooks.show_session();
        }
    };
    options.detail_key = "i";
    options.detail = [&context](const tui::ListRow& row) {
        const std::filesystem::path path = config_file(context);
        return lines_of(read_model_info(harness::load_config(path), row.key, path));
    };
    options.actions = {make_default(context)};
    return options;
}

tui::ListOptions chats_view_options(const WorkbenchHooks& hooks) {
    tui::ListOptions options;
    options.title = "Chats";
    options.columns = {"UPDATED", "TURNS", "NAME", "ID"};
    options.load = []() {
        std::vector<tui::ListRow> rows;
        // `chats list`'s own read, newest first.
        for (const logger::Session& session : logger::list_sessions()) {
            const nlohmann::json object = session_row_view(session);
            rows.push_back(tui::ListRow{.key = field(object, "id"),
                                        .cells = {field(object, "updated"), field(object, "turns"),
                                                  field(object, "name"), field(object, "id")}});
        }
        return std::pair{std::vector<std::string>{}, std::move(rows)};
    };
    options.enter_label = "open in the session";
    options.open = [hooks](const tui::ListRow& row) {
        (void)hooks.open_chat(row.key);
        hooks.show_session();
    };
    options.detail_key = "i";
    options.detail = [](const tui::ListRow& row) {
        return lines_of(format_session_info(logger::load(row.key, {}).session));
    };
    options.actions = {
        tui::ListAction{.key = "x",
                        .label = "delete",
                        .confirm =
                            [](const tui::ListRow& row) {
                                return "Delete chat " + row.cells.at(2) + " (" + row.key +
                                       "), its attachments' index and its recall summary?";
                            },
                        .run = [](const tui::ListRow& row) { return delete_chat(row.key); }}};
    return options;
}

tui::ListOptions suites_view_options(const RootContext& context, const WorkbenchHooks& hooks) {
    tui::ListOptions options;
    options.title = "Suites";
    options.columns = {"NAME", "MEMBERS", "DESCRIPTION"};
    options.load = [&context]() {
        const std::filesystem::path path = config_file(context);
        const harness::Config config = harness::load_config(path);
        std::vector<tui::ListRow> rows;
        for (const auto& [name, suite] : config.suites) {
            std::string members;
            for (const auto& [role, member] : suite.members) {
                members += (members.empty() ? "" : " ") + role + "=" + member.backend;
            }
            rows.push_back(tui::ListRow{.key = name,
                                        .cells = {name, members, suite.description},
                                        .look = name == written_defaults(config).suite
                                                    ? tui::ListRow::Look::Active
                                                    : tui::ListRow::Look::Plain});
        }
        return std::pair{config_heading(path, config), std::move(rows)};
    };
    options.enter_label = "use in the session";
    options.open = [hooks](const tui::ListRow& row) {
        (void)hooks.use_suite(row.key);
        hooks.show_session();
    };
    options.actions = {tui::ListAction{
        .key = "d", .label = "make default", .run = [&context](const tui::ListRow& row) {
            return point_default_suite(config_file(context), row.key);
        }}};
    return options;
}

tui::ListOptions config_view_options(const RootContext& context) {
    tui::ListOptions options;
    options.title = "Config";
    options.columns = {"BACKEND", "TYPE", "MODEL", "KEY"};
    options.load = [&context]() {
        const std::filesystem::path path = config_file(context);
        const harness::Config config = harness::load_config(path);
        std::vector<tui::ListRow> rows;
        for (const auto& [name, backend] : config.backends) {
            // The admin plane's view of a backend: a key is only ever whether
            // one is set -- the type has no field to carry one.
            const nlohmann::json view = httpserver::backend_view(name, backend);
            std::string model = field(view, "model");
            if (model.empty()) {
                model = field(view, "model_path");
            }
            rows.push_back(tui::ListRow{
                .key = name,
                .cells = {field(view, "name"), field(view, "type"), model,
                          view.value("api_key_set", false) ? "set in config" : "-"},
                .look = name == written_defaults(config).backend ? tui::ListRow::Look::Active
                                                                 : tui::ListRow::Look::Plain});
        }
        return std::pair{config_heading(path, config), std::move(rows)};
    };
    options.actions = {make_default(context),
                       tui::ListAction{.key = "x",
                                       .label = "remove",
                                       .confirm =
                                           [&context](const tui::ListRow& row) {
                                               return "Remove backend '" + row.key + "' from " +
                                                      config_file(context).string() + "?";
                                           },
                                       .run =
                                           [&context](const tui::ListRow& row) {
                                               return remove_backend(config_file(context), row.key);
                                           }}};
    return options;
}

std::vector<std::unique_ptr<tui::ListView>> make_workbench(tui::Pump& pump, tui::Theme theme,
                                                           const RootContext& context,
                                                           const WorkbenchHooks& hooks) {
    std::vector<std::unique_ptr<tui::ListView>> views;
    views.push_back(
        std::make_unique<tui::ListView>(pump, theme, models_view_options(context, hooks)));
    views.push_back(std::make_unique<tui::ListView>(pump, theme, chats_view_options(hooks)));
    views.push_back(
        std::make_unique<tui::ListView>(pump, theme, suites_view_options(context, hooks)));
    views.push_back(std::make_unique<tui::ListView>(pump, theme, config_view_options(context)));
    return views;
}

}  // namespace apogee::commands
