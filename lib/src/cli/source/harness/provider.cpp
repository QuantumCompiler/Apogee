#include "harness/provider.h"

namespace apogee::harness {

ChatResponse LLMProvider::complete(const ChatRequest& request,
                                   const CancellationToken& cancellation) {
    // The default is exactly "chat with whatever you were given". A provider
    // with a genuinely cheaper single-turn endpoint overrides this; most do
    // not, and duplicating the call in each of them would only create places
    // for the two paths to drift.
    return chat(request, cancellation);
}

}  // namespace apogee::harness
