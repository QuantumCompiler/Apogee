#pragma once

#include <string>
#include <string_view>

/// Turning what a user typed into something FTS5 will accept.
namespace apogee::embedstore {

/// Builds an FTS5 MATCH expression from natural-language `query`.
///
/// **This is a security boundary, and a usability one, and they are the same
/// boundary.** FTS5's query language has operators — `AND`, `OR`, `NOT`,
/// `NEAR`, `*`, `^`, `:`, quotes, parentheses — and a user searching for
/// *"quotes and parentheses"* is not writing a query language. Passing their
/// text through unaltered produces two failures at once: a syntax error for an
/// innocent question, and an injection surface for a hostile one.
///
/// So **every term is quoted as a literal string**. FTS5 treats a
/// double-quoted term as text, and doubling any embedded quote escapes it, so
/// the result can express only "find documents containing these words" — which
/// is exactly what a natural-language query means.
///
/// Ommi arrived at the same rule; this carries it over rather than
/// rediscovering it through a bug report.
///
/// Terms are joined with **OR**, not FTS5's implicit AND. A question is not a
/// keyword list: "what is the capital of France" under AND requires the
/// document to contain the word *what*, so a corpus that plainly answers it
/// returns nothing. OR is also what makes BM25 meaningful — it ranks by how
/// many terms matched and how rare they were, which AND discards by refusing
/// anything imperfect.
///
/// A query with no usable terms yields an empty string, which callers must
/// treat as "no results" rather than passing to MATCH — an empty MATCH is
/// itself a syntax error.
[[nodiscard]] std::string fts_match_query(std::string_view query);

/// Maps a raw BM25 relevance to (0, 1].
///
/// `s / (1 + s)`, carried from Ommi. Raw BM25 is unbounded and corpus-relative,
/// so a bare number is meaningless to a user comparing collections and to any
/// threshold written in a config. This is monotonic, so it never reorders
/// results — it only makes them comparable to a human.
///
/// SQLite's `bm25()` returns a **negative** number where more relevant is more
/// negative, so the sign is flipped before scaling. Forgetting that inverts the
/// ranking, which is why the flip lives here rather than at each call site.
[[nodiscard]] double normalize_bm25(double raw) noexcept;

}  // namespace apogee::embedstore
