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

    /// Dispatches `request`: the gate for the admin prefix, then the matching
    /// handler's response, a 405 with an `Allow` header when the path exists
    /// but the method does not, or a 404 -- every one of them in the OpenAI
    /// error shape.
    [[nodiscard]] HttpResponse dispatch(const HttpRequest& request) const;

    /// Every route this server can answer, both planes.
    [[nodiscard]] static std::vector<RouteSpec> routes();

    [[nodiscard]] bool admin_mounted() const noexcept {
        return admin_ != nullptr;
    }

private:
    Handler* handler_;
    AdminHandler* admin_ = nullptr;
    std::string admin_token_;
};

/// Matches `path` against `pattern`. Returns the captured `{id}` segment
/// (empty when the pattern has none), or nullopt when the path does not match.
[[nodiscard]] std::optional<std::string> match_route(std::string_view pattern,
                                                     std::string_view path);

/// Whether `path` is under the control plane's prefix.
[[nodiscard]] bool is_admin_path(std::string_view path) noexcept;

}  // namespace apogee::httpserver
