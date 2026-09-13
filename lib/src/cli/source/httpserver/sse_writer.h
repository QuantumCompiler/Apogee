#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "harness/cancellation.h"
#include "harness/types.h"
#include "httpserver/http_types.h"

/// Server-sent events in the OpenAI streaming dialect.
///
/// Every frame is `data: <one JSON object>\n\n`, and the stream ends with the
/// literal `data: [DONE]\n\n`. The JSON never contains a raw newline -- the
/// serializer escapes them -- so a frame boundary is always exactly the blank
/// line, which is the property a client's SSE parser depends on.
namespace apogee::httpserver {

inline constexpr std::string_view kSseDone = "data: [DONE]\n\n";

/// `payload` framed as one SSE data event.
[[nodiscard]] std::string sse_frame(const nlohmann::json& payload);

/// A `harness::StatusEvent` as the `meta` object of a meta-frame: `type` and
/// `phase` always, and each optional field only when it carries a value --
/// absent is not zero, here as everywhere.
[[nodiscard]] nlohmann::json status_event_json(const harness::StatusEvent& event);

/// What every frame of one response shares.
struct StreamIdentity {
    std::string id;
    std::string model;
    std::int64_t created = 0;
};

/// One `chat.completion.chunk`. `finish_reason` is `null` on every chunk but the
/// last, exactly as a stock client expects.
[[nodiscard]] nlohmann::json chat_chunk(const StreamIdentity& identity, nlohmann::json delta,
                                        const std::optional<std::string>& finish_reason);

/// A meta-frame: a `chat.completion.chunk` whose `delta.content` is the empty
/// string, carrying `event` under a top-level `meta` key. A spec-compliant
/// client appends nothing and ignores the field it does not know; an
/// event-aware client renders the status. **The empty delta is the contract**
/// -- a meta-frame with content would print status text into an answer.
[[nodiscard]] nlohmann::json meta_chunk(const StreamIdentity& identity,
                                        const harness::StatusEvent& event);

/// One `text_completion` chunk for the legacy completions endpoint.
[[nodiscard]] nlohmann::json completion_chunk(const StreamIdentity& identity, std::string_view text,
                                              const std::optional<std::string>& finish_reason);

/// Writes frames to one client and knows when that client has gone.
///
/// A failed write closes the writer and **cancels the token** it was given, so
/// the loop streaming into it stops at its next check rather than generating
/// an answer nobody will read -- on a paid backend that is money, on a local
/// one it is the single inference slot.
class SseWriter {
public:
    SseWriter(WriteFn write, harness::CancellationToken cancellation);

    /// Frames and writes `payload`. False once the client is gone.
    bool send(const nlohmann::json& payload);

    /// Writes `bytes` as they are. False once the client is gone.
    bool send_raw(std::string_view bytes);

    /// Writes the terminal `[DONE]`.
    void done();

    [[nodiscard]] bool closed() const noexcept {
        return closed_;
    }

    /// Frames written so far, `[DONE]` included.
    [[nodiscard]] std::size_t frames_sent() const noexcept {
        return frames_;
    }

private:
    WriteFn write_;
    harness::CancellationToken cancellation_;
    bool closed_ = false;
    std::size_t frames_ = 0;
};

}  // namespace apogee::httpserver
