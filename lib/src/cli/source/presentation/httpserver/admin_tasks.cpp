#include "httpserver/admin_tasks.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "contracts/layout.h"
#include "tasks/ledger.h"
#include "tasks/task.h"
#include "tasks/view.h"

namespace apogee::httpserver {

HttpResponse admin_list_tasks(const HttpRequest& request) {
    const std::string all = request.query_value("all");
    if (!all.empty() && all != "true" && all != "false") {
        return error_response(400, "all must be true or false");
    }
    // An unreadable ledger is left out, as `task list` leaves it out; the
    // reason names its path, which a served response never does.
    const std::vector<tasks::Task> listed = tasks::list_tasks(harness::tasks_dir());
    return json_response(200, tasks::to_json(tasks::make_task_list(listed, all == "true")));
}

HttpResponse admin_get_task(std::string_view id) {
    if (!tasks::valid_task_id(id)) {
        return error_response(400, "'" + std::string{id} + "' is not a task id");
    }
    const std::filesystem::path root = harness::tasks_dir();
    if (!std::filesystem::exists(tasks::ledger_path(root, id))) {
        return error_response(404, "no task '" + std::string{id} + "'");
    }
    std::string error;
    const std::optional<tasks::Task> task = tasks::load_task(root, id, error);
    if (!task.has_value()) {
        // `error` names the ledger's path; the response says what is wrong
        // without it.
        return error_response(500, "task '" + std::string{id} + "''s ledger cannot be read");
    }
    return json_response(200,
                         tasks::to_json(tasks::make_task_view(*task, tasks::lock_holder(root))));
}

}  // namespace apogee::httpserver
