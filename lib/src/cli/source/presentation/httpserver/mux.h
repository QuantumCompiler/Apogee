#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "httpserver/admin.h"
#include "httpserver/handler.h"
#include "httpserver/http_types.h"

/// The route table, and the one gate in front of part of it.
///
/// A `Mux` maps a method and a path onto one handler method. It holds no
/// socket and starts no thread: the conformance suite drives it directly with
/// `HttpRequest`s, which is how every route is tested without a port.
/// `serve.cpp` is the only thing that puts a listener in front of it.
///
/// Two planes share the table. The public inference plane is open, for
/// OpenAI-client compatibility. The `/v1/admin/` prefix is the control plane:
/// mounted only when the mux is given an `AdminHandler` and a token, and
/// gated **before routing** -- an unauthenticated request under that prefix
/// is a `401` whether or not the path exists, so a probe learns nothing about
/// which routes there are. An authenticated one gets the ordinary `404` for a
/// missing path, which is what the parity test reads.
///
/// In front of both, on every route, the **request gate** (57n): who may
/// reach the server at all. A page the user opens can aim `fetch` at
/// `127.0.0.1` with a CORS-safelisted body, and after a DNS rebinding can
/// read the reply too; the socket's peer is loopback either way, so the gate
/// reads what the browser cannot hide -- `Host` and `Origin` -- and refuses
/// with a `403` before any route runs, telling the caller nothing about them.
namespace apogee::httpserver {

/// One row of the table, for documentation checks and tests.
struct RouteSpec {
    std::string method;
    /// A path, with `{id}` standing for one non-empty segment.
    std::string pattern;
    /// Whether the row sits behind the bearer gate.
    bool admin = false;
};

class Mux {
public:
    /// The public plane only.
    explicit Mux(Handler& handler);

    /// Both planes. `admin_token` is what a bearer must match.
    Mux(Handler& handler, AdminHandler& admin, std::string admin_token);

    /// Dispatches `request`: the request gate (`request_refusal`, a `403`),
    /// the bearer gate for the admin prefix, then the matching
    /// handler's response, a 405 with an `Allow` header when the path exists
    /// but the method does not, or a 404 -- every one of them in the OpenAI
    /// error shape.
    [[nodiscard]] HttpResponse dispatch(const HttpRequest& request) const;

    /// Every route this server can answer, both planes.
    [[nodiscard]] static std::vector<RouteSpec> routes();

    [[nodiscard]] bool admin_mounted() const noexcept {
        return admin_ != nullptr;
    }

    /// Whether the gate checks `Host` (57n): a loopback bind without
    /// `--allow-remote` (`host_check_applies`). `run_server` sets it from the
    /// bind before the first request; until then it is true, the stricter
    /// answer.
    void set_host_check(bool check) noexcept {
        host_check_ = check;
    }

    [[nodiscard]] bool host_check() const noexcept {
        return host_check_;
    }

private:
    Handler* handler_;
    AdminHandler* admin_ = nullptr;
    std::string admin_token_;
    bool host_check_ = true;
};

/// Why `request` is refused before any route runs (57n), or empty when it
/// may go on. Two rules, each about a header a browser always sets and a
/// page cannot:
///
/// - **`Host`, when `check_host`** -- a loopback bind without
///   `--allow-remote`: a request must be addressed to the server by a
///   loopback name -- `localhost`, `127.x.x.x` written as four numbers,
///   `[::1]` -- at any port. A rebinding page's requests carry the attacker's
///   own name. Under `--allow-remote`, on any bind, the operator's proxy and
///   access control own the name, which can be any: a reverse proxy on the
///   same host forwards the client's. A request with no `Host` is no
///   browser's and passes.
/// - **`Origin`, on every bind**: a present one passes when its host is a
///   loopback name or the request's own `Host` (ports aside); anything else
///   -- another site, `null` from a sandboxed frame or a file -- is refused.
///   A request with no `Origin` -- curl, the OpenAI SDKs, every non-browser
///   client -- is unaffected. A browser sends `Origin` on every POST, so a
///   cross-site `text/plain` POST is refused whatever its body's type.
///
/// Strictly names, never prefixes: `127.attacker.example` is the attacker's.
/// The message echoes the refused value, which the client set, mended to
/// UTF-8, and no other header.
[[nodiscard]] std::string request_refusal(const HttpRequest& request, bool check_host);

/// Matches `path` against `pattern`. Returns the captured `{id}` segment
/// (empty when the pattern has none), or nullopt when the path does not match.
[[nodiscard]] std::optional<std::string> match_route(std::string_view pattern,
                                                     std::string_view path);

/// Whether `path` is under the control plane's prefix.
[[nodiscard]] bool is_admin_path(std::string_view path) noexcept;

}  // namespace apogee::httpserver
