#pragma once

#include <nlohmann/json_fwd.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "events/bus.h"
#include "harness/cancellation.h"

/// The async-job substrate for long-running admin work.
///
/// A route that would take minutes -- a server-local ingest, a model pull --
/// starts a job and answers `202 {job_id}` at once. Progress reaches a client
/// two ways: as `admin.job.*` events on the bus, and in this registry, which
/// `GET /v1/admin/jobs[/{id}]` exposes so a client can poll, or catch up after
/// a reconnect. Jobs live for the process; nothing here is persisted.
///
/// **Cancel wins.** `DELETE /v1/admin/jobs/{id}` fires the job's
/// `harness::CancellationToken` -- the same token every provider call already
/// checks -- and marks it cancelled. A worker that then dies with an error and
/// calls `fail`, or finishes and calls `finish`, changes nothing: both are
/// no-ops once a job has left `running`, so a cancelled job can never be
/// overwritten to failed.
namespace apogee::httpserver {

enum class JobStatus : std::uint8_t { Running, Succeeded, Failed, Cancelled };

[[nodiscard]] std::string_view to_string(JobStatus status) noexcept;

/// One job's tracked state, as the routes serialize it.
struct JobRecord {
    std::string id;
    std::string kind;
    JobStatus status = JobStatus::Running;
    /// The latest progress line.
    std::string message;
    /// Set on success.
    nlohmann::json result;
    /// Set on failure.
    std::string error;
    std::string started;
    std::string finished;
};

[[nodiscard]] nlohmann::json job_json(const JobRecord& record);

class JobRegistry {
public:
    explicit JobRegistry(events::Bus& bus);

    struct Started {
        std::string id;
        /// The worker must run its work under this token.
        harness::CancellationToken cancellation;
    };

    /// Registers a running job and emits `admin.job.started` with `fields`.
    [[nodiscard]] Started start(std::string kind, nlohmann::json fields);

    /// Records the latest progress line and emits `admin.job.progress`.
    void progress(const std::string& id, std::string message, nlohmann::json fields);

    /// Marks the job succeeded and emits `admin.job.completed`. A no-op once
    /// the job has left `running`.
    void finish(const std::string& id, nlohmann::json result);

    /// Marks the job failed and emits `admin.job.failed`. A no-op once the
    /// job has left `running`.
    void fail(const std::string& id, std::string error);

    /// Fires the job's token, marks it cancelled, emits `admin.job.cancelled`.
    /// Idempotent on a finished job (returns the record unchanged); nullopt
    /// for an unknown id.
    std::optional<JobRecord> cancel(const std::string& id);

    [[nodiscard]] std::optional<JobRecord> get(const std::string& id) const;

    /// Every job, newest started first.
    [[nodiscard]] std::vector<JobRecord> list() const;

private:
    struct Entry {
        JobRecord record;
        harness::CancellationToken cancellation;
    };

    events::Bus* bus_;
    mutable std::mutex mutex_;
    std::map<std::string, Entry> jobs_;
};

/// Turns a tool's line-buffered output into progress events, one per
/// non-empty line, so a stream mirrors what a CLI user would see scroll by.
class JobWriter {
public:
    JobWriter(JobRegistry& registry, std::string id);

    void write(std::string_view bytes);

    /// Emits a trailing partial line, if any.
    void flush();

private:
    JobRegistry* registry_;
    std::string id_;
    std::string buffer_;
};

/// The threads a route's async work runs on. **Destruction cancels, then
/// joins**: every token handed out is fired first, so a worker blocked in a
/// model call returns through the provider's own cancellation check, and no
/// thread outlives the registry, the plane or the harness it works on.
class JobWorkers {
public:
    JobWorkers() = default;
    ~JobWorkers();

    JobWorkers(const JobWorkers&) = delete;
    JobWorkers& operator=(const JobWorkers&) = delete;
    JobWorkers(JobWorkers&&) = delete;
    JobWorkers& operator=(JobWorkers&&) = delete;

    /// Runs `work` on its own thread. `cancellation` is the job's token, kept
    /// so shutdown can fire it. Finished threads are reaped on the next call.
    void run(harness::CancellationToken cancellation, std::function<void()> work);

    /// Threads not yet joined -- for a test to wait on, never for logic.
    [[nodiscard]] std::size_t active() const;

private:
    struct Worker {
        harness::CancellationToken cancellation;
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> done;
    };

    void reap_locked();

    mutable std::mutex mutex_;
    std::vector<Worker> workers_;
};

/// A fresh job id: `job_` plus twelve hex characters.
[[nodiscard]] std::string new_job_id();

}  // namespace apogee::httpserver
