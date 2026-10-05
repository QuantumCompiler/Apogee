#include "tasks/ledger.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <system_error>
#include <utility>

#include "contracts/config_edit.h"
#include "platform/platform.h"

namespace apogee::tasks {

std::filesystem::path task_dir(const std::filesystem::path& root, std::string_view id) {
    return root / std::string{id};
}

std::filesystem::path ledger_path(const std::filesystem::path& root, std::string_view id) {
    return task_dir(root, id) / kLedgerFileName;
}

std::string new_task_id(const std::filesystem::path& root,
                        std::chrono::system_clock::time_point now) {
    const std::string base = std::string{kTaskIdPrefix} + platform::utc_time(now, "%Y%m%d-%H%M%S");
    std::string id = base;
    std::error_code code;
    for (int suffix = 2; std::filesystem::exists(task_dir(root, id), code); ++suffix) {
        id = base + "-" + std::to_string(suffix);
    }
    return id;
}

bool valid_task_id(std::string_view id) noexcept {
    if (id.empty() || id.size() > 64) {
        return false;
    }
    return std::ranges::all_of(id, [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               c == '-' || c == '_';
    });
}

std::string now_timestamp() {
    return platform::utc_time(std::chrono::system_clock::now(), "%Y-%m-%dT%H:%M:%SZ");
}

std::string save_task(const std::filesystem::path& root, const Task& task) {
    if (!valid_task_id(task.id)) {
        return "not a task id: '" + task.id + "'";
    }
    std::error_code code;
    std::filesystem::create_directories(task_dir(root, task.id), code);
    if (code) {
        return "could not create " + task_dir(root, task.id).string() + ": " + code.message();
    }
    try {
        harness::write_file_atomically(ledger_path(root, task.id),
                                       task_to_json(task).dump(2) + "\n");
    } catch (const std::exception& e) {
        return e.what();
    }
    return {};
}

std::optional<Task> load_task(const std::filesystem::path& root, std::string_view id,
                              std::string& error) {
    error.clear();
    if (!valid_task_id(id)) {
        error = "not a task id: '" + std::string{id} + "'";
        return std::nullopt;
    }
    const std::filesystem::path path = ledger_path(root, id);
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        error = "no task '" + std::string{id} + "'";
        return std::nullopt;
    }
    const nlohmann::json json = nlohmann::json::parse(in, nullptr, false);
    if (json.is_discarded()) {
        error = path.string() + " is not JSON";
        return std::nullopt;
    }
    try {
        return task_from_json(json);
    } catch (const std::exception& e) {
        error = path.string() + ": " + e.what();
        return std::nullopt;
    }
}

std::vector<Task> list_tasks(const std::filesystem::path& root,
                             std::vector<std::string>* problems) {
    std::vector<Task> tasks;
    std::error_code code;
    for (std::filesystem::directory_iterator it{root, code}, end; !code && it != end;
         it.increment(code)) {
        if (!it->is_directory(code)) {
            continue;
        }
        const std::string id = it->path().filename().string();
        if (!id.starts_with(kTaskIdPrefix)) {
            continue;
        }
        std::string error;
        std::optional<Task> task = load_task(root, id, error);
        if (task.has_value()) {
            tasks.push_back(std::move(*task));
        } else if (problems != nullptr) {
            problems->push_back(error);
        }
    }
    // Newest first: the ids sort by their start time, and a later suffix is
    // a later task.
    std::ranges::sort(tasks, [](const Task& a, const Task& b) {
        if (a.created_at != b.created_at) {
            return a.created_at > b.created_at;
        }
        return a.id.size() != b.id.size() ? a.id.size() > b.id.size() : a.id > b.id;
    });
    return tasks;
}

std::string_view to_string(Request request) noexcept {
    switch (request) {
        case Request::Halt:
            return "halt";
        case Request::Cancel:
            return "cancel";
        case Request::None:
            break;
    }
    return "none";
}

std::string write_request(const std::filesystem::path& root, std::string_view id, Request request) {
    if (!valid_task_id(id)) {
        return "not a task id: '" + std::string{id} + "'";
    }
    try {
        harness::write_file_atomically(task_dir(root, id) / kRequestFileName,
                                       std::string{to_string(request)} + "\n");
    } catch (const std::exception& e) {
        return e.what();
    }
    return {};
}

Request read_request(const std::filesystem::path& root, std::string_view id) {
    std::ifstream in{task_dir(root, id) / kRequestFileName};
    std::string word;
    if (!(in >> word)) {
        return Request::None;
    }
    if (word == to_string(Request::Cancel)) {
        return Request::Cancel;
    }
    if (word == to_string(Request::Halt)) {
        return Request::Halt;
    }
    return Request::None;
}

void clear_request(const std::filesystem::path& root, std::string_view id) {
    std::error_code code;
    std::filesystem::remove(task_dir(root, id) / kRequestFileName, code);
}

std::optional<LockHolder> lock_holder(const std::filesystem::path& root) {
    const std::optional<platform::PidLock::Holder> holder =
        platform::PidLock::holder(root / kLockFileName);
    if (!holder.has_value()) {
        return std::nullopt;
    }
    return LockHolder{.pid = holder->pid, .task_id = holder->label, .running = holder->running};
}

std::optional<std::string> running_task(const std::filesystem::path& root) {
    const std::optional<LockHolder> holder = lock_holder(root);
    if (!holder.has_value() || !holder->running || holder->task_id.empty()) {
        return std::nullopt;
    }
    return holder->task_id;
}

TaskLock::TaskLock(platform::PidLock lock) : lock_{std::move(lock)} {}

std::optional<TaskLock> TaskLock::acquire(const std::filesystem::path& root,
                                          std::string_view task_id, std::string& error) {
    error.clear();
    platform::PidLock::Refusal refusal;
    std::optional<platform::PidLock> lock =
        platform::PidLock::acquire(root / kLockFileName, task_id, /*take_stale=*/true, refusal);
    if (lock.has_value()) {
        return TaskLock{std::move(*lock)};
    }
    if (!refusal.holder.has_value()) {
        error = refusal.error;
        return std::nullopt;
    }
    const platform::PidLock::Holder& holder = *refusal.holder;
    if (holder.label.empty()) {
        error = "another task holds the lock " + (root / kLockFileName).string() +
                " -- one task runs at a time. If none is running, remove the file";
        return std::nullopt;
    }
    error = "task " + holder.label + " is running (process " + std::to_string(holder.pid) +
            ") -- one task runs at a time. 'apogee task status " + holder.label +
            "' shows it; 'apogee task halt' or 'apogee task cancel' stops it";
    return std::nullopt;
}

}  // namespace apogee::tasks
