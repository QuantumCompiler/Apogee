#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

/// A streaming filter that removes chat-template control-token **headers** from
/// a generation stream, leaving everything else untouched.
///
/// ## Deliberately not the reasoning filter
///
/// `ThinkFilter` recognises open/close pairs that **wrap** content and routes
/// the interior to a thinking view. A header is the opposite shape and the
/// opposite intent: it is a bare marker with an identifier after it, carries no
/// content of its own, and must be removed for every model regardless of
/// tuning. Shoehorning headers into `ThinkFilter` would make it wrong about
/// both, which is why they are two files.
///
/// ## The grammar, verified against real output
///
/// OPEN, a bare identifier naming the section, optional whitespace, then an
/// **optional** CLOSE. Observed on gpt-oss-20b (MXFP4) on 2026-09-07, where a
/// reply to "What is 2+2?" reached the caller as:
///
/// ```
/// <|channel|>analysis<|message|>The user asks: … The answer is 4.<|end|>
/// <|start|>assistant<|channel|>final<|message|>4
/// ```
///
/// Every one of those characters is visible text: gpt-oss's framing tokens are
/// USER_DEFINED rather than CONTROL, so llama.cpp detokenizes them into the
/// stream even with `special = false`. Two header shapes appear there and both
/// are handled: `<|channel|>NAME<|message|>` closes, `<|start|>NAME` does not.
///
/// **The close is optional because the identifier run is what actually bounds a
/// header.** Models emit the close inconsistently, so binding to it would let
/// one dropped token swallow a paragraph of answer. Bounding by the identifier
/// costs a single word in the worst case.
///
/// ## Streaming
///
/// `write`/`flush` mirror `ThinkFilter`'s contract so the two compose in one
/// stream. Bytes that could still turn out to be part of a marker are held back
/// until a later call resolves them -- gpt-oss tokenizes `commentary` as
/// `comment` + `ary`, so a marker split across reads is the normal case. A
/// partial marker at end of stream was never a marker and is emitted verbatim;
/// an unterminated header's residue is dropped, because those bytes are the
/// identifier and framing either way.
namespace apogee::backends {

/// One `OPEN NAME [CLOSE]` control-token header.
///
/// `close` may be empty, which means this header ends at its identifier run.
struct HeaderMarker {
    std::string open;
    std::string close;
};

class MarkupFilter {
public:
    MarkupFilter() = default;

    /// An empty list makes `write` a pure pass-through, so callers need no
    /// null check and an unprofiled model pays nothing.
    explicit MarkupFilter(std::vector<HeaderMarker> headers) : headers_{std::move(headers)} {}

    /// Consumes `chunk` and returns the text safe to emit now.
    [[nodiscard]] std::string write(std::string_view chunk);

    /// Returns whatever is still held, end of stream resolving any ambiguity.
    [[nodiscard]] std::string flush();

    /// Whether this filter can remove anything at all.
    [[nodiscard]] bool active() const noexcept {
        return !headers_.empty();
    }

private:
    [[nodiscard]] std::string drain(bool at_end);
    /// Eats the header body after an OPEN. Returns whether it resolved, and how
    /// many bytes to drop. Nothing is ever emitted from here.
    [[nodiscard]] std::pair<bool, std::size_t> consume_header(bool at_end) const;

    std::vector<HeaderMarker> headers_;
    std::string pending_;
    /// Index into `headers_` of the header being consumed; -1 means outside.
    int inside_ = -1;
};

/// One-shot convenience for text that is already complete.
[[nodiscard]] std::string strip_markup_headers(std::string_view text,
                                               const std::vector<HeaderMarker>& headers);

/// The turn markers a model with no chat template spills (26r).
///
/// A base model read every family's transcripts in pretraining, and writes
/// their markers as it imitates a conversation -- whole, chained, or cut short
/// (`<|end|`, `<|end|><|im|`). With no template none of them can be anything
/// but noise, so one of a known family (`model_profile.h`'s
/// `base_turn_markers`) ends the reply, as the guessed framing's own markers
/// already do, and a `<|` fragment still open when the stream ends is never
/// emitted. One of no known family (`<|fiap|`, seen on Gemma 4 E4B) is dropped
/// where it stands and the reply goes on: with no template a marker is noise
/// by definition. A `<|` that starts no name (`a <| b`) is text.
///
/// Built only for a model with no template: one that has a template may be
/// quoting `<|end|>`, and is never filtered here. Text that could still be a
/// marker is held until a later write resolves it, so a fragment split across
/// reads never reaches anyone in part.
class TurnMarkerFilter {
public:
    TurnMarkerFilter() = default;

    /// `markers` whole, as `<|im_end|>`. An empty list makes `write` a pure
    /// pass-through.
    explicit TurnMarkerFilter(const std::vector<std::string>& markers);

    /// Consumes `chunk` and returns the text safe to emit now. Sets `stopped`
    /// when a known marker ended the reply; it and everything after it are
    /// dropped.
    [[nodiscard]] std::string write(std::string_view chunk, bool& stopped);

    /// What is still held at end of stream: a `<|` fragment that never closed
    /// is dropped; a lone `<` was never one, and comes out.
    [[nodiscard]] std::string flush();

    [[nodiscard]] bool active() const noexcept {
        return !names_.empty();
    }

private:
    /// Whether `name` -- between `<|` and, when `complete`, `|>` -- is a
    /// known family's marker, or one cut short.
    [[nodiscard]] bool known(std::string_view name, bool complete) const;

    /// The markers' names: `im_end` for `<|im_end|>`.
    std::vector<std::string> names_;
    std::string pending_;
    bool stopped_ = false;
};

}  // namespace apogee::backends
