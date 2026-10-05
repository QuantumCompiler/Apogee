#include "platform/pid_lock.h"

#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

#include "platform/platform.h"

namespace apogee::platform {
namespace {

/// The file's bytes, or nullopt when it cannot be read.
[[nodiscard]] std::optional<std::string> read_text(const std::filesystem::path& path) {
    const std::ifstream in{path, std::ios::binary};
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

[[nodiscard]] PidLock::Holder parse_holder(const std::string& text) {
    PidLock::Holder holder;
    std::istringstream lines{text};
    std::string first;
    if (std::getline(lines, first)) {
        try {
            std::size_t used = 0;
            const long pid = std::stol(first, &used);
            holder.pid = pid > 0 ? pid : 0;
        } catch (const std::exception&) {
            holder.pid = 0;
        }
    }
    std::string second;
    if (std::getline(lines, second)) {
        while (!second.empty() && (second.back() == '\r' || second.back() == ' ')) {
            second.pop_back();
        }
        holder.label = second;
    }
    holder.running = holder.pid > 0 && process_running(holder.pid);
    return holder;
}

}  // namespace

PidLock::PidLock(std::filesystem::path path) : path_{std::move(path)} {}

PidLock::~PidLock() {
    release();
}

PidLock::PidLock(PidLock&& other) noexcept : path_{std::move(other.path_)} {
    other.path_.clear();
}

PidLock& PidLock::operator=(PidLock&& other) noexcept {
    if (this != &other) {
        release();
        path_ = std::move(other.path_);
        other.path_.clear();
    }
    return *this;
}

std::optional<PidLock> PidLock::acquire(const std::filesystem::path& path, std::string_view label,
                                        bool take_stale, Refusal& refusal) {
    refusal = Refusal{};
    std::error_code code;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), code);
        if (code) {
            refusal.error =
                "could not create " + path.parent_path().string() + ": " + code.message();
            return std::nullopt;
        }
    }
    std::string content = std::to_string(current_process_id()) + "\n";
    if (!label.empty()) {
        content += std::string{label} + "\n";
    }
    // Two attempts at most: the second only after a stale holder's file was
    // removed, so a lock that keeps reappearing is someone else's to keep.
    for (int attempt = 0; attempt < 2; ++attempt) {
        bool exists = false;
        if (create_exclusive_file(path, content, exists)) {
            return PidLock{path};
        }
        if (!exists) {
            refusal.error = "could not create the lock " + path.string();
            return std::nullopt;
        }
        const std::optional<std::string> text = read_text(path);
        if (!text.has_value()) {
            // Removed between the create and the read: try once more.
            continue;
        }
        const Holder found = parse_holder(*text);
        refusal.holder = found;
        // A file with no readable id is never taken over: nothing says whose
        // it is, so nothing says it is stale.
        if (!take_stale || found.running || found.pid == 0 || attempt > 0) {
            return std::nullopt;
        }
        // Removed only while it still says what was read: a taker that won the
        // race in between has rewritten it with its own id.
        if (read_text(path) != text) {
            return std::nullopt;
        }
        std::filesystem::remove(path, code);
        refusal = Refusal{};
    }
    if (!refusal.holder.has_value() && refusal.error.empty()) {
        refusal.error = "could not take the lock " + path.string();
    }
    return std::nullopt;
}

std::optional<PidLock::Holder> PidLock::holder(const std::filesystem::path& path) {
    const std::optional<std::string> text = read_text(path);
    if (!text.has_value()) {
        return std::nullopt;
    }
    return parse_holder(*text);
}

void PidLock::release() noexcept {
    if (path_.empty()) {
        return;
    }
    std::error_code code;
    std::filesystem::remove(path_, code);
    path_.clear();
}

}  // namespace apogee::platform
