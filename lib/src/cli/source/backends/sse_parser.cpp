#include "backends/sse_parser.h"

#include <utility>

namespace apogee::backends {
namespace {

/// Strips one optional leading space after the colon, per the SSE spec:
/// `data: x` and `data:x` both carry "x".
std::string_view strip_leading_space(std::string_view value) {
    if (!value.empty() && value.front() == ' ') {
        value.remove_prefix(1);
    }
    return value;
}

}  // namespace

SseParser::SseParser(SseEventSink sink) : sink_{std::move(sink)} {}

bool SseParser::feed(std::string_view bytes) {
    if (stopped_) {
        return false;
    }
    carry_.append(bytes);

    std::size_t start = 0;
    while (true) {
        const std::size_t newline = carry_.find('\n', start);
        if (newline == std::string::npos) {
            break;
        }
        std::string_view line{carry_.data() + start, newline - start};
        // Tolerate CRLF as well as LF: the spec allows both, and a proxy may
        // rewrite one into the other.
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        start = newline + 1;

        if (!handle_line(line)) {
            carry_.erase(0, start);
            return false;
        }
    }
    // Whatever is left is a partial line -- keep it for the next chunk. This
    // single line is what makes a mid-frame chunk boundary a non-event.
    carry_.erase(0, start);
    return true;
}

bool SseParser::handle_line(std::string_view line) {
    // A blank line terminates the current event.
    if (line.empty()) {
        return dispatch();
    }
    // A line starting with ':' is a comment -- keep-alive pings arrive this
    // way and must not be mistaken for data.
    if (line.front() == ':') {
        return true;
    }

    const std::size_t colon = line.find(':');
    const std::string_view field = colon == std::string_view::npos ? line : line.substr(0, colon);
    const std::string_view value = colon == std::string_view::npos
                                       ? std::string_view{}
                                       : strip_leading_space(line.substr(colon + 1));

    if (field == "data") {
        // Repeated data: fields are joined with newlines, per the spec.
        if (!current_.data.empty()) {
            current_.data.push_back('\n');
        }
        current_.data.append(value);
        has_field_ = true;
    } else if (field == "event") {
        current_.name = value;
        has_field_ = true;
    } else if (field == "id") {
        current_.id = value;
        has_field_ = true;
    }
    // Unknown fields (including `retry`) are ignored, per the spec. Ignoring
    // rather than erroring matters: a vendor adding a field must not break a
    // stream mid-answer.
    return true;
}

bool SseParser::dispatch() {
    if (!has_field_) {
        // Consecutive blank lines, or a stream that opens with one.
        return true;
    }
    SseEvent event = std::move(current_);
    current_ = SseEvent{};
    has_field_ = false;

    if (sink_ && !sink_(event)) {
        stopped_ = true;
        return false;
    }
    return true;
}

bool SseParser::finish() {
    if (stopped_) {
        return false;
    }
    // A final line with no trailing newline still forms a field.
    if (!carry_.empty()) {
        std::string_view line{carry_};
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        const bool keep_going = handle_line(line);
        carry_.clear();
        if (!keep_going) {
            return false;
        }
    }
    return dispatch();
}

}  // namespace apogee::backends
