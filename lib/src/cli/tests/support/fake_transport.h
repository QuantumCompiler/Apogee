#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "backends/http_client.h"

namespace apogee::testing {

/// A scripted HttpTransport — the seam that makes every cloud-backend test
/// hermetic.
///
/// No network, no API key, no charges. More importantly it can produce the
/// failure modes a live endpoint will not give you on demand: a 429 followed by
/// a 200, a body delivered one byte at a time, a stream that stops mid-frame.
/// Those are exactly the cases that break streaming clients in production.
class FakeTransport final : public backends::HttpTransport {
public:
    struct Reply {
        long status = 200;
        std::string body;
        /// Deliver the body in chunks of this size. 0 sends it whole.
        ///
        /// The parameter that matters: an SSE frame split across chunks is the
        /// single most common streaming bug, and it only reproduces when the
        /// chunk boundary lands mid-frame.
        std::size_t chunk_size = 0;
        /// Simulates a transport-level failure (DNS, TLS, reset) instead of a
        /// response.
        bool transport_error = false;
        std::string error_message = "simulated transport failure";
        /// Sent as the `retry-after` header value.
        std::optional<std::chrono::milliseconds> retry_after;
        /// Deliver this many bytes to the sink, then raise a transport error.
        ///
        /// The mid-stream connection drop: the caller has already seen part of
        /// an answer when the socket dies. It is the case the no-retry-after-
        /// delivery guard exists for, and it cannot be provoked on demand
        /// against a live endpoint.
        std::optional<std::size_t> fail_after_bytes;
    };

    /// Replies are served in order; the last one repeats once exhausted.
    explicit FakeTransport(std::vector<Reply> replies);

    /// Convenience for a single 200 reply.
    static std::unique_ptr<FakeTransport> ok(std::string body, std::size_t chunk_size = 0);

    [[nodiscard]] backends::HttpResponse send(
        const backends::HttpRequest& request, const backends::BodySink& sink,
        const harness::CancellationToken& cancellation) override;

    /// Every request received, in order — the assertion point for headers,
    /// URLs, and request bodies.
    [[nodiscard]] const std::vector<backends::HttpRequest>& requests() const noexcept {
        return requests_;
    }

    [[nodiscard]] std::size_t attempts() const noexcept {
        return requests_.size();
    }

private:
    std::vector<Reply> replies_;
    std::vector<backends::HttpRequest> requests_;
    std::size_t next_ = 0;
};

/// Records backoff delays instead of sleeping, so retry tests run instantly.
struct SleepRecorder {
    std::vector<std::chrono::milliseconds> delays;

    [[nodiscard]] backends::HttpClient::Sleeper sleeper() {
        return [this](std::chrono::milliseconds delay) { delays.push_back(delay); };
    }
};

}  // namespace apogee::testing
