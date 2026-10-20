#include "contracts/text_diff.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace apogee::harness {
namespace {

constexpr std::size_t kContext = 3;

/// Past this many edits the middle of a change is shown as removed whole and
/// added whole: still a true diff, only not the shortest, and the search for
/// the shortest costs time and memory growing with the square of it.
constexpr std::int64_t kMaxEdits = 1000;

constexpr std::string_view kCutPrefix = "[diff cut at ";

/// `text`'s lines, each with its `\n` -- the last one without, when the text
/// does not end in one. Compared whole, so an unterminated last line differs
/// from the same text terminated, as diff(1) has it.
std::vector<std::string_view> lines_of(std::string_view text) {
    std::vector<std::string_view> out;
    std::size_t from = 0;
    while (from < text.size()) {
        const std::size_t newline = text.find('\n', from);
        const std::size_t end = newline == std::string_view::npos ? text.size() : newline + 1;
        out.push_back(text.substr(from, end - from));
        from = end;
    }
    return out;
}

enum class Kind : std::uint8_t { Equal, Removed, Added };

struct Op {
    Kind kind;
    /// The line in `before` (Equal, Removed) or in `after` (Added).
    std::size_t line;
};

/// The shortest edit script from `a[a0, a1)` to `b[b0, b1)` (Myers, 1986),
/// appended to `out` -- or, past `kMaxEdits`, the whole range removed then
/// added.
void middle_ops(const std::vector<std::string_view>& a, std::size_t a0, std::size_t a1,
                const std::vector<std::string_view>& b, std::size_t b0, std::size_t b1,
                std::vector<Op>& out) {
    const auto n = static_cast<std::int64_t>(a1 - a0);
    const auto m = static_cast<std::int64_t>(b1 - b0);
    const auto whole = [&] {
        for (std::size_t i = a0; i < a1; ++i) {
            out.push_back({Kind::Removed, i});
        }
        for (std::size_t j = b0; j < b1; ++j) {
            out.push_back({Kind::Added, j});
        }
    };
    if (n == 0 || m == 0) {
        whole();
        return;
    }
    const std::int64_t limit = std::min(n + m, kMaxEdits);
    const std::int64_t offset = limit + 1;
    std::vector<std::int64_t> v(static_cast<std::size_t>(2 * offset + 1), 0);
    // Each round's furthest reaches on diagonals -d-1..d+1 -- all a round
    // reads -- kept to walk the path back.
    std::vector<std::vector<std::int64_t>> trace;
    std::int64_t found = -1;
    for (std::int64_t d = 0; d <= limit && found < 0; ++d) {
        trace.emplace_back(v.begin() + (offset - d - 1), v.begin() + (offset + d + 2));
        for (std::int64_t k = -d; k <= d; k += 2) {
            const auto at = static_cast<std::size_t>(k + offset);
            std::int64_t x =
                (k == -d || (k != d && v[at - 1] < v[at + 1])) ? v[at + 1] : v[at - 1] + 1;
            std::int64_t y = x - k;
            while (x < n && y < m &&
                   a[a0 + static_cast<std::size_t>(x)] == b[b0 + static_cast<std::size_t>(y)]) {
                ++x;
                ++y;
            }
            v[at] = x;
            // Exactly the far corner: a reach past one side's end is no path,
            // and one that ends here was never one.
            if (x == n && y == m) {
                found = d;
                break;
            }
        }
    }
    if (found < 0) {
        whole();
        return;
    }
    // Walked back from the end, then reversed.
    std::vector<Op> path;
    std::int64_t x = n;
    std::int64_t y = m;
    for (std::int64_t d = found; d > 0; --d) {
        const std::vector<std::int64_t>& before = trace[static_cast<std::size_t>(d)];
        const auto reach = [&](std::int64_t diagonal) {
            return before[static_cast<std::size_t>(diagonal + d + 1)];
        };
        const std::int64_t k = x - y;
        const bool down = k == -d || (k != d && reach(k - 1) < reach(k + 1));
        const std::int64_t prev_k = down ? k + 1 : k - 1;
        const std::int64_t prev_x = reach(prev_k);
        const std::int64_t prev_y = prev_x - prev_k;
        while (x > prev_x && y > prev_y) {
            --x;
            --y;
            path.push_back({Kind::Equal, a0 + static_cast<std::size_t>(x)});
        }
        if (down) {
            --y;
            path.push_back({Kind::Added, b0 + static_cast<std::size_t>(y)});
        } else {
            --x;
            path.push_back({Kind::Removed, a0 + static_cast<std::size_t>(x)});
        }
    }
    while (x > 0 && y > 0) {
        --x;
        --y;
        path.push_back({Kind::Equal, a0 + static_cast<std::size_t>(x)});
    }
    out.insert(out.end(), path.rbegin(), path.rend());
}

/// `-12,3`: a side's start and length as a hunk header writes them -- the
/// line before the hunk when it holds none of that side, the length left out
/// when it is one.
std::string range(char sign, std::size_t start0, std::size_t count) {
    std::string out{sign};
    out += std::to_string(count == 0 ? start0 : start0 + 1);
    if (count != 1) {
        out += "," + std::to_string(count);
    }
    return out;
}

std::string with_marker(char sign, std::string_view line) {
    std::string out{sign};
    out += line;
    if (line.empty() || line.back() != '\n') {
        out += "\n\\ No newline at end of file\n";
    }
    return out;
}

std::string kilobytes(std::size_t bytes) {
    return bytes % 1024 == 0 ? std::to_string(bytes / 1024) + " KB"
                             : std::to_string(bytes) + " bytes";
}

}  // namespace

std::string unified_diff(std::string_view path, std::string_view before, std::string_view after,
                         bool before_exists, std::size_t cap) {
    if (before_exists && before == after) {
        return {};
    }
    const std::string name{path};
    if (std::memchr(before.data(), '\0', before.size()) != nullptr ||
        std::memchr(after.data(), '\0', after.size()) != nullptr) {
        return "Binary files " + (before_exists ? "a/" + name : std::string{"/dev/null"}) +
               " and b/" + name + " differ\n";
    }
    const std::vector<std::string_view> a = lines_of(before);
    const std::vector<std::string_view> b = lines_of(after);

    // The lines both ends share are never searched: an edit's diff costs
    // what the edit changed, not what the file holds.
    std::size_t head = 0;
    while (head < a.size() && head < b.size() && a[head] == b[head]) {
        ++head;
    }
    std::size_t tail = 0;
    while (tail < a.size() - head && tail < b.size() - head &&
           a[a.size() - 1 - tail] == b[b.size() - 1 - tail]) {
        ++tail;
    }
    std::vector<Op> ops;
    ops.reserve(a.size() + b.size());
    for (std::size_t i = 0; i < head; ++i) {
        ops.push_back({Kind::Equal, i});
    }
    middle_ops(a, head, a.size() - tail, b, head, b.size() - tail, ops);
    for (std::size_t i = a.size() - tail; i < a.size(); ++i) {
        ops.push_back({Kind::Equal, i});
    }

    // Lines of each side before each op: a hunk's start and length.
    std::vector<std::size_t> old_before(ops.size() + 1, 0);
    std::vector<std::size_t> new_before(ops.size() + 1, 0);
    std::size_t added = 0;
    std::size_t removed = 0;
    for (std::size_t p = 0; p < ops.size(); ++p) {
        old_before[p + 1] = old_before[p] + (ops[p].kind == Kind::Added ? 0 : 1);
        new_before[p + 1] = new_before[p] + (ops[p].kind == Kind::Removed ? 0 : 1);
        added += ops[p].kind == Kind::Added ? 1 : 0;
        removed += ops[p].kind == Kind::Removed ? 1 : 0;
    }

    std::string out = "--- " + (before_exists ? "a/" + name : std::string{"/dev/null"}) +
                      "\n+++ b/" + name + "\n";
    const auto line_text = [&](const Op& op) {
        switch (op.kind) {
            case Kind::Equal:
                return with_marker(' ', a[op.line]);
            case Kind::Removed:
                return with_marker('-', a[op.line]);
            case Kind::Added:
                break;
        }
        return with_marker('+', b[op.line]);
    };
    const auto header = [&](std::size_t start, std::size_t stop) {
        return "@@ " + range('-', old_before[start], old_before[stop] - old_before[start]) + " " +
               range('+', new_before[start], new_before[stop] - new_before[start]) + " @@\n";
    };

    std::size_t i = 0;
    while (i < ops.size()) {
        while (i < ops.size() && ops[i].kind == Kind::Equal) {
            ++i;
        }
        if (i == ops.size()) {
            break;
        }
        const std::size_t start = i >= kContext ? i - kContext : 0;
        std::size_t j = i;
        while (true) {
            while (j < ops.size() && ops[j].kind != Kind::Equal) {
                ++j;
            }
            std::size_t k = j;
            while (k < ops.size() && ops[k].kind == Kind::Equal) {
                ++k;
            }
            // Changes closer than both contexts share one hunk.
            if (k < ops.size() && k - j <= 2 * kContext) {
                j = k;
                continue;
            }
            break;
        }
        const std::size_t stop = std::min(ops.size(), j + kContext);
        std::string body;
        std::size_t kept = start;
        // Room for the header, whose numbers are not known until the end.
        constexpr std::size_t kHeaderRoom = 64;
        bool cut = false;
        for (std::size_t p = start; p < stop; ++p) {
            std::string text = line_text(ops[p]);
            if (out.size() + kHeaderRoom + body.size() + text.size() > cap) {
                cut = true;
                break;
            }
            body += text;
            kept = p + 1;
        }
        if (kept > start) {
            out += header(start, kept) + body;
        }
        if (cut) {
            out += std::string{kCutPrefix} + kilobytes(cap) + ": +" + std::to_string(added) + " -" +
                   std::to_string(removed) + " lines in all]\n";
            return out;
        }
        i = stop;
    }
    return out;
}

DiffCounts diff_counts(std::string_view diff) {
    DiffCounts counts;
    if (diff.starts_with("Binary files ")) {
        counts.binary = true;
        return counts;
    }
    bool in_hunks = false;
    std::size_t from = 0;
    while (from < diff.size()) {
        const std::size_t newline = diff.find('\n', from);
        const std::size_t end = newline == std::string_view::npos ? diff.size() : newline;
        const std::string_view line = diff.substr(from, end - from);
        from = end + 1;
        if (line.starts_with(kCutPrefix)) {
            // The cut line's own totals: `… KB: +120 -45 lines in all]`.
            const std::size_t plus = line.find(": +");
            const std::size_t minus = line.find(" -", plus == std::string_view::npos ? 0 : plus);
            if (plus != std::string_view::npos && minus != std::string_view::npos) {
                counts.added = std::stoull(std::string{line.substr(plus + 3, minus - plus - 3)});
                counts.removed = std::stoull(std::string{line.substr(minus + 2)});
            }
            counts.cut = true;
            return counts;
        }
        if (line.starts_with("@@ ")) {
            in_hunks = true;
            continue;
        }
        if (!in_hunks) {
            continue;
        }
        if (line.starts_with('+')) {
            ++counts.added;
        } else if (line.starts_with('-')) {
            ++counts.removed;
        }
    }
    return counts;
}

std::string describe_diff_counts(const DiffCounts& counts) {
    if (counts.binary) {
        return "binary";
    }
    return "+" + std::to_string(counts.added) + " -" + std::to_string(counts.removed) +
           (counts.cut ? " (cut)" : "");
}

}  // namespace apogee::harness
