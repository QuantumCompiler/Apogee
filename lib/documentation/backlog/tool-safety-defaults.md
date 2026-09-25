# Tool safety defaults: ask before a new website, work in the launch folder

**What / why.** Two defaults that decide what a model with `--tools` can reach without anyone being asked.

1. **Outbound fetches ask per website.** `fetch_url` asks the first time a conversation reaches each new host, using the gate's existing `[y]es / [n]o / [a]lways / [s]ession` prompt. `always` records the host in the config, so a known site stays fast.
2. **The file tools work in the folder Apogee was started in.** `tools.fs_root` unset today means the home directory. It will mean the process's working directory at launch, like a coding assistant's workspace. `tools.fs_root` still overrides it.

Today `read_file` and `fetch_url` are both read-only in the gate, so neither ever asks, and file access reaches the whole home directory. A model with tools on can read a file and send its contents out inside a URL, with no prompt at any point. A web page carrying hidden instructions is enough to trigger that. The exposure already exists for every cloud backend run with `--tools`. It would reach local models the day [local tool calling](local-tool-calling.md) lands, and [web search](web-search-searxng.md) multiplies the untrusted pages a model reads. Found by the local-tools spike (2026-09-25).

**Core constraint(s).**
- **One config mutation path.** `always` writes the host through a new `harness/config_edit.h` transform, byte-exact against the shipped template. Nothing else writes the file.
- **The gate on every surface, and nobody to ask means deny.** On a pipe, on `serve`, and in a machine-mode session without a driver, an unlisted host is refused. A host already allowed in the config passes. Machine mode's existing `question` event (`"kind":"permission"`) carries the host as its target.
- **`check` never edits config.** It reports the allowed hosts and the effective file root.
- **Parity.** The allow-list gets an admin twin beside `PUT /v1/admin/permissions/{id}`, byte-identical to the CLI edit.
- **Redirects are hops, and each hop is a host.** The shared `HttpClient` follows redirects itself (`CURLOPT_FOLLOWLOCATION`). The fetch tool must follow them one at a time instead, so a redirect to a new host is asked about like a direct fetch. Otherwise the guard is one 302 away from useless.
- **Exact hosts.** `docs.python.org` does not admit `python.org` or `evil.docs.python.org.example`; the comparison is on the parsed host, never a substring.

**Seam + files.**
- `agent/tool.h`: the gate already passes `(tool, target)` to `PermissionChecker` and asks `Tool::describe_target` for the target. `fetch_url` gains a target (its URL's host) and enters the gate. Either it declares `writes`, or a new `Tool::outbound` flag keeps "writes" meaning what it says; pick one and record it.
- `agent/fetch_url.h/.cpp`: redirects followed manually with each hop's host passed back through the gate, and a download size cap (today the whole body is read before the 8 KB cut).
- `commands/permissions.h/.cpp`: `make_permission_checker` resolves `fetch_url` by host (the config's allow-list, then the session's per-host answers, then `Ask`). `terminal_confirm_fn` and `make_driver_confirm_fn` show the host. `always` writes the host.
- `commands/helpers.cpp`: the fetcher passed to `make_fetch_url_tool` stops following redirects.
- `harness/config.h/.cpp`, `harness/config_edit.h/.cpp`, `harness/config_template.cpp`: `tools.allowed_hosts` (a list, empty in the template with a comment explaining it), plus an append-host transform.
- `tools/toolsets.cpp`: `fs_root` unset resolves to the working directory captured at startup, not `platform::home_directory()`.
- `commands/check.cpp`: the `Tools` section shows the allowed hosts and the effective file root and where it came from.
- `httpserver/`: the allow-list's admin twin; [http-api.md](../reference/http-api.md) documents it and its conformance check pins it.

**Reference (Ommi).** Ommi's `fetch_url` (`src/tools/web.go`) fetched anything with no gate, and its sandbox defaulted to the home directory; both were ported to Apogee as they were. Ommi had no outbound guard, so this is new ground. It follows the shape of the permission gate Milestone V built.

**Decisions made:**
- 2026-09-25 — **Ask per new website** (the user's call, over asking only once a file has been read, an allow-list with no prompt, or no guard). A known site stays fast, and a new one is always a visible choice.
- 2026-09-25 — **The file tools default to the launch folder** (the user's call, over keeping the home directory). A chat started in a project works in that project; `tools.fs_root` still widens it.
- 2026-09-25 — First in the local-agent-tools track, because it closes an exposure that exists today and every later item in the track widens it.

**Open calls:**
- [default: a new `Tool::outbound` flag] rather than marking `fetch_url` as `writes`. Outbound is a different risk from mutation, and `read-only` agents (Milestone X's policy filter) should decide separately whether they may reach the network.
- [default: `always` writes the host to `tools.allowed_hosts`] and `session` remembers it for the process only, mirroring what `always` and `session` mean for a tool today.
- [default: a surface with nobody to ask fetches only from `tools.allowed_hosts`] That is a behaviour change for `serve --tools`, which fetches anything today, and it is recorded in http-api.md.
- [default: the configured search provider's host (see [web search](web-search-searxng.md)) is trusted by configuration] because the user named it. The pages a search returns are ordinary fetches and ask.
- [default: `chat --resume` uses the folder it is resumed in, not the one the session started in] The file root is a property of the process, not of the transcript.

**Guardrail(s).** Each mutation-tested in the style of Milestone V:
- A fetch to a new host asks, and a second fetch to the same host in the session does not.
- `always` lands in the config through the editor, byte-exact.
- A pipe, `serve`, and a driverless machine-mode session refuse an unlisted host and fetch a listed one.
- A redirect from an allowed host to a new one asks, and its refusal fetches nothing.
- The host comparison refuses the prefix and suffix tricks.
- `fs_root` unset resolves to the launch directory, and set overrides it.
- `check` reports both, and the admin twin is byte-identical to the CLI edit.

**Acceptance criteria:**
- [ ] With `--tools` on a terminal, a model's first fetch to a host prompts, naming the host; `session` and `always` behave as described; and a refusal becomes a tool result the model reads, never a failed turn.
- [ ] `apogee complete --tools` on a pipe fetches only from `tools.allowed_hosts`.
- [ ] A redirect to an unlisted host is asked about (terminal) or refused (pipe), hop by hop.
- [ ] With `tools.fs_root` unset, `read_file` outside the launch folder is refused, and the refusal names the root and how to widen it.
- [ ] `apogee check` shows the allowed hosts and the effective file root; `cli.config_lifecycle` round-trips an `always` answer.

**Scope note.** Item **25a**, the first of the local-agent-tools track; gated on nothing. Out of scope: taint tracking (asking only once private data has been read), per-path file permissions, and guarding MCP servers' own network use, which happens in their processes, not in Apogee's.
