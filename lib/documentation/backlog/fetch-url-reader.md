# `fetch_url` as a reader: the page's content, its links, and the rest of it

**What / why.** `fetch_url` returns what a model needs from a page. The main content goes first, without the navigation, headers, footers, cookie banners and forms around it. Headings, lists and links stay as light Markdown, so a model can see the page's structure and follow a link it finds. A long page can be read in parts instead of ending at a cut.

Today it strips every tag (`strip_html`, "crude on purpose"), keeps the first 8,000 bytes, and marks the rest `[truncated]`. On a documentation or news page, those 8,000 bytes are mostly menu text. The links are gone, so a model cannot move on from the page it was given. And whatever is past the cut is unreachable. The same bytes also cost a local model a prompt-reading pass, so the noise is paid for twice. Found by the local-tools spike (2026-09-25), which also found two smaller faults:
- the cut is `std::string::resize`, which can split a UTF-8 sequence (the fault just fixed in `sanitize_title`);
- no content type is checked, so a PDF or an image is "stripped" into noise.

**Core constraint(s).**
- **No new dependency by default.** The stripper's rationale ("a real HTML parser would be a dependency and a parsing-difference surface") still holds for extraction by tag and landmark. A parser library is the fallback if the fixture corpus below cannot be met without one.
- **Honest about what was cut and what was refused.** Every truncation says so, and says how to read on (`offset`). A content type the reader cannot read (PDF, images, archives) is refused by name, never returned as bytes.
- **`agent/` includes no transport,** and the extraction is a pure function tested on saved pages.
- **Bounded.** The download is capped (the body is read whole today), and each call returns at most one page of text.
- **The per-website prompt** of [tool safety defaults](tool-safety-defaults.md) wraps every fetch, including the redirect hops it adds.

**Seam + files.**
- `agent/fetch_url.h/.cpp`:
  - `extract_readable(html, base_url)` returns the title, the main content (`<main>`, `<article>`, `role="main"`, else the body minus `nav`, `header`, `footer`, `aside`, `form`, `script`, `style`, `noscript`, `svg`), and headings, list items, links (`[text](absolute url)`), code blocks and table rows as plain Markdown.
  - The tool gains an `offset` argument and returns `Page N of M` with the next offset.
  - The cut lands on a UTF-8 boundary, and preferably on a paragraph.
  - A header line gives the final URL (after redirects) and the title.
- The fetcher closure in `commands/helpers.cpp` passes the response's `Content-Type` back in `FetchResult`: `text/html` is extracted, `text/plain`, JSON, XML and Markdown pass through, and other types are refused by name.
- `tests/agent/`: a fixture corpus of saved pages (documentation, a news article, a GitHub release, a Wikipedia article, a page that is mostly script), each with the text it must contain and the menu text it must not.

**Reference (Ommi).** Ommi's `FetchURL` (`src/tools/web.go`) was the same design: `stripHTML`, 8,000 bytes, `[content truncated]`. It was ported unchanged in Milestone F; this is the first revision.

**Decisions made:**
- 2026-09-25 — From the local-tools spike: the page reader is half of "search the internet and read the content from the pages", and the half that exists today returns mostly menus.
- 2026-09-25 — After [tool safety defaults](tool-safety-defaults.md), whose redirect handling this shares; independent of the other items otherwise.

**Open calls:**
- [default: a hand-written landmark extractor, no library] Revisit with a parser (lexbor or gumbo) only if the fixture corpus fails; that would be a dependency across all five targets.
- [default: 12 KiB per page of text] Big enough for a documentation section, small enough for a local model to read in seconds; the tool's description states it.
- [default: PDFs refused with a note] Text extraction from PDF is its own dependency decision and its own item.
- [default: a 5 MB download cap] Past that the fetch is refused, naming the size.
- [default: links kept inline as Markdown] A trailing numbered link list costs the model a lookup for every link it wants to follow.

**Guardrail(s).**
- Every fixture page's expected content is present and its menu text absent.
- Links are absolute.
- Paging:
  - `offset` walks the whole text with no gap or overlap;
  - no page ends inside a UTF-8 sequence (fuzzed with multi-byte text at every cut).
- A PDF and an image are refused by name; plain text and JSON pass through.
- The download cap refuses before reading past it.
- Each mutation-tested.

**Acceptance criteria:**
- [ ] On a documentation page and a news article (both in the corpus), the first page of `fetch_url` output is the article's own text, with its headings and links.
- [ ] A model reads a page longer than one call's worth by paging with `offset`, and can quote from its last section.
- [ ] A model follows a link from one fetched page to another in the same task.
- [ ] A PDF URL returns a refusal that says PDFs are not read, not a page of noise.

**Scope note.** Item **25f**; build after 25a. Out of scope:
- JavaScript rendering (a page that is empty without its scripts reports "no readable text", as now);
- PDF text extraction;
- caching pages across calls.
