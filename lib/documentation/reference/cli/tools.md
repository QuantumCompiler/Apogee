# Tools

The reference for setting up the tools a model reaches with `--tools` that
need something set up first: **web search**, answered by a SearXNG instance
you run; **consult**, answered by a member of a model suite you configure --
and beside it **validation**, the same member checking the others' work;
**graph navigation**, answered by a knowledge graph you build; the
**Orchestrator**, an `execute` session's symphonies offered to its model as
tools; **code execution**, the Python server Apogee ships, registered
with one command; the **MCP presets**, every server Apogee ships, listed
with what it needs; the **servers an ACP host hands a session** -- Xcode's own
tools in an `apogee acp` session; and **`apogee mcp serve`**, Apogee itself as
an MCP server for another host -- its local models among the tools where you
list them.

The native toolsets (files, the shell, git, notes, document search, graph
navigation) and `fetch_url` need no setup beyond that, and nor do the
session's own two: `todo_write`, the plan the model keeps where you can see
it (`/todo` prints it), and **`delegate`**, below. `apogee check` lists each one's permission level
and the folder the file tools start in; `permissions:` and `tools:` in the
config are where both are changed. The shell and git start other programs,
which the Windows build cannot do yet (`platform::start_child`): there
neither `run_command` nor any git tool is offered or served, `check`'s `git`
row is skipped saying why, and `--allow`, `--deny`, `/allow` or a task's
grant naming one is refused with that reason (57k).

**A run refused for its flags starts no MCP server** (CLI-M38). `--allow`,
`--deny` and `--allow-host` -- on `chat`, `execute`, `tui`, `complete` and
each `apogee acp` session -- and a task's grants are judged before any server
is started, wherever the verdict cannot depend on one: every name that does
not start with `mcp__`. So a typo, a tool the config denies, the agent or the
suite's pin leaves out, a tool that never asks, or a name that is not a
website is refused at once, in the same words as before, with no program of
yours started and no wait on a server that does not answer. A misspelled
tool's refusal lists the built-in tools that ask, then each server's by its
prefix, not started: `...; and mcp__xcode__*, whose server a refused run does
not start`. A name that starts with `mcp__` waits for the servers, the only
place that knows whether it exists and writes (and whether `permissions:`
binds it): they are started, and it is judged then, as before. So when several
flags are wrong, a refusal of one that is not an `mcp__` name is the one said;
`chat`'s `--input-format` without the matching `--output-format` is said
before a bad `-m` or a missing backend; and `complete`'s grant flags before a
bad `--image`, `--retriever` or `--rag`. On `complete --output-format
stream-json` a refused non-`mcp__` name comes before the `session` event, an
`mcp__` one after it. And `task run` and `complete`, whose suite pin is fixed
for the run, start no server at all when the pin leaves out the `mcp`
toolset; `chat` starts them as ever, since `/suite` and `/model` can move the
pin.

**What the gate shows is what it enforces** (57o). The file tools work inside
`tools.fs_root` (the launch folder when unset), and a path is judged by where
it lands: every link in it is followed, one whose target does not exist yet
included, so a link that leads out of the root is refused by every file tool
and nothing is written through it. A file writer's question names the file the
call writes, relative to the root -- `notes/../a.md` is asked about as `a.md`
-- and a path that lands outside is refused before anyone is asked. The git
tools never ask about a read, so they are confined instead: a `repo` the
model names must be inside the same root (with none, they use the repository
above the launch folder, as before), and so must the repository git opens
there -- a `.git` file, link or folder, a `core.worktree`, a `commondir` or an
`objects/info/alternates` that leads out of the root is refused, and so is a
link anywhere in the repository's git folder, or in an object store it borrows
from, that leads out (57y). The whole folder is judged -- loose objects,
refs, `shallow`, `config`, `reftable/` and the tables its `tables.list` names,
the files `git status` reads, each submodule's git folder under `modules/` --
all but the folders `hooks/`, `lfs/` and `annex/` at the top of each git
folder, which the tools never read. A link that lands inside the root is
followed and judged where it lands, and a folder in it that cannot be listed
is refused, since git can still open a file there by name. Since 57z the
same holds for what git reaches past the repository's own git folder: each
submodule git reads through the work tree -- its `.git` folder, file or link,
and its own config -- is held as the repository is, before `git_status` and a
diff against the work tree; and a file the repository's own config names for
git to open -- an `include.path` or `includeIf` include, `mailmap.file`,
`core.excludesFile`, `core.attributesFile` -- must be inside the root, when the
config file that names it is. A config file outside the root is yours, and so
is what it names: your global settings, and the repository above the folder
Apogee was started in. The checkout a launch in a subfolder sits in, a linked worktree and a submodule's
checkout still read. No git they start runs a command a repository's own
config names -- no `core.fsmonitor`, filter driver, external diff or textconv,
hook (from `.git/hooks` or a `core.hooksPath`), or signature check, nor
anything inside a submodule: a submodule shows as the commit ids it moved
between, never diffed, logged or summarised inside it, whatever
`diff.submodule` or `status.submoduleSummary` asks -- and none of their reads
writes the repository's index, nor fetches a missing object from a partial
clone's promisor remote (git 2.44 and later honor this) -- whatever your own
`GIT_NO_LAZY_FETCH` says, since the tools set it to `1` in place of yours.

**The review fetch stays in bounds** (57x). `git_diff` with `head` or
`base`, and `git_log` under `--branch` or `/branch`, fetch a ref that is not
here -- the one thing the git tools do that can leave the machine, so it
asks. `remote` must be a remote the repository has: a path, a URL or an
unknown name is refused (`'<remote>' is not a remote this repository has`).
Its URL is read and judged before git runs. A folder must be inside the
root, its repository confined as `repo` is -- so a store whose objects are
links out of the root is never fetched from, nothing copied in (57y) -- and
the folder must itself be
that repository's top -- not a plain folder a `.git` beside it (`sub.git`)
would make git serve from outside. A folder whose path holds `@[` or begins
with `[` -- in the remote's URL, or in the full path git is handed once links
are followed -- is refused too, since git reads that as a host and enters a
different folder. A folder is fetched from with no question, since nothing
leaves the machine. An `https`, `http`, `ssh`
(`user@host:path` too) or `git` URL is asked about **by its host, as a
website** -- the same gate `fetch_url` goes through: `tools.allowed_hosts`,
the session's answers, the prompt (`git_diff -> github.com`, the URL shown
under it with any password masked, `always` adding the host to the list),
and a no where nobody can answer, saying how to allow the host:
`--allow-host <host>` where the command takes it (`chat`, `execute`,
`complete`), `config add-allowed-host <host>`, or `always` when asked. Each
host is asked about once per call. Any other transport -- `ext::`, `fd::`, a
`<vcs>::` helper, `ftp://` -- and a URL or host beginning with `-` are
refused, as are an `ssh://` or `git://` path holding a `@`, `[`, `]` or `%`
and a user part holding a `[` or `]` (git would read either back into the
host, reaching one never asked about: `ssh://[other]@github.com/x` goes to
`other`). Your flags (`--branch`, `--remote`, `--fetch`) choose which remote
and when; they grant no host. Git then fetches **by that URL**, never by the
remote's name, with an explicit refspec into `refs/remotes/<remote>/<branch>`:
no `remote.<name>.uploadpack` or other setting of the remote applies, no
submodule is fetched into, no redirect is followed, and no bundle or
protocol-v2 packfile URI a server names on another host is taken --
`http.followRedirects`, `transfer.bundleURI` and `fetch.uriProtocols` are
each set off on every fetch, so only the judged transport, to the one host
asked about, is used. A repository whose own config -- including a file its
`include.path` pulls in -- sets something a fetch would run or follow
(`core.sshCommand`, `core.gitProxy`, `core.askPass`,
`core.alternateRefsCommand`, a `credential.helper`, a `url.<base>.insteadOf`
(an empty base, `[url ""]`, included), an `http.proxy`,
`http.curloptResolve`, `http.followRedirects`, `http.cookieFile` or
`http.saveCookies` -- which would write a cookie jar over a file -- or
`fetch.bundleURI`), or that sends a header naming another site (an
`http.extraHeader` that is a `Host` header, which a server holding several
sites at one address -- a shared host, a CDN, a reverse proxy -- would answer
from the site it names; or an `http.extraHeader` or `http.userAgent` holding
a line break or other control character, which would start a header line of
its own), is not fetched from, the refusal naming the key: fetch it
yourself, or remove the setting. Any other header the repository's own config
sets -- a credential, such as the `AUTHORIZATION` header `actions/checkout`
writes -- is sent, to the host asked about. A `GIT_CONFIG` in your
environment hides no such setting: only `git config` reads that variable,
and the tools' own config reads drop it, so they see what the fetch will
read, as they see a repository's filter drivers. Nor does a long config:
one whose listing of those settings, or of its filter drivers, runs past the
256 KiB the tools read whole is not fetched from, or not read at all, since
what it set first could not be judged. Your own global and
system settings and your environment (but for `GIT_NO_LAZY_FETCH`, above)
stay yours -- your credential helper,
your ssh and its config, your proxy and your own `insteadOf` all apply --
with three the `-c` switches above override whatever their scope: your global
`transfer.bundleURI`, `http.followRedirects` and `fetch.uriProtocols` are not
used by the review fetch, since the host asked about must be the host
reached -- though your own `http.<url>.followRedirects`, set for a URL, still
applies, to that server's redirects and the alternates it names, since git ranks a key that names a URL above the bare switch. One
rewrite of yours applies: a URL your rewrites would change a second time is
not fetched (`git would rewrite its URL again`), since git would then reach
an address nobody judged. A
fetch that is not made leaves the review to what is already here, said as
`Not fetched: ...` in the answer's header, or refused when the ref is not
here at all.
`--deny`, `/deny` and `/revoke` take every host `--allow-host` takes,
`localhost` and IPv6 literals such as `[::1]` included, and every host the
config lists or the session answered, whatever its shape: a listed `my_host`
is denied by its own name (CLI-M39). Any other name with a `_` and no dot is
read as a mistyped tool and refused, the refusal naming the spelling that
denies the website, `'my_host.' is the website my_host`. A toolset's word or
a tool's name is still taken as a website, so `--deny git` keeps refusing an
intranet host named `git`, but it says so, since the tool still runs:
`--deny shell` prints `'shell' is taken as a website; to refuse the shell's
tool, --deny run_command, or leave the toolset out with tools.disabled:
[shell]` (on stderr, and as a `notice` in machine mode), and in chat
`/deny shell` and `/allow shell` add the same pointer in their own words. A
host the config lists or the session answered is yours, and is said plainly.
`apogee config delete-allowed-host` removes what the list holds (CLI-M40):
an entry by how it is written -- a pasted URL or pattern, which allows
nothing and which `check` names with `apogee config delete-allowed-host
'<entry>'` as its remedy -- or a host by every spelling of it, so over
`Example.COM.` and `example.com` one `delete-allowed-host example.com` removes
both and the website asks again; its line names the host (and how many entries
went), or the entry and that it allowed nothing. A name no entry answers to
deletes nothing and exits 1.
In an older `config.yaml`, the host verbs read the list nested under its key
or compact at the key's own indent (`allowed_hosts:` then `- host` lines
beside it, as PyYAML and Ruby write it), and refuse by name an item written
over more than one line (`write each - host on one line`).
Every `config` verb edits an older `config.yaml` in the layout Apogee wrote
(CLI-M44): each level 2 spaces in from its key, a key written bare. A section
the verb edits in another indent -- a 4-space file, as Go's yaml.v3 writes it,
or a 1-space one -- or a key it looks for written quoted or spaced
(`"backends":`, `models :`) is refused, the file unchanged, naming the indent or
the key and `apogee config migrate`, after which every verb lands. A new
section goes before a final `...`; a file whose second document holds content,
or one saved as UTF-16, is refused naming the hand edit or the encoding. An
edit whose result would not load says it was not applied and why.
`add-allowed-host`, `delete-allowed-host` and `set-permission` print a success
only when Apogee reads the change back from the edited file (CLI-M43): where
the file writes the list or the level where the editor cannot see it -- a key
written twice, or in YAML's explicit `? key` form -- the verb exits 1 saying the
change would not take effect, the file unchanged, and names `apogee config
migrate`. It writes no second copy beside the one Apogee reads, even when the
level or the host already reads as asked. A host still named by an entry
written through a variable (`${MY_HOST}`), which Apogee expands, is named for
you to edit by hand.

**The question shows what will run, whole and as it is** (57p). The prompt
for `run_command` shows the whole command, however long, and the folder it
runs in (`cwd: …`); an MCP tool's shows its whole arguments -- on the
terminal, in the full-screen shell, and in machine mode's `detail`. Whatever
the model wrote is drawn, never obeyed: a control character in a command, a
path, a question or an option shows as U+FFFD, so a carriage return and an
erase cannot draw a harmless command over the real one, and each line of a
long command sits indented under the prompt.

**`apogee serve --tools` offers this same set, whole** (57n): every native
toolset, `fetch_url` (and `web_search` where configured), `todo_write` and
every enabled MCP server -- the registry `chat` builds, so files under
`tools.fs_root` (the launch folder when unset), notes and git history are
readable by any client of the port. Nobody is there to answer a prompt, so a
tool that writes runs only where `permissions:` says `allow` -- an `[a]lways`
given in chat counts -- and a website only when `tools.allowed_hosts` lists
it. The startup line says both: `-- tools: <n> (files under <root>, shell, git,
…)` and `-- runs unasked: <tools>` or `none`
([http-api.md](http-api.md#starting-the-server)).

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
backend run with `--search`, or whose entry lists `server_tools:
[web_search]`, also has its provider's own server-side search, and the model
may use either. The provider's search reaches sites from the provider's
servers, where `tools.allowed_hosts` cannot see, which is why it is consent
you give per run or per entry.

## MCP presets: the servers Apogee ships

A fresh install configures no MCP server, and `apogee mcp list` says where to
start: `apogee mcp presets` lists the servers Apogee ships, each with what it
provides and whether this machine can run it.

```sh
apogee mcp presets                         # what ships, and where each stands here
apogee mcp create code --from code-exec    # register one by name
apogee mcp create xcode --from xcode       # Xcode's own tools, on a Mac with Xcode 26.3+
```

`--from` registers exactly the command and arguments the preset resolves to
here, through the same path a typed `--command` takes, so the config is byte
for byte what typing them would write. A preset this machine cannot run still
lists, saying what is missing and the command that provides it; `--from`
refuses it with the same words. The listing is also a document (`--output-format
json`, in [machine-mode.md](machine-mode.md)), and the admin plane's `POST
/v1/admin/mcp-servers` takes `from` the same way.

| Preset | What it is |
|---|---|
| `code-exec` | The code-execution server below: `run_python` and `write_input`, registered as `code`. It needs the server seeded (`apogee check --fix`) and a Python: the training environment's, else `python3` on PATH. |
| `xcode` | Xcode's own tools -- build, run, tests, previews, documentation search, the issue navigator, file reads and writes (`BuildProject`, `GetBuildLog`, `RunAllTests`, `XcodeRead`, `XcodeListWindows` and the rest, as Xcode lists them) -- served by Xcode 26.3 or later through `xcrun mcpbridge`, registered as `xcode` with `xcrun`'s absolute path. It lists everywhere and registers on macOS only (`Xcode runs on macOS only` elsewhere). Its note says what Apogee cannot see: Xcode must be running with the project open, its Settings › Intelligence › Model Context Protocol › "Allow External Agents to Use Xcode Tools" on, and the first connection asks you in Xcode to allow the binary -- asked again after every install or update, since Xcode approves an agent by its code signature and hash and Apogee's macOS builds are ad-hoc signed. What the bridge does with Xcode closed is not yet measured (the first live run records it); either way a server that does not answer is skipped at a run's start, never fatal. `apogee check`'s `Xcode` section says whether this Mac's Xcode has the bridge. `apogee bootstrap` never registers it: that is yours to do, once Xcode is set. |
| `self` | Apogee's own read-only tools (files, git, notes, document search, graph navigation) and its status reads -- and `complete`, where you list it on a local backend -- served on stdio by [`apogee mcp serve`](#apogee-as-an-mcp-server-apogee-mcp-serve) **for another MCP host**: a desktop assistant or an editor takes the entry the listing prints. It is never registered into Apogee, which already has these tools natively, so `--from self` is refused with that reason. |

Web search is not a preset. It stays yours: point Apogee at a SearXNG
instance you run, as [*Web search through SearXNG*](#web-search-through-searxng)
above walks through.

## Servers an ACP host hands a session

An agent host that runs Apogee over the Agent Client Protocol -- Xcode, through
[`apogee acp`](acp.md) -- may name MCP servers of its own when it opens a
session (`session/new`'s `mcpServers`). That is how a host gives its agent its
own tools: Xcode's are its build, test, preview, documentation and file tools,
served by `xcrun mcpbridge`. Apogee connects them **for that session alone**:

- **Stdio servers are taken.** Each stdio entry -- its name, command, arguments
  and `env` -- is connected by the session beside the config's own servers,
  before `session/new` answers, under the same 20-second bound on connecting and
  the handshake. Its tools reach the model as `mcp__<server>__<tool>`, the name
  made a server name by `mcp create`'s rule (`.` and `:` become `-`). An entry
  of the same name as a config server (compared without case), or with the same
  command and arguments (a bare command found on `PATH` first), sets the
  config's aside for that session: the host's copy carries the session's own
  binding, and two copies would show the model every tool twice.
- **Nothing else is.** An http or sse entry is never dialled -- Apogee's MCP
  client speaks stdio, and `initialize` advertises neither (`mcpCapabilities:
  {http: false, sse: false}`) -- and is named in the session's notice:
  `Apogee connects stdio MCP servers only; skipped: <names>`. So is an entry
  whose name `mcp create` would refuse, a second entry of one name, one whose
  `env` names a variable that is empty or holds `=`, and a server that will not
  start, dies, or never finishes its handshake -- with its last stderr line. A
  server skipped is never fatal: the session opens without it. Everything set
  aside is said in **one** notice -- shown by the host, or leading the next
  answer's thinking, and on `acp`'s stderr.
- **The session's, never yours.** The config file is never written -- byte for
  byte the same afterwards -- and `apogee mcp list` never shows a host's
  server. Nothing of them is saved with the session, and the next session sees
  only what its own `session/new` names. When the session ends -- a newer one
  replaces it, or the host closes `acp` -- its servers end with it.
- **The values are the host's.** An `env` value is a credential by the
  protocol's word: it reaches the server's process and nothing else -- never a
  command line, a log line, a notice or stderr, which name a server, its command
  and its variables' names. It is passed as given: `${VAR}` and `~` are expanded
  in the config's entries, never in a host's.
- **The gate is Apogee's.** A host handing over a server is not you agreeing to
  its calls. Each of its tools is gated as any MCP tool is: one its server marks
  `readOnlyHint` runs unasked unless `permissions:` gives it `deny` or `ask`
  (57o: those bind whatever the hint says), and every other asks through the host's
  permission sheet -- in accept-edits mode too, which never covers an MCP tool.

**Xcode asks too.** The first time an agent reaches Xcode's tools, Xcode shows
its own prompt ("Do you want to allow the agent …"), answered in Xcode -- Apogee
never answers or hides it. Xcode ties the approval to the agent's binary, which
Apogee does not sign, so expect it again after every install or update of
`apogee` (or `apogee-dev`).

**If Xcode names no server** in `session/new`, its tools can still reach every
session from the config: register `xcrun mcpbridge` as an ordinary server --
by name, `apogee mcp create xcode --from xcode` (the [`xcode`
preset](#mcp-presets-the-servers-apogee-ships), 56d). It needs Xcode's own
switch, "Allow external agents to use Xcode tools" (Settings › Intelligence ›
Model Context Protocol), turned on, and the project open in Xcode.

## Apogee as an MCP server: `apogee mcp serve`

`apogee mcp serve` makes Apogee an MCP server for another host: a desktop
assistant, an editor, a Claude plugin. The host spawns it and speaks MCP's
JSON-RPC over its stdin and stdout until it closes stdin. It is pipes only:
no port is opened, and stdout carries the protocol and nothing else. The
entry a host takes is the one `apogee mcp presets` prints for `self`:

```json
"apogee": {"command": "/path/to/apogee", "args": ["mcp", "serve"]}
```

with this machine's path to the binary filled in (`--output-format json`
gives it as `host_entry`). `apogee mcp serve --help` says the same.

**What it serves.** Two kinds of tool, every one read-only
(`readOnlyHint: true`), and a third you switch on yourself. Nothing writes,
and nothing asks a question, since a server has nobody to ask. A model
generates text only where your config lists it ([*Calling a local model:
`complete`*](#calling-a-local-model-complete), below). The one other model
call is `search_documents` embedding a query (next):

- **The native read-only tools**: `read_file`, `list_directory`,
  `search_files`, `grep_files`, the git reads (`git_status`, `git_log`,
  `git_diff`, `git_show`; not served by the Windows build, which cannot start
  git yet), the notes reads (`list_notes`, `read_note`),
  `search_documents` and the collection reads (`list_collections`,
  `collection_info`), and the four [graph tools](#navigating-a-knowledge-graph).
  These are the same tools a tool-using run offers a model, with the same
  `tools:` settings: `fs_root` is the folder the file tools start in, and
  `tools.disabled` switches a toolset off here too. A served `git_diff`'s
  review fetch reaches a network remote only when `tools.allowed_hosts`
  lists its host (57x): nobody is there to be asked. The writers (`write_file`,
  `edit_file`, `write_note`, `run_command`, ...) are withheld, because
  Apogee's permission gate lives in its own loop. `search_documents`
  searches as it does for a model in a chat (since 54b): its retriever is
  decided by the one resolver, with the collection's embedder, so a
  collection ingested with vectors is searched by vector, and `retriever`
  takes `vector` and `hybrid` as well as `lexical`. Before 54b the server
  reached no embedder, so every search ran lexical. A vector search embeds
  the query through the collection's embedder: one small model call per
  search, made on the host's initiative. When that embedder is a paid one,
  the call is billed, under the same rule a chat's search keeps (a question
  against a collection already built with a model may embed through it;
  vectorising a whole collection through a paid embedder needs your yes).
  A search asked for `lexical` makes no model call at all. A collection is
  named, never pathed (CLI-M45): `search_documents` and `collection_info`
  take any name `list_collections` lists -- a spaced or a long one included
  -- and refuse a path (`../x`, `/x`) in `embed`'s words, `'../x' is not a
  plain collection name`. Every other reader holds the same rule: `--rag` on
  `complete`, `chat` and `analyze`, the config's `auto_rag`, an agent's
  collection and `serve --rag` refuse a path before anything is opened, so
  no flag or config value can make Apogee open a database outside its home.
- **The status reads** (since 54a). Each one answers with the JSON document
  its command prints under `--output-format json`, byte for byte, minus the
  newline that ends the command's output:

| Tool | Arguments | The document of |
|---|---|---|
| `models_list` | `all` (boolean) | `apogee models list [--all]`: every configured backend and stored model |
| `models_status` | `suite` (a suite, or `off`) | `apogee models status [--suite]`: which backend each role resolves to |
| `train_status` | none | `apogee train status`: the cycle, the active pipeline, the newest runs, the active versions |
| `train_versions` | `backend` | `apogee train versions [<backend>]`: the promoted versions, every ledger or one |
| `check` | none | `apogee check`: every row with its status, detail and remedy, and the counts. Nothing is repaired: `check --fix` stays yours. |

The shapes are in [machine-mode.md](machine-mode.md#everything-else-is-a-cli-command), since they are the
very documents a GUI renders. Each read runs at its call, so a host asking
after a promotion sees the promotion. A read that cannot answer returns an
error result (`isError: true`) with the command's own reason after
`Error: `: a config that will not load, a backend with no version history,
an unknown suite, or an argument of the wrong type, named.

**The tool set only grows.** Tools are added and never renamed or removed,
and a new Apogee keeps every tool an older one served. This is the same
promise the machine protocol makes ([ADR 0006](../../adrs/cli/backwards-compatibility.md)).
A host registered against an older Apogee keeps working, and the new tools
appear in its next `tools/list`. The server still answers to its earlier,
hidden spelling, `apogee __mcp-tools`. A host registered with it before 54a
needs no change and gets the same tools.

### Calling a local model: `complete`

Since 54b, the server can hand a host's prompt to one of your local models
-- the one-shot `apogee complete -m <backend> "<prompt>"` runs -- as the
tool `complete`. It is off until you list it, backend by backend, in the
backend's entry:

```sh
apogee config set-backend local --mcp-serve complete
```

which writes one line, `"mcp_serve": ["complete"]`, through the one config
editor (`--mcp-serve ""` takes it back). That line is your consent for a host
to run that model. The rules:

- **Absent until listed.** With no entry listing it, `complete` is not in
  `tools/list` at all. A host cannot pass a flag that opens it, and calling
  it anyway answers `Error: no such tool 'complete'`.
- **Local and unmetered only, by the provider's own word.** A served
  generation runs on the host's initiative, which never spends: only a
  backend whose provider says it runs locally and bills nothing per call (a
  llama.cpp or MLX model, today) may list it. `config set-backend` asks the
  provider, and refuses a cloud API, a vendor CLI or a backend that cannot
  be built here, naming the rule, with the file untouched. It asks of the
  entry exactly as the edit writes it, so an edit that moves the entry's
  type or path under a listing is asked again, whatever else the same call
  changes. A listing written by hand on a backend that bills is left out
  when the server starts, and said on its stderr.
- **Bounded.** A call generates at most 1024 tokens, or the backend's own
  `max_tokens` when that is lower. A host's `max_tokens` argument asks for
  fewer, never more, and a reply that reaches its bound ends there.
- **The command's own call.** A call runs the core `apogee complete` runs
  with the backend's own system prompt and sampling, and nothing more: no
  tools, no retrieval (not even `auto_rag`), and no call to the utility
  model, so a thinking `auto` is decided by rule. Its answer is the text
  `apogee complete -m <backend> -n <bound>` prints.
- **Read again at each call.** The config is read when a call arrives: a
  listing taken back since the server started is refused with that reason,
  and nothing runs.
- **Narrated on stderr.** Each call says `complete on '<backend>', at most N
  tokens` and how it ended on the server's stderr, the stream a host
  captures. stdout stays the protocol's.

The tool's description tells the host's model the same: what one call is,
the backends it may name with each one's bound, that the spend is this
machine's time and never money, and why it is offered. Its arguments:

| Argument | Required | Takes |
|---|---|---|
| `prompt` | yes | What the model is asked |
| `backend` | yes | One of the listed backends (the schema's `enum`) |
| `max_tokens` | no | 1 to the backend's bound; the bound when omitted |

A refusal is an error result in the CLI's words after `Error: `: a backend
the tool does not name, a bound out of range, a listing taken back, or the
backend's own failure.

`complete` is the one call there is. Each further one would be a deliberate
listing of its own, with its own rules.

## Running code: the code-execution server

`run_python` runs Python for any model, local or cloud, the way ChatGPT's
interpreter does: code goes in, and what it printed and the files it wrote
come back. It is an MCP server Apogee ships, `code_exec_server.py`, seeded
with the drivers under the data directory's `training/scripts/` by `apogee
check --fix`. It is registered like any other server, in one command:

```sh
apogee mcp create code --from code-exec
```

which is the same as typing the line `apogee check` prints with this
machine's paths filled in:

```sh
apogee mcp create code --command python3 \
    --args ~/.apogee/training/scripts/code_exec_server.py
```

Its tools then reach every tool-using run as `mcp__code__run_python` and
`mcp__code__write_input`. Code runs under the training environment's
interpreter when there is one (`apogee train setup`, which is where packages
like numpy belong), else under the interpreter running the server. Name it as
`--command` if you want that one always.

### What the model gets

- **`run_python`** takes `code` and runs it as a script in the session's
  working directory. It returns stdout, then stderr under `[stderr]`, then
  `[exit N]`, then `[files written]` with each new or changed file's full
  path and size. An error comes back as the interpreter's own traceback.
- **`write_input`** takes a relative `path` and `content` (text, or bytes
  with `encoding: base64`) and writes a data file there for code to read.

Files persist between calls. Variables do not, since each call is a new
process, so code that the next call builds on saves to a file.

### The sandbox, said plainly

| What | How |
|---|---|
| Working directory | New for each session, under `cache/code-exec/`, and **deleted when the session ends**: copy out anything worth keeping. A server that was killed leaves its directory behind, and the next one removes it at start. |
| Wall clock | 30 seconds a run. Then the code and every process it started are stopped, and the result says so. |
| Output | The first and last 8 KB, like `run_command`, with how much was cut between them. |
| Disk | 256 MiB in the working directory. A run that goes past it has what it wrote removed, and is told; a `write_input` that would is refused. |
| Network | The code cannot import `socket`, so Python's own networking (urllib, http.client, requests) fails with a message saying the network is off. |
| Writes | Code that writes, deletes, makes a folder, changes a mode, owner, date or extended attribute, renames or links outside the working directory is refused, naming the path. A link inside the working directory that points outside can be removed or renamed, since that changes only the link. Its mode, owner and dates cannot be changed, even without following it, so copying one with its metadata (`shutil.copytree(..., symlinks=True)`, `copystat`, `copy2(..., follow_symlinks=False)`) reports an error, though the copy is made. |
| Environment | An allow-list: `PATH`, the locale, the time zone. `HOME` and the temporary directory point at the working directory, so no key in Apogee's environment reaches the code. |

The network and write guards are **best effort, not a boundary**: a
subprocess or a C extension can get around them. The write guard does not
judge a call relative to a directory descriptor or made on an open one
(`os.chmod(fd, ...)`), `os.mkfifo` or `os.mknod` (Python raises no audit
event for them), or what standard-library modules write through their own C
code (`sqlite3`, `dbm`); and on Python 3.8.0 or 3.8.1, which raise almost
none of the os module's filesystem audit events, it judges little but `open`
and `os.truncate`. The boundary is the permission gate. Both tools declare
that they write, so each call is asked like `run_command`, or allowed for a
run with `--allow mcp__code__run_python`, or for good with `permissions:` in
the config. The session's permission mode governs them too: plan and dont-ask
refuse them, and accept-edits does not pass them unasked.

The server speaks MCP over pipes, never a port, and its stderr is captured
like any server's. The limits are constants at the top of the script, and
`--max-seconds` and `--max-session-bytes` in the registration's `--args`
change them for that entry.

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

## Delegating a piece of the job

`delegate` lets the model hand a self-contained piece of a longer job -- reading
through many files, a search across the project, a side investigation -- to a
fresh sub-session, and keep only its conclusion (41e). It takes `brief`, the
whole task, and optionally `role`. It is offered wherever `--tools` is, in
`chat` and `execute`, with no suite needed.

The sub-session sees **the brief and nothing else** of the conversation, so the
model is told to put everything the task needs into it. It gets:

- **your tools, less `delegate`** -- one level deep, never a delegate of a
  delegate;
- **the session's own model**, or with `role` that role's model through the
  same resolver every member call uses;
- **the session's own gate, whole**: your permissions and the session's
  answers and mode -- a prompt reaches you where the session's would, with
  nobody to ask it is a deny, and a plan-mode session's delegate plans too;
- the session's project context, and its cancellation: Ctrl-C stops it
  mid-step.

### The bounds

| Bound | Value | Over it |
|---|---|---|
| Model calls | 8 | It answers without tools, and its result says it stopped at the cap. |
| An answer's length | 4,096 tokens per call | Cut there. |
| A role's model | never one billed per call | Refused with the reason, as `consult` refuses one: a delegation is the model's initiative, so it never spends money. |

You watch it in the thinking block as one line --
`delegate — a sub-session on <model>: <brief>` -- with its tool lines inside
it; machine mode says them as `tool_status`. Its result is its answer and a
one-line account of what it spent, such as `[delegate on root: 3 model calls,
tools read_file ×2, 1,840 tokens]`. That result, in the session's own record,
is all that is kept: no second chat appears in `chats list`.

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
them, and with none, it says how to build one. Either is a name, never a path
(CLI-M45): a path is refused, and so is a named graph whose member is written
as one.

**Always there, never asking.** The toolset is registered with the others
(switch it off with `tools.disabled: [graph]`, or leave it out of a suite
member's `toolset:`); tool selection (26g) keeps the four off the menu for a
question they do not fit. None writes or reaches out, so the permission gate
never asks about one, on any surface -- and `apogee mcp serve` serves all
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

