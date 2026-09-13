#pragma once

#include <nlohmann/json_fwd.hpp>

#include <functional>
#include <map>
#include <string>
#include <string_view>

/// The transport-neutral request and response every handler speaks.
///
/// Everything under `httpserver/` except `serve.cpp` works in these terms, and
/// that split is load-bearing twice over. It is what makes the server
/// **testable without a socket**: the conformance suite hands a handler an
/// `HttpRequest` and reads the `HttpResponse` back, so a streaming turn can be
/// asserted frame by frame with no port, no thread pool and no timing. And it
/// is what keeps `listen()`/`accept()` in exactly one translation unit, which
/// the no-listen symbol check allow-lists by name -- every other file in this
/// package is as socket-free as the rest of the library.
namespace apogee::httpserver {

struct HttpRequest {
    /// Upper-case, as received: `GET`, `POST`, `DELETE`.
    std::string method;
    /// The path alone; the query string is parsed into `query`.
    std::string path;
    std::map<std::string, std::string> query;
    /// Header names are lower-cased on the way in, so lookups need not guess.
    std::map<std::string, std::string> headers;
    std::string body;
    std::string remote_address;

    [[nodiscard]] bool has_query(std::string_view key) const;
    /// The query value, or empty when absent.
    [[nodiscard]] std::string query_value(std::string_view key) const;
    /// A header by case-insensitive name, or empty when absent.
    [[nodiscard]] std::string header(std::string_view name) const;
};

/// Writes one piece of a streamed body. Returns false once the client is gone,
/// which is the handler's cue to stop generating.
using WriteFn = std::function<bool(std::string_view)>;

/// Produces a streamed body by calling `write` as pieces become available, and
/// returns when the stream is complete. The transport runs it on whatever
/// thread serves the connection; a test runs it inline and collects.
using StreamBody = std::function<void(const WriteFn& write)>;

struct HttpResponse {
    int status = 200;
    std::string content_type = "application/json";
    std::map<std::string, std::string> headers;
    std::string body;
    /// When set, the body is produced by streaming and `body` is ignored.
    StreamBody stream;

    [[nodiscard]] bool streamed() const noexcept {
        return static_cast<bool>(stream);
    }
};

/// The `type` a served error carries. OpenAI's vocabulary where it has a word,
/// Apogee's where it does not.
inline constexpr std::string_view kInvalidRequestError = "invalid_request_error";
inline constexpr std::string_view kNotFoundError = "not_found_error";
inline constexpr std::string_view kSessionNotFound = "session_not_found";
inline constexpr std::string_view kBackendUnavailable = "backend_unavailable";
inline constexpr std::string_view kServerError = "server_error";

[[nodiscard]] HttpResponse json_response(int status, const nlohmann::json& body);

/// The OpenAI error envelope: `{"error":{"message":…,"type":…}}`. Every error
/// this server returns -- a bad body, an unknown model, a vanished session, a
/// provider failure -- takes this shape, because a stock client library
/// already knows how to read it and turn it into a typed exception.
[[nodiscard]] nlohmann::json error_body(std::string_view message, std::string_view type);

[[nodiscard]] HttpResponse error_response(int status, std::string_view message,
                                          std::string_view type = kInvalidRequestError);

}  // namespace apogee::httpserver
