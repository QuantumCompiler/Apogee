# Web page fixtures

The corpus `fetch_url`'s reader is held to (`agent/readable`, 25f): for each
page, the text it must contain and the page furniture it must not.

| File | Kind | Where it comes from |
|---|---|---|
| `python-json-docs.html` | Documentation (`role="main"`, a sidebar and a related bar, code blocks, definition lists) | **Recorded** 2026-09-28 from <https://docs.python.org/3/library/json.html>. Copyright © 2001 Python Software Foundation; used under the [PSF License](https://docs.python.org/3/license.html). |
| `wikipedia-metasearch.html` | Encyclopedia article (`<main>` holding page tools and a language menu, citations, references, navigation boxes) | **Recorded** 2026-09-28 from <https://en.wikipedia.org/wiki/Metasearch_engine>. Text by Wikipedia contributors, [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/); the file keeps that licence. |
| `news-article.html` | News story (no `<main>`; a consent banner, ad slots, share buttons, a newsletter form, related teasers, comments) | **Hand-written** in the structure news sites use, with invented text, so that no newspaper's copyright ends up in the repository. |
| `github-release.html` | A GitHub release (a repository header, tabs, a Markdown body, assets, reactions) | **Hand-written** in the structure of GitHub's release page, for an invented repository. |
| `script-app.html` | A page built entirely by JavaScript (an empty root, a `noscript` notice, inline scripts) | **Hand-written**: the shape of a single-page application's shell. |
