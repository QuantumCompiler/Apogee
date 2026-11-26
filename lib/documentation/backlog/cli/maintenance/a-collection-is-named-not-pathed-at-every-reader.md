# CLI-M45 · A collection is named, not pathed, at every reader

| Field | Value |
|---|---|
| **Type** | 🐞 Bug |
| **Project** | CLI |
| **Status** | 🟢 Ready |
| **Fix version** | Maintenance (any time) |
| **Component** | Retrieval › collection names at `--rag`, `auto_rag`, agent collections, the rag and graph tools, completion |
| **Labels** | `rag` `collections` `safety` `completion` |
| **Epic** | — (maintenance has no track) |
| **Blocked by** | — |
| **Blocks** | [M46](a-collection-the-config-cannot-hold-is-refused-before-it-is-written.md) |
| **Reporter** | CLI-M42's probes (2026-11-22), then a sandboxed investigation, two skeptics and a critic the same day |
| **Created** | 2026-11-22 |

---

## Summary

`embed` refuses a collection name that is a path, as its docs promise: "the path is built, never accepted". The retrieval behind `--rag` checks nothing. So `apogee complete --rag /some/where/app` opens any SQLite file on the machine and writes Apogee's whole schema into it, as do `auto_rag`, an agent's collection and `serve --rag`. Two more rules disagree on the read side. The rag tools refuse registered collections that `list_collections` lists, and completion offers names the verbs split or read as a flag. The item puts one collection read rule in contracts and applies it at every reader.

## Description

### User story

**As** someone who points Apogee at a collection by name, on the command line, in the config or in an agent,
**I want** every reader to take a collection by name only, and the same names everywhere,
**so that** a typo or a pasted path can never rewrite a database outside Apogee's home, and every collection `list_collections` shows can be searched.

### Steps to reproduce

The sandbox, 2026-11-22, with the built binary `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`: a throwaway `APOGEE_HOME` and `HOME`, `env -i`, stdin from `/dev/null`, and a mock backend `echo`.

1. Make an unrelated SQLite file outside the home: `sqlite3 <dir>/outside/foreign.db "create table t(x); insert into t values(1);"`. Record its md5 and `select count(*) from sqlite_master` (1).
2. Run `apogee complete -m echo --rag <dir>/outside/foreign "fox"`.
3. Run `apogee embed query -- <dir>/outside/foreign fox`.

### Actual result

2. `[apogee] no matching context in '<dir>/outside/foreign' [lexical]`, then the answer, exit 0. Afterwards `foreign.db`'s md5 has changed (`ad1705d0…` to `b65b2b34…`), `sqlite_master` holds 49 entries (Apogee's chunk, graph and meta tables beside `t`), and `foreign.db-wal` and `foreign.db-shm` exist beside it.
3. `apogee embed: '<dir>/outside/foreign' is not a plain collection name`, exit 1.

### Expected result

2. The same refusal as `embed query`: `is not a plain collection name`, exit 1, before anything is opened, with `foreign.db`'s bytes unchanged.

### Background

A collection is a file the home builds: `embeddings/<name>.db`. The rule that a collection is named, not pathed, was written into `embed` on 2026-09-07 (5c0d514e, `plain_name_error`, `embed.cpp:40-60`). Its comment says why: "A `..` or an absolute path here would turn 'search my notes' into a way to address any file on the machine". DEVELOPER.md's `embed` row says it too: "the name must be a plain one -- the path is built, never accepted".

The rule was copied by hand into six places, and never into the reader the most surfaces share. `retrieve_for_collection` (`presentation/operations/retrieval.cpp:21`) sets `turn.store_path = collection_path(collection)` with no check. It serves `--rag` on `complete`, `chat` and `analyze`, `auto_rag`, an agent's collection, and `serve --rag`. The rag tools carry a third, stricter rule of their own.

CLI-M42's investigation found the spaced collection, and this run's critic found the path escape while checking the fix. The write-side half, a name the config can't register, is CLI-M46, which builds on this item's rule.

### Evidence

All measured 2026-11-22 by an investigator, two skeptics and a critic, each in its own sandbox. The path escape was measured again by the spec's author.

1. **A read writes outside the home.**
   - `complete -m echo --rag <abs>/outside/foreign` and, with an `embeddings/` folder present, `--rag ../foreign` both open the foreign file. Its md5 changes, `sqlite_master` goes from 1 entry to 49, and `-wal` and `-shm` files appear.
   - Nothing on the read path checks the name: `retrieval.cpp:21`.
   - `embed query` on the same path refuses (`embed.cpp:45-54`).
2. **Which inputs reach it.** All are local:
   - the `--rag` flag on `complete`, `chat` and `analyze`;
   - `auto_rag` (`config.cpp:1983`) and `agents.*.collection` (`config.cpp:925`), neither of which is path-checked at load, while `knowledge.db` is (`config.cpp:2380-2386`);
   - `agents create --collection` and `POST /v1/admin/agents` (admin token), which accept a path;
   - `serve --rag`, the operator's flag (`handler.cpp:496`).

   An HTTP chat client can't choose the collection: the handler takes only the retriever and rerank from the request (`handler.cpp:336-339`).
3. **The path rule is copied six times**: `embed.cpp:45-54`, `graph.cpp:60-66`, `knowledge.cpp:92-97`, `admin_knowledge.cpp:64-67`, `admin_graph.cpp:39-42` and `operations/graph_members.cpp:64-80`. The graph tools take any file on disk (`business/graph/navigate.cpp:73-76`).
4. **The rag tools have a third rule.** `valid_collection_name` (`business/tools/rag_query.cpp:60-67`: ASCII letters and digits plus `. _ -`, at most 128 characters) is applied by `search_documents` and `collection_info` (`:91-92`, `:240-241`). `list_collections` (`:207-224`) lists every file. Measured with a hand-written JSONC entry `"my notes"` and a registered 129-character name:
   - `list_collections` lists `my notes  7 chunks`;
   - `search_documents {collection: "my notes"}` answers `Error: collection is required: a name like the ones list_collections shows`;
   - a 128-character name is searched, and a 129-character one gets the same error, while `embed query` reads it.

   `mcp serve` registers the same tools (`toolsets.cpp:96`), by reading.
5. **Completion offers names the verbs can't take.** The collection, graph and list kinds (`complete_sources.cpp:361-369`) offer every file stem. Measured: `__complete embed query ''` offers `-x`, `a`, `b`, `c,d` and `my notes`, where `a` and `b` are one store named `a\nb`. `embed ingest -x b.txt` then reads `-x` as a flag (`path is required`, exit 106). A comma list (`config add-graph g --collections`) splits `c,d`.
6. **Windows, by reading only.** `embeddings_dir() / (name + ".db")` with a drive-relative name (`D:x`, `C:notes`) would rebase or replace the path under `std::filesystem`'s append rules, so the path rule alone does not hold there. No Windows host was run.
7. **What already works**, and must stay:
   - `embed query`, `list`, `info` and `delete` refuse `..`, `/` and `\`;
   - every store under `embeddings/` reads, whatever its name, `my notes` and `a@b` included;
   - on APFS, case variants reach the same file (`Notes` after `notes` appends to `notes.db`, and `graph build Notes` enables `notes`).
8. **Not pinned.** No test runs `--rag` with a path, and no test searches a registered name the rag tools' rule refuses.

## Acceptance criteria

- [ ] **Given** `--rag <abs>/x`, `--rag ../x` or `--rag a/b`, on `complete`, `chat` or `analyze`, **when** the run starts, **then** it exits 1 with `'<name>' is not a plain collection name` before any file is opened, and a file at that path keeps its bytes.
- [ ] **Given** `auto_rag` or `agents.<a>.collection` holding a path in an older config, **then** the config still loads, the turn that would read it refuses with the same words, and `check` fails that row naming the key.
- [ ] **Given** `agents create --collection <path>` or `POST /v1/admin/agents` with one, **then** it is refused at add (exit 1, or 400), the config unchanged.
- [ ] **Given** `serve --rag <path>`, **then** serve refuses to start with the same words.
- [ ] **Given** a registered or on-disk collection named `my notes`, or one of 129 characters, **when** a model calls `search_documents` or `collection_info` with it, **then** it is searched and described, as `list_collections` lists it. `../x` and `/x` are refused.
- [ ] **Given** stores named `a\nb`, `c,d` and `-x`, **when** Tab completes a collection, graph or comma-list word, **then** `a\nb` is never offered, `c,d` is not offered in a comma list, and `-x` is offered only where the verb takes it (after `--`). `__complete graph stats -- ''` keeps offering every graph and collection, dash names included, since the Mac app's Graph list asks it that way (APP-M21).
- [ ] **Given** any store under `embeddings/` today, **then** every reader reaches it as before.

## Constraints

- **A collection is named, not pathed** (`embed.cpp:40-44`, DEVELOPER.md's `embed` row): one rule, in contracts, so cli, operations, httpserver and business tools all reach the same function. No reader builds a store path from an unchecked name.
- **Older configs load** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)). A path already in `auto_rag` or an agent's collection is refused at use and named by `check`, never a failed load.
- **The read rule accepts every store that exists.** It refuses only what can escape the folder: empty, `..`, `/`, `\`, and on Windows a root name. No cap or character set below what the create rule (CLI-M46) writes, so no collection that reads today stops reading.
- **The offer is a contract** ([ADR 0007](../../../adrs/cli/tab-completion.md)): completion offers no name the verb would split or misread.
- **Mode parity** ([ADR 0002](../../../adrs/cli/mode-parity.md)): the HTTP agent route refuses as the CLI verb does.

## Technical approach

Paths under `lib/src/cli/`.

| File | Change |
|---|---|
| `source/data/contracts/collection_name.h`, `.cpp` (new) | `collection_read_error(std::string_view name)`: empty, `..`, `/`, `\`, and `std::filesystem::path(name).has_root_name()` (which fires only on Windows, so a macOS store named `a:b` keeps reading), with today's words (`'<name>' is not a plain collection name`). CLI-M46 adds the create rule beside it. |
| `source/presentation/operations/retrieval.cpp` | `retrieve_for_collection` (`:21`) checks the name before `collection_path`, and refuses with the rule's words. |
| `source/business/tools/rag_query.cpp` | `valid_collection_name` (`:60-67`) is replaced by the read rule, keeping the tools' existence check, so anything `list_collections` lists can be searched. |
| `source/business/graph/navigate.cpp` | The graph tools (`:73-76`) take the read rule. |
| `source/presentation/cli/embed.cpp`, `graph.cpp`, `knowledge.cpp`, `source/presentation/operations/graph_members.cpp`, `source/presentation/httpserver/admin_knowledge.cpp`, `admin_graph.cpp` | The six copies of the path rule call the shared one (`graph_members.cpp`'s graph-name check stays its own words, `not a plain graph name`, over the same rule). |
| `source/presentation/cli/agents_cmd.cpp`, `source/presentation/httpserver/admin_agents.cpp` | `agents create --collection` and the POST's `collection` take the read rule at add. |
| `source/presentation/cli/check.cpp` | A row for `auto_rag` and each agent's collection that holds a path: fail, naming the key and the hand edit. |
| `source/presentation/cli/serve_cmd.cpp` | `--rag` takes the read rule at start. |
| `source/presentation/cli/complete_sources.cpp` | The collection, graph and list kinds (`:361-369`) drop names holding CR or LF, and the list kind also drops names holding `,`. A name starting with `-` is offered only after `--` (or not at all, if the walk can't tell). |
| `tests/data/contracts/collection_name_test.cpp` (new) | The rule's table, and under `_WIN32` `D:x` and `C:notes`. |
| `tests/business/tools/rag_query_test.cpp`, `tests/presentation/cli/complete_sources_test.cpp`, `tests/presentation/httpserver/admin_agents_test.cpp`, `tests/scripts/cmake/config_e2e.cmake` | The fixtures under Testing. |

**Reference.** `plain_name_error` (`embed.cpp:40-60`, 5c0d514e), the rule this lifts into contracts. `knowledge.db`'s load-time path check (`config.cpp:2380-2386`), the one config value that already refuses a path.

**Docs when it ships:**
- DEVELOPER.md's `embed`, `rag_query` and retrieval rows: one read rule, at every reader.
- `reference/cli/tools.md`'s rag tools and `--rag`.
- `reference/cli/http-api.md`'s agent routes.
- MILESTONES: a dated entry.

## Testing

Each fixture fails on today's binary (measured 2026-11-22).

- **The bug as a fixture: the path escape.** In `config_e2e.cmake`: copy any store file to `foreign.db` outside the home's `embeddings/`, and record its bytes. `complete -m echo --rag ../foreign` (with `embeddings/` present) and `--rag <abs>/foreign` each exit 1 with `is not a plain collection name`, and `foreign.db`'s bytes and directory listing are unchanged (no `-wal`, no `-shm`).
- **Agent and `auto_rag` values**: a hand-written agent collection `<abs>/x` loads, a turn using the agent refuses, and `check` fails the row. `agents create h --collection ../x` exits 1, the config unchanged.
- **The rag tools**: a registered `my notes` and a 129-character name are searched and described; `../x` and `/x` are refused.
- **Completion**: with stores `a\nb`, `c,d` and `-x`, the collection, graph and list kinds never offer `a\nb`, and the list kind doesn't offer `c,d`.
- **Unchanged**: every store under `embeddings/` reads (`my notes`, `a@b`, a 244-byte name), and on a case-insensitive filesystem `Notes` reaches `notes.db`.
- **Gate:** `lib/scripts/cicd.sh --app cli --test`, clang-format, and the MinGW scan of the changed files.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Should a config value that is already a path (`auto_rag`, an agent's collection) fail the config load, as `knowledge.db` does? | `[default]` | No. An older config loads (ADR 0006). The reader refuses at use with the plain-name words, `check` fails the row, and new values are refused at add. |
| Should the rag tools keep a stricter rule for model-supplied names? | `[default]` | No. They take the shared read rule, which still refuses every escape, and keep their existence check. So anything `list_collections` lists can be searched and described. |
| A store name starting with `-`: offer it at all? | `[default]` | Offer it only where the walk knows the word follows `--`; otherwise drop it. The verb reads it only after `--`. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-22 | Specced from CLI-M42's Out of scope, a sandboxed investigation, two skeptics and a critic (Evidence). Placed in **Maintenance** as M45, ahead of the write-side half (CLI-M46), because a read that rewrites a file outside the home is the most severe part, and the smallest. | the spec |
| 2026-11-22 | **Local input, not remote.** Every input that reaches the unchecked reader is the person's own flag, config or admin-token call. An HTTP chat client can't pick the collection. So this is a data-integrity fix, not a network exposure. | the spec |

## Out of scope

- **A name the config can't register**, refused before anything is written, the registration made one step, and the length caps: [CLI-M46](a-collection-the-config-cannot-hold-is-refused-before-it-is-written.md).
- **The Mac app passes no `--` before a collection name** (`CollectionsViewModel.swift:29-35`, `:48-50`), so a store named `-x` can't be read, ingested into or deleted from the app (exit 106): [APP-M21](../../darwin/macos/maintenance/a-listed-name-reaches-the-verb-as-listed.md), and the app's other sites, [APP-M23](../../darwin/macos/maintenance/every-name-the-app-passes-is-read-as-a-name.md).
- **How far `--rag` into a live third-party database hurts it** beyond what was measured (tables added, WAL on). The fix makes it unreachable.

## Links

- **Blocked by:** — · **Blocks:** [CLI-M46](a-collection-the-config-cannot-hold-is-refused-before-it-is-written.md), which adds the create rule beside this read rule
- **Relates to:**
  - [CLI-M42](stubs-read-the-words-as-the-shell-does.md), whose investigation found the spaced collection;
  - 5c0d514e, the path rule's first commit;
  - DEVELOPER.md's `embed` row ("the path is built, never accepted");
  - [APP-M21](../../darwin/macos/maintenance/a-listed-name-reaches-the-verb-as-listed.md), whose Graph list relies on this item's completion offering dash names after `--`.
