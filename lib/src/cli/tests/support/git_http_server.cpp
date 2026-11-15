#include "support/git_http_server.h"

#include <charconv>
#include <chrono>
#include <cstddef>
#include <fstream>
#include <system_error>
#include <utility>

namespace apogee::testing {
namespace {

/// The server: the folder's files, or every request refused or redirected,
/// each path and the Host headers it named appended to the log, a tab between,
/// before anything is answered. It prints its port once it listens and stops
/// when its stdin closes.
constexpr std::string_view kServer = R"PY(
import functools, http.server, os, socketserver, subprocess, sys, threading
folder, log, mode = sys.argv[1], sys.argv[2], sys.argv[3]
port = 0

def backend(handler, method):
    # git http-backend as a CGI: the one path a smart fetch reaches.
    path, _, query = handler.path.partition("?")
    if handler.headers.get("Transfer-Encoding", "").lower() == "chunked":
        body = b""
        while True:
            size = int(handler.rfile.readline().strip() or b"0", 16)
            if size == 0:
                handler.rfile.readline()
                break
            body += handler.rfile.read(size)
            handler.rfile.readline()
    else:
        length = int(handler.headers.get("Content-Length") or 0)
        body = handler.rfile.read(length) if length else b""
    env = dict(os.environ)
    env.update({"GIT_PROJECT_ROOT": folder, "GIT_HTTP_EXPORT_ALL": "1",
                "PATH_INFO": path, "QUERY_STRING": query, "REQUEST_METHOD": method,
                "CONTENT_TYPE": handler.headers.get("Content-Type", ""),
                "CONTENT_LENGTH": str(len(body)), "REMOTE_ADDR": "127.0.0.1"})
    protocol = handler.headers.get("Git-Protocol")
    if protocol:
        env["GIT_PROTOCOL"] = protocol
    out = subprocess.run(["git", "http-backend"], input=body, env=env, capture_output=True).stdout
    head, _, rest = out.partition(b"\r\n\r\n")
    status, headers = 200, []
    for line in head.split(b"\r\n"):
        if not line:
            continue
        key, _, value = line.decode().partition(":")
        if key.lower() == "status":
            status = int(value.strip().split()[0])
        else:
            headers.append((key, value.strip()))
    handler.send_response(status)
    for key, value in headers:
        handler.send_header(key, value)
    handler.end_headers()
    handler.wfile.write(rest)

class Handler(http.server.SimpleHTTPRequestHandler):
    def note(self):
        # The path, then every Host header the request named, comma-joined
        # (57x): a second one a header setting slipped in is seen too.
        hosts = ",".join(self.headers.get_all("Host") or [])
        with open(log, "a") as out:
            out.write(self.path + "\t" + hosts + "\n")

    def do_GET(self):
        self.note()
        if mode == "smart":
            backend(self, "GET")
            return
        if mode == "unauthorized":
            self.send_response(401)
            self.send_header("WWW-Authenticate", 'Basic realm="x"')
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if mode == "redirect":
            self.send_response(301)
            self.send_header("Location", "http://127.0.0.1:%d/elsewhere%s" % (port, self.path))
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        super().do_GET()

    def do_POST(self):
        self.note()
        if mode == "smart":
            backend(self, "POST")
            return
        self.send_response(405)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def log_message(self, *args):
        pass

class Server(socketserver.ThreadingTCPServer):
    daemon_threads = True
    allow_reuse_address = True

server = Server(("127.0.0.1", 0), functools.partial(Handler, directory=folder))
port = server.server_address[1]
print(port, flush=True)
threading.Thread(target=server.serve_forever, daemon=True).start()
sys.stdin.read()
server.shutdown()
)PY";

std::string_view mode_word(GitHttpServer::Mode mode) {
    switch (mode) {
        case GitHttpServer::Mode::Unauthorized:
            return "unauthorized";
        case GitHttpServer::Mode::Redirect:
            return "redirect";
        case GitHttpServer::Mode::Smart:
            return "smart";
        case GitHttpServer::Mode::Serve:
            break;
    }
    return "serve";
}

/// One field of every request the log holds: its path, or its Host headers.
std::vector<std::string> logged(const std::filesystem::path& log, bool host) {
    std::vector<std::string> out;
    std::ifstream in{log, std::ios::binary};
    for (std::string line; std::getline(in, line);) {
        if (line.empty()) {
            continue;
        }
        const std::size_t tab = line.find('\t');
        if (!host) {
            out.push_back(line.substr(0, tab));
        } else {
            out.push_back(tab == std::string::npos ? std::string{} : line.substr(tab + 1));
        }
    }
    return out;
}

}  // namespace

GitHttpServer::GitHttpServer(const std::filesystem::path& folder, std::filesystem::path log,
                             Mode mode)
    : log_{std::move(log)} {
    const std::string python = platform::find_on_path("python3");
    if (python.empty() || !platform::supports_child_processes()) {
        return;
    }
    std::ofstream{log_, std::ios::binary | std::ios::trunc}.flush();
    platform::ChildCommand command;
    command.program = python;
    command.arguments = {"-I",
                         "-c",
                         std::string{kServer},
                         folder.string(),
                         log_.string(),
                         std::string{mode_word(mode)}};
    std::string error;
    child_ = platform::start_child(command, error);
    if (child_ == nullptr) {
        return;
    }
    std::string said;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{15};
    while (said.find('\n') == std::string::npos && std::chrono::steady_clock::now() < deadline) {
        std::string piece;
        const platform::ReadStatus status =
            child_->read_stdout(piece, std::chrono::milliseconds{200});
        if (status == platform::ReadStatus::Data) {
            said += piece;
        } else if (status != platform::ReadStatus::Timeout) {
            break;
        }
    }
    int port = 0;
    const std::string line = said.substr(0, said.find('\n'));
    const auto [end, failed] = std::from_chars(line.data(), line.data() + line.size(), port);
    if (failed == std::errc{} && end == line.data() + line.size() && port > 0) {
        port_ = port;
    }
}

GitHttpServer::~GitHttpServer() {
    if (child_ == nullptr) {
        return;
    }
    child_->close_stdin();
    if (!child_->wait_for_exit(std::chrono::seconds{5}).has_value()) {
        child_->terminate();
        (void)child_->wait_for_exit(std::chrono::seconds{5});
    }
}

std::string GitHttpServer::url(std::string_view path) const {
    return "http://127.0.0.1:" + std::to_string(port_) + "/" + std::string{path};
}

std::vector<std::string> GitHttpServer::requests() const {
    return logged(log_, false);
}

std::vector<std::string> GitHttpServer::hosts() const {
    return logged(log_, true);
}

}  // namespace apogee::testing
