#include "markdown/layout.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include "ansi/text_width.h"

namespace apogee::markdown {
namespace {

/// One codepoint of content, and the span it came from.
struct Atom {
    std::string_view bytes;
    std::size_t cells = 0;
    std::size_t span = 0;
    bool space = false;
};

[[nodiscard]] std::vector<Atom> atoms_of(const std::vector<Span>& content) {
    std::vector<Atom> atoms;
    for (std::size_t s = 0; s < content.size(); ++s) {
        const std::string_view text = content[s].text;
        for (std::size_t i = 0; i < text.size();) {
            const std::size_t length = std::min(
                ansi::utf8_sequence_length(static_cast<unsigned char>(text[i])), text.size() - i);
            const char32_t point = ansi::decode_utf8(text, i, length);
            atoms.push_back(
                Atom{text.substr(i, length), ansi::codepoint_cells(point), s, point == U' '});
            i += length;
        }
    }
    return atoms;
}

/// One row under construction: a prefix, then content codepoints merged into
/// the last span whenever they look the same.
class RowBuilder {
public:
    RowBuilder(const std::vector<Span>& content, std::size_t width)
        : content_{content}, width_{width} {}

    void start(const Row& prefix) {
        row_ = prefix;
        used_ = row_width(prefix);
        has_content_ = false;
    }

    void add(const Atom& atom) {
        const Span& source = content_[atom.span];
        if (!row_.empty() && row_.back().attributes == source.attributes &&
            row_.back().link == source.link) {
            row_.back().text += atom.bytes;
        } else {
            row_.push_back(Span{std::string{atom.bytes}, source.attributes, source.link});
        }
        used_ += atom.cells;
        has_content_ = true;
    }

    [[nodiscard]] std::size_t room() const noexcept {
        return width_ > used_ ? width_ - used_ : 0;
    }

    [[nodiscard]] bool has_content() const noexcept {
        return has_content_;
    }

    [[nodiscard]] Row take() {
        return std::move(row_);
    }

private:
    const std::vector<Span>& content_;
    std::size_t width_;
    Row row_;
    std::size_t used_ = 0;
    bool has_content_ = false;
};

}  // namespace

std::size_t row_width(const Row& row) {
    std::size_t cells = 0;
    for (const Span& span : row) {
        cells += ansi::display_width(span.text);
    }
    return cells;
}

std::string plain_text(const Row& row) {
    std::string text;
    for (const Span& span : row) {
        text += span.text;
    }
    return text;
}

std::vector<Row> wrap(const std::vector<Span>& content, std::size_t width, const Row& first,
                      const Row& rest) {
    const std::vector<Atom> atoms = atoms_of(content);
    const std::size_t rest_room = width > row_width(rest) ? width - row_width(rest) : 0;
    std::vector<Row> rows;
    RowBuilder row{content, width};
    row.start(first);
    const auto next_row = [&rows, &row, &rest]() {
        rows.push_back(row.take());
        row.start(rest);
    };

    std::size_t i = 0;
    while (i < atoms.size()) {
        if (atoms[i].space) {
            std::size_t j = i;
            std::size_t spaces = 0;
            while (j < atoms.size() && atoms[j].space) {
                spaces += atoms[j].cells;
                ++j;
            }
            if (j == atoms.size()) {
                break;  // spaces at the very end are dropped
            }
            std::size_t word = 0;
            for (std::size_t k = j; k < atoms.size() && !atoms[k].space; ++k) {
                word += atoms[k].cells;
            }
            if (!row.has_content()) {
                // Spaces the text starts with are an indent it carries, kept
                // on the first row; at the start of a wrapped row they go.
                for (std::size_t k = i; rows.empty() && k < j && row.room() > 0; ++k) {
                    row.add(atoms[k]);
                }
                i = j;
                continue;
            }
            if (spaces + word <= row.room()) {
                for (; i < j; ++i) {
                    row.add(atoms[i]);
                }
                continue;
            }
            if (word <= rest_room) {
                next_row();
                i = j;
                continue;
            }
            // The next word is too long for any row: keep the space when it
            // fits, and let the word break across rows from here.
            for (std::size_t k = i; k < j && row.room() > 0; ++k) {
                row.add(atoms[k]);
            }
            i = j;
            continue;
        }

        std::size_t k = i;
        std::size_t word = 0;
        while (k < atoms.size() && !atoms[k].space) {
            word += atoms[k].cells;
            ++k;
        }
        if (word <= row.room()) {
            for (; i < k; ++i) {
                row.add(atoms[i]);
            }
            continue;
        }
        if (row.has_content() && word <= rest_room) {
            next_row();
            continue;
        }
        // Longer than a whole row: broken between codepoints. A row with no
        // room at all still takes one, so a degenerate width cannot loop.
        for (; i < k; ++i) {
            if (atoms[i].cells > row.room() && row.has_content()) {
                next_row();
            }
            row.add(atoms[i]);
        }
    }
    rows.push_back(row.take());
    return rows;
}

std::vector<Row> hard_wrap(const std::vector<Span>& content, std::size_t width, const Row& first,
                           const Row& rest) {
    std::vector<Row> rows;
    RowBuilder row{content, width};
    row.start(first);
    for (const Atom& atom : atoms_of(content)) {
        if (atom.cells > row.room() && row.has_content()) {
            rows.push_back(row.take());
            row.start(rest);
        }
        row.add(atom);
    }
    rows.push_back(row.take());
    return rows;
}

}  // namespace apogee::markdown
