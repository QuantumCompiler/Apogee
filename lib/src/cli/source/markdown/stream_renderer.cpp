#include "markdown/stream_renderer.h"

#include <array>
#include <utility>

#include "markdown/inline.h"
#include "markdown/layout.h"

namespace apogee::markdown {
namespace {

/// Bullets by nesting depth, cycling past the third.
constexpr std::array<std::string_view, 3> kBullets{"•", "◦", "▪"};
constexpr std::string_view kReplacement = "\xEF\xBF\xBD";  // U+FFFD

[[nodiscard]] ansi::TextAttributes dim_attributes() {
    ansi::TextAttributes attributes;
    attributes.dim = true;
    return attributes;
}

/// Tabs expanded to four-column stops, carriage returns dropped, and every
/// control character -- C0, DEL, C1 -- replaced. A model's output is shown,
/// never obeyed: an escape sequence in an answer would otherwise drive the
/// terminal, and a character the width table cannot count breaks the erase
/// arithmetic the open area depends on.
[[nodiscard]] std::string clean(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    std::size_t column = 0;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const auto byte = static_cast<unsigned char>(raw[i]);
        if (byte == '\r') {
            continue;
        }
        if (byte == '\t') {
            const std::size_t spaces = 4 - (column % 4);
            out.append(spaces, ' ');
            column += spaces;
            continue;
        }
        if (byte < 0x20 || byte == 0x7F) {
            out += kReplacement;
            ++column;
            continue;
        }
        if (byte == 0xC2 && i + 1 < raw.size()) {
            const auto next = static_cast<unsigned char>(raw[i + 1]);
            if (next >= 0x80 && next <= 0x9F) {
                out += kReplacement;
                ++column;
                ++i;
                continue;
            }
        }
        out += raw[i];
        if ((byte & 0xC0U) != 0x80U) {
            ++column;
        }
    }
    return out;
}

/// `text` without a UTF-8 sequence cut off at its end. A chunk can end
/// inside a character; painted, the fragment is a cell the width arithmetic
/// cannot count, and it waits for the rest of its bytes as a terminal would.
[[nodiscard]] std::string_view whole_characters(std::string_view text) {
    std::size_t start = text.size();
    for (std::size_t back = 0; back < 4 && start > 0; ++back) {
        --start;
        if ((static_cast<unsigned char>(text[start]) & 0xC0U) != 0x80U) {
            break;
        }
    }
    if (start < text.size()) {
        const auto lead = static_cast<unsigned char>(text[start]);
        std::size_t length = 1;
        if ((lead & 0xE0U) == 0xC0U) {
            length = 2;
        } else if ((lead & 0xF0U) == 0xE0U) {
            length = 3;
        } else if ((lead & 0xF8U) == 0xF0U) {
            length = 4;
        }
        if (start + length > text.size()) {
            return text.substr(0, start);
        }
    }
    return text;
}

[[nodiscard]] bool blank(std::string_view text) {
    return text.find_first_not_of(" \t\r") == std::string_view::npos;
}

[[nodiscard]] std::size_t leading_spaces(std::string_view text) {
    const std::size_t first = text.find_first_not_of(' ');
    return first == std::string_view::npos ? text.size() : first;
}

[[nodiscard]] std::string_view trim(std::string_view text) {
    const std::size_t first = text.find_first_not_of(' ');
    if (first == std::string_view::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(' ');
    return text.substr(first, last - first + 1);
}

/// A row of `count` plain spaces; empty for none.
[[nodiscard]] Row spaces(std::size_t count) {
    return count == 0 ? Row{} : Row{Span{std::string(count, ' '), {}, {}}};
}

[[nodiscard]] std::string repeat(std::string_view piece, std::size_t count) {
    std::string out;
    out.reserve(piece.size() * count);
    for (std::size_t i = 0; i < count; ++i) {
        out += piece;
    }
    return out;
}

/// Positions of the `|` that separate cells: not escaped, not inside a code
/// span.
[[nodiscard]] std::vector<std::size_t> pipes(std::string_view text) {
    std::vector<std::size_t> found;
    std::size_t ticks = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\\') {
            ++i;
        } else if (c == '`') {
            std::size_t run = 0;
            while (i + run < text.size() && text[i + run] == '`') {
                ++run;
            }
            ticks = ticks == run ? 0 : (ticks == 0 ? run : ticks);
            i += run - 1;
        } else if (c == '|' && ticks == 0) {
            found.push_back(i);
        }
    }
    return found;
}

[[nodiscard]] std::vector<std::string> split_cells(std::string_view row) {
    const std::string_view text = trim(row);
    std::vector<std::size_t> cuts = pipes(text);
    std::size_t begin = 0;
    std::size_t end = text.size();
    if (!cuts.empty() && cuts.front() == 0) {
        begin = 1;
        cuts.erase(cuts.begin());
    }
    if (!cuts.empty() && cuts.back() + 1 == text.size() && text.size() > begin) {
        end = cuts.back();
        cuts.pop_back();
    }
    std::vector<std::string> cells;
    std::size_t start = begin;
    const auto add = [&cells, &text](std::size_t from, std::size_t to) {
        std::string cell{trim(text.substr(from, to - from))};
        for (std::size_t at = cell.find("\\|"); at != std::string::npos;
             at = cell.find("\\|", at + 1)) {
            cell.erase(at, 1);
        }
        cells.push_back(std::move(cell));
    };
    for (const std::size_t cut : cuts) {
        add(start, cut);
        start = cut + 1;
    }
    add(start, end);
    return cells;
}

/// The alignment row under a table's header (`|---|:--:|--:|`), or nothing.
[[nodiscard]] std::optional<std::vector<Align>> delimiter_row(std::string_view text) {
    if (pipes(text).empty() || text.find('-') == std::string_view::npos) {
        return std::nullopt;
    }
    std::vector<Align> aligns;
    for (const std::string& cell : split_cells(text)) {
        if (cell.empty()) {
            return std::nullopt;
        }
        const bool left = cell.front() == ':';
        const bool right = cell.back() == ':';
        const std::string_view dashes = std::string_view{cell}.substr(
            left ? 1 : 0, cell.size() - (left ? 1 : 0) - (right ? 1 : 0));
        if (dashes.empty() || dashes.find_first_not_of('-') != std::string_view::npos) {
            return std::nullopt;
        }
        aligns.push_back(left && right ? Align::Center : (right ? Align::Right : Align::Left));
    }
    return aligns;
}

struct Heading {
    int level = 0;
    std::string_view text;
};

[[nodiscard]] std::optional<Heading> heading(std::string_view line) {
    const std::size_t indent = leading_spaces(line);
    if (indent > 3) {
        return std::nullopt;
    }
    std::size_t hashes = 0;
    while (indent + hashes < line.size() && line[indent + hashes] == '#') {
        ++hashes;
    }
    if (hashes == 0 || hashes > 6) {
        return std::nullopt;
    }
    const std::size_t after = indent + hashes;
    if (after < line.size() && line[after] != ' ') {
        return std::nullopt;  // "#hashtag" is text
    }
    std::string_view text = trim(line.substr(std::min(after, line.size())));
    // A closing run of #s, when a space sets it apart, is decoration.
    const std::size_t last = text.find_last_not_of('#');
    if (last == std::string_view::npos) {
        text = {};
    } else if (last + 1 < text.size() && text[last] == ' ') {
        text = trim(text.substr(0, last));
    }
    return Heading{static_cast<int>(hashes), text};
}

/// A rule: `---`, `***`, `___` -- and `===`, which is a setext heading's
/// underline. Line at a time, the heading's text is already committed when its
/// underline arrives, so the underline is drawn as a rule rather than left as
/// a row of equals signs (Llama 3.2 writes its headings this way).
[[nodiscard]] bool thematic_break(std::string_view line) {
    if (leading_spaces(line) > 3) {
        return false;
    }
    const std::string_view text = trim(line);
    if (text.empty() || (text.front() != '-' && text.front() != '*' && text.front() != '_' &&
                         text.front() != '=')) {
        return false;
    }
    std::size_t marks = 0;
    for (const char c : text) {
        if (c == text.front()) {
            ++marks;
        } else if (c != ' ') {
            return false;
        }
    }
    return marks >= 3;
}

struct FenceOpen {
    char marker = '`';
    std::size_t length = 0;
    std::size_t indent = 0;
    std::string_view info;
};

[[nodiscard]] std::optional<FenceOpen> fence_open(std::string_view line) {
    const std::size_t indent = leading_spaces(line);
    if (indent >= line.size() || (line[indent] != '`' && line[indent] != '~')) {
        return std::nullopt;
    }
    const char marker = line[indent];
    std::size_t length = 0;
    while (indent + length < line.size() && line[indent + length] == marker) {
        ++length;
    }
    if (length < 3) {
        return std::nullopt;
    }
    const std::string_view info = trim(line.substr(indent + length));
    if (marker == '`' && info.find('`') != std::string_view::npos) {
        return std::nullopt;  // ```` ```inline``` ```` is a code span
    }
    return FenceOpen{marker, length, indent, info.substr(0, info.find(' '))};
}

[[nodiscard]] bool fence_close(std::string_view line, char marker, std::size_t length) {
    const std::string_view text = trim(line);
    std::size_t run = 0;
    while (run < text.size() && text[run] == marker) {
        ++run;
    }
    return run >= length && run == text.size();
}

struct Item {
    std::size_t indent = 0;
    std::size_t content_indent = 0;
    bool ordered = false;
    std::string_view marker;
    std::string_view content;
};

[[nodiscard]] std::optional<Item> list_item(std::string_view line) {
    const std::size_t indent = leading_spaces(line);
    std::size_t end = indent;
    bool ordered = false;
    if (end < line.size() && (line[end] == '-' || line[end] == '+' || line[end] == '*')) {
        ++end;
    } else {
        while (end < line.size() && end - indent < 9 && line[end] >= '0' && line[end] <= '9') {
            ++end;
        }
        if (end == indent || end >= line.size() || (line[end] != '.' && line[end] != ')')) {
            return std::nullopt;
        }
        ++end;
        ordered = true;
    }
    if (end >= line.size() || line[end] != ' ') {
        return std::nullopt;
    }
    const std::string_view rest = line.substr(end);
    const std::size_t gap = leading_spaces(rest);
    return Item{indent, end + std::min<std::size_t>(gap, 4), ordered,
                line.substr(indent, end - indent), trim(rest)};
}

struct Quote {
    std::size_t depth = 0;
    std::string_view content;
};

[[nodiscard]] Quote quote(std::string_view line) {
    Quote result;
    std::size_t at = leading_spaces(line);
    if (at > 3) {
        return result;
    }
    while (at < line.size() && line[at] == '>') {
        ++result.depth;
        ++at;
        while (at < line.size() && line[at] == ' ') {
            ++at;
        }
    }
    result.content = line.substr(std::min(at, line.size()));
    return result;
}

[[nodiscard]] Row placeholder(std::size_t rows) {
    return Row{Span{"table · " + std::to_string(rows) + (rows == 1 ? " row…" : " rows…"),
                    dim_attributes(),
                    {}}};
}

/// Before a block's first row: the blank line the source put between it and
/// the last block, once.
void start_block(bool& began, bool& blank_pending, std::vector<Row>& out) {
    if (blank_pending && began) {
        out.emplace_back();
    }
    blank_pending = false;
    began = true;
}

void append(std::vector<Row>& out, std::vector<Row> rows) {
    for (Row& row : rows) {
        out.push_back(std::move(row));
    }
}

}  // namespace

RenderOps StreamRenderer::feed(std::string_view chunk, std::size_t width) {
    RenderOps ops;
    pending_ += chunk;
    for (std::size_t newline = pending_.find('\n'); newline != std::string::npos;
         newline = pending_.find('\n')) {
        const std::string line = pending_.substr(0, newline);
        pending_.erase(0, newline + 1);
        process(state_, line, width, ops.commit);
    }
    ops.open = open_area(width);
    return ops;
}

RenderOps StreamRenderer::finish(std::size_t width) {
    RenderOps ops;
    if (!pending_.empty()) {
        process(state_, pending_, width, ops.commit);
        pending_.clear();
    }
    if (state_.table_candidate.has_value()) {
        const std::string candidate = std::move(*state_.table_candidate);
        state_.table_candidate.reset();
        ordinary(state_, candidate, width, ops.commit);
    }
    if (!state_.table.empty()) {
        flush_table(state_, width, ops.commit);
    }
    state_.fence.reset();
    return ops;
}

void StreamRenderer::reset() {
    state_ = State{};
    pending_.clear();
}

bool StreamRenderer::began() const noexcept {
    return state_.began || !blank(pending_);
}

void StreamRenderer::process(State& state, std::string_view raw, std::size_t width,
                             std::vector<Row>& out) const {
    const std::string text = clean(raw);

    if (state.fence.has_value()) {
        const Fence fence = *state.fence;
        if (fence_close(text, fence.marker, fence.length)) {
            state.fence.reset();
            return;
        }
        const std::size_t strip = std::min(fence.indent, leading_spaces(text));
        const Row gutter = spaces(fence.block_indent + 2);
        start_block(state.began, state.blank_pending, out);
        append(out,
               hard_wrap({Span{text.substr(strip), dim_attributes(), {}}}, width, gutter, gutter));
        return;
    }

    if (!state.table.empty()) {
        if (!blank(text) && !pipes(text).empty()) {
            state.table.push_back(text);
            return;
        }
        flush_table(state, width, out);
    }

    if (state.table_candidate.has_value()) {
        const std::string candidate = std::move(*state.table_candidate);
        state.table_candidate.reset();
        if (const std::optional<std::vector<Align>> aligns = delimiter_row(text);
            aligns.has_value() && aligns->size() == split_cells(candidate).size()) {
            state.table = {candidate};
            state.table_delimiter = text;
            state.aligns = *aligns;
            return;
        }
        ordinary(state, candidate, width, out);
    }

    if (blank(text)) {
        state.blank_pending = state.began;
        state.in_item = false;
        return;
    }

    const std::size_t indent = leading_spaces(text);

    if (const std::optional<FenceOpen> open = fence_open(text)) {
        std::size_t block_indent = 0;
        // Indented to an item's text, the block belongs to the item; at the
        // margin it ends the list (a fence is never a lazy continuation).
        if (!state.lists.empty() && indent >= state.lists.back().content_indent) {
            block_indent = state.lists.back().hang;
        } else {
            state.lists.clear();
        }
        start_block(state.began, state.blank_pending, out);
        if (!open->info.empty()) {
            ansi::TextAttributes label = dim_attributes();
            label.italic = true;
            Row row = spaces(block_indent + 2);
            row.push_back(Span{std::string{open->info}, label, {}});
            out.push_back(std::move(row));
        }
        state.fence = Fence{open->marker, open->length, open->indent, block_indent};
        state.in_item = false;
        return;
    }

    if (const std::optional<Heading> found = heading(text)) {
        state.lists.clear();
        state.in_item = false;
        InlineOptions options{.hyperlinks = options_.hyperlinks};
        options.base.bold = true;
        if (found->level <= 2) {
            options.base.color = ansi::Color::Cyan;
            options.base.underline = found->level == 1;
        }
        start_block(state.began, state.blank_pending, out);
        append(out, wrap(render_inline(found->text, options), width, {}, {}));
        return;
    }

    if (thematic_break(text)) {
        state.lists.clear();
        state.in_item = false;
        start_block(state.began, state.blank_pending, out);
        const std::string_view stroke = trim(text).front() == '=' ? "═" : "─";
        out.push_back(Row{Span{repeat(stroke, width), dim_attributes(), {}}});
        return;
    }

    // A line that could head a table waits one line for its delimiter row.
    // Models write tables with leading pipes; without one, a line is taken as
    // a candidate only when nothing else claims it.
    const bool leading_pipe = trim(text).starts_with('|');
    if (leading_pipe ||
        (!pipes(text).empty() && !list_item(text).has_value() && quote(text).depth == 0)) {
        state.table_candidate = text;
        return;
    }

    ordinary(state, text, width, out);
}

void StreamRenderer::ordinary(State& state, const std::string& text, std::size_t width,
                              std::vector<Row>& out) const {
    const InlineOptions options{.hyperlinks = options_.hyperlinks};
    const std::size_t indent = leading_spaces(text);

    if (const Quote found = quote(text); found.depth > 0) {
        state.lists.clear();
        state.in_item = false;
        Row gutter;
        for (std::size_t d = 0; d < found.depth; ++d) {
            gutter.push_back(Span{"│ ", dim_attributes(), {}});
        }
        start_block(state.began, state.blank_pending, out);
        if (blank(found.content)) {
            out.push_back(gutter);
        } else {
            append(out, wrap(render_inline(found.content, options), width, gutter, gutter));
        }
        return;
    }

    if (const std::optional<Item> item = list_item(text)) {
        while (!state.lists.empty() && state.lists.back().marker_indent > item->indent) {
            state.lists.pop_back();
        }
        if (state.lists.empty() || item->indent > state.lists.back().marker_indent) {
            state.lists.emplace_back();
        }
        const std::size_t depth = state.lists.size() - 1;
        std::string marker = item->ordered ? std::string{item->marker}
                                           : std::string{kBullets[depth % kBullets.size()]};
        std::string_view content = item->content;
        if (content.starts_with("[ ] ") || content == "[ ]") {
            marker += " ☐";
            content = trim(content.substr(3));
        } else if (content.starts_with("[x] ") || content.starts_with("[X] ") || content == "[x]" ||
                   content == "[X]") {
            marker += " ☑";
            content = trim(content.substr(3));
        }
        // A nested list starts under its parent's text, not its bullet.
        Row first = spaces(depth > 0 ? state.lists[depth - 1].hang : 0);
        first.push_back(Span{marker + " ", {}, {}});
        const std::size_t hang = row_width(first);
        state.lists.back() = ListLevel{item->indent, item->content_indent, hang};
        start_block(state.began, state.blank_pending, out);
        append(out, wrap(render_inline(content, options), width, first, spaces(hang)));
        state.in_item = true;
        return;
    }

    if (!state.lists.empty()) {
        // A line indented to an item's text continues that item; so does an
        // unindented one straight after it (CommonMark's lazy continuation).
        std::optional<std::size_t> level;
        for (std::size_t d = state.lists.size(); d-- > 0;) {
            if (indent >= state.lists[d].content_indent) {
                level = d;
                break;
            }
        }
        if (!level.has_value() && state.in_item) {
            level = state.lists.size() - 1;
        }
        if (level.has_value()) {
            state.lists.resize(*level + 1);
            const Row hang = spaces(state.lists.back().hang);
            start_block(state.began, state.blank_pending, out);
            append(out, wrap(render_inline(trim(text), options), width, hang, hang));
            state.in_item = true;
            return;
        }
        state.lists.clear();
    }

    state.in_item = false;
    // Four spaces or more is an indent the text keeps.
    const Row prefix = indent >= 4 ? spaces(indent) : Row{};
    start_block(state.began, state.blank_pending, out);
    append(out, wrap(render_inline(trim(text), options), width, prefix, prefix));
}

void StreamRenderer::flush_table(State& state, std::size_t width, std::vector<Row>& out) const {
    const std::size_t columns = state.aligns.size();
    std::vector<std::vector<std::vector<Span>>> cells;
    std::vector<std::size_t> widths(columns, 0);
    for (std::size_t r = 0; r < state.table.size(); ++r) {
        InlineOptions options{.hyperlinks = options_.hyperlinks};
        options.base.bold = r == 0;
        std::vector<std::string> raw = split_cells(state.table[r]);
        raw.resize(columns);
        std::vector<std::vector<Span>> row;
        for (std::size_t c = 0; c < columns; ++c) {
            row.push_back(render_inline(raw[c], options));
            widths[c] = std::max(widths[c], row_width(row.back()));
        }
        cells.push_back(std::move(row));
    }
    std::size_t total = columns == 0 ? 0 : 3 * (columns - 1);
    for (const std::size_t w : widths) {
        total += w;
    }

    start_block(state.began, state.blank_pending, out);
    const std::size_t separators = columns == 0 ? 0 : 3 * (columns - 1);
    // Narrower than this, a column's words break more than they wrap.
    constexpr std::size_t kNarrowest = 8;
    if (columns == 0 || width < separators + (kNarrowest * columns)) {
        // Not even narrow columns fit: the rows as written, wrapped.
        const InlineOptions options{.hyperlinks = options_.hyperlinks};
        for (std::size_t r = 0; r < state.table.size(); ++r) {
            append(out, wrap(render_inline(trim(state.table[r]), options), width, {}, {}));
            if (r == 0 && !state.table_delimiter.empty()) {
                append(out,
                       wrap({Span{std::string{trim(state.table_delimiter)}, dim_attributes(), {}}},
                            width, {}, {}));
            }
        }
    } else {
        // Each column as wide as its widest cell, when the table fits. When
        // it does not, the columns narrower than an even share keep their
        // width and the wide ones divide what is left, their cells wrapping
        // within them (a long-celled comparison is what models write).
        std::vector<std::size_t> target = widths;
        if (total > width) {
            std::size_t budget = width - separators;
            std::vector<bool> settled(columns, false);
            std::size_t open = columns;
            for (bool changed = true; changed && open > 0;) {
                changed = false;
                const std::size_t share = budget / open;
                for (std::size_t c = 0; c < columns; ++c) {
                    if (!settled[c] && widths[c] <= share) {
                        settled[c] = true;
                        budget -= widths[c];
                        --open;
                        changed = true;
                    }
                }
            }
            std::size_t leftover = open == 0 ? 0 : budget % open;
            for (std::size_t c = 0; c < columns; ++c) {
                if (!settled[c]) {
                    target[c] = (budget / open) + (leftover > 0 ? 1 : 0);
                    leftover -= leftover > 0 ? 1 : 0;
                }
            }
        }
        const Span separator{" │ ", dim_attributes(), {}};
        for (std::size_t r = 0; r < cells.size(); ++r) {
            std::vector<std::vector<Row>> wrapped;
            std::size_t height = 1;
            for (std::size_t c = 0; c < columns; ++c) {
                wrapped.push_back(cells[r][c].empty() ? std::vector<Row>{Row{}}
                                                      : wrap(cells[r][c], target[c], {}, {}));
                height = std::max(height, wrapped.back().size());
            }
            for (std::size_t line = 0; line < height; ++line) {
                Row row;
                for (std::size_t c = 0; c < columns; ++c) {
                    const Row cell = line < wrapped[c].size() ? wrapped[c][line] : Row{};
                    const std::size_t used = row_width(cell);
                    const std::size_t pad = target[c] > used ? target[c] - used : 0;
                    std::size_t before = 0;
                    if (state.aligns[c] == Align::Right) {
                        before = pad;
                    } else if (state.aligns[c] == Align::Center) {
                        before = pad / 2;
                    }
                    // The last column is not padded on the right: trailing
                    // spaces would only lengthen the row.
                    const std::size_t after = c + 1 < columns ? pad - before : 0;
                    if (before > 0) {
                        row.push_back(Span{std::string(before, ' '), {}, {}});
                    }
                    row.insert(row.end(), cell.begin(), cell.end());
                    if (after > 0) {
                        row.push_back(Span{std::string(after, ' '), {}, {}});
                    }
                    if (c + 1 < columns) {
                        row.push_back(separator);
                    }
                }
                out.push_back(std::move(row));
            }
            if (r == 0) {
                std::string rule;
                for (std::size_t c = 0; c < columns; ++c) {
                    rule += repeat("─", target[c]);
                    if (c + 1 < columns) {
                        rule += "─┼─";
                    }
                }
                out.push_back(Row{Span{std::move(rule), dim_attributes(), {}}});
            }
        }
    }
    state.table.clear();
    state.table_delimiter.clear();
    state.aligns.clear();
    state.in_item = false;
}

std::vector<Row> StreamRenderer::open_area(std::size_t width) const {
    std::vector<Row> rows;
    State copy = state_;
    if (!copy.table.empty()) {
        // A streaming table shows only its placeholder: laying it out here
        // would paint the whole table on every chunk, and a tall one would
        // outgrow the screen the open area must stay on.
        start_block(copy.began, copy.blank_pending, rows);
        rows.push_back(placeholder(copy.table.size()));
        return rows;
    }
    if (const std::string_view open = whole_characters(pending_); !blank(open)) {
        process(copy, open, width, rows);
    }
    if (copy.table_candidate.has_value()) {
        const std::string candidate = std::move(*copy.table_candidate);
        copy.table_candidate.reset();
        ordinary(copy, candidate, width, rows);
    }
    if (!copy.table.empty()) {
        start_block(copy.began, copy.blank_pending, rows);
        rows.push_back(placeholder(copy.table.size()));
    }
    return rows;
}

}  // namespace apogee::markdown
