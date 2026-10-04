#include "httpserver/admin_events.h"

#include <nlohmann/json.hpp>

#include <optional>

#include "httpserver/shutdown.h"

namespace apogee::httpserver {

std::string sse_named_event(const events::Event& event) {
    const nlohmann::json payload{{"type", event.type}, {"time", event.time}, {"data", event.data}};
    return "event: " + event.type + "\ndata: " + payload.dump() + "\n\n";
}

HttpResponse event_stream(events::Bus& bus, const EventStreamOptions& options) {
    HttpResponse response;
    response.status = 200;
    response.content_type = "text/event-stream";
    response.headers["Cache-Control"] = "no-cache";
    // Tells an nginx-style proxy not to buffer: a buffered event stream is a
    // stream a client sees all at once, later.
    response.headers["X-Accel-Buffering"] = "no";
    response.stream = [&bus, options](const WriteFn& write) {
        events::Subscription subscription = events::subscribe(bus);
        events::Subscriber& subscriber = subscription.subscriber();

        // Opens the stream -- and flushes the headers -- with a comment.
        if (!write(": apogee event stream open\n\n")) {
            return;
        }
        auto last_write = std::chrono::steady_clock::now();
        const std::chrono::milliseconds wait =
            options.poll < options.heartbeat ? options.poll : options.heartbeat;
        while (!subscriber.closed() && !shutdown_requested()) {
            const std::optional<events::Event> event = subscriber.wait_for(wait);
            if (event.has_value()) {
                if (!write(sse_named_event(*event))) {
                    return;  // the client left
                }
                last_write = std::chrono::steady_clock::now();
                continue;
            }
            if (std::chrono::steady_clock::now() - last_write >= options.heartbeat) {
                if (!write(": heartbeat\n\n")) {
                    return;
                }
                last_write = std::chrono::steady_clock::now();
            }
        }
    };
    return response;
}

}  // namespace apogee::httpserver
