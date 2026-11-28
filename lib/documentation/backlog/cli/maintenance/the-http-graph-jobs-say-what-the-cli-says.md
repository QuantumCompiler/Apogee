# CLI-M59 · The HTTP graph jobs say what the CLI says, and build a mixed graph's code as the CLI does

| Field | Value |
|---|---|
| **Type** | 🐞 Bug |
| **Project** | CLI |
| **Status** | 🟢 Ready |
| **Fix version** | Maintenance (any time) |
| **Component** | Serve › the admin plane's graph jobs (`POST /v1/admin/graph/{id}/build`, `/communities`) and the graph notes' words in `operations/` |
| **Labels** | `graph` `parity` `admin-plane` `warnings` |
| **Epic** | — (maintenance has no track) |
| **Blocked by** | — |
| **Blocks** | — |
| **Reporter** | The investigation of the APP-M21 and APP-M22 probes' CLI asides (2026-11-26): found by its sandboxed investigator, then two skeptics and a critic the same day |
| **Created** | 2026-11-26 |

---

## Summary

A finished graph job on the admin plane drops three notes that the CLI prints from the same producers:
- the entity-vector skip;
- each member collection without a database;
- the communities run's summary-vector skip.

The same route answers 404 for a named graph that has source trees but whose member collections hold no data. For that graph, `apogee graph build` builds the code and exits 0. The item adds a `warnings` array to both job results, in the CLI's words from one producer. It also runs that mixed graph's code as a job, as the CLI does.

## Description

### User story

**As** someone who drives graph builds over the admin plane (a script today, and the Mac app against a server once APP-7c lands),
**I want** a finished job to say what the CLI says for the same build, and a graph the CLI builds to build over HTTP too,
**so that** I learn why entity search is full-text only, or which member contributed nothing, without running the CLI beside the server.

### Steps to reproduce

The sandbox, 2026-11-26, with the built binary `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`: a throwaway `APOGEE_HOME` and `HOME` nested three levels deep, `env -i` with `PATH=/usr/bin:/bin`, stdin from `/dev/null`, and mock backends only.

1. Use a home `<h1>` with a mock extractor and a mock embedder. Make graph `ghosty` with `apogee config add-graph ghosty --collections ghost`, then `apogee graph build ghosty --source <h1>/src3 -q`. It now holds the source tree `src3` and the member `ghost`, which has no database. Make graph `code` over three source trees, then move one of them, `src2`, away.
2. Start `apogee serve --bind 127.0.0.1 --port 18797 -m extractor`, with the admin token from `apogee serve --print-admin-token`.
3. Send `POST /v1/admin/graph/ghosty/build {}` and `POST /v1/admin/graph/code/build {"update": true}`. Poll `GET /v1/admin/jobs/{id}` until each job leaves `running`.
4. Run `apogee graph build ghosty -q` on the same home.
5. Use homes with no embedder, each with collection `notes` ingested and graph `mix` over `[notes, ghost]`:
   - In a home with a mock extractor only, under `serve` on port 18791, send `POST /v1/admin/graph/notes/build`, `POST /v1/admin/graph/mix/build` and `POST /v1/admin/graph/notes/communities {"min_size": 2, "model": "extractor"}`.
   - In a home set up the same way plus a mock summarizer, still with no embedder, run `graph build notes -q`, `graph build mix -q` and `graph communities notes --min-size 2 -m summarizer` on the CLI.

### Actual result

3. The build of `ghosty` answers `404 {"error":{"message":"no member collection of graph 'ghosty' has any data","type":"not_found_error"}}`. The update job succeeds, and its result keeps `code.missing_sources: ["<h1>/src2"]` and `empty_sources: []`.
4. Exit 0 with stderr empty. Stdout holds the code summary, then:
   ```
   [graph] member collection "ghost" has no database -- it contributes nothing this build.
   [graph] no member collection of "ghosty" has any data yet -- only its code was built.
   ```
5. Each job succeeds, and no result says why the vectors are missing or that `ghost` has no database:
   - the collection build's result holds `entities_embedded: 0`;
   - the named build's result holds `collections: ["notes","ghost"]` and `entities_embedded: 0`;
   - the communities run's result holds `embedded: 0`.

   The CLI, with the same backends, says each fact as a `[graph]` line on stdout:
   - `entity vectors skipped for "notes" -- backend 'extractor' cannot embed; entity search will be full-text only.`;
   - `member collection "ghost" has no database -- it contributes nothing this build.`, then the same entity line for `"mix"`;
   - `summary vectors skipped for "notes" -- backend 'extractor' cannot embed; summaries will be full-text searchable only.`

### Expected result

3. The build of `ghosty` answers 202, with no extractor call, and the job succeeds with `graph`, `code` and these `warnings`, the facts the CLI says for the same graph:
   ```json
   "warnings": ["member collection \"ghost\" has no database -- it contributes nothing this build.",
                "no member collection of \"ghosty\" has any data yet -- only its code was built."]
   ```
   The update job is unchanged.
4. Unchanged.
5. Each result gains `warnings`, holding the CLI's words for the same build:
   - the entity-vector skip on both builds, with the `ghost` line before it on the named build;
   - the summary-vector skip on the communities run.

   Every existing field stays as it is: `missing_sources`, `empty_sources`, `embed_error` and `config_warning`.

### Background

The admin plane's graph routes are the CLI's twins: the parity table pairs `graph build`, `graph update` and `graph communities` each with a `POST` route (`tests/presentation/httpserver/parity_test.cpp:115-118`). A route renders the build it runs into a job result:
- `build_fields` for the counts (`admin_graph.cpp:256-279`);
- `code_fields` for a code layer (`:203-229`);
- the communities counts (`:882-889`).

The CLI prints the same outcome, plus a few notes from the same producers:
- `graph::EntityEmbedder::note` (`business/graph/build.h:98-99`) says why entities stay full-text only;
- a member whose database is missing, which `open_graph_members` maps to null (`operations/graph_members.cpp:47-55`);
- the same embedder note for a communities run's summaries.

The routes compute the embedder and open the members, then keep only what the build needs.

**History.** The commits and their dates come from the investigation's `git log -S`:
- 2026-09-19: the CLI's entity, member and summary notes arrived (34ed0ce4, a6e3fa54).
- 2026-10-05: the CLI's branch for a mixed graph whose members hold no data came with 27k (c3127496). The HTTP route's code-only branch came the same day: MILESTONES' 27k entry records "a graph of trees alone runs as a job **with no generation backend served**".

**Where the route goes wrong.** The route decides a graph is code-only only when it has no collections at all (`admin_graph.cpp:387-390`). So a graph holding both trees and collections takes the prose path, where no member holding data is a 404 (`:439-443`). `http-api.md:827-828` documents that 404 without telling a mixed graph apart. The CLI's `build_named` builds the code first, and returns when no member has data and the graph has trees (`graph.cpp:1039-1060`). That happens before any extractor is resolved (`:1064-1066`).

**How it came about.** It was found beside the APP-M22 probes. Their aside asked "Should this also cover warnings printed on stdout?" (APP-M22's Evidence 11 and Open questions). The investigation of that aside, run beside APP-M21's, went in three steps:
1. It first counted the HTTP drops as part of the CLI's stdout-to-stderr move, now [CLI-M58](../../../assistant/cli/MILESTONES.md#2026-11-27--a-runs-notes-are-said-once-on-stderr-maintenance-item-m58-a-runs-notes-are-said-once-on-stderr-in-the-house-voice) (shipped 2026-11-27).
2. Its review split them out. The job results lack these fields whatever stream the CLI uses: a root cause of its own in `admin_graph.cpp`, shippable alone under ADR 0002.
3. Checking that split found the mixed graph's 404, which no one had reported.

So CLI-M58 moves the CLI's notes, CLI-M60 does the models verbs, and this item makes the HTTP twins say the same facts.

**The design**, in three parts:
- **One producer.** The graph notes' words live in one producer in `operations/`, called by both the CLI's sites and the routes.
- **A `warnings` field.** Both job results gain a `warnings` array of strings, present only when it is not empty. It has the shape `POST /v1/admin/graphs` already answers with (`http-api.md:1092-1100`).
- **The mixed graph.** A mixed graph whose members hold no data runs as the code-only job. Its `warnings` name the empty members and say that only its code was built.

A named graph with no source trees keeps its 404.

### Evidence

All measured 2026-11-26, each in its own sandbox, on `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`, which carries HEAD 5b8927c9's CLI strings (point 8). Probes are cited by their labels: the critic's `c-*` and `v0`, the investigator's `g5`–`g7`, `e3` and `http-jobs`, and the cause skeptic's re-run of `http-jobs` in his own home.

1. **The mixed graph: refused over HTTP, built by the CLI.**
   - **The HTTP side** (`c-http`): `serve` on 127.0.0.1:18797 over `<h1>`, the token from `serve --print-admin-token`, a 90 s deadline, `serve` terminated after. Serve's stderr runs `apogee serve: listening on http://127.0.0.1:18797` … `apogee serve: stopped`. The answer:
     ```json
     "build-named-ghosty-code-and-empty-member": {"post_status": 404, "post": {"error": {"message": "no member collection of graph 'ghosty' has any data", "type": "not_found_error"}}}
     ```
   - **The CLI side, same home** (`c-n1b`, `graph build ghosty -q`): exit 0 with stderr empty. Stdout runs `Parsing the code of "ghosty" from [src3] -- tree-sitter, no model...`, then the code summary (its `Files used:` row ends `1 (python 1) -- 0 parsed, 1 unchanged since the last build`), then the two `[graph]` lines under Actual result.
   - **How the graph was made.** `c-cag` (`config add-graph ghosty --collections ghost`) said `added graph 'ghosty' …` and `apogee config: warning: member collection 'ghost' is not configured yet (ingest registers it on first use)`. `c-n1` then ran `graph build ghosty --source <h1>/src3 -q`, the Mac app's New Graph sheet's argv: exit 0, ending in the same two lines.
2. **Why the two answers differ.** Code read at HEAD.
   - **The route:** `code_only` (`admin_graph.cpp:387-390`) holds only for a named graph with trees and no collections. It is checked at `:426-427`, and every other named graph goes on to `open_graph_members` and `any_data()`, which ends in the 404 (`:431-443`).
   - **The CLI:** `build_named` builds the code first (`graph.cpp:1039-1046`), says each member without a database (`:1048-1054`), and returns once it has said `only its code was built` when the graph has trees (`:1055-1060`). Only a graph with no trees is refused (`:1061-1062`), and the extractor is resolved after all of this (`:1064-1066`).
   - **Not measured.** On a server that serves no generation backend, the same graph gets 501 (`:408-418`), by reading only: `code_only` is false for it there too.
3. **The entity-vector skip is dropped.**
   - **Measured twice**, by the two `http-jobs` runs, each in a home with no embedder (`serve` on 127.0.0.1:18791, and the re-run on port 18793). `build-collection-notes` succeeds with `{chunks_extracted: 2, …, enabled: true, entities_embedded: 0, …}`, and no field names the skip. `build-named-mix` is the same.
   - **The CLI with the same backends** (`g5`, in a home with no embedder but not the served one): exit 0 with stderr empty. Stdout's line 1 is `[graph] entity vectors skipped for "notes" -- backend 'extractor' cannot embed; entity search will be full-text only.` In the served home itself, `embed ingest fresh <dir> --graph -q` (`e3`) says it as well.
   - **The code.** `admin_graph.cpp:454-456` keeps `entity_embedder.embed` and `.model` and never reads `.note`. The CLI prints it at `graph.cpp:954-957` (a collection) and `:1069-1072` (a named graph).
   - **Not re-measured in `c-http`.** That home had an embedder (`entities_embedded: 2` in its collection build), so the drop was confirmed there by reading only.
4. **A member without a database is dropped.**
   - **HTTP:** `http-jobs`' `build-named-mix` lists `collections: ["notes","ghost"]` and nothing more.
   - **The CLI** (`g6`): `[graph] member collection "ghost" has no database -- it contributes nothing this build.`, before the entity line.
   - **The code.** `build_fields` writes the entry's `collections` (`admin_graph.cpp:269-271`). The null stores in `members` (`:437-438`) are never read for the result. The CLI prints the line at `graph.cpp:1049-1053`.
5. **The summary-vector skip is dropped.**
   - **HTTP:** `http-jobs`' `communities-notes` result is `{clustered: 0, detected: 1, embedded: 0, failed: 0, pruned: 0, summaries_absent: 0, summarized: 1, unchanged: 0}`.
   - **The CLI** (`g7`): stdout's line 1 is `[graph] summary vectors skipped for "notes" -- backend 'extractor' cannot embed; summaries will be full-text searchable only.`, and stderr holds only the progress line `[graph] summarising community 1/1`.
   - **The code.** The route keeps only `resolve_embedder(plane, *config, target).embed` (`admin_graph.cpp:851`), and the counts (`:882-889`) have no place for the note. The CLI prints it at `graph.cpp:1663-1666`.
6. **Root cause.** `admin_graph.cpp` renders its job results from the build's result structs (`build_fields`, `code_fields`, the communities counts), never from the notes the CLI prints from the same producers:
   - the `EntityEmbedder` note is computed at `:454` and dropped;
   - the null member stores are never read for the result;
   - the communities job keeps only `.embed` (`:851`);
   - the route decides "code only" before "no member data" in a way the CLI does not (point 2).

   [ADR 0002](../../../adrs/cli/mode-parity.md): "Reads have twins … same facts, mode-appropriate shape", and "Parity gaps are bugs with a home".
7. **The contract as written.** In `http-api.md`:
   - **The build's result fields** are listed at `:803-811`.
   - **The 404:** "`404` when the collection has no data or no member of a named graph has any" (`:827-828`).
   - **Graphs with trees** (`:872-878`): "A graph of source trees alone needs **no generation backend**", and "A graph holding collections too still needs its extractor for them".
   - **The communities counts** are at `:985-986`.

   The precedents for a soft fact on a result:
   - `POST /v1/admin/graphs` answers `201 {"data": {…}, "warnings"?: […]}` (`:1092-1100`), plain strings from `validate_named_graph` (`graph_members.cpp:136-147`);
   - reindex says a mixed collection's fact in `note` (`:784-787`), and capture says its own in `note` (`:651`).

   `JobRegistry::finish` merges a result's fields into the `admin.job.completed` event (`jobs.cpp:103-122`), so a result field reaches `GET /v1/admin/events` too.
8. **The probed binary stands for HEAD.**
   - `v0` (`apogee version`) prints `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`.
   - `strings` finds 5b8927c9's CLI changes in it.
   - `git diff --stat 33ea685d HEAD -- lib/src/cli/source` lists 11 files, none of them `admin_graph`, `graph` or `graph_members`.
   - Only the macOS binary was probed. `admin_graph.cpp` has no platform branch.
9. **Not pinned.**
   - `admin_graph_test.cpp` asserts the 404 for a graph of collections alone (`:477`) and its 501 with nothing served (`:801-802`). It holds no mixed graph whose members are empty.
   - No case reads a note from a job result, and `:573` asserts `embedded == 0` with nothing beside it.
   - No CLI test pins `only its code was built`. `graph_test.cpp:166`, `:391` and `:514` pin the other three lines on stdout.
10. **Blast radius.**
    - **Only these routes change:** the admin plane's graph-build and graph-communities routes, and their sections of `http-api.md` (`:792-878`, `:972-1003`).
    - **The new field is additive.** The one change in behaviour is that a mixed graph with empty members gets a 202 job where it got a 404: a parity fix, with a dated revision of the documented 404.
    - **No client in the repository reads these results.** The Mac app drives the CLI over pipes. The future APP-7c fills a remote job's notes from a server job's warnings (APP-M22's Out of scope).
    - **The CLI's output does not change.** Collection builds and their `config_warning` are unchanged.
11. **What already works**, and must stay:
    - **Facts that fields already carry:** `code.missing_sources`, `code.empty_sources`, `embed_error` and `config_warning` ride on the job result (`c-http`; `admin_graph.cpp:223-224`, `:275-277`, `:539-540`).
    - **A failed chunk** is said as a `chunk extraction failed` progress event (`:301-307`) and counted in `chunks_failed`.
    - **A graph of trees alone** runs as a job with no generation backend, served or not (`:359-390`, `:408-414`).
    - **A named graph with no trees** whose members hold no data is a 404 (`admin_graph_test.cpp:477`), as the CLI refuses it (`graph.cpp:1061-1062`).
    - **`POST /v1/admin/graphs`** returns `warnings` for a member not configured yet or a tree that is not a directory.
    - **The reindex and capture twins** carry their notes as `note`.

## Acceptance criteria

- [ ] **Given** the `ghosty` graph of `c-http` (sources `[src3]`, collections `[ghost]`, no `ghost` database), **when** `POST /v1/admin/graph/ghosty/build {}` is sent, **then** it answers 202. `GET /v1/admin/jobs/{id}` shows the job `succeeded`, with `graph: "ghosty"`, `code` (the tree's files parsed) and exactly these `warnings`:
  - `member collection "ghost" has no database -- it contributes nothing this build.`
  - `no member collection of "ghosty" has any data yet -- only its code was built.`

  No extractor call is made. Today: `404 no member collection of graph 'ghosty' has any data` (`c-http`), while `graph build ghosty` exits 0 having built the code (`c-n1b`).
- [ ] **Given** the same graph on a server that serves no generation backend, **when** its build is posted, **then** it answers 202 and builds the same, as a graph of trees alone does. Today, by reading, it answers 501.
- [ ] **Given** a named graph with no source trees whose members hold no data, or a collection never ingested, **when** its build is posted, **then** it still answers 404 with today's words. With nothing served, the named graph still answers 501.
- [ ] **Given** a collection and no embedder, **when** `POST /v1/admin/graph/<c>/build` finishes, **then** the job result's `warnings` holds `entity vectors skipped for "<c>" -- backend '<b>' cannot embed; entity search will be full-text only.`, the words `graph build <c>` prints for the same config. Today there is no field, only `entities_embedded: 0`.
- [ ] **Given** a named graph with a member that has no database, **when** its build finishes, **then** `warnings` holds `member collection "<m>" has no database -- it contributes nothing this build.`, before the entity-vector line when that is said too, in the CLI's order. Today only `collections` lists it.
- [ ] **Given** no embedder, **when** `POST /v1/admin/graph/<c>/communities` finishes with summaries, **then** `warnings` holds `summary vectors skipped for "<c>" -- backend '<b>' cannot embed; summaries will be full-text searchable only.` Today the result holds `embedded: 0` and no note. With `"summaries": false` no embedder is resolved and nothing is said, as on the CLI.
- [ ] **Given** any build or communities run with nothing to warn (an embedder that embeds, every member with a database, or a graph of trees alone), **then** `warnings` is absent. `missing_sources`, `empty_sources`, `embed_error` and `config_warning` are unchanged, and a fact one of them carries is not repeated in `warnings`.
- [ ] **Given** the CLI's `graph build` and `graph communities` over the same homes, **then** their output is byte for byte what it is today. Moving those lines is CLI-M58's.

## Constraints

- **Additive only** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)). The results gain `warnings`, present only when not empty. No field is removed, renamed or given a new meaning. The reference doc is a behaviour contract named in a record, so the one change in behaviour (the mixed graph's 404) lands with a dated revision of `http-api.md`.
- **One wording** ([ADR 0002](../../../adrs/cli/mode-parity.md), "One core, many renderers"). The words come from one producer in `operations/`, which the CLI's sites print too. `admin_graph.cpp` never spells a second copy.
- **The CLI is the parity contract** ([machine-mode.md](../../../reference/cli/machine-mode.md), `:1041-1042`). The mixed graph builds as `graph build` builds it: the code, no extractor, the empty members said. A named graph with no trees keeps its 404, as `graph.cpp:1061-1062` refuses it, and so does a collection never ingested (`missing_target`, `admin_graph.cpp:444-445`).
- **No prefix or tag in a `warnings` entry.** Each entry is the bare sentence, as `POST /v1/admin/graphs`' warnings are. `apogee graph: ` and `[graph] ` belong to the CLI's rendering.
- **The CLI's output is unchanged by this item**: stream, tag and bytes. CLI-M58 moves those lines to stderr in the house voice.
- **Mode parity, and the mode skipped.** The CLI and HTTP say the same facts after this item. Machine mode has no face for these verbs (they refuse `--output-format json` and `stream-json` with exit 109, measured 2026-11-26), so the record names it as skipped (ADR 0002).
- **`operations/` holds what both surfaces run** (`source/presentation/CLAUDE.md`). Neither `cli/` nor `httpserver/` includes the other. A new source is a `.h`/`.cpp` pair listed in its layer's `CMakeLists.txt`, with its test in the mirrored tree ([ADR 0004](../../../adrs/cli/tests-mirror-architecture.md)).
- **Hermetic tests**: the in-process plane, mock backends, a temporary home, no network and no model. A live check, if one is run, uses `apogee-dev serve` over a temporary `APOGEE_HOME`, never `apogee` ([the dev channel](../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel)).
- **Code style and the compilers** ([CLAUDE.md → Code Style](../../../assistant/cli/CLAUDE.md#code-style)): formatted with the Homebrew LLVM clang-format (v22), a default on every struct member an initializer may leave out, and no new warning on any CI compiler (the MinGW scan of the changed files).

## Technical approach

Paths under `lib/src/cli/`, except the last row, which is under `lib/documentation/`.

| File | Change |
|---|---|
| `source/presentation/operations/graph_notes.h`, `.cpp` (new) | The graph notes' words, in one place: the entity-vector skip, the summary-vector skip, a member without a database, only the code built, and the list of a build's members without a database. |
| `source/presentation/CMakeLists.txt` | The pair listed between `graph_members` and `graph_sources` (`:88-91`). |
| `source/presentation/httpserver/admin_graph.cpp` | `warnings` on the graph-build result (the entity note from `:454`, each null member store) and on the graph-communities result (the summary note, the whole `EntityEmbedder` kept at `:851`). A named graph with trees whose members hold no data runs the code-only job with its `warnings`, instead of the 404 at `:439-443` and the 501 at `:408-418`. |
| `source/presentation/cli/graph.cpp` | The five sites print the producer's words: `:954-957`, `:1049-1053`, `:1057-1058`, `:1069-1072` and `:1663-1666`. Stream, tag and bytes stay as they are. If CLI-M58 landed first, they already do, and this row is empty. |
| `tests/presentation/httpserver/admin_graph_test.cpp` | The fixtures under Testing. |
| `tests/presentation/operations/graph_notes_test.cpp` (new), `tests/CMakeLists.txt` | The producer's words, one case per sentence. |
| `reference/cli/http-api.md` | `warnings` on both job results, and a dated revision of the 404 rule (`:827-828`) and of "A graph holding collections too still needs its extractor" (`:877-878`) for a mixed graph whose members hold no data. |

The producer, in `apogee::commands` beside `GraphMembers`:

```cpp
/// The graph notes' words: said by the CLI, and carried in an admin job's
/// `warnings`. One sentence each, no prefix and no tag.
[[nodiscard]] std::string entity_vectors_skipped(std::string_view graph, std::string_view why);
[[nodiscard]] std::string summary_vectors_skipped(std::string_view graph, std::string_view why);
[[nodiscard]] std::string member_without_database(std::string_view collection);
[[nodiscard]] std::string only_code_built(std::string_view graph);
/// One line per member whose store is null, in the members' map order, which
/// is the order the CLI's member loop says them in.
[[nodiscard]] std::vector<std::string> members_without_database(const GraphMembers& members);
```

The words are today's, with the `[graph] ` tag dropped:
- `entity vectors skipped for "<g>" -- <why>; entity search will be full-text only.`
- `summary vectors skipped for "<g>" -- <why>; summaries will be full-text searchable only.`
- `member collection "<m>" has no database -- it contributes nothing this build.`
- `no member collection of "<g>" has any data yet -- only its code was built.`

`<why>` is `EntityEmbedder::note` as the business layer gives it (`business/graph/build.cpp:214-216`): the spend rule's note, the resolver's reason (`backend '<b>' cannot embed`, or that no embedding backend is configured: `business/agentloop/embed_func.cpp:15-25`), or `no embedding backend resolves`.

**In `admin_graph.cpp`.** The changes, route by route:

- **The build.** Before `jobs.start`, the route gathers its `warnings`: for a named graph, `members_without_database(*members)`; then, when `entity_embedder.embed` is null, `entity_vectors_skipped(target.name, entity_embedder.note)`. That is the CLI's order (`graph.cpp:1048-1054`, then `:1069-1072`). The list is captured into the worker, and set on `fields` after `build_fields`, only when it is not empty. `config_warning` keeps its place and its own words (CLI-M46 owns those).
- **The communities run.** It keeps the whole `EntityEmbedder` from `resolve_embedder` (`:851`). With summaries and no `embed`, its result gets `warnings: [summary_vectors_skipped(target.name, note)]`. With `"summaries": false` no embedder is resolved and nothing is said, as on the CLI (`graph.cpp:1648-1649`).
- **The mixed graph.** `start_code_only` (`:359-386`) takes a list of warnings, which it sets on the code-only result when the list is not empty. In the no-data branch (`:439-443`), a named graph with source trees starts that job, with `members_without_database(*members)` and then `only_code_built(target.name)`. A graph with no trees keeps the 404. The branch for a server with nothing served (`:408-418`) asks the same question before its 501, so the mixed graph builds there as a graph of trees alone does. Both are decided before `resolve_generation`, as the CLI decides before `graph.cpp:1064-1066`, so `model` is not consulted for it.

The `admin.job.completed` event already carries every result field (`jobs.cpp:103-122`), so `warnings` reaches `GET /v1/admin/events` with no more code.

**Shared files and shared words.**
- **CLI-M58** routes the CLI's graph notes through its stderr writer. Whichever of CLI-M58 and this item lands second reuses the first one's producer, with no second wording.
- **[CLI-M45](../../../assistant/cli/MILESTONES.md#2026-11-27--a-collection-is-named-not-pathed-at-every-reader-maintenance-item-m45-a-collection-is-named-not-pathed-at-every-reader)** (shipped 2026-11-27) (the plain-name check at `admin_graph.cpp:39-42`) and **[CLI-M46](../../../assistant/cli/MILESTONES.md#2026-11-27--a-collection-the-config-cannot-hold-is-refused-before-it-is-written-maintenance-item-m46-a-collection-the-config-cannot-hold-is-refused-before-it-is-written)** (shipped 2026-11-27) (`config_warning`'s words, the graph toggle's status) edit the same file. They are built one after the other.

**Reference.** The in-house precedents:
- `validate_named_graph`'s warnings (`graph_members.cpp:136-147`), which `POST /v1/admin/graphs` answers as `"warnings"?: […]` (`http-api.md:1092-1100`): the field's shape, plain strings with no prefix;
- `start_code_only` (`admin_graph.cpp:359-386`), the job the mixed graph runs;
- `build_named` (`graph.cpp:1039-1063`), the order it follows;
- `config_warning` (`:538-541`), a soft fact on a job result;
- `admin_graph_test.cpp:782`'s `CHECK(fixture.provider->requests().empty())`, the "no model call" assertion;
- `admin_knowledge_test.cpp:72` and `:98`, which register a `MockEmbeddingProvider` on the plane's harness.

**Docs when it ships:**
- `reference/cli/http-api.md`: `warnings` on both job results, and the dated revision of the 404 and of "still needs its extractor".
- DEVELOPER.md: the `operations/` row (`graph_notes`) and the `httpserver/admin_graph_test.cpp` row.
- MILESTONES: a dated entry naming the revision.

## Testing

Hermetic: the in-process plane over a temporary home, mock backends, no network and no model. Each fixture fails on today's binary unless it is marked as a guard. The before-state is recorded in `c-http` (the 404, `missing_sources`) and in both `http-jobs` runs (the dropped notes).

- **The bug as a fixture: the mixed graph**, in `admin_graph_test.cpp`. Use the `SourceTree` helper with an entry `ghosty: {sources: [<tree>], collections: [ghost]}` and no `ghost` store. `POST …/ghosty/build {}` answers 202, and the job succeeds with `graph`, `code` (`files_parsed == 2`) and `warnings` equal to exactly the two lines under Acceptance criteria. `fixture.provider->requests().empty()`.
- **The same with nothing served** (`Fixture{{}, /*serve=*/false, …}`): 202 and the same result.
- **Guards:**
  - the graph of collections alone stays 404 (`:477`) and 501 with nothing served (`:801-802`);
  - a collection never ingested stays 404 (`:262`);
  - a code-only build with a gone tree keeps `missing_sources` and has no `warnings`.
- **No embedder.** The fixture's `mock` cannot embed, so the reason is `backend 'mock' cannot embed`.
  - `notes`' build: `warnings` is exactly the entity line for `"notes"`.
  - The named `work` graph (members `[notes, meetings, absent]`): `warnings` is the `absent` line, then the entity line for `"work"`, beside today's assertions (`:486-495`).
  - The communities run (`:563-573`): `warnings` is the summary line. With `"summaries": false`: no `warnings`.
- **A paid embedder.** With a `MockEmbeddingProvider` set metered as the embedding role, the entity line carries the spend rule's note as its `<why>`, the words `graph build` prints for the same config.
- **Nothing to warn.** With a `MockEmbeddingProvider` registered as the embedding role (`admin_knowledge_test.cpp:98` precedent) and every member present, `warnings` is absent from both results. Every other field is as today.
- **The event.** The `admin.job.completed` event on the fixture's bus carries the same `warnings`.
- **The words.** `graph_notes_test.cpp` pins each sentence. `graph_test.cpp`'s existing assertions on the CLI's stdout (`:166`, `:391`, `:514`) stay green, unedited: the CLI's bytes are unchanged.
- **Gate:** `lib/scripts/cicd.sh --app cli --test`, clang-format, and the MinGW scan of the changed files.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Should a mixed named graph with empty members build its code over HTTP instead of answering 404? | `[default]` | Yes. Build the code and say the empty members in `warnings`, as the CLI does (exit 0, `c-n1b`). The 404 sentence in `http-api.md` (`:827-828`) gets a dated revision. A named graph with no source trees keeps its 404. |
| Is `warnings` an array of strings or of objects? | `[default]` | An array of strings in the CLI's words, as `POST /v1/admin/graphs` already returns (`http-api.md:1100`). |
| Does that mixed graph need a generation backend over HTTP? | `[default]` | No. It is decided before the extractor is resolved, as the CLI decides before `graph.cpp:1064-1066`. On a server that serves none, it builds as a graph of trees alone does (`http-api.md:873-874`), and `model` is not consulted. The sentence "A graph holding collections too still needs its extractor for them" (`:877-878`) is revised with the 404. |
| Does `warnings` repeat a fact another field already carries? | `[default]` | No. A gone or empty tree stays in `code.missing_sources` and `code.empty_sources`, a stopped embed in `embed_error`, a failed config edit in `config_warning`, and a failed chunk in `chunks_failed` and its progress event. `warnings` holds only what no field says. |
| Where do the words live? | `[default]` | A new `presentation/operations/graph_notes.h/.cpp` beside `graph_members`. `operations/` is what the CLI and the HTTP server both run (`source/presentation/CLAUDE.md`). If CLI-M58 landed first with these words elsewhere in `operations/`, this item uses that producer. |
| How does the CLI say these notes: which stream, which prefix, and does it keep the `[graph] ` tag? | `(consumed decision)` | [CLI-M58](../../../assistant/cli/MILESTONES.md#2026-11-27--a-runs-notes-are-said-once-on-stderr-maintenance-item-m58-a-runs-notes-are-said-once-on-stderr-in-the-house-voice) (shipped 2026-11-27). It moves them to stderr as `apogee graph: …` and keeps `only its code was built` on stdout as the outcome, its tag dropped and its first word capitalised. This item takes only the bare sentences from the shared producer, in today's words, so a `warnings` entry stays lower-case. |
| What does `config_warning` say when a collection's registration fails? | `(consumed decision)` | [CLI-M46](../../../assistant/cli/MILESTONES.md#2026-11-27--a-collection-the-config-cannot-hold-is-refused-before-it-is-written-maintenance-item-m46-a-collection-the-config-cannot-hold-is-refused-before-it-is-written) (shipped 2026-11-27), whose job warning names the edit that failed. This item leaves the field where it is and does not copy it into `warnings`. |
| How does the app show a server job's `warnings`? | `(consumed decision)` | [APP-7c](../../darwin/v0.2.4/every-place-against-a-server.md), which fills a remote job's notes from them (APP-M22's Out of scope). |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-26 | **Diagnosed:** `admin_graph.cpp` renders its job results from the build's result structs, never from the notes the CLI prints from the same producers. It also tells a code-only graph by its having no collections, so a mixed graph whose members hold no data reaches the 404 the CLI never gives (Evidence 2 and 6). | the spec |
| 2026-11-26 | **Placed in Maintenance as M59**, by the user's call to spec the APP-M21 and APP-M22 probes' CLI asides as maintenance items from M48, grouped only where they share one seam. This is its own item because its seam is the admin plane's job results in `admin_graph.cpp`, not the CLI's text stream that CLI-M58 changes. The job results lack these fields whatever stream the CLI uses. Its surface (`http-api.md`, `admin_graph_test`) and its fix ship alone. | the user (placement) · the spec (the split) |
| 2026-11-26 | **No gate either way.** Built one after the other with CLI-M45 and CLI-M46, which edit `admin_graph.cpp`, and with CLI-M58, which shares the graph notes' producer. Whichever of CLI-M58 and this item lands second reuses the producer the first one made. **Not gated on APP-M22**, as CLI-M58 is: no client in the repository reads these job results, so no note the app shows today moves (Evidence 10). | the spec |
| 2026-11-26 | **The mixed graph's change ships as a dated revision of the reference.** If the defaults of the first and third open questions stand, the build revises `http-api.md:827-828` ("`404` when … no member of a named graph has any") and `:877-878` ("A graph holding collections too still needs its extractor for them") in the same change. It is a parity fix under ADR 0002, written as a dated revision because ADR 0006 holds a behaviour contract named in the reference docs until one says otherwise. A named graph with no trees keeps its 404. | the spec |
| 2026-11-27 | **CLI-M58 landed first, so this item reuses its producer**: `presentation/operations/graph_notes.h/.cpp` exists with the five functions sketched here (`entity_vectors_skipped`, `summary_vectors_skipped`, `member_without_database`, `only_code_built`, `members_without_database`), today's words with no prefix or tag, pinned by `tests/presentation/operations/graph_notes_test.cpp`. Its new-file rows are reuse, and its `graph.cpp` row is empty: the CLI's sites already print the producer's words (on stderr as `apogee graph: …`, the code-only outcome on stdout with its first word capitalised). Its acceptance line on the CLI's bytes now means CLI-M58's bytes. | CLI-M58's build |

## Out of scope

- **An HTTP ingest route**, and with it `embed ingest --graph`'s twin. None exists yet (`http-api.md:1606-1609`).
- **The CLI's own stream for these notes**: moving them to stderr in the house voice, and saying the member fact once: [CLI-M58](../../../assistant/cli/MILESTONES.md#2026-11-27--a-runs-notes-are-said-once-on-stderr-maintenance-item-m58-a-runs-notes-are-said-once-on-stderr-in-the-house-voice) (shipped 2026-11-27).
- **The models verbs' notes**: [CLI-M60](the-models-verbs-say-their-notes-on-stderr.md).
- **`config_warning`'s words and the graph toggle's status codes**: [CLI-M46](../../../assistant/cli/MILESTONES.md#2026-11-27--a-collection-the-config-cannot-hold-is-refused-before-it-is-written-maintenance-item-m46-a-collection-the-config-cannot-hold-is-refused-before-it-is-written) (shipped 2026-11-27).
- **The plain-name check on `{id}`** (`admin_graph.cpp:39-42`): [CLI-M45](../../../assistant/cli/MILESTONES.md#2026-11-27--a-collection-is-named-not-pathed-at-every-reader-maintenance-item-m45-a-collection-is-named-not-pathed-at-every-reader) (shipped 2026-11-27).
- **The Mac app filling a remote job's notes from these `warnings`**: [APP-7c](../../darwin/v0.2.4/every-place-against-a-server.md).
- **Who may see a graph's members over the admin plane**: [CLI-63b](../v0.2.4/personal-data-has-an-owner.md).
- **A structured face (`--output-format json` or `stream-json`) for `graph build`, `update` and `communities` on the CLI.** [CLI-59d](../v0.2.3/reads-and-commands-over-the-network.md) records giving those verbs their own stream-json as their own work.
- **An adjacent aside the investigation left, not in this item.** Chat's in-session capture never says `result.decision.note` (`chat_session.cpp:2261-2285`), while `knowledge capture` and `POST /v1/admin/knowledge` carry it. It is a twin dropping a note, like this item's, but in knowledge capture, and it cannot be driven with mocks today.

## Links

- **Blocked by:** — · **Blocks:** —
- **Sibling** (the same investigation's seams): [CLI-M58](../../../assistant/cli/MILESTONES.md#2026-11-27--a-runs-notes-are-said-once-on-stderr-maintenance-item-m58-a-runs-notes-are-said-once-on-stderr-in-the-house-voice) (shipped 2026-11-27), the CLI's notes said once on stderr, whose producer this item shares; [CLI-M60](the-models-verbs-say-their-notes-on-stderr.md), the models verbs' notes.
- **Relates to:**
  - [CLI-M45](../../../assistant/cli/MILESTONES.md#2026-11-27--a-collection-is-named-not-pathed-at-every-reader-maintenance-item-m45-a-collection-is-named-not-pathed-at-every-reader) (shipped 2026-11-27) and [CLI-M46](../../../assistant/cli/MILESTONES.md#2026-11-27--a-collection-the-config-cannot-hold-is-refused-before-it-is-written-maintenance-item-m46-a-collection-the-config-cannot-hold-is-refused-before-it-is-written) (shipped 2026-11-27), which also edit `admin_graph.cpp`: built one after the other;
  - [APP-M22](../../../assistant/darwin/MILESTONES.md#2026-11-26--jobs-notes-reach-the-person-maintenance-item-m22-a-jobs-notes-reach-the-person), whose stdout-warnings question this investigation answered, and [APP-M21](../../../assistant/darwin/MILESTONES.md#2026-11-26--listed-name-reaches-the-verb-maintenance-item-m21-a-listed-name-reaches-the-verb-as-listed), whose probes ran beside it;
  - [APP-7c](../../darwin/v0.2.4/every-place-against-a-server.md), which reads these `warnings` as a remote graph job's notes;
  - [CLI-59d](../v0.2.3/reads-and-commands-over-the-network.md), whose door runs `graph build|update|communities` as text jobs, and [CLI-63b](../v0.2.4/personal-data-has-an-owner.md), which changes whom the graph admin twins answer.
- **ADRs:** [ADR 0002](../../../adrs/cli/mode-parity.md) (reads have twins; parity gaps have a home) and [ADR 0006](../../../adrs/cli/backwards-compatibility.md) (additive fields; a dated revision of a recorded contract).
- **Revises:** `http-api.md`'s graph-build 404 rule (`:827-828`) and its "still needs its extractor" sentence (`:877-878`).
- **Follows from:** [Milestone AE](../../../assistant/cli/MILESTONES.md#milestone-ae--the-code-graph) (27k), which gave the route its code-only branch.
