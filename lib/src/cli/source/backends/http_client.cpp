#include "backends/http_client.h"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <mutex>
#include <thread>
#include <utility>

#include "harness/errors.h"

namespace apogee::backends {
namespace {

/// Collects body bytes when no sink was given.
struct WriteContext {
    const BodySink* sink = nullptr;
    std::string* accumulator = nullptr;
    const harness::CancellationToken* cancellation = nullptr;
    /// The handle, so the status can be read on the first body byte -- curl
    /// runs the header callback first, so it is known by then.
    CURL* handle = nullptr;
    bool aborted_by_sink = false;
    bool cancelled = false;
    /// Resolved once, on the first write: whether this response is a success
    /// and therefore eligible to be streamed to the sink.
    std::optional<bool> streamable;
};

std::size_t write_callback(char* data, std::size_t size, std::size_t nmemb, void* user_data) {
    auto* context = static_cast<WriteContext*>(user_data);
    const std::size_t length = size * nmemb;
    const std::string_view chunk{data, length};

    if (context->cancellation != nullptr && context->cancellation->stop_requested()) {
        context->cancelled = true;
        return 0;  // any short count aborts the transfer
    }

    if (!context->streamable.has_value()) {
        long status = 0;
        if (context->handle != nullptr) {
            curl_easy_getinfo(context->handle, CURLINFO_RESPONSE_CODE, &status);
        }
        context->streamable = status >= 200 && status < 300;
    }

    if (*context->streamable && context->sink != nullptr && *context->sink) {
        if (!(*context->sink)(chunk)) {
            context->aborted_by_sink = true;
            return 0;
        }
    } else if (context->accumulator != nullptr) {
        // Error bodies land here, so HttpClient still sees them as un-delivered
        // and a retryable status stays retryable.
        context->accumulator->append(chunk);
    }
    return length;
}

struct HeaderContext {
    std::string retry_after;
};

std::size_t header_callback(char* data, std::size_t size, std::size_t nmemb, void* user_data) {
    auto* context = static_cast<HeaderContext*>(user_data);
    const std::size_t length = size * nmemb;
    const std::string_view line{data, length};

    constexpr std::string_view kRetryAfterField = "retry-after:";
    if (line.size() > kRetryAfterField.size()) {
        std::string lowered;
        lowered.reserve(kRetryAfterField.size());
        for (std::size_t i = 0; i < kRetryAfterField.size(); ++i) {
            lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(line[i]))));
        }
        if (lowered == kRetryAfterField) {
            std::string_view value = line.substr(kRetryAfterField.size());
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
                value.remove_prefix(1);
            }
            while (!value.empty() &&
                   (value.back() == '\r' || value.back() == '\n' || value.back() == ' ')) {
                value.remove_suffix(1);
            }
            context->retry_after = std::string{value};
        }
    }
    return length;
}

/// Fires every ~100ms during a transfer so cancellation is noticed even while
/// the server is silent -- a stalled stream must still respond to Ctrl-C.
int progress_callback(void* user_data, curl_off_t /*dltotal*/, curl_off_t /*dlnow*/,
                      curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
    const auto* cancellation = static_cast<const harness::CancellationToken*>(user_data);
    return (cancellation != nullptr && cancellation->stop_requested()) ? 1 : 0;
}

/// Deleters for curl's C handles. The Code Style rule: a C API handle is
/// wrapped in a unique_ptr at the boundary and the raw handle never escapes.
struct CurlEasyDeleter {
    void operator()(CURL* handle) const noexcept {
        curl_easy_cleanup(handle);
    }
};

struct CurlSlistDeleter {
    void operator()(curl_slist* list) const noexcept {
        curl_slist_free_all(list);
    }
};

using CurlEasy = std::unique_ptr<CURL, CurlEasyDeleter>;
using CurlSlist = std::unique_ptr<curl_slist, CurlSlistDeleter>;

}  // namespace

// ---------------------------------------------------------------------------
// CurlTransport
// ---------------------------------------------------------------------------

struct CurlTransport::GlobalInit {
    GlobalInit() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    ~GlobalInit() {
        curl_global_cleanup();
    }

    GlobalInit(const GlobalInit&) = delete;
    GlobalInit& operator=(const GlobalInit&) = delete;
    GlobalInit(GlobalInit&&) = delete;
    GlobalInit& operator=(GlobalInit&&) = delete;
};

namespace {

/// One global init for the process, kept alive by every live transport.
///
/// curl_global_init is not thread-safe and must not be re-entered, so it is
/// guarded and shared rather than run per transport.
std::shared_ptr<CurlTransport::GlobalInit> acquire_global_init();

std::mutex g_global_mutex;
std::weak_ptr<CurlTransport::GlobalInit> g_global_init;

std::shared_ptr<CurlTransport::GlobalInit> acquire_global_init() {
    const std::scoped_lock lock{g_global_mutex};
    std::shared_ptr<CurlTransport::GlobalInit> existing = g_global_init.lock();
    if (!existing) {
        existing = std::make_shared<CurlTransport::GlobalInit>();
        g_global_init = existing;
    }
    return existing;
}

}  // namespace

CurlTransport::CurlTransport() : global_{acquire_global_init()} {}

CurlTransport::~CurlTransport() = default;

HttpResponse CurlTransport::send(const HttpRequest& request, const BodySink& sink,
                                 const harness::CancellationToken& cancellation) {
    cancellation.throw_if_cancelled();

    const CurlEasy handle{curl_easy_init()};
    if (!handle) {
        throw HttpError("could not initialise the HTTP client");
    }

    HttpResponse response;
    WriteContext write;
    write.sink = &sink;
    write.accumulator = &response.body;
    write.cancellation = &cancellation;
    write.handle = handle.get();
    HeaderContext headers;

    CurlSlist header_list;
    for (const HttpHeader& header : request.headers) {
        const std::string line = header.name + ": " + header.value;
        curl_slist* appended = curl_slist_append(header_list.get(), line.c_str());
        if (appended == nullptr) {
            throw HttpError("could not build request headers");
        }
        // curl_slist_append takes ownership of the existing list and returns
        // the new head, so the old pointer must be released without freeing.
        [[maybe_unused]] curl_slist* const released = header_list.release();
        header_list.reset(appended);
    }

    curl_easy_setopt(handle.get(), CURLOPT_URL, request.url.c_str());
    curl_easy_setopt(handle.get(), CURLOPT_CUSTOMREQUEST, request.method.c_str());
    if (!request.body.empty()) {
        curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDS, request.body.c_str());
        curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDSIZE,
                         static_cast<long>(request.body.size()));
    }
    if (header_list) {
        curl_easy_setopt(handle.get(), CURLOPT_HTTPHEADER, header_list.get());
    }
    curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &write);
    curl_easy_setopt(handle.get(), CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(handle.get(), CURLOPT_HEADERDATA, &headers);
    curl_easy_setopt(handle.get(), CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(handle.get(), CURLOPT_XFERINFOFUNCTION, progress_callback);
    curl_easy_setopt(handle.get(), CURLOPT_XFERINFODATA, &cancellation);
    curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_CONNECTTIMEOUT,
                     static_cast<long>(request.connect_timeout.count()));
    curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT, static_cast<long>(request.timeout.count()));
    // Platform trust store; no bundled CA list. See the header.
    curl_easy_setopt(handle.get(), CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(handle.get(), CURLOPT_ACCEPT_ENCODING, "");

    const CURLcode code = curl_easy_perform(handle.get());

    if (write.cancelled || cancellation.stop_requested()) {
        throw harness::CancelledError();
    }
    if (code != CURLE_OK && !write.aborted_by_sink) {
        // curl_easy_strerror never contains request data, so this cannot leak
        // an API key -- headers are not echoed into it.
        throw HttpError(std::string{"request failed: "} + curl_easy_strerror(code));
    }

    long status = 0;
    curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status);
    response.status = status;
    if (!headers.retry_after.empty()) {
        response.retry_after = parse_retry_after(headers.retry_after);
    }
    return response;
}

// ---------------------------------------------------------------------------
// HttpClient
// ---------------------------------------------------------------------------

HttpClient::HttpClient(std::unique_ptr<HttpTransport> transport, RetryPolicy policy)
    : transport_{std::move(transport)},
      policy_{policy},
      sleeper_{[](std::chrono::milliseconds d) { std::this_thread::sleep_for(d); }} {}

void HttpClient::set_sleeper(Sleeper sleeper) {
    sleeper_ = std::move(sleeper);
}

std::chrono::milliseconds HttpClient::delay_for(int attempt) const {
    // Exponential: base, 2*base, 4*base, capped.
    auto delay = policy_.base_delay;
    for (int i = 1; i < attempt; ++i) {
        delay *= 2;
        if (delay > policy_.max_delay) {
            return policy_.max_delay;
        }
    }
    return std::min(delay, policy_.max_delay);
}

HttpResponse HttpClient::send(const HttpRequest& request, const BodySink& sink,
                              const harness::CancellationToken& cancellation) {
    const int attempts = std::max(1, policy_.max_attempts);
    std::string last_transport_error;
    bool delivered_any = false;

    // A sink that records whether the caller has seen output. Once it has,
    // retrying would replay a partial answer -- the caller would receive the
    // first half of the response twice.
    const BodySink guarded = sink ? BodySink{[&sink, &delivered_any](std::string_view chunk) {
        delivered_any = true;
        return sink(chunk);
    }}
                                  : BodySink{};

    for (int attempt = 1; attempt <= attempts; ++attempt) {
        cancellation.throw_if_cancelled();

        std::chrono::milliseconds wait = delay_for(attempt);

        try {
            HttpResponse response = transport_->send(request, guarded, cancellation);
            if (!response.retryable() || attempt == attempts || delivered_any) {
                return response;
            }
            // A 429 usually carries retry-after. Obeying it is both politer and
            // more effective than guessing: come back too early and the next
            // attempt is refused too, spending an attempt for nothing.
            if (policy_.honour_retry_after && response.retry_after.has_value()) {
                wait = std::min(*response.retry_after, policy_.max_delay);
            }
        } catch (const harness::CancelledError&) {
            throw;
        } catch (const HttpError& e) {
            last_transport_error = e.what();
            if (attempt == attempts || delivered_any) {
                throw;
            }
        }

        sleeper_(wait);
    }

    // Unreachable: the loop either returns or throws on its final attempt.
    throw HttpError(last_transport_error.empty() ? "request failed" : last_transport_error);
}

std::optional<std::chrono::milliseconds> parse_retry_after(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }
    if (value.empty()) {
        return std::nullopt;
    }
    long long seconds = 0;
    const auto* begin = value.data();
    const auto* end = begin + value.size();
    const auto result = std::from_chars(begin, end, seconds);
    if (result.ec != std::errc{} || seconds < 0) {
        // The HTTP-date form. Rare in practice, and not worth a date parser
        // here -- the backoff curve covers it.
        return std::nullopt;
    }
    return std::chrono::milliseconds{seconds * 1000};
}

}  // namespace apogee::backends
