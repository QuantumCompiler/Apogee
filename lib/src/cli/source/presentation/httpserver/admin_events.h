#pragma once

#include <chrono>
#include <string>

#include "events/bus.h"
#include "httpserver/http_types.h"

/// `GET /v1/admin/events`: one server-wide Server-Sent Events stream a client
/// subscribes to once.
///
/// Distinct from the per-request `apogee_events` meta-frames on a chat
/// completion: this carries the lifecycle catalog -- sessions, model loads,
/// agent runs, admin jobs -- from the process-wide bus, for as long as the
/// client stays connected. Each event is a **named** SSE frame
/// (`event: <type>` plus the JSON as `data:`), and a `: heartbeat` comment
/// keeps the connection alive through proxies and idle timeouts.
///
/// Bearer-only, like the rest of the plane: a browser's bare `EventSource`
/// cannot set a header, a fetch-style streaming reader can, and a token in a
/// query string would land in request logs.
namespace apogee::httpserver {

struct EventStreamOptions {
    std::chrono::milliseconds heartbeat{25000};
    /// How often the stream wakes to check for shutdown when nothing is
    /// happening. Bounds how long a Ctrl-C waits on an idle subscriber.
    std::chrono::milliseconds poll{1000};
};

/// One event as an SSE frame: `event: <type>\ndata: <json>\n\n`.
[[nodiscard]] std::string sse_named_event(const events::Event& event);

/// The streamed response. Subscribes when the body starts, unsubscribes when
/// the client leaves or the server stops.
[[nodiscard]] HttpResponse event_stream(events::Bus& bus, const EventStreamOptions& options);

}  // namespace apogee::httpserver
