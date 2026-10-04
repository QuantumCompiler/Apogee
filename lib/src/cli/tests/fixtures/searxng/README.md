# SearXNG fixtures

**Recorded, then trimmed.** Each `.json` is the body a real SearXNG answered
to `GET /search?...&format=json` on 2026-09-28, cut to its first six results
(every field of those left as sent); `forbidden.html` is the whole body of the
refusal an instance with JSON off sends.

**Pinned instance: the `searxng/searxng` image, version `2026.9.25-12f8b6515`**,
its default engines, `search.formats: [html, json]`, the limiter off. Recorded
so a change in the API's shape is detected rather than silently absorbed.

| File | The request | What it captures |
|---|---|---|
| `normal.json` | `q=llama.cpp b6000 release` | Ordinary results with no dates, one failing engine (`[["duckduckgo", "CAPTCHA"]]`), and no `number_of_results` at all. |
| `dated.json` | `q=llama.cpp release`, `time_range=month` | Results carrying `publishedDate` (`2026-09-09T17:29:28`). |
| `zero.json` | `q=!wp zqxjvkwpt flurbnaxel` (Wikipedia alone) | No results, no failing engine: a search that ran and found nothing. |
| `unresponsive.json` | `q=!ddg llama.cpp release` (DuckDuckGo alone) | No results because the one engine asked failed: a search that never really ran. |
| `answer.json` | `q=avg 1 2 3` | A direct answer, as the object form current SearXNG sends (`{"answer": "[en] avg(1, 2, 3) = 2 ", ...}`), and no results. |
| `infobox.json` | `q=Albert Einstein` | Wikipedia's infobox (`infobox`, `content`, `id`, `urls`) beside results. |
| `forbidden.html` | any search, JSON off | HTTP 403 with an HTML body: SearXNG's default when `json` is not among `search.formats`. |
