# CLI-M46 · A collection the config can't hold is refused before it is written

| Field | Value |
|---|---|
| **Type** | 🐞 Bug |
| **Project** | CLI |
| **Status** | 🔒 Blocked |
| **Fix version** | Maintenance (any time) |
| **Component** | Stores › `embed ingest`, `graph build`, the knowledge store, their HTTP twins and the adds that name a collection |
| **Labels** | `collections` `knowledge` `graph` `config` |
| **Epic** | — (maintenance has no track) |
| **Blocked by** | [M45](a-collection-is-named-not-pathed-at-every-reader.md) |
| **Blocks** | — |
| **Reporter** | CLI-M42's probes (2026-11-22), then a sandboxed investigation, two skeptics and a critic the same day |
| **Created** | 2026-11-22 |

---

## Summary

`apogee embed ingest "my notes" <file>` writes `embeddings/my notes.db`, then prints `could not register 'my notes' in config` and exits 0. The store chains check a new name with the path rule only. The config refuses it afterwards with its own add rule, a refusal that was certain before the first byte was written. Of 30 probed names, 23 leave a store like this: it reads, but it forgets its chunking, its graph is never enabled, and every re-ingest repeats the error. The knowledge store and `graph build` do the same, over the CLI and HTTP. The item adds one create rule and one registration step in operations: a name the config can't hold is refused before anything is written, the way `models pull` already refuses one.

## Description

### User story

**As** someone naming a new collection, a knowledge store or a graph's collection,
**I want** a name Apogee can't keep to be refused before it embeds or writes anything,
**so that** "ingested" means the collection is registered, keeps its chunking and can have its graph, and I'm never left with a half-made store.

### Steps to reproduce

The sandbox, 2026-11-22, with the built binary `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`: a throwaway `APOGEE_HOME` and `HOME`, `env -i`, stdin from `/dev/null`, `config init`, a mock backend, and a mock embedder and extractor.

1. Run `apogee embed ingest -- "my notes" "docs/a note.txt"`, then `echo $?`, `ls embeddings` and `cmp` the config with its earlier copy.
2. Run `apogee embed ingest -q --chunk-size 64 --chunk-overlap 8 -- "my notes2" docs2/long.txt`, then the same without the flags.
3. Run `apogee graph build "my notes" -m extractor`, then `apogee complete -m echo --rag "my notes" --retriever lexical fox`.
4. Under `apogee serve`, send `POST /v1/admin/knowledge {"db":"kn x", …}`.

### Actual result

1. `1 file(s), 1 chunk(s)`, then `apogee embed: could not register 'my notes' in config -- collection name 'my notes' is not one both config formats can edit -- a name is letters, digits, '_', '-', '.' and '/', first neither '-' nor '.', and not null`, exit 0. `my notes.db` exists, and the config is unchanged.
2. 7 chunks, then 1 chunk (`1 replaced`), with the same error each time. A plain name keeps 7.
3. The graph is built (`Nodes upserted: 2`), then `could not register` and `could not set graph.enabled`, exit 0. The `--rag` run has no `[Knowledge graph: my notes]` section; the same steps on `notes` give one.
4. HTTP 201, `"registered":false`, a `could not register` note, and `embeddings/kn x.db` created.

### Expected result

1. Exit 1 with one stderr line naming the rule and that nothing was written. No store file, no embedding spend, and the config byte-identical.
2. Not reached: the name is refused at the first run.
3. For a store already on disk under such a name: said before extraction that retrieval can't expand through the graph without an entry; built; no post-build `could not` lines; exit 0.
4. HTTP 400 before the clerk runs, and no store file.

### Background

Every store chain writes first and registers second, and treats a refused registration as never fatal:
- `embed ingest`: the write at `embed.cpp:353`, the registration at `:446-470` ("Reported and never fatal: the corpus is already on disk"), the error at `:467`.
- `graph build`: on the CLI at `graph.cpp:977-1003`, and over HTTP at `admin_graph.cpp:519-541`.
- The knowledge store core: the write at `knowledge_core.cpp:165`, the registration at `:189-204`. It serves `knowledge capture`, chat `/capture`, `knowledge.auto_capture`, and `POST /v1/admin/knowledge` and `/capture` (`knowledge.cpp:528`, `chat_session.cpp:2261`, `admin_knowledge.cpp:233`, `:284`).

There is no shared helper: four inline copies call `append_embedding`.

The never-fatal policy is recorded intent (MILESTONES' lexical-embeddings decisions: "Registration failure | Reported, never fatal", and "Registration is a convenience, not a requirement"). It was written for config trouble, such as a config that will not load. The defect is admission. A new name is checked only by the path rule (`plain_name_error`, `embed.cpp:45-54`, and its copies), while the registration applies the config's add rule, `yaml_plain_key` (`config_yaml.cpp:206-216`, enforced in `append_entry`, `config_edit.cpp:715-725`). The two sets disagree in both directions. For a name only the path rule accepts, the refusal is certain before anything is written, and nothing asks.

The cure exists in the repository. `models pull`'s `check_registrations` (`models_pull.cpp:2846-2856`) says "A name the config cannot hold is refused now by the one edit that will add it, run on the text and kept nowhere". MILESTONES records it as "A registration that would refuse at the end refuses before the first conversion". The store chains never adopted it.

The history:
- 96bb082d (2026-08-25): `append_entry` refuses spaces, tabs, `:` and `#`.
- 92351332 (2026-09-12): ingest registers after the write, never fatal. Spaced names have half-failed since that day.
- 8d5aec51 (2026-10-25, 55f): the add rule widened to `yaml_plain_key`'s set, "so agents, MCP servers, collections, graphs, suites and review targets are covered too". That is true of the editor, but the commit did not touch the store chains.

Registration has since become load-bearing, without a dated revision of "a convenience":
- the chunking lives on the entry (`--chunk-size` is remembered there);
- own-graph expansion needs `entry->graph.enabled` (`graph_context.cpp:198-201`, 34ed0ce4).

CLI-M45 puts the collection read rule in contracts. This item adds the create rule beside it, and the one registration step.

### Evidence

All measured 2026-11-22 by an investigator, two skeptics and a critic, each in its own sandbox.

1. **The core case**, reproduced three times: the store written, the registration refused, exit 0, the config byte-identical.
2. **Which names.** A 30-name matrix, each name in a fresh home copy (`matrix.sh`):
   - **23 left a half-made store with exit 0**: a space, `@`, `café`, `null`, `Null`, `~`, `.x`, `-x`, `:`, `#`, quotes, `[x]`, `,`, a tab, a newline, `.`, `-`, a leading or trailing space, `*`, `{x}` and `|`. A newline name is written as `embeddings/a` + `b.db` on one line; `.` writes `..db`.
   - **3 registered cleanly**: `notes`, `true`, `x.db`.
   - **4 were refused before any write**: `a/b`, `a..b`, `a\b` and the empty name.

   The reverse holds as well: the config's rule accepts `a/b` and `a..b` (`config add-backend 'a/../../x'` was added), and the path rule refuses them.
3. **What the half-made store loses.**
   - Its chunking: 7 chunks, then 1 on a re-ingest with no flags (a plain name keeps 7). That contradicts `config_e2e.cmake:152-153`'s intent and DEVELOPER.md's "The first ingest of a name registers it".
   - Its graph, for retrieval: `graph build` builds it, refuses both edits and exits 0, and `--rag` never expands through it. `embed ingest --graph "fresh one"` prints three refusals after spending extraction calls.
   - Quiet: every re-ingest or capture repeats the error, and `check` lists only registered collections.

   Reads keep working: `embed list`, `info`, `query` and `delete`, and the graph tools. A hand-written JSONC entry restores the chunking and graph, but no Apogee verb can write one.
4. **The knowledge store.** `knowledge capture` into a hand-set `knowledge.db: team notes` writes the record, the raw archive and the store, notes the refusal, and exits 0. `POST /v1/admin/knowledge {"db":"kn x"}` answers 201 with `"registered":false`, after the clerk call.
5. **The HTTP graph twin.**
   - The build job ends `succeeded` with `config_warning: "could not set graph.enabled: collection name 'my notes' …"`, naming the wrong edit when the registration is what failed (`admin_graph.cpp:524-541`).
   - `PUT /v1/admin/embeddings/{id}/graph` maps every `ConfigEditError` to 404 (`admin_graph.cpp:773-774`). That is right for an absent entry, wrong for a hand-written entry the config holds (measured 404 on `my%20notes`), and wrong for a lock timeout (by reading). MILESTONES says an editor refusal over HTTP is a 400.
6. **Names a config already references.** `config add-graph team --collections "my notes,notes"` exits 0 with `member collection 'my notes' is not configured yet (ingest registers it on first use)`, which is false for that name. `POST /v1/admin/graphs` and `POST /v1/admin/agents` take such names with 201.
7. **Length.**
   - A 244-byte name ingests, registers and builds a graph.
   - A 245-byte name fails after creating a 0-byte `.db`: `database error: unable to open database file`, exit 1. `<name>.db-journal` passes NAME_MAX 255, and `embed list` then shows the 0-byte file as unreadable.
   - A named graph fails at 242 bytes, at its build lock (`<name>.db.build.lock: File name too long`, `graph_sources.cpp:129-130`).
8. **`check`'s remedies print the name unquoted** (`check.cpp:1764`, `:2123`): `run: apogee embed ingest fresh one <path>` would ingest `fresh`.
9. **What already works**, and must stay:
   - **A config-less home**: a plain name ingests with `not registered in config -- … cannot open config file`, exit 0, and registers on its next ingest after `config init`.
   - **Case variants on APFS**: `Notes` after `notes` appends to `notes.db`, and `graph build Notes` enables `notes`.
   - **Rebuilds**: a hand-written, enabled entry (`"my notes": {…, graph: {enabled: true}}`) rebuilds with no edit and exit 0.
   - **Cleanup and recovery**: `embed delete --yes` removes a half-made store, and the content is kept by moving the file to a plain name, re-ingesting and rebuilding the graph.
10. **Not pinned.** No test greps for `could not register` or `config_warning` outside 55f's name-rule strings.
11. **Who is hit next.** CLI-59d (v0.2.3) plans to relay `embed ingest` to paired devices as a network job, so today's exit 0 would reach them as a success. The Mac app reports the ingest and graph build as done, showing only the first or last stdout line (`CollectionsViewModel.swift:233`, `GraphViewModel.swift:291`).

## Acceptance criteria

- [ ] **Given** a new name the create rule refuses (`my notes`, `a@b`, `café`, `null`, `.x`, `-x`), **when** `embed ingest -- <name> <file>` runs, **then** it exits 1 with one last stderr line naming the rule and saying nothing was written. `embeddings/<name>.db` does not exist, no embedding or model call was made, and the config is byte-identical.
- [ ] **Given** the same names, **when** `knowledge capture` with that `knowledge.db`, `POST /v1/admin/knowledge` or `/capture` runs, **then** it is refused before the clerk runs (exit 1, or HTTP 400), and no store or archive file is written.
- [ ] **Given** a 245-byte or longer new name, **then** it is refused before any file exists (the cap is 200 bytes), and a 244-byte store already on disk still reads.
- [ ] **Given** a store already on disk under a name the config can't hold, or a name the config already references (a graph member, an agent's collection, `knowledge.db`, `auto_rag`), **when** ingest or capture writes into it, **then** it goes ahead, exit 0, with one stderr line naming what it can't keep (the chunking, the graph's enable) and the way out (move the file to a plain name and re-ingest).
- [ ] **Given** `graph build` over such an existing store, **then** it says before extraction that retrieval won't expand through the graph without an entry, builds, makes no edit the dry run says will fail, and exits 0. Over HTTP, the job's warning names the edit that failed.
- [ ] **Given** `PUT /v1/admin/embeddings/{id}/graph`, **then** an absent entry answers 404, and every other editor refusal (a held lock included) answers 400.
- [ ] **Given** `config add-graph g --collections "new one"`, `POST /v1/admin/graphs`, `agents create h --collection "new one"` or `POST /v1/admin/agents` with a new name the create rule refuses and no store on disk, **then** it is refused, the config unchanged.
- [ ] **Given** a registration that still fails after the write (a lock timeout, a raced edit), **then** the command keeps the recorded never-fatal answer, exit 0, with words naming the store left on disk and why, and `--graph` does not go on to the graph.
- [ ] **Given** `check`'s collection and agent remedies, **then** each name is shell-quoted.
- [ ] **Given** a plain name, a config-less home, a case variant on APFS, or a rebuild of an enabled entry, **then** every chain behaves as today.

## Constraints

- **One rule, in contracts.** `collection_create_error` sits beside CLI-M45's `collection_read_error` and includes it. No chain keeps its own copy.
- **Refuse by the one edit that will add it** (`check_registrations`'s precedent). The plan dry-runs the registration on the config's text, unlocked and kept nowhere (CLAUDE.md, 57f: a read before the lock may answer a read-only case), then the locked edit runs after the write. The dry run is advisory, so the post-write path stays.
- **Dry-run only the edits today's conditions would run**: the registration when there is no entry (`embed.cpp:455`, `graph.cpp:980`, `knowledge_core.cpp:192`), and `graph.enabled` only when it is not already true. An unconditional dry run would refuse the working rebuild of an enabled hand-written entry.
- **Existing names keep working** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md), and 55f's "existing odd names reached"). "New" means no store file at this spelling and no config reference to the name.
- **The never-fatal policy stays for trouble after the write** (MILESTONES' "Registration failure | Reported, never fatal"). This item changes when a certain refusal is asked, not that policy. A dated revision records that registration now carries the chunking and the graph's enable.
- **Mode parity** ([ADR 0002](../../../adrs/cli/mode-parity.md)): the three chains and their HTTP twins move together, and a refusal is a 400 over HTTP.
- **The refusal is one line, and the last on stderr**, so the Mac app's failure line (`JobProgress.swift:141`, `stderrTail.lastLine`) shows it with no app change, for a name that reaches the verb. A flag-shaped name (`-x`, `-h`) is read by the parser as an option until the app passes it after `--` ([APP-M21](../../../assistant/darwin/MILESTONES.md#2026-11-26--listed-name-reaches-the-verb-maintenance-item-m21-a-listed-name-reaches-the-verb-as-listed)).
- **Every note keeps the house voice on stderr**: one line beginning `apogee <verb>: `, the graph build's note before extraction included, never on stdout. The Mac app keeps exactly those lines as a finished run's notes ([APP-M22](../../../assistant/darwin/MILESTONES.md#2026-11-26--jobs-notes-reach-the-person-maintenance-item-m22-a-jobs-notes-reach-the-person)), and the tests pin the prefix.

## Technical approach

Paths under `lib/src/cli/`.

| File | Change |
|---|---|
| `source/data/contracts/collection_name.h`, `.cpp` | `collection_create_error(name, existing)`: CLI-M45's read rule, the config's add rule (`yaml_plain_key`), a 200-byte cap, Windows device names (`con`, `prn`, `aux`, `nul`, `com1-9`, `lpt1-9`, case-folded) on every platform, and a refusal when a new name folds onto an existing entry or store spelled differently, naming it. |
| `source/presentation/operations/collections.h`, `.cpp` | `plan_collection_write`: before any file, clerk call, embed or extraction, the create rule for a new name and a dry run of the edits today's conditions would run. It answers go, refuse (with the words), or go-with-a-note (an existing or referenced name). `register_collection`: the locked edit after the write, its failure kept never fatal with words naming what was left. The four inline copies call these. |
| `source/presentation/cli/embed.cpp`, `graph.cpp`, `source/presentation/operations/knowledge_core.cpp` | The plan first, the registration step after. `--graph` doesn't chain after a failed registration. |
| `source/presentation/httpserver/admin_knowledge.cpp`, `admin_graph.cpp` | The knowledge routes answer 400 before the clerk. The graph job's warning names the edit that failed. The toggle (`:773-774`) answers 404 only for an absent entry, and 400 otherwise. |
| `source/presentation/operations/graph_members.cpp`, `source/presentation/cli/agents_cmd.cpp`, `source/presentation/httpserver/admin_agents.cpp` | A new collection reference refused by the create rule at add, unless a store exists. The words "ingest registers it on first use" are said only of a name it can register. Named-graph names get the 200-byte cap. |
| `source/presentation/cli/check.cpp` | The remedies at `:1764` and `:2123` shell-quote the name. |
| `tests/data/contracts/collection_name_test.cpp`, `tests/presentation/operations/collections_test.cpp` (new), `tests/presentation/httpserver/admin_knowledge_test.cpp`, `admin_graph_test.cpp`, `tests/scripts/cmake/config_e2e.cmake` | The fixtures under Testing. |

**Reference.** `check_registrations` (`models_pull.cpp:2846-2856`), the refuse-before-the-first-conversion pattern. 55f's add rule (`yaml_plain_key`) and its record. `admin_knowledge_test.cpp:279`'s `CHECK(fixture.provider->requests().empty())`, the "no clerk call" assertion.

**Docs when it ships:**
- MILESTONES: a dated revision of the lexical-embeddings decisions ("Reported, never fatal" now scoped to trouble after the write; "a convenience" now carrying the chunking and the graph's enable), and a dated entry.
- DEVELOPER.md's `embed` row.
- `reference/cli/http-api.md`'s knowledge, graph-toggle and graph-build lines, and the registration promise in the graph routes.
- The words in `graph_members.cpp:140`.

## Testing

Each fixture fails on today's binary unless marked as a guard (measured 2026-11-22).

- **The bug as a fixture**, in `config_e2e.cmake` (a fresh home, the shipped JSONC template, a mock backend). For `my notes`, `a@b`, `café`, `null`, `.x` and `-x`, each after `--`: `embed ingest` exits 1. The last stderr line holds `a name is letters, digits` and says nothing was written, `embeddings/<n>.db` is absent, and the config is byte-identical.
- **Length**: a 245-byte name is refused before any file exists; a 244-byte name registers (guard).
- **An existing store**: copy a store file to `embeddings/my notes.db`. list, info, query and `delete --yes` exit 0 (guard). Ingest exits 0 with exactly one stderr line and the config unchanged.
- **Hand-written JSONC entries.** Write `"my notes": {chunk_size 64, chunk_overlap 8, graph: {enabled: true}}` and `"a@b": {chunk_size 64}`.
  - A re-ingest of `my notes` gives 7 chunks and empty stderr (guard).
  - `graph build "my notes"` exits 0 with no edit (guard).
  - `graph build a@b` says before extraction that the graph can't be enabled, with no post-build `could not set`, and exits 0.
- **A referenced name**: with `config add-graph team --collections notes` and a hand-written `"my notes"` member, `embed ingest --graph -- "my notes"` exits 0 with one note, and `complete --rag "my notes"` prints `[Knowledge graph: team]`.
- **New references at add**: `config add-graph g --collections "new one"` and `agents create h --collection "new one"` exit 1, the config unchanged.
- **Case variants**: on a filesystem probed as case-insensitive, `Notes` after `notes` appends to `notes.db` (guard).
- **Unit**:
  - the create rule's table, the cap and the fold;
  - the plan's outcomes for a new, existing and referenced name, with a config present or absent, and a dry-run refusal;
  - a post-write failure, injected by holding `config.json.lock` through `platform::FileLock` with a short wait.
- **HTTP**:
  - `POST /v1/admin/knowledge {db:"kn x"}` gives 400, `fixture.provider->requests().empty()`, and no `kn x.db`;
  - a build over a pre-existing unregistrable store warns, naming the registration;
  - the toggle on an absent entry gives 404, and with a held lock 400.
- **The house voice**: each note this item prints on exit 0 (an existing store's line, the graph build's note before extraction, a post-write failure) is one stderr line beginning `apogee embed: `, `apogee graph: ` or `apogee knowledge: `, and stdout holds none of them.
- **Gate:** `lib/scripts/cicd.sh --app cli --test`, clang-format, and the MinGW scan of the changed files.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Which rule gives way: should collection names shrink to the config's rule, or should the config editor learn quoted keys so `my notes` works everywhere? | `[default]` | The config's rule wins. 55f recorded "names are restricted at add to the set both formats edit, with existing odd names reached". Teaching the line editor and the JSONC bridge quoted keys would reverse 55f, and is bigger than maintenance. |
| When registration fails after the write (a lock timeout or a raced edit), should the command exit non-zero? | `(consumed decision)` | No. MILESTONES' lexical-embeddings decision "Registration failure \| Reported, never fatal" owns it. Once the plan refuses certain failures up front, only transient trouble reaches this path, and the words name the store left on disk. |
| A store already on disk, or a name the config already references, that the config can't register: should ingest and capture still write into it? | `[default]` | Yes (ADR 0006; 55f's "existing odd names reached"). Exit 0, with one stderr line per run naming what it can't keep and the way out. |
| Should `graph build` over such an existing store be refused before extraction? | `[default]` | No. The graph tools and `graph stats` read that graph. Say before extraction that retrieval won't expand through it, skip the doomed edits, and exit 0. |
| What length cap, and on what? | `[default]` | 200 bytes, for new collection names and new named-graph names. The measured limits on APFS are 244 and 241, and Windows MAX_PATH was not measured. Existing longer stores keep reading, since the cap is not in the read rule. |
| How are case variants handled on a case-sensitive filesystem? | `[default]` | "New" means no store file at this spelling. A new name that folds onto an existing entry or store spelled differently is refused, naming the existing one. On APFS and NTFS the file exists, so today's working path is unchanged. |
| Should this add `embed rename`? | `[default]` | No. The refusal names the manual way out (move the file, re-ingest, rebuild), measured to work. A rename verb is a follow-up. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-22 | Specced from CLI-M42's Out of scope, a sandboxed investigation, two skeptics and a critic (Evidence). Placed in **Maintenance** as M46, after CLI-M45, whose read rule this item's create rule includes. | the spec |
| 2026-11-22 | **The seam is admission, not the never-fatal policy.** The policy is recorded intent for trouble after the write. The certain refusal is asked first, by the edit that will run, as `check_registrations` does. | the spec |
| 2026-11-22 | **Not a gate on CLI-M44**, but best built before or with it. Its dry run turns M44's new layout refusals into refusals before the write, instead of half-made stores with M44's words. | the spec |
| 2026-11-26 | **Every note keeps the house voice on stderr**, the graph build's note before extraction included, and the tests pin the prefix. Added from the Mac app's investigation (APP-M22), which keeps exactly those lines as a finished run's notes. The "no app change" for refusals now holds only for a name that reaches the verb: a flag-shaped one needs APP-M21's `--` first. | the spec |
| 2026-11-26 | **CLI-M48 renders every printed command through one renderer.** If it lands first, `check`'s two `embed ingest` remedies (`check.cpp:1764`, `:2123`, Evidence 8) are built through it rather than quoted by hand here; if this item lands first, M48 folds this quoting in. **CLI-M58 moves notes of the same code to stderr** and is built one after this item, the second routing the other's notes through its writer. No gate either way. | the spec |

## Out of scope

- **The read rule at every reader**, the `--rag` path escape and the completion filters: [CLI-M45](a-collection-is-named-not-pathed-at-every-reader.md).
- **Widening the config editor to quoted keys** (above).
- **An `embed rename` verb**: a follow-up.
- **The Mac app** passing `--` before a collection name ([APP-M21](../../../assistant/darwin/MILESTONES.md#2026-11-26--listed-name-reaches-the-verb-maintenance-item-m21-a-listed-name-reaches-the-verb-as-listed)), and showing a finished run's notes ([APP-M22](../../../assistant/darwin/MILESTONES.md#2026-11-26--jobs-notes-reach-the-person-maintenance-item-m22-a-jobs-notes-reach-the-person)). The app needs no change to show this item's refusals for a name that reaches the verb, since they are the last stderr line.

## Links

- **Blocked by:** [CLI-M45](a-collection-is-named-not-pathed-at-every-reader.md) · **Blocks:** —
- **Relates to:**
  - [CLI-M42](stubs-read-the-words-as-the-shell-does.md), whose investigation found the spaced collection;
  - [CLI-M44](yaml-editor-refuses-the-layouts-it-does-not-write.md), whose refusals this item's dry run asks before the write;
  - CLI-59d (v0.2.3, [reads and commands over the network](../v0.2.3/reads-and-commands-over-the-network.md)), which would relay today's exit 0 to devices;
  - [APP-M21](../../../assistant/darwin/MILESTONES.md#2026-11-26--listed-name-reaches-the-verb-maintenance-item-m21-a-listed-name-reaches-the-verb-as-listed) and [APP-M22](../../../assistant/darwin/MILESTONES.md#2026-11-26--jobs-notes-reach-the-person-maintenance-item-m22-a-jobs-notes-reach-the-person), the Mac app's side;
  - [CLI-M48](a-suggested-command-runs-as-printed.md), whose renderer builds `check`'s two `embed ingest` remedies that this item quotes by hand (Evidence 8);
  - [CLI-M52](a-refused-scaffold-writes-nothing.md) and [CLI-M56](a-new-dataset-name-is-one-every-verb-reads.md), the same create-rule shape for agents and datasets; M52's dry run keeps this item's `--collection` refusal before the first write, whichever lands second;
  - [CLI-M58](a-runs-notes-are-said-once-on-stderr.md), which moves the notes of the same `embed ingest` tail, graph build and knowledge core to stderr: built one after the other, the second routing the other's notes through one writer;
  - [CLI-M59](the-http-graph-jobs-say-what-the-cli-says.md), which also edits `admin_graph.cpp`;
  - 55f (8d5aec51), the add rule; 92351332, the never-fatal registration; `check_registrations`, the precedent.
