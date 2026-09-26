# Web search through the user's own SearXNG

**What / why.** A `web_search` tool, answered by a SearXNG instance the user runs, so a model with `--tools` can find pages as well as read them. SearXNG is a self-hosted metasearch engine: it queries the public engines itself and answers over a stable JSON API (`GET /search?q=…&format=json`). No key, no account, and the queries stay on hardware the user controls. Local models have no provider-side search, so today they can read a URL they are given (`fetch_url`) but cannot find one. The tool returns the top results (title, URL, snippet, and a date when the engine gives one), and the model reads pages through `fetch_url` behind the per-website prompt of [tool safety defaults](tool-safety-defaults.md). Any backend with tools on gets it, cloud ones included.

**Core constraint(s).**
- **This revises the 2026-08-26 decision** ("provider server-side tools only"; DEVELOPER.md → `agent/`; MILESTONES → Milestone F). That decision rejected Ommi's search because scraping a results page breaks silently and returns nothing rather than erroring, and it left "the registry seam open for a pluggable one." A JSON API is not a page to scrape. SearXNG maintains its own engines against upstream markup changes, and its failures are explicit: an HTTP error, or `unresponsive_engines` in the response. DEVELOPER.md's "no local web-search tool" paragraph is rewritten when this ships.
- **Never silently empty.** No results is a result that says so and names the query. Failing engines are named (`unresponsive_engines`). A refused JSON request (HTTP 403, SearXNG's answer when `json` is not among its enabled formats, which is the default) names the setting to change.
- **`agent/` includes no transport.** As with `UrlFetcher`, the HTTP call arrives as a closure from the composition root (`commands/helpers.cpp`), so the tool and its response parser are tested with no network.
- **Outbound, and guarded.** The search host is the user's configured instance, trusted by configuration (a consumed default of [tool safety defaults](../assistant/MILESTONES.md#milestone-v--the-native-toolsets), recorded there with the rest of 25a). Every result the model then opens is an ordinary `fetch_url`, asked per website.
- **Off until configured.** No search section means no `web_search` tool registered: a model is never offered a tool that can only fail. `check` says how to turn it on.
- **One config mutation path, and the layout is untouched.** A new config section, read by the loader; the template carries a commented example.

**Seam + files.**
- `agent/web_search.h/.cpp` (new): the `SearchProvider` closure (`query, count, time_range → results or error`), `make_web_search_tool`, and `parse_searxng_response`, a pure function from the JSON body to results, with `answers`, `infoboxes` and `unresponsive_engines` read too. `agent/` is guarded, so this includes only `harness/` and `agent/` itself.
- `commands/helpers.cpp` (`make_built_in_tools`): registers the tool when the config names a provider, wiring the closure to `backends::HttpClient` with a timeout.
- `harness/config.h/.cpp`: `tools.search` holds `provider: searxng`, `url`, and `results` (default 5). `harness/config_template.cpp` gets a commented example under `tools:`.
- `commands/check.cpp`: the `Tools` section reports the provider and URL, or "not configured" with the lines to add.
- Documentation: a short setup note in a new `lib/documentation/reference/tools.md` covering:
  - running SearXNG in a container (`searxng/searxng`, its port 8080 mapped to a local one);
  - enabling `json` under `search.formats` in its `settings.yml`;
  - leaving its bot limiter off for a private instance;
  - the `tools.search` lines.

**Reference (Ommi).** Ommi's `web_search` (`src/tools/web.go`, `SearchDDG`) tried DuckDuckGo's instant-answer JSON, then scraped `html.duckduckgo.com` with regular expressions. That is the fragility the 2026-08-26 decision named, and it stays unported. Ommi also shipped a `web-search.yaml` training kit that teaches its prose protocol; see the scope note.

**Decisions made:**
- 2026-09-25 — **SearXNG** (the user's call, over Brave's or Tavily's keyed APIs and over MCP-only). No key, private, local by default. MCP remains open to anyone who prefers a hosted search server; nothing here prevents it.
- 2026-09-25 — **Pluggable from the start**: `provider:` is a field, and `SearchProvider` is the seam, so a keyed API can be added later as a second provider without reshaping the tool.
- 2026-09-25 — After [tool safety defaults](../assistant/MILESTONES.md#milestone-v--the-native-toolsets) (shipped 2026-09-25), because search multiplies the untrusted pages a model reads. After [local tool calling](local-tool-calling.md), because local models are who it is for.

**Open calls:**
- [default: 5 results, each with title, URL, snippet and date] A local model reads every result it is given; five is enough to choose one to open.
- [default: an optional `time_range` argument (day, week, month, year)] "What changed recently" is the commonest search a model makes, and SearXNG supports it.
- [default: `check` reports configuration only and makes no request] `check` never waits on the network; a failing instance is reported by the tool when used, with its error.
- [default: SearXNG's own `safesearch` and language defaults] The instance's owner already chose them.
- [default: available to every backend with tools on] A cloud backend with `--search` has its provider's search as well; the model may use either.

**Guardrail(s).**
- `parse_searxng_response` against recorded fixtures: normal results, zero results, `unresponsive_engines` only, answers and infoboxes, and a malformed body. Each outcome is either results or a named error, never an empty success.
- HTTP 403 names the `search.formats` setting; a connection refusal names the configured URL.
- No `tools.search` means no tool is registered, and `check` says how to add it.
- A result's page is fetched through `fetch_url` behind the per-website prompt.
- Each mutation-tested.

**Acceptance criteria:**
- [ ] With SearXNG running locally and `tools.search` set, `apogee complete --tools -m <Qwen3-VL-8B>` asked what changed in a named llama.cpp release calls `web_search`, opens a result with `fetch_url` (asked, on a terminal), and answers citing that URL.
- [ ] Against a SearXNG with JSON disabled, the tool's result names the setting to enable.
- [ ] Without `tools.search`, no `web_search` is offered, and `apogee check` shows how to configure it.
- [ ] The tools reference documents the setup end to end.

**Scope note.** Item **25e**; build after 25b (25a, the per-website prompt it relies on, shipped 2026-09-25). Out of scope:
- keyed providers (Brave, Tavily), the second implementation the seam exists for, each needing a credential-store slot;
- image and news categories;
- Ommi's `web-search` training kit, which rides the training tool kits that 25b unblocks.
