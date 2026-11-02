#include "support/temp_mode_watch.h"

#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

namespace apogee::testing {

TempModeWatch::TempModeWatch(std::filesystem::path directory)
    : directory_(std::move(directory)), thread_([this] { run(); }) {}

TempModeWatch::~TempModeWatch() {
    stop();
}

void TempModeWatch::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

void TempModeWatch::run() {
    namespace fs = std::filesystem;
    constexpr fs::perms kBeyondOwner = fs::perms::group_all | fs::perms::others_all;
    // Each write's temporary file has a name of its own (a random suffix), so
    // a name is one write: the loop stats one live file many times, and only
    // its first catch counts toward `seen_`, its first wide one toward `wider_`.
    std::unordered_set<std::string> caught;
    std::unordered_set<std::string> caught_wider;
    while (running_) {
        std::error_code code;
        fs::directory_iterator it{directory_, code};
        for (; !code && it != fs::directory_iterator{}; it.increment(code)) {
            std::string name = it->path().filename().string();
            if (name.find(".tmp.") == std::string::npos) {
                continue;
            }
            // Gone between the listing and the stat: the rename won, and
            // nothing was caught.
            const fs::file_status status = fs::status(it->path(), code);
            if (code || !fs::exists(status)) {
                code.clear();
                continue;
            }
            if ((status.permissions() & kBeyondOwner) != fs::perms::none &&
                caught_wider.insert(name).second) {
                ++wider_;
            }
            if (caught.insert(std::move(name)).second) {
                ++seen_;
            }
        }
    }
}

UmaskGuard::UmaskGuard() {
#if !defined(_WIN32)
    previous_ = ::umask(022);
#endif
}

UmaskGuard::~UmaskGuard() {
#if !defined(_WIN32)
    (void)::umask(static_cast<mode_t>(previous_));
#endif
}

}  // namespace apogee::testing
