#include "backends/markup_filter.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace apogee::backends {
namespace {

/// Whether `byte` can appear in a channel or section name.
///
/// Names are bare identifiers -- `analysis`, `final`, `commentary`, `assistant`
/// -- so the first byte that is not one ends the header and begins whatever
/// follows it.
[[nodiscard]] bool is_ident_byte(unsigned char byte) noexcept {
    return std::isalnum(byte) != 0 || byte == '_' || byte == '-';
}

[[nodiscard]] bool is_space_byte(unsigned char byte) noexcept {
    return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r';
}

/// The offset of the longest proper suffix of `text` that is a prefix of
/// `marker`, or `npos` when no suffix could still grow into one.
///
/// This is what lets a marker split across reads survive: those bytes are held,
/// never emitted.
[[nodiscard]] std::size_t partial_suffix(std::string_view text, std::string_view marker) noexcept {
    const std::size_t most = std::min(marker.size() - 1, text.size());
    for (std::size_t take = most; take > 0; --take) {
        if (text.substr(text.size() - take) == marker.substr(0, take)) {
            return text.size() - take;
        }
    }
    return std::string_view::npos;
}

}  // namespace

std::string MarkupFilter::write(std::string_view chunk) {
    if (!active() || chunk.empty()) {
        return std::string{chunk};
    }
    pending_ += chunk;
    return drain(false);
}

std::string MarkupFilter::flush() {
    if (!active()) {
        return {};
    }
    std::string out = drain(true);
    pending_.clear();
    return out;
}

std::pair<bool, std::size_t> MarkupFilter::consume_header(bool at_end) const {
    const std::string_view pending{pending_};

    std::size_t name_end = 0;
    while (name_end < pending.size() &&
           is_ident_byte(static_cast<unsigned char>(pending[name_end]))) {
        ++name_end;
    }
    if (name_end == pending.size() && !at_end) {
        // The name may continue in the next chunk -- `commentary` really does
        // arrive as `comment` then `ary`.
        return {false, 0};
    }

    std::size_t after = name_end;
    while (after < pending.size() && is_space_byte(static_cast<unsigned char>(pending[after]))) {
        ++after;
    }
    if (after == pending.size() && !at_end) {
        // Trailing whitespace may still be followed by the close marker.
        return {false, 0};
    }

    const std::string& close = headers_[static_cast<std::size_t>(inside_)].close;
    if (!close.empty()) {
        const std::string_view rest = pending.substr(after);
        if (rest.starts_with(close)) {
            return {true, after + close.size()};
        }
        if (!at_end && !rest.empty() && std::string_view{close}.starts_with(rest)) {
            // `rest` is a prefix of the close: it could still be growing into it.
            return {false, 0};
        }
    }

    // No close marker: the header ended at the identifier run. The whitespace
    // skipped past the name is given back, so a reply that legitimately starts
    // with a blank line keeps it.
    return {true, name_end};
}

std::string MarkupFilter::drain(bool at_end) {
    std::string out;

    while (true) {
        if (inside_ >= 0) {
            const auto [done, consumed] = consume_header(at_end);
            pending_.erase(0, consumed);
            if (!done) {
                break;  // more bytes needed to know where the header ends
            }
            inside_ = -1;
            continue;
        }

        // Outside a header: emit up to the next marker, holding back any tail
        // that could still be growing into one.
        std::size_t at = std::string::npos;
        int which = -1;
        bool partial = false;
        const auto consider = [&](std::size_t offset, int header, bool is_partial) {
            if (offset != std::string::npos && (at == std::string::npos || offset < at)) {
                at = offset;
                which = header;
                partial = is_partial;
            }
        };

        for (std::size_t index = 0; index < headers_.size(); ++index) {
            consider(pending_.find(headers_[index].open), static_cast<int>(index), false);
            if (!headers_[index].close.empty()) {
                // A stray close with no open before it: models emit these when
                // the open token was dropped. Framing either way.
                consider(pending_.find(headers_[index].close), -1, false);
            }
        }
        if (at == std::string::npos && !at_end) {
            for (std::size_t index = 0; index < headers_.size(); ++index) {
                consider(partial_suffix(pending_, headers_[index].open), static_cast<int>(index),
                         true);
                if (!headers_[index].close.empty()) {
                    consider(partial_suffix(pending_, headers_[index].close), -1, true);
                }
            }
        }

        if (at == std::string::npos) {
            out += pending_;
            pending_.clear();
            break;
        }

        out.append(pending_, 0, at);
        if (partial) {
            pending_.erase(0, at);  // hold the ambiguous tail
            break;
        }
        if (which >= 0) {
            pending_.erase(0, at + headers_[static_cast<std::size_t>(which)].open.size());
            inside_ = which;
            continue;
        }

        // A stray close. Drop exactly the marker that matched here, so the loop
        // always makes forward progress.
        std::size_t width = 1;
        for (const HeaderMarker& header : headers_) {
            if (!header.close.empty() &&
                std::string_view{pending_}.substr(at).starts_with(header.close)) {
                width = header.close.size();
                break;
            }
        }
        pending_.erase(0, at + width);
    }

    return out;
}

std::string strip_markup_headers(std::string_view text, const std::vector<HeaderMarker>& headers) {
    MarkupFilter filter{headers};
    std::string out = filter.write(text);
    out += filter.flush();
    return out;
}

}  // namespace apogee::backends
