# Tools

The reference for setting up the tools a model reaches with `--tools` that
need something outside Apogee. Today that is one: **web search**, answered by
a SearXNG instance you run.

The native toolsets (files, the shell, git, notes, document search) and
`fetch_url` need no setup. `apogee check` lists each one's permission level
and the folder the file tools start in; `permissions:` and `tools:` in the
config are where both are changed.

## Web search through SearXNG

`web_search` finds pages; `fetch_url` reads them. SearXNG is a self-hosted
metasearch engine: it queries the public engines itself and answers over a
JSON API. There is no key and no account.

### What happens to a query

- The model's query goes to **your** SearXNG instance, and from there to the
  engines that instance is set to use. What those engines see is the query,
  from your instance's address.
- The instance's host is **trusted by configuration**: naming it in
  `tools.search` is the permission, so a search never asks. That holds on a
  pipe, in machine mode and on `serve` too.
- **Every page a search finds is an ordinary `fetch_url`.** It is asked about
  per website on a terminal, and refused where nobody can answer unless the
  website is in `tools.allowed_hosts`.

### 1. Run SearXNG

In a container, on a local port only:

```bash
mkdir -p ~/searxng
docker run -d --name searxng --restart unless-stopped \
  -p 127.0.0.1:8888:8080 \
  -v ~/searxng:/etc/searxng \
  searxng/searxng:latest
```

`127.0.0.1:8888:8080` maps the container's port 8080 to port 8888 on this
machine, reachable from this machine alone. Any other way of running SearXNG
works too; Apogee needs only its address.

### 2. Turn on its JSON output

SearXNG answers JSON **only when `json` is among its formats, and it is not
by default**. Without it every search is refused with HTTP 403, and
`web_search` says so, naming this setting.

On its first start the container writes `~/searxng/settings.yml`, with a
`secret_key` it generated. Keep that key, and add two settings so the file
reads:

```yaml
use_default_settings: true

server:
  secret_key: "…the one it generated…"
  image_proxy: true
  limiter: false

search:
  formats:
    - html
    - json
```

- **`search.formats`** must list `json`.
- **`server.limiter: false`** turns off SearXNG's bot limiter. It exists for
  public instances, and on one only you use it can answer searches with
  HTTP 429. `web_search` names this setting when that happens.

Then restart it and check that it answers JSON:

```bash
docker restart searxng
curl -s 'http://127.0.0.1:8888/search?q=test&format=json' | head -c 200
```

The answer should begin with `{"query": "test"`. A `403 Forbidden` page means
`json` is not yet among the formats.

### 3. Point Apogee at it

Under `tools:` in `~/.apogee/config/config.yaml` (the shipped config carries
these lines commented out):

```yaml
tools:
  search:
    provider: searxng
    url: http://127.0.0.1:8888
    results: 5          # 1 to 20; how many results each search returns
```

`url` may carry `${ENV_VAR}` references and a path, when the instance is
served under one (`https://search.example/searxng`).

### 4. Check it

```bash
apogee check
```

The `Tools` section shows where search points:

```
  ok   search  searxng at http://127.0.0.1:8888/, 5 results -- web_search is on, and 127.0.0.1 is reached without asking
```

`check` reads the configuration only. It never sends a search, so an instance
that is down is reported by the tool, when it is used. Without `tools.search`,
the row says how to add it, and **no `web_search` is offered to the model**.
A model is never offered a tool that can only fail.

### What the model gets

`web_search` takes a `query`, and an optional `time_range`: `day`, `week`,
`month` or `year`. It returns up to `results` results, each with its title,
URL, date when the engine gave one, and a snippet. It also returns any direct
answer or fact box an engine gave, and names the engines that failed.

It is never silently empty:

| What happened | What the model is told |
|---|---|
| Nothing matched | `No results for "…"`, naming the query. |
| Every engine asked failed | An error naming them and why (`duckduckgo (CAPTCHA)`). |
| JSON is off (HTTP 403) | An error naming `search.formats` in `settings.yml`. |
| The bot limiter (HTTP 429) | An error naming `server.limiter`. |
| Nothing is listening | An error naming the configured URL. |
| A redirect | An error naming where it went, to put in `tools.search.url`. |

When the instance itself fails (the last four rows), **the tool is withdrawn
for the rest of that turn**. Different words will not fix an instance, and a
small model told so still rephrases and retries. The next turn offers it
again, in case the instance was fixed.

### Every backend

Any backend with `--tools` gets `web_search`, cloud ones included. A cloud
backend run with `--search` also has its provider's own server-side search,
and the model may use either.
