#include "embedstore/fts.h"

#include <cctype>

namespace apogee::embedstore {
namespace {

/// Whether `c` can be part of a search term.
///
/// Deliberately permissive about what counts as a word character and
/// deliberately blind to FTS5's operators: the operators are not preserved as
/// operators anywhere, so there is nothing to distinguish. Everything that is
/// not a separator becomes part of a quoted literal.
[[nodiscard]] bool is_term_byte(unsigned char c) noexcept {
    return std::isalnum(c) != 0 || c == '_' || c >= 0x80;
}

}  // namespace

std::string fts_match_query(std::string_view query) {
    std::string out;

    std::size_t index = 0;
    while (index < query.size()) {
        // Skip separators. Punctuation, operators, quotes -- all of it.
        while (index < query.size() && !is_term_byte(static_cast<unsigned char>(query[index]))) {
            ++index;
        }
        const std::size_t start = index;
        while (index < query.size() && is_term_byte(static_cast<unsigned char>(query[index]))) {
            ++index;
        }
        if (index == start) {
            break;
        }

        const std::string_view term = query.substr(start, index - start);
        if (!out.empty()) {
            // OR, not FTS5's implicit AND. Found by running it: a question is
            // not a keyword list, and "what is the capital of France" under
            // implicit AND requires the document to contain the word "what" --
            // so a corpus that plainly answers the question returns nothing.
            //
            // OR is also what makes BM25 worth having: it ranks by how many
            // terms matched and how rare they are, so the document sharing
            // "capital" and "France" outranks one sharing only "the". AND
            // throws that ranking away by refusing everything imperfect.
            out += " OR ";
        }
        // Quoted, so FTS5 reads it as a literal. Any embedded quote is doubled
        // -- though `is_term_byte` already excluded it, the escaping stays
        // because a future widening of the term alphabet must not silently
        // reopen the hole.
        out += '"';
        for (const char c : term) {
            if (c == '"') {
                out += '"';
            }
            out += c;
        }
        out += '"';
    }

    return out;
}

double normalize_bm25(double raw) noexcept {
    // SQLite's bm25() is negative, more relevant being more negative. Flip
    // first: scaling the raw value would rank the worst match highest.
    const double positive = raw < 0.0 ? -raw : raw;
    return positive / (1.0 + positive);
}

}  // namespace apogee::embedstore
