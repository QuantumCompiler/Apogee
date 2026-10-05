# Tools

The reference for setting up the tools a model reaches with `--tools` that
need something set up first. Today that is two: **web search**, answered by
a SearXNG instance you run, and **consult**, answered by a member of a model
suite you configure.

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

## Consulting a suite member

`consult` lets the chat model hand a sub-task to another model of its suite
(27f) -- the small one beside it, say -- and read the answer back as the
tool's result. It exists only while the session runs under a suite whose
`consultable:` names members:

```bash
apogee config add-suite research --chat root --utility helper --consultable utility
apogee chat --suite research --tools
```

```yaml
suites:
  research:
    members:
      chat: root
      utility:
        backend: helper
        context_size: 4096
    consultable: [utility]
    consult_caps:          # optional; these are the defaults
      per_turn: 4
      brief_tokens: 1024
      answer_tokens: 512
```

### What the member sees

**The question, and nothing else**: no conversation, no system prompt, no
retrieval, no attached files, no tools. The model is told so in the tool's
description, which also names each member it can consult and its backend,
so it puts every fact the member needs into the question. That is what lets
a member with a 4K window help a root with a 32K one.

### The bounds

| Bound | Default | Over it |
|---|---|---|
| Consults per turn | 4 | `Not consulted: consult budget spent this turn: 4 of 4 member calls made -- answer with what you have.` |
| A question's length | about 1,024 tokens | Refused, with its length: shorten it and ask again. |
| An answer's length | 512 tokens | Cut there, and the result says so. |
| The member's window | its own | A question and answer that would not fit are refused. |

A refusal is a result the model reads, never an error that ends the turn.
Consults run one at a time, each a line in the thinking block --
`consult — asking utility (helper): …` -- and in machine mode a
`tool_status`.

### Local members only

A consult runs on the model's initiative, so it never spends money: a member
whose backend is billed per call -- every cloud API -- cannot be made
consultable. `config add-suite`/`set-suite` (and their admin twins) ask the
member's provider and refuse it with the reason; a config edited by hand to
name one fails in `apogee check`, and the tool does not offer it. There is no
permission prompt: a consult reads nothing and writes nothing.

