#include "support/fake_transport.h"

#include <algorithm>
#include <utility>

namespace apogee::testing {

FakeTransport::FakeTransport(std::vector<Reply> replies) : replies_{std::move(replies)} {
    if (replies_.empty()) {
        replies_.push_back(Reply{});
    }
}

std::unique_ptr<FakeTransport> FakeTransport::ok(std::string body, std::size_t chunk_size) {
    Reply reply;
    reply.body = std::move(body);
    reply.chunk_size = chunk_size;
    return std::make_unique<FakeTransport>(std::vector<Reply>{std::move(reply)});
}

backends::HttpResponse FakeTransport::send(const backends::HttpRequest& request,
                                           const backends::BodySink& sink,
                                           const harness::CancellationToken& cancellation) {
    cancellation.throw_if_cancelled();
    requests_.push_back(request);

    const Reply& reply = replies_[std::min(next_, replies_.size() - 1)];
    ++next_;

    if (reply.transport_error) {
        throw backends::HttpError(reply.error_message);
    }

    backends::HttpResponse response;
    response.status = reply.status;
    response.retry_after = reply.retry_after;
    response.location = reply.location;

    // The real transport's cap: the transfer stops once the body passes
    // `max_body_bytes`, whatever the status, and says so rather than failing.
    std::string_view body = reply.body;
    if (request.max_body_bytes != 0 && body.size() > request.max_body_bytes) {
        body = body.substr(0, request.max_body_bytes);
        response.body_limit_exceeded = true;
    }

    // Same contract as the real transport: an error response is accumulated,
    // never streamed. Otherwise a fake would let a bug through that production
    // would hit -- HttpClient treating a 429's body as "already delivered" and
    // refusing to retry.
    if (!sink || !response.ok()) {
        response.body = std::string{body};
        return response;
    }

    const std::size_t chunk =
        std::max<std::size_t>(reply.chunk_size == 0 ? body.size() : reply.chunk_size, 1);
    std::size_t delivered = 0;

    for (std::size_t offset = 0; offset < body.size(); offset += chunk) {
        cancellation.throw_if_cancelled();
        const std::string_view slice = body.substr(offset, chunk);

        if (reply.fail_after_bytes.has_value() && delivered >= *reply.fail_after_bytes) {
            throw backends::HttpError("connection dropped mid-stream");
        }
        if (!sink(slice)) {
            break;
        }
        delivered += slice.size();
    }
    if (reply.fail_after_bytes.has_value() && delivered >= *reply.fail_after_bytes) {
        throw backends::HttpError("connection dropped mid-stream");
    }
    return response;
}

}  // namespace apogee::testing
