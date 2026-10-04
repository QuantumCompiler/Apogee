# Shell completion and store-aware arguments

**What / why.** The user's delete transcript (2026-10-03) shows the gap twice in three commands: `apogee models delete gemma-4-E2B-F16` — the backend's name, exactly as `models list` prints it — was refused ("no model 'gemma-4-E2B-F16' in /Users/taylor/.apogee/models"), and the store path that *does* work (`google--gemma-4-E2B/gguf/513ee1b91245`) had to be hunted by hand; then the accepted command's own warning proved the mapping exists the other way ("backend 'gemma-4-E2B-F16' points into this and will stop working"). Registration is the same friction inverted: `config add-backend` hand-types a name, `--type llamacpp` and a `--model-path` the store's metadata already knows. Three fixes, one item: **(a) shell tab completion** — `apogee models delete <TAB>` completes backend names and store entries from the live install, `apogee config add-backend <TAB>` completes the names `models list` shows — through a hidden `apogee __complete` resolver (the `__mcp-tools` hidden-verb precedent) called by thin zsh/bash scripts that `apogee completion <shell>` prints; **(b) `models delete` accepts a backend name**, resolving through the backend's configured model path to the store entry it points into, then running today's exact will-remove / backend-warning / `--yes` flow; **(c) `config add-backend <name>` auto-fills from the store** when the name matches a store entry — type from the format (`.gguf` ⇒ `llamacpp` today, resolvable to other types as formats arrive), model path from the entry's metadata — with explicit flags always winning.

**Core constraint(s).**
- **The resolver is read-only and instant.** `__complete` reads config and the store's directory records only — never a GGUF header, never a model load, never the network (the M2 lesson: a listing-path's cost is system calls, and completion runs on every keystroke). No state is written, nothing listens.
- **One config-mutation path:** the auto-filled registration goes through the same add-backend writer and editor as a hand-typed one — the fill composes arguments, it never grows a second writer.
- **Delete's safety flow is untouched:** backend-name resolution feeds the existing confirm text, the pointing-backends warning, and `--yes`; nothing is removed with fewer words than today. A name matching both a backend and a literal store path is taken as the store path (today's meaning), and the collision is said.
- **The hidden verb is a stable contract:** the emitted scripts call `__complete` across binary upgrades, so its output shape is versioned like `__mcp-tools`' surface — additive only — and it stays out of `--help` like the other `__` verbs.
- **Scripts are printed, never installed:** `apogee completion zsh` writes to stdout and the user sources it where they choose — nothing touches shell rc files on Apogee's initiative (Ommi's `completions/` convenience was explicitly NOT installed, and the no-silent-config spirit holds).
- Code style carries: `.h`/`.cpp` pairs, smart pointers only; captured child output where any exists.

**Seam + files.**
- `commands/completion.cpp/.h` (new): the `completion <shell>` emitters (zsh first, bash beside it) and the `__complete` resolver — pure candidate computation over an injected config + store view, table-testable without a shell.
- `commands/models_cmd` (delete): the backend-name → store-entry resolution ahead of the existing flow — the inverse of the pointing-backends lookup the warning already does.
- `commands/config_cmd.cpp` (add-backend): name → store-entry match → `--type`/`--model-path` defaults from the entry's format and metadata; explicit flags override; a non-store name behaves exactly as today.
- Reuses: the store's listing/metadata reads (shipped, M2-fast), the config reader, the M4-lineage display names (completion candidates are the names `models list` prints).
- Tests: `tests/commands/` resolver tables (words → candidates, against a scripted store + config: registered, unregistered-GGUF, collision, empty store); delete-by-name incl. ambiguity and the unchanged `--yes` flow; add-backend auto-fill round-tripped through the editor with comments preserved; golden zsh/bash script output.

**Reference (Ommi).** Ommi's CLI had a `completion` subcommand for free — cobra generates shell completion for every verb — and a manually-sourced developer convenience (`lib/cli/completions/ommi-make.zsh`, documented "NOT installed"). Apogee is CLI11, which generates nothing: the port is the *capability* via the hidden-resolver pattern, not the framework. The not-installed convention is adopted deliberately.

**Decisions made** (dated):
- 2026-10-03 — Asked for by the user from the delete transcript, placed in **Maintenance** at their direction. Takes **M7** by the ever-assigned rule: M5 and M6 were assigned, vacated by the day's moves, and live on in dated history notes — reusing them would make those notes ambiguous.
- 2026-10-03 — Candidates come from the live install at completion time (config + store reads), never from a cached list that can go stale between a pull and a delete.
- 2026-10-03 — `.gguf` ⇒ `llamacpp` is the whole type map today, by the user's note ("in the future it can be resolved to other types too"): the map lives beside the store's format knowledge, one row per format, so MLX's `mlx/` row (31b) joins it without touching this code path.

**Open calls:**
- [default: zsh and bash ship in this item; fish is unscheduled until asked] Shell coverage.
- [default: an unregistered store GGUF's candidate name is its filename stem, matching what `models list` shows for it] Name derivation.
- [default: completion also covers the obvious neighbors sharing the resolver — `models info`/`models status` backend names, `chat -m` — where it is free; anything needing new candidate logic is out of scope] Free riders.

**Guardrail(s).**
- Resolver tables over the scripted store: every state in the matrix yields exactly the expected candidates, in list-print order, with zero header reads (asserted by the injected view's call counts).
- The transcript, inverted: delete-by-backend-name resolves to the entry the warning named, and the confirm/`--yes` text is byte-identical to today's.
- Auto-fill writes exactly the entry a hand-typed command would have, comments preserved (editor round-trip test).
- The emitted scripts are golden-tested; `__complete`'s output shape is pinned (additive-only, the machine-surface discipline).

**Acceptance criteria:**
- [ ] `apogee models delete gemma-4-E2B-F16` (a registered backend) prints the same will-remove text the store path gets, warning included, and `--yes` removes the same entry.
- [ ] `apogee __complete` for `models delete` lists the backend names and store entries of a sandbox install; for `config add-backend`, the names its `models list` shows.
- [ ] `apogee config add-backend gemma-4-E2B-F16` with no flags writes a `llamacpp` entry pointing at that store file, through the one editor.
- [ ] `apogee completion zsh | source /dev/stdin` in a zsh completes both verbs against a temp `APOGEE_HOME`; nothing is written anywhere by printing it.

**Scope note.** **Maintenance item M7**; gated on nothing pending. Out of scope: installing completion scripts into rc files; fish; completing every verb (the resolver's architecture allows it; this item wires the verbs the transcript showed plus the free riders); non-GGUF type inference beyond the format map's rows.
