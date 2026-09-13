#include "httpserver/jobs.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <iomanip>
#include <random>
#include <sstream>
#include <utility>

namespace apogee::httpserver {
namespace {

nlohmann::json with_identity(const JobRecord& record, nlohmann::json fields) {
    nlohmann::json out = std::move(fields);
    if (!out.is_object()) {
        out = nlohmann::json::object();
    }
    out["job_id"] = record.id;
    out["kind"] = record.kind;
    return out;
}

}  // namespace

std::string_view to_string(JobStatus status) noexcept {
    switch (status) {
        case JobStatus::Running:
            return "running";
        case JobStatus::Succeeded:
            return "succeeded";
        case JobStatus::Failed:
            return "failed";
        case JobStatus::Cancelled:
            return "cancelled";
    }
    return "running";
}

nlohmann::json job_json(const JobRecord& record) {
    nlohmann::json out{{"id", record.id},
                       {"kind", record.kind},
                       {"status", std::string{to_string(record.status)}},
                       {"started", record.started}};
    if (!record.message.empty()) {
        out["message"] = record.message;
    }
    if (!record.result.is_null() && !record.result.empty()) {
        out["result"] = record.result;
    }
    if (!record.error.empty()) {
        out["error"] = record.error;
    }
    if (!record.finished.empty()) {
        out["finished"] = record.finished;
    }
    return out;
}

std::string new_job_id() {
    static thread_local std::mt19937_64 engine{std::random_device{}()};
    std::ostringstream out;
    out << "job_" << std::hex << std::setw(12) << std::setfill('0')
        << (engine() & 0xFFFFFFFFFFFFULL);
    return out.str();
}

JobRegistry::JobRegistry(events::Bus& bus) : bus_{&bus} {}

JobRegistry::Started JobRegistry::start(std::string kind, nlohmann::json fields) {
    Entry entry;
    entry.record.id = new_job_id();
    entry.record.kind = std::move(kind);
    entry.record.status = JobStatus::Running;
    entry.record.started = events::utc_now();
    entry.cancellation = harness::CancellationToken::create();

    Started started{entry.record.id, entry.cancellation};
    const nlohmann::json data = with_identity(entry.record, std::move(fields));
    {
        const std::lock_guard<std::mutex> lock{mutex_};
        jobs_[started.id] = std::move(entry);
    }
    bus_->publish(events::Event{std::string{events::kJobStarted}, {}, data});
    return started;
}

void JobRegistry::progress(const std::string& id, std::string message, nlohmann::json fields) {
    nlohmann::json data;
    {
        const std::lock_guard<std::mutex> lock{mutex_};
        const auto it = jobs_.find(id);
        if (it == jobs_.end()) {
            return;
        }
        it->second.record.message = message;
        data = with_identity(it->second.record, std::move(fields));
    }
    data["message"] = std::move(message);
    bus_->publish(events::Event{std::string{events::kJobProgress}, {}, std::move(data)});
}

void JobRegistry::finish(const std::string& id, nlohmann::json result) {
    nlohmann::json data;
    {
        const std::lock_guard<std::mutex> lock{mutex_};
        const auto it = jobs_.find(id);
        // The rule: nothing overwrites a job that has left running. A worker
        // returning after a cancel reports success into the void.
        if (it == jobs_.end() || it->second.record.status != JobStatus::Running) {
            return;
        }
        it->second.record.status = JobStatus::Succeeded;
        it->second.record.result = result;
        it->second.record.message.clear();
        it->second.record.finished = events::utc_now();
        data = with_identity(it->second.record,
                             result.is_object() ? result : nlohmann::json::object());
    }
    data["ok"] = true;
    bus_->publish(events::Event{std::string{events::kJobCompleted}, {}, std::move(data)});
}

void JobRegistry::fail(const std::string& id, std::string error) {
    nlohmann::json data;
    {
        const std::lock_guard<std::mutex> lock{mutex_};
        const auto it = jobs_.find(id);
        // A cancelled job's dying worker does not turn "cancelled" into
        // "failed": the outcome the operator asked for is the outcome kept.
        if (it == jobs_.end() || it->second.record.status != JobStatus::Running) {
            return;
        }
        it->second.record.status = JobStatus::Failed;
        it->second.record.error = error;
        it->second.record.finished = events::utc_now();
        data = with_identity(it->second.record, nlohmann::json::object());
    }
    data["error"] = std::move(error);
    bus_->publish(events::Event{std::string{events::kJobFailed}, {}, std::move(data)});
}

std::optional<JobRecord> JobRegistry::cancel(const std::string& id) {
    JobRecord snapshot;
    harness::CancellationToken token;
    {
        const std::lock_guard<std::mutex> lock{mutex_};
        const auto it = jobs_.find(id);
        if (it == jobs_.end()) {
            return std::nullopt;
        }
        if (it->second.record.status != JobStatus::Running) {
            return it->second.record;  // already final: idempotent
        }
        it->second.record.status = JobStatus::Cancelled;
        it->second.record.finished = events::utc_now();
        snapshot = it->second.record;
        token = it->second.cancellation;
    }
    // Outside the lock: the worker may be mid-check on the same token.
    token.cancel();
    bus_->publish(events::Event{
        std::string{events::kJobCancelled}, {}, with_identity(snapshot, nlohmann::json::object())});
    return snapshot;
}

std::optional<JobRecord> JobRegistry::get(const std::string& id) const {
    const std::lock_guard<std::mutex> lock{mutex_};
    const auto it = jobs_.find(id);
    if (it == jobs_.end()) {
        return std::nullopt;
    }
    return it->second.record;
}

std::vector<JobRecord> JobRegistry::list() const {
    std::vector<JobRecord> out;
    {
        const std::lock_guard<std::mutex> lock{mutex_};
        out.reserve(jobs_.size());
        for (const auto& [id, entry] : jobs_) {
            out.push_back(entry.record);
        }
    }
    std::ranges::sort(out, [](const JobRecord& a, const JobRecord& b) {
        return a.started != b.started ? a.started > b.started : a.id > b.id;
    });
    return out;
}

JobWriter::JobWriter(JobRegistry& registry, std::string id)
    : registry_{&registry}, id_{std::move(id)} {}

void JobWriter::write(std::string_view bytes) {
    buffer_ += bytes;
    std::size_t newline = buffer_.find('\n');
    while (newline != std::string::npos) {
        std::string line = buffer_.substr(0, newline);
        buffer_.erase(0, newline + 1);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        if (!line.empty()) {
            registry_->progress(id_, std::move(line), nlohmann::json::object());
        }
        newline = buffer_.find('\n');
    }
}

void JobWriter::flush() {
    if (!buffer_.empty()) {
        registry_->progress(id_, buffer_, nlohmann::json::object());
        buffer_.clear();
    }
}

}  // namespace apogee::httpserver
