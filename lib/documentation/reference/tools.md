# Tools

The reference for setting up the tools a model reaches with `--tools` that
need something set up first: **web search**, answered by a SearXNG instance
you run; **consult**, answered by a member of a model suite you configure --
and beside it **validation**, the same member checking the others' work;
**graph navigation**, answered by a knowledge graph you build; and the
**Orchestrator**, an `execute` session's symphonies offered to its model as
tools.

The native toolsets (files, the shell, git, notes, document search, graph
navigation) and `fetch_url` need no setup beyond that. `apogee check` lists each one's permission level
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

Under `tools` in `~/.apogee/config/config.json` (the shipped config carries
these lines commented out):

```jsonc
"tools": {
  "search": {
    "provider": "searxng",
    "url": "http://127.0.0.1:8888",
    "results": 5          // 1 to 20; how many results each search returns
  }
}
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
tool's result. It takes two arguments: `member`, one of the consultable
roles, and `question`. It exists only while the session runs under a suite whose
`consultable:` names members:

```bash
apogee config add-suite research --chat root --utility helper --consultable utility
apogee chat --suite research --tools
```

```jsonc
"suites": {
  "research": {
    "members": {
      "chat": "root",
      "utility": {
        "backend": "helper",
        "context_size": 4096
      }
    },
    "consultable": ["utility"],
    "consult_caps": {          // optional; these are the defaults
      "per_turn": 4,
      "brief_tokens": 1024,
      "answer_tokens": 512
    }
  }
}
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

## Navigating a knowledge graph

The `graph` toolset (27l) lets the model walk a built knowledge graph itself
-- a code graph (`apogee graph build --source <dir>`, parsed with tree-sitter,
no model) or a collection's graph -- with four tools. Each returns the JSON
document `apogee graph <verb> --output-format json` prints for the same
question, byte for byte:

| Tool | Arguments | What it answers |
|---|---|---|
| `graph_query` | `question`; `hops` (1 or 2), `max_entities` (1-50) | The entities a question's words name, and the bounded neighbourhood around them |
| `graph_path` | `from`, `to`; `max_hops` (1-32, default 8), `directed`, `relations` | How one entity reaches another: the shortest path, each hop with its relation |
| `graph_explain` | `node`; `max_neighbors` (1-100, default 12) | One entity: its kind, where it is defined or mentioned, its neighbours by relation |
| `graph_neighbors` | `node`; `relation`, `direction` (`both`, `out`, `in`), `max_neighbors` | An entity's neighbours, optionally one relation and one direction |

A node is addressed by name, `kind:name` (`function:pkg.mod.run`) or
`path:line` (`src/app.py:12`) -- a code node also by its unqualified name when
only one qualified name ends in it. A name several nodes share is never
guessed: the error lists each by the address that names it alone.

**Which graph.** Each tool also takes `graph` (a named graph, else a
collection's own) or `collection` (the named graph listing it, else its own).
With neither, the one graph built is read; with several, the error lists
them, and with none, it says how to build one.

**Always there, never asking.** The toolset is registered with the others
(switch it off with `tools.disabled: [graph]`, or leave it out of a suite
member's `toolset:`); tool selection (26g) keeps the four off the menu for a
question they do not fit. None writes or reaches out, so the permission gate
never asks about one, on any surface -- and `apogee __mcp-tools` serves all
four to an MCP client. Every cap is an argument with a ceiling: a tool never
returns an unbounded subgraph.

**A chat's attached code** (27o). When a folder of code attached to a chat
has been graphed (`/attach`, `--attach`, `--graph code`), the four names are
replaced by a scoped set over that chat's own graph: the same tools without
`graph`/`collection`, their descriptions naming the attached folders, and
the environment note telling the model to read the graph before answering a
question about that code's structure.

## The Orchestrator: symphonies as tools

An `apogee execute` session that orchestrates (27t) -- `--orchestrate`, or the
suite's own `orchestrate: true` -- offers its chat model each symphony as a
tool, `play_<name>`, so the model can choose to run one:

```bash
apogee config set-suite research --orchestrate on
apogee execute --suite research
```

Each tool carries the symphony's own description and one required argument,
`input` (a string): the whole text the play works on, since its stages see
nothing else. The play runs through the same walk `/play` uses, each stage a
line in the thinking block (in machine mode a `tool_status`), and its output
comes back as the tool's result, which the model answers from.

- **Offered without `--tools`.** Orchestrating is the session's consent to
  the model starting a play; `--tools` adds the other toolsets as usual.
- **Local members only.** A play the model starts never spends money: a
  symphony that reaches a member billed per call, not built, or not named in
  the suite is not offered, and the session says why; `orchestrate: true` is
  refused at config time over a billed member a symphony reaches, naming
  both. A symphony that takes an image is not offered either -- a tool call
  cannot carry one; `apogee symphonies play <name> --image <file>` plays it.
- **One budget.** Plays draw on the turn's member calls
  (`consult_caps.per_turn`, shared with consults and validation) and on the
  config's `symphony_caps:` as any play does. A play the budget cannot finish is refused before
  its first call, said as `orchestrate: <name> not played -- …`, and the
  model answers without it.
- **Off means absent.** Without orchestration no `play_` tool exists.

## Validation: members checking each other's work

A suite can have one member check the others' work (27g) -- the small
helper reading over the root's shoulder. It is off until a suite's
`validate:` block switches a seam on:

```bash
apogee config set-suite research --validate tool_args=on --validate extraction=on
```

```jsonc
"suites": {
  "research": {
    "members": {
      "chat": "root",
      "utility": "helper"
    },
    "validate": {
      "verifier": "utility",    // optional: the member that checks; utility by default
      "tool_args": "on",        // a tool that writes or reaches out, checked before it runs
      "extraction": "on",       // a knowledge capture's record, checked against its source
      "answers": "always"       // optional: request (the default -- /check only) or always
    }
  }
}
```

### What is checked

| Seam | When | What the verifier sees |
|---|---|---|
| `tool_args` | Before a tool that writes or sends data off the machine runs (`write_file`, `edit_file`, `delete_file`, the shell, `fetch_url`, …) | The call, the request it serves (your latest message) and what the tool does |
| `extraction` | When `knowledge capture`, chat's `/capture` or the admin twin draws a record from a conversation | The conversation and the record, against a fixed rubric: every required field supported, every name, number, count, date and link matching, nothing contradicted |
| `answers` | `/check` in chat, on the last answer -- or after every answer with `answers: always` (chat, `complete`) | The question and the answer |

**Structure first.** Before any model is asked, a tool call's arguments are
parsed, held to the tool's own schema and, for `edit_file` and
`delete_file`, to the file existing; a capture's record is held to its
schema. Whatever structure catches, no model is woken for.

**The verifier is a consult.** It sees the brief in the table and nothing
else, runs one call at a time, and spends from the same per-turn budget as
`consult` (`consult_caps:`). With the budget spent, a check falls back to
structure alone, and says so. Like a consultable member, the verifier must
be local and unmetered.

### One round, then you decide

An objection goes back once:

- **A tool call** objected to does not run: the objection is the call's
  result, so the model can correct it. Its next call of that tool is checked
  again and runs once it passes -- or, if the verifier still objects (or the
  model makes the same call again), runs anyway with the dispute shown first.
  The permission prompt is exactly what it always is: a check never stands in
  for it.
- **A capture**'s clerk is shown the objection and writes the record once
  more; the revision is checked again. A record still objected to is kept as
  the clerk wrote it, with the dispute beside it (on stderr, and as
  `validation` in `--json` and the admin route).
- **An answer** is checked once; on an objection the chat model is shown it
  once and answers -- and both are printed. Nothing is added to the
  conversation.

There is never a second revision. A check that passes is a line in the
thinking block (`validate — asking utility (helper): …`); an objection, a
dispute and a check that could not be made are kept on screen, and in
machine mode they are `notice` events.

