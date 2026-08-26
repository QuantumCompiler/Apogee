#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "harness/cancellation.h"

/// The shared HTTP client for every cloud backend.
///
/// Two layers, split so the retry logic can be tested without a network and
/// the transport can be swapped for a fake:
///
///   HttpTransport  -- performs one request. `CurlTransport` is the real one.
///   HttpClient     -- retry/backoff policy over a transport.
///
/// That split is the injectable seam this project insists on from the first
/// interface. Every test of the Anthropic backend runs against a scripted
/// transport: no network, no API key, no charges, and failure modes (a 529, a
/// truncated stream, a mid-frame disconnect) that are impossible to provoke
/// against a live endpoint.
namespace apogee::backends {

struct HttpHeader {
    std::string name;
    std::string value;
};

struct HttpRequest {
    std::string method = "POST";
    std::string url;
    std::vector<HttpHeader> headers;
    std::string body;

    /// Whole-request timeout. 0 disables it, which is what streaming wants: a
    /// long generation is not a hung connection, and a timeout that cannot
    /// tell them apart cuts off real answers.
    std::chrono::seconds timeout{0};

    /// Connect-phase timeout. Bounded even when `timeout` is not -- an
    /// unreachable host should fail in seconds, not hang forever.
    std::chrono::seconds connect_timeout{30};
};

/// Receives response body bytes as they arrive.
/// Return false to abort the transfer (cancellation, or a parse error).
using BodySink = std::function<bool(std::string_view)>;

struct HttpResponse {
    long status = 0;
    /// Accumulated body. Empty when a sink consumed it instead.
    std::string body;

    /// The server's `retry-after`, when it sent one in the delta-seconds form.
    /// A server that says when to come back knows better than a backoff curve.
    std::optional<std::chrono::milliseconds> retry_after;

    [[nodiscard]] bool ok() const noexcept {
        return status >= 200 && status < 300;
    }

    /// 429 and 5xx are worth retrying; 4xx (other than 429) is the caller's
    /// fault and will fail identically next time.
    [[nodiscard]] bool retryable() const noexcept {
        return status == 429 || (status >= 500 && status < 600);
    }
};

/// Raised when a request could not be completed at the transport level --
/// DNS failure, TLS failure, connection reset. Distinct from a non-2xx
/// response, which IS a completed request.
class HttpError : public std::runtime_error {
public:
    explicit HttpError(const std::string& message) : std::runtime_error(message) {}
};

/// Performs one HTTP request. The injectable seam.
class HttpTransport {
public:
    HttpTransport() = default;
    virtual ~HttpTransport() = default;
    HttpTransport(const HttpTransport&) = delete;
    HttpTransport& operator=(const HttpTransport&) = delete;
    HttpTransport(HttpTransport&&) = delete;
    HttpTransport& operator=(HttpTransport&&) = delete;

    /// Sends `request`. When `sink` is set AND the response is 2xx, body bytes
    /// are delivered to it as they arrive and `HttpResponse::body` is left
    /// empty; otherwise the body is accumulated into `HttpResponse::body`.
    ///
    /// **An error response is never streamed to the sink.** Two reasons, and
    /// the second is not obvious: an error body is not the content the sink
    /// was written to parse (feeding a JSON error to an SSE parser is at best
    /// noise), and -- more importantly -- delivering it would make the request
    /// look "already partly answered" to HttpClient, which then refuses to
    /// retry a perfectly retryable 429. Keeping error bodies out of the sink is
    /// what lets retry and streaming coexist.
    ///
    /// Throws HttpError on a transport failure. A non-2xx status is NOT an
    /// error here -- it is a completed request the caller must inspect.
    [[nodiscard]] virtual HttpResponse send(const HttpRequest& request, const BodySink& sink,
                                            const harness::CancellationToken& cancellation) = 0;
};

/// libcurl-backed transport.
///
/// TLS uses the platform's own certificate store (decided 2026-08-25): the
/// system trust store is what the user's administrator already manages, it
/// updates without an Apogee release, and bundling a CA list means shipping a
/// revocation problem. curl is built against Secure Transport on macOS, OpenSSL
/// on Linux, and Schannel on Windows -- one code path here, three native trust
/// stores under it.
class CurlTransport final : public HttpTransport {
public:
    CurlTransport();
    ~CurlTransport() override;
    CurlTransport(const CurlTransport&) = delete;
    CurlTransport& operator=(const CurlTransport&) = delete;
    CurlTransport(CurlTransport&&) = delete;
    CurlTransport& operator=(CurlTransport&&) = delete;

    [[nodiscard]] HttpResponse send(const HttpRequest& request, const BodySink& sink,
                                    const harness::CancellationToken& cancellation) override;

    /// Owns the curl_global_init/cleanup pairing for the process.
    ///
    /// Public only so the file-scope refcount helper in the .cpp can name it;
    /// it is an incomplete type here and nothing outside can construct one.
    struct GlobalInit;

private:
    std::shared_ptr<GlobalInit> global_;
};

/// How a failed request is retried.
///
/// At namespace scope rather than nested in HttpClient so it can be a default
/// argument there -- a nested type is not complete inside its own enclosing
/// class definition.
struct RetryPolicy {
    /// Total attempts including the first. 1 disables retrying.
    int max_attempts = 4;
    /// Doubles each attempt: 500ms, 1s, 2s.
    std::chrono::milliseconds base_delay{500};
    std::chrono::milliseconds max_delay{8000};

    /// Whether to honour a `retry-after` header when the server sends one.
    /// A server that tells you when to come back knows better than the
    /// backoff curve does.
    bool honour_retry_after = true;
};

/// Retry/backoff over a transport.
class HttpClient {
public:
    /// Waits for a backoff interval. Injected so retry tests run instantly:
    /// a test that actually slept through three backoffs would take seconds
    /// and nobody would run it.
    using Sleeper = std::function<void(std::chrono::milliseconds)>;

    explicit HttpClient(std::unique_ptr<HttpTransport> transport, RetryPolicy policy = {});

    /// Overrides the sleeper. Tests pass a recorder.
    void set_sleeper(Sleeper sleeper);

    /// Sends with retries. Returns the last response when every attempt was
    /// retryable; throws HttpError when the transport failed on every attempt.
    ///
    /// **A streaming request is retried only before any body byte is
    /// delivered.** Once the sink has seen output, a retry would replay a
    /// partial answer into it -- the caller would see the first half twice.
    [[nodiscard]] HttpResponse send(const HttpRequest& request, const BodySink& sink,
                                    const harness::CancellationToken& cancellation);

    [[nodiscard]] const RetryPolicy& policy() const noexcept {
        return policy_;
    }

private:
    [[nodiscard]] std::chrono::milliseconds delay_for(int attempt) const;

    std::unique_ptr<HttpTransport> transport_;
    RetryPolicy policy_;
    Sleeper sleeper_;
};

/// Parses a `retry-after` header value (delta-seconds only).
/// Returns nullopt for the HTTP-date form, which is rare in practice and not
/// worth a date parser here -- the backoff curve covers it.
[[nodiscard]] std::optional<std::chrono::milliseconds> parse_retry_after(std::string_view value);

}  // namespace apogee::backends
