#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "httpserver/handler.h"
#include "httpserver/http_types.h"

/// The route table, and nothing else.
///
/// A `Mux` maps a method and a path onto one `Handler` method. It holds no
/// socket and starts no thread: the conformance suite drives it directly with
/// `HttpRequest`s, which is how every route is tested without a port.
/// `serve.cpp` is the only thing that puts a listener in front of it.
namespace apogee::httpserver {

/// One row of the table, for documentation checks and tests.
struct RouteSpec {
    std::string method;
    /// A path, with `{id}` standing for one non-empty segment.
    std::string pattern;
};

class Mux {
public:
    explicit Mux(Handler& handler);

    /// Dispatches `request`: the matching handler's response, a 405 with an
    /// `Allow` header when the path exists but the method does not, or a
    /// 404 -- every one of them in the OpenAI error shape.
    [[nodiscard]] HttpResponse dispatch(const HttpRequest& request) const;

    /// Every route this server answers.
    [[nodiscard]] static std::vector<RouteSpec> routes();

private:
    Handler* handler_;
};

/// Matches `path` against `pattern`. Returns the captured `{id}` segment
/// (empty when the pattern has none), or nullopt when the path does not match.
[[nodiscard]] std::optional<std::string> match_route(std::string_view pattern,
                                                     std::string_view path);

}  // namespace apogee::httpserver
