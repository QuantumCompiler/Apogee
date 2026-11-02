#pragma once

#include <string>
#include <string_view>

#include "contracts/cancellation.h"
#include "transport/http_client.h"

/// The one door from a direct-API provider (`anthropic`, `openai`, `google`)
/// to the network, and the words its failures share (57c).
///
/// A transport failure -- DNS, TLS, a connection refused, or one dropped
/// mid-stream -- leaves `HttpClient` as `HttpError`, a plain `runtime_error`
/// no surface handles: it ended `chat`'s whole process (exit 1, no `error`
/// event, the next line never served) and reached `serve` as a 500. Turned
/// into the entry's `ProviderError` here, once, it is what every surface
/// already makes a failed turn of -- the `error` event, exit 2 from
/// `complete`, 502 `backend_error` from `serve` -- with nothing above the
/// providers taught about the transport.
namespace apogee::backends {

/// Sends `request` through `client` for the backend entry `backend_name`.
///
/// Every outcome `HttpClient::send` has, unchanged -- a response of any
/// status, a cancellation -- except a transport failure, which is thrown as
/// `ProviderError(backend_name, <curl's text>)`: `anthropic: request failed:
/// Couldn't connect to server`. curl's text never carries the request, so no
/// key can reach the message.
[[nodiscard]] HttpResponse provider_send(HttpClient& client, const std::string& backend_name,
                                         const HttpRequest& request, const BodySink& sink,
                                         const harness::CancellationToken& cancellation);

/// What a stream that ended before its terminal event fails with: the body
/// stopped cleanly (an HTTP/2 END_STREAM, a proxy closing a close-delimited
/// response) with the answer unfinished. Never kept as an answer -- the loop
/// saves whatever a provider returns.
inline constexpr std::string_view kStreamCutMessage =
    "the response stream ended before the answer finished";

}  // namespace apogee::backends
