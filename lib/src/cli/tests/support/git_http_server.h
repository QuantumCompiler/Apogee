#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "platform/child_process.h"

namespace apogee::testing {

/// A web server on 127.0.0.1 that the test owns (57x): python3's `http.server`
/// over a folder, every request's path and Host headers written to a log. Git's dumb HTTP
/// protocol fetches a bare repository in that folder once `git
/// update-server-info` has run there, so a review fetch over `http://` is
/// tested with no network beyond this machine's loopback.
///
/// It stops when the object goes. `running()` is false where it could not
/// start -- no python3, or a build with no child processes -- and a test
/// skips rather than passing on nothing.
class GitHttpServer {
public:
    enum class Mode : std::uint8_t {
        /// Serves the folder's files.
        Serve,
        /// Answers every request 401 with a Basic challenge, as a server
        /// asking for credentials does.
        Unauthorized,
        /// Answers every request 301 to the same path under `/elsewhere`, so
        /// a client that follows a redirect is seen asking for
        /// `/elsewhere/...`.
        Redirect,
        /// Git's smart HTTP protocol, `git http-backend` as a CGI over the
        /// folder (57x): protocol v2 and a packfile-URI offer are answered,
        /// so a fetch that asks for a packfile URI can be served and the host
        /// it names reached. The folder is `GIT_PROJECT_ROOT`; a bare
        /// repository under it is served with `update-server-info` not run.
        Smart,
    };

    GitHttpServer(const std::filesystem::path& folder, std::filesystem::path log,
                  Mode mode = Mode::Serve);
    ~GitHttpServer();

    GitHttpServer(const GitHttpServer&) = delete;
    GitHttpServer& operator=(const GitHttpServer&) = delete;
    GitHttpServer(GitHttpServer&&) = delete;
    GitHttpServer& operator=(GitHttpServer&&) = delete;

    [[nodiscard]] bool running() const noexcept {
        return port_ != 0;
    }

    /// The port it listens on, or 0.
    [[nodiscard]] int port() const noexcept {
        return port_;
    }

    /// `http://127.0.0.1:<port>/<path>`.
    [[nodiscard]] std::string url(std::string_view path) const;

    /// Every path asked for so far, in order, the query included.
    [[nodiscard]] std::vector<std::string> requests() const;

    /// The Host headers each request so far named, in the same order, a
    /// request's several comma-joined (empty for one that named none): the
    /// site a request asked for, which a repository's own `http.extraHeader`
    /// could change (57x).
    [[nodiscard]] std::vector<std::string> hosts() const;

private:
    std::unique_ptr<platform::ChildProcess> child_;
    std::filesystem::path log_;
    int port_ = 0;
};

}  // namespace apogee::testing
