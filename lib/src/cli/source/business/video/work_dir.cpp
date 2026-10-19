#include "video/work_dir.h"

#include <random>
#include <string_view>
#include <system_error>
#include <utility>

#include "platform/platform.h"

namespace apogee::video {
namespace {

constexpr std::string_view kRunPrefix = "run-";

/// The process that owns `dir` (`run-<pid>-<random>`), or 0 when its name
/// says none.
[[nodiscard]] long owner_of(const std::filesystem::path& dir) {
    const std::string name = dir.filename().string();
    if (!name.starts_with(kRunPrefix)) {
        return 0;
    }
    long pid = 0;
    for (std::size_t at = kRunPrefix.size(); at < name.size() && name[at] != '-'; ++at) {
        if (name[at] < '0' || name[at] > '9') {
            return 0;
        }
        pid = (pid * 10) + (name[at] - '0');
    }
    return pid;
}

}  // namespace

void sweep_abandoned(const std::filesystem::path& root) {
    std::error_code code;
    for (const auto& entry : std::filesystem::directory_iterator(root, code)) {
        const long pid = owner_of(entry.path());
        if (pid > 0 && entry.is_directory(code) && !platform::process_running(pid)) {
            std::filesystem::remove_all(entry.path(), code);
        }
    }
}

std::filesystem::path make_work_dir(const std::filesystem::path& root, std::string& error) {
    std::error_code code;
    std::filesystem::create_directories(root, code);
    std::random_device device;
    for (int attempt = 0; attempt < 16; ++attempt) {
        const std::filesystem::path dir =
            root / (std::string{kRunPrefix} + std::to_string(platform::current_process_id()) + "-" +
                    std::to_string(device()));
        if (std::filesystem::create_directory(dir, code) && !code) {
            return dir;
        }
    }
    error = "could not make a working directory under " + root.string() +
            (code ? ": " + code.message() : std::string{});
    return {};
}

WorkDir::WorkDir(std::filesystem::path dir) : dir_{std::move(dir)} {}

WorkDir::~WorkDir() {
    if (!dir_.empty()) {
        std::error_code code;
        std::filesystem::remove_all(dir_, code);
    }
}

}  // namespace apogee::video
