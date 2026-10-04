#include "backends/think_filter.h"

#include <algorithm>

namespace apogee::backends {
namespace {

/// How much of `haystack`'s tail could be the start of `needle`.
///
/// The whole hold-back decision. If the buffer ends with `<thi` and a marker is
/// `<think>`, those four bytes must wait: emitting them now and the marker's
/// tail later would print half a tag and then swallow the rest.
[[nodiscard]] std::size_t partial_suffix(std::string_view haystack, std::string_view needle) {
    const std::size_t most = std::min(haystack.size(), needle.size() - 1);
    for (std::size_t length = most; length > 0; --length) {
        if (haystack.substr(haystack.size() - length) == needle.substr(0, length)) {
            return length;
        }
    }
    return 0;
}

}  // namespace

const std::vector<TagPair>& default_think_pairs() {
    static const std::vector<TagPair> pairs{
        {.open = "<think>", .close = "</think>"},
        {.open = "<thinking>", .close = "</thinking>"},
        {.open = "<reasoning>", .close = "</reasoning>"},
    };
    return pairs;
}

std::size_t ThinkFilter::longest_marker() const noexcept {
    std::size_t longest = 0;
    for (const TagPair& pair : pairs_) {
        longest = std::max({longest, pair.open.size(), pair.close.size()});
    }
    return longest;
}

std::string ThinkFilter::write(std::string_view chunk) {
    if (pairs_.empty()) {
        // A verified-none profile. No arithmetic at all here, which is what
        // keeps the empty case from underflowing the hold-back -- the bug
        // Ommi hit as a negative-slice panic.
        return std::string{chunk};
    }
    if (chunk.empty()) {
        return {};
    }
    pending_.append(chunk);
    return drain(false);
}

std::string ThinkFilter::flush() {
    if (pairs_.empty()) {
        std::string out;
        out.swap(pending_);
        return out;
    }
    return drain(true);
}

std::string ThinkFilter::drain(bool at_end) {
    std::string out;

    for (;;) {
        if (!close_.empty()) {
            // Inside a block: everything up to the close marker is reasoning.
            const std::size_t end = pending_.find(close_);
            if (end == std::string::npos) {
                if (at_end) {
                    // Unterminated. The residue is reasoning, not answer --
                    // routing it to the answer would print the model's working.
                    if (thinking_ && !pending_.empty()) {
                        thinking_(pending_);
                    }
                    pending_.clear();
                    close_.clear();
                    return out;
                }
                // Hold back only what could be the start of the close marker;
                // everything before it is certainly reasoning and can go now.
                const std::size_t keep = partial_suffix(pending_, close_);
                if (thinking_ && pending_.size() > keep) {
                    thinking_(std::string_view{pending_}.substr(0, pending_.size() - keep));
                }
                pending_.erase(0, pending_.size() - keep);
                return out;
            }
            if (thinking_ && end > 0) {
                thinking_(std::string_view{pending_}.substr(0, end));
            }
            pending_.erase(0, end + close_.size());
            close_.clear();
            continue;
        }

        // Outside a block: find the earliest opener.
        std::size_t best = std::string::npos;
        const TagPair* opener = nullptr;
        for (const TagPair& pair : pairs_) {
            const std::size_t at = pending_.find(pair.open);
            if (at < best) {
                best = at;
                opener = &pair;
            }
        }

        if (opener != nullptr) {
            out.append(pending_, 0, best);
            pending_.erase(0, best + opener->open.size());
            close_ = opener->close;
            continue;
        }

        if (at_end) {
            // A partial marker at end of stream was never a marker.
            out.append(pending_);
            pending_.clear();
            return out;
        }

        // Hold back the longest tail that could still become an opener.
        std::size_t keep = 0;
        for (const TagPair& pair : pairs_) {
            keep = std::max(keep, partial_suffix(pending_, pair.open));
        }
        out.append(pending_, 0, pending_.size() - keep);
        pending_.erase(0, pending_.size() - keep);
        return out;
    }
}

std::string strip_think_blocks(std::string_view text, const std::vector<TagPair>& pairs) {
    ThinkFilter filter{pairs};
    std::string out = filter.write(text);
    out += filter.flush();
    return out;
}

}  // namespace apogee::backends
