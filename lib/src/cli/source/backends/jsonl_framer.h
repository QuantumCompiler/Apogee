#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

/// Turning a byte stream into complete JSONL lines.
///
/// **This is the bug class the whole vendor-CLI family lives or dies on.** A
/// child process's stdout is a pipe: bytes arrive in whatever sizes the kernel
/// felt like, and a single JSON object routinely spans several reads. Handle
/// that wrong and the failure is not a clean parse error — it is a line
/// silently split into two invalid halves, both dropped, and a turn that
/// mysteriously loses its middle.
///
/// So the framer is a separate, pure component with no I/O in it at all. That
/// makes the adversarial test possible: feed the same fixture in 1-byte,
/// 3-byte, 4096-byte, and whole-file chunks, and require the emitted line
/// sequence to be byte-for-byte identical every time. A framer that only ever
/// sees whole lines in tests is a framer that has never been tested.
namespace apogee::backends {

/// Accumulates bytes and emits complete lines.
///
/// Deliberately not a JSON parser: it knows about newlines and nothing else.
/// Keeping the two apart means a malformed object cannot desynchronise the
/// framing, which is what makes a mid-object truncation recoverable rather
/// than fatal.
class JsonlFramer {
public:
    /// Receives one complete line, newline stripped.
    using LineSink = std::function<void(std::string_view)>;

    /// Feeds `bytes`, calling `on_line` for each complete line it completes.
    ///
    /// A partial trailing line is retained for the next call. `\r\n` is
    /// normalised to `\n` — a CRLF that reached the JSON parser as a trailing
    /// `\r` would make an otherwise valid object fail to parse.
    void feed(std::string_view bytes, const LineSink& on_line);

    /// Emits whatever is buffered as a final line, if it is non-empty.
    ///
    /// Called at EOF. A child that exits without a trailing newline still has
    /// a real last event, and dropping it loses the `result` — which is where
    /// the session id and cost accounting live.
    void flush(const LineSink& on_line);

    /// Bytes currently held for the next feed. For tests and diagnostics.
    [[nodiscard]] std::size_t pending() const noexcept {
        return carry_.size();
    }

    /// Drops buffered bytes. Used when a child is replaced, so a half-line
    /// from the dead process cannot prefix the new one's first event.
    void reset() noexcept {
        carry_.clear();
    }

private:
    std::string carry_;
};

/// Whether `line` could be a JSON object — the first non-space byte is `{`.
///
/// The reader skips anything else instead of parsing it. A vendor CLI is
/// entitled to print a banner, a progress note, or a warning on stdout, and
/// the design notes say so explicitly; treating those as parse failures would
/// turn ordinary noise into a broken turn.
[[nodiscard]] bool looks_like_json_object(std::string_view line) noexcept;

/// Splits `bytes` into complete lines in one call. Convenience for tests and
/// for replaying a recorded fixture.
[[nodiscard]] std::vector<std::string> split_lines(std::string_view bytes);

}  // namespace apogee::backends
