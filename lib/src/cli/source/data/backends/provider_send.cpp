#include "backends/provider_send.h"

#include "contracts/errors.h"

namespace apogee::backends {

HttpResponse provider_send(HttpClient& client, const std::string& backend_name,
                           const HttpRequest& request, const BodySink& sink,
                           const harness::CancellationToken& cancellation) {
    try {
        return client.send(request, sink, cancellation);
    } catch (const HttpError& e) {
        // The transport's own words, under the entry that failed. A
        // HarnessError from the sink -- a cancellation, a stream's own
        // failure -- is not an HttpError and passes untouched.
        throw harness::ProviderError(backend_name, e.what());
    }
}

}  // namespace apogee::backends
