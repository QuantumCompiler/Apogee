# Attachment options and defaults

**What / why.** Attaching has no option channel: `/attach takes a file, a folder or a glob` (`cli/chat.cpp`), an `@` mention is a bare path by contract (`mentioned_paths` — "The message itself is never changed"), `complete --attach` takes specs only, and no `attachments:` block exists in config. That was fine while attach did one thing; with [27n](attachment-code-graph.md) it does two, and the user asked for the method to be "set with defaults or overridden with inline commands." This item is that surface: **`--graph=code|off`** accepted by `/attach` and by `complete --attach` (per invocation), a new **`attachments:` config block** holding the default (`graph: code`), written only through the comment-preserving config editor, and completion for the flag through the one command table. The `@` mention **stays a bare path** — the spike's recorded call: the mention contract promises the message reaches the model unchanged, so flags typed there would arrive as prose; a user who wants the non-default method uses `/attach` (one Tab away, 24's completion).

**Core constraint(s).**
- **One config-mutation path:** the `attachments:` block is read by the config loader and written only by the comment-preserving editor — no second writer, per the standing invariant.
- **Flag beats config, config beats built-in** — the resolution is one function, table-tested, and the attach notice says which method ran when it isn't the built-in default.
- **The mention contract is untouched:** `@` parsing, completion (24) and the message's bytes stay exactly as shipped; this item adds no mention syntax.
- **Completion through the one command table** (the 26o/24 precedent): `/attach` completes `--graph=` and its values from the same table every other slash command uses — no bespoke completer.
- **`off` is honest:** an attach with `--graph=off` (or config `graph: off`) indexes chunks exactly as today and says nothing about graphs; a later `/attach` of the same folder with `code` builds it then.
- Code style carries: `.h`/`.cpp` pairs, smart pointers only.

**Seam + files.**
- `cli/chat.cpp` — `/attach` argument parsing grows the flag (spec stays first, flags after; a quoted path with spaces keeps working).
- `cli/complete.cpp` — `--graph` beside `--attach`, applying to that invocation's attaches.
- `cli/chat_completer.cpp` — the flag and its values in the command table.
- `contracts/config.h` / `config.cpp` — the `attachments:` block (`graph: code|off`, default `code`); `contracts/config_edit.cpp` — its editor entry, comment-preserving like every other.
- `cli/chat_attachments.cpp/.h` — `attach()` takes the resolved method; the resolution function lives here, pure.
- Tests: `tests/data/contracts/config` round-trips of the block (comments preserved); `tests/presentation/commands/` parse tables for `/attach` (paths with spaces, bad values refused with the valid set named); the precedence table.
- Consumes: [27n](attachment-code-graph.md) (the method the option selects — including its `complete`-skips-graph default, which `--graph=code` overrides); 24 (shipped: completion); 26o (shipped: the flag-completion precedent).

**Reference (Ommi).** No analog — Ommi has neither chat attachments nor per-command option completion of this shape; the config-block-plus-editor pattern is Apogee's own standing mechanism.

**Decisions made** (dated):
- 2026-10-03 — Split from the attachment-representation spike; gated on 27n because an option with nothing to select is dead weight, and kept apart from it so the build/wiring items stay single-session sized.
- 2026-10-03 — **Flags live on `/attach` and `--attach` only; `@` stays bare** (the user delegated the call; recorded as the decision): the mention contract's "message never changed" promise outranks the convenience, and `/attach` already completes.
- 2026-10-03 — The value set is `code|off`, deliberately small: `code` is 27n's model-free build; richer methods (prose enrichment of attachments) are out of scope until something ships to name.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): `--graph=<value>`, with `--graph code` also accepted — the repo's flag conventions.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): the default rides `config set attachments.graph …` if dotted keys reach blocks; otherwise the editor gains the block, no new CLI surface here.

**Guardrail(s).**
- Precedence table: built-in < config < flag, each combination asserted, the notice line checked where the method is non-default.
- Config round-trip: the block survives the editor with comments intact; an invalid value is refused naming the valid set.
- `/attach` parse table: quoted paths, flags before/after the spec, unknown flags refused with help.
- Completion: `--graph=` and values complete from the command table (the existing completer tests' shape).

**Acceptance criteria:**
- [ ] `/attach src --graph=off` indexes chunks only; the same folder re-attached with `--graph=code` builds the graph.
- [ ] `attachments: { graph: off }` in config makes bare `/attach` skip the graph, and `--graph=code` overrides it for one attach, with the notice saying so.
- [ ] `apogee complete "q" --attach ./src --graph=code` builds the graph in the one-shot store (overriding 27n's one-shot default).
- [ ] Tab after `/attach <path> ` offers `--graph=` and its values.

**Scope note.** Item **27p** (30f under the then-v0.1.7, 31p under the then-v0.1.8, until 2026-10-03's merge and migration — the user's calls), earmarked for **v0.1.4**; **gated on [27n](attachment-code-graph.md)**. Out of scope: new `@` mention syntax; methods beyond `code|off`; per-directory config; retrieval flags (`--retriever` and friends are shipped and separate).
