#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "agent/fetch_url.h"

/// A web page as a model should read it (25f): the page's own content, as
/// light Markdown, without the menus, banners and footers around it.
///
/// **Hand-written, not a parser library** (the recorded default): a
/// forgiving tokenizer builds a flat tree of the page, a landmark picks the
/// content (`<main>`, `role="main"`, the dominant `<article>`, else the body),
/// and page furniture is dropped by element, by role and by a short list of
/// class names. The fixture corpus under `tests/fixtures/web/` is what holds
/// it to that; a parser library is the fallback if a page type there cannot
/// be met without one.
///
/// Everything here is pure: bytes in, text out, no transport.
namespace apogee::agent {

/// What a page reads as.
struct ReadablePage {
    /// `<title>`, else the first `<h1>`; whitespace collapsed.
    std::string title;
    /// The main content: paragraphs, `#` headings, `-` and `1.` list items,
    /// fenced code, `|` table rows, and links as `[text](absolute URL)`.
    std::string text;
};

/// The readable content of `html`, fetched from `page` -- which resolves
/// every relative link, after any `<base href>` the page declares.
[[nodiscard]] ReadablePage extract_readable(std::string_view html, const HttpUrl& page);

/// `bytes` as valid UTF-8. ISO-8859-1 and windows-1252 (and ASCII) are
/// converted, as `charset` names them; anything else is read as UTF-8, with
/// every byte that does not belong to a character replaced by U+FFFD. A tool
/// result is serialized as JSON downstream, where a stray byte is an error.
[[nodiscard]] std::string as_utf8(std::string_view bytes, std::string_view charset);

/// The `charset` a `Content-Type` value names, lowercased, or empty.
[[nodiscard]] std::string charset_of(std::string_view content_type);

/// The charset an HTML page declares in its first 4 KiB -- `<meta charset>`
/// or the `http-equiv` form -- lowercased, or empty.
[[nodiscard]] std::string meta_charset(std::string_view html);

/// One page of a long text.
struct TextPage {
    /// The page's text, without the blank lines around it.
    std::string text;
    /// 1-based, and how many pages the whole text makes from the start.
    std::size_t number = 1;
    std::size_t count = 1;
    /// Where the next page starts, or 0 on the last.
    std::size_t next = 0;
};

/// The page of `text` that starts at `offset`: at most `limit` bytes -- or
/// `first_limit`, when non-zero, for the first page -- cut at a paragraph
/// break in its second half, else a line break, else a space, else between
/// characters. Walking `next` from 0 covers the whole text with no gap and no
/// overlap; an offset past the end is nullopt-like -- `count` is 0.
[[nodiscard]] TextPage page_of(std::string_view text, std::size_t offset, std::size_t limit,
                               std::size_t first_limit = 0);

}  // namespace apogee::agent
