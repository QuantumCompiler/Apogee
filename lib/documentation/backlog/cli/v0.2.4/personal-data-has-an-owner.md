# CLI-63b · Personal data has an owner at every door (split first)

| Field | Value |
|---|---|
| **Type** | ✨ Feature |
| **Project** | CLI |
| **Status** | 🔒 Blocked |
| **Fix version** | v0.2.4 |
| **Component** | Personal data: the layout, sessions, recall, notes, knowledge, tasks and training, at every door (CLI, inference, harness and admin planes) |
| **Labels** | `split-first` `people` `privacy` `serve` |
| **Epic** | Track 63 · people and access |
| **Blocked by** | [63a](people-on-one-server.md) |
| **Blocks** | — |
| **Reporter** | The admin-mode and people spike (2026-11-14), wall P2 |
| **Created** | 2026-11-14 |

---

## Summary

On a server with several people, personal data is not separated: a second bearer lists, reads, continues and deletes the first's served sessions, and every personal row is one install-wide folder. This item makes everything a person keeps **theirs**: stored where only they reach it, stamped with who they are, and filtered at every door that lists, reads, writes or deletes it. The two exceptions are the owner's metadata reads, which never carry contents, and shared libraries, which the owner hands out per person.

## Description

### User story

**As** a member of a household or small team using the owner's Apogee server,
**I want** my chats, recall, notes, decisions and tasks to be mine alone, with shared libraries reaching me only when the owner hands them out,
**so that** no other member reads my data and the owner sees my activity and metadata, never my chat contents.

### Background

The user's calls of 2026-11-14 make `serve` a server for **people**: an owner (the account that owns the server Mac's `APOGEE_HOME`) and members, in a household or a small team. Each member has "their own private data (chats, recall, notes, decisions, tasks)", and "the owner sees members' activity and metadata, never their chat contents" ([SPEC.md](../../../assistant/cli/SPEC.md) → Core surfaces 4, "Revised again 2026-11-14"). [63a](people-on-one-server.md) gives every credential a person. This item makes everything a person keeps theirs.

It closes wall **P2** of the admin-mode and people spike (2026-11-14): personal data is not separated.

### Evidence

Evidence re-checked 2026-11-14 at HEAD (`4acf4a2b`), and the spike's central probe re-run the same day on the branch binary (`apogee 0.2.0 (b37b0a9f)`), in a scratch `APOGEE_HOME` under the scratchpad, with one `mock` backend, under `serve --port 0` on loopback, killed after.

1. **P2.1 — Served sessions belong to whoever asks.** This is the regression fixture: the **two-bearer probe**. Bearer `person_alice` created a session (`"session_id":"new"`, message `ALICE-PRIVATE-63B`), and its id came back as `20261114-200346-fb61`. Then, under bearer `person_bob`:
   - `GET /v1/sessions` returned `200` and listed alice's session.
   - `GET /v1/sessions/<id>` returned `200` with `ALICE-PRIVATE-63B`.
   - A `POST /v1/chat/completions` carrying alice's `session_id` returned `200`, and alice's own read then held `bob: injected` as a user turn.
   - `DELETE /v1/sessions/<id>` returned `204`, and alice's next `GET` was `404 session_not_found`.

   Run once more on 2026-11-14 for this document's review (session `20261114-201120-95e2`), it gave the same four answers. The `204` drops the session from the server's memory (`SessionStore::erase`, `presentation/httpserver/session.cpp:85-101`). The transcript stayed in `sessions/` at `-rw-r--r--`, so alice lost her conversation on the server while its contents stayed on disk.
2. **P2.1 — Why: the session doors never read the caller.** The session routes are ungated (`admin=false`, `presentation/httpserver/mux.cpp:52-63`). The bearer is read only on `/v1/admin` paths (`:631-641`). `list_sessions`, `get_session` and `delete_session` take the request as an unused `/*request*/` (`presentation/httpserver/handler.cpp:917-957`), and a turn continues any id it is handed (`:347-356`). `SessionStore::create` stamps no caller (`presentation/httpserver/session.cpp:32-46`). A served session **is** a chat file (`presentation/httpserver/session.h:24-28`). Its stored keys were `backend, chat_id, compactions, messages, schema_version, started_at, turns, updated_at`, with no person. `apogee chats list --output-format json` listed it with `"origin":""`. Ids are a timestamp plus 16 random bits (`data/logger/session.cpp:251-269`), so guessing one is cheap. `logger::Session` has no field for a person; `origin` and `cwd` are ACP's alone (`data/logger/session.h:227-236`).
3. **P2.2 — Personal rows are single install-wide folders.** The layout declares 20 rows (`data/contracts/layout.cpp:17-78`). `LayoutEntry` carries a path, a purpose, `private_mode` and `user_data`, and no scope (`data/contracts/layout.h:35-50`). The spike classified the rows for a server with several people:
   - **7 personal:** `sessions`, `attachments`, `memory`, `notes`, `analyses`, `knowledge` (raw conversations) and `tasks`.
   - **8 shared:** `models`, `embeddings`, `prompts`, `schemas`, `symphonies`, `recipes`, `review-skills` and `cache`.
   - **5 the server's own (admin):** `config`, `logs`, `mcp`, `training` and `review`.

   Each personal row is one flat folder for everyone.
4. **P2.3 — One recall index mixes everyone's chats.** There is one index, `memory/chats.db` (`layout.cpp:98-100`), and one `memory/pending/` (`layout.h:69`). `RecallEntry` is `{chat_id, title, updated_at, summary}`, with no owner (`business/agentloop/recall.h:55-63`). Recall is on by default for chat (`data/contracts/config.h:910-914`; `presentation/cli/chat_session.cpp:1294`). It is kept off `serve` only structurally (`recall.h:23-24`). 59b's network sessions are machine-mode `chat` children, which do recall. 59f turns recall off for apps only ([paired-apps-with-scopes.md](../v0.2.3/paired-apps-with-scopes.md), Technical approach, the harness plane's sessions: under `--scope-file` the child's recall is off). A member's device session would recall every other person's summaries.
5. **P2.4 — Notes, analyses, knowledge, collections and graphs have no owner.**
   - Served tools register the notes toolset once, over the one `notes/` folder (`presentation/cli/serve_cmd.cpp:358-375`; `business/tools/toolsets.cpp:91-94`).
   - `notes` and `analyses` are not private (`layout.cpp:37,41`), and on the probe home both were `drwxr-xr-x`.
   - A knowledge `Record` has no author or audience (`business/knowledge/record.h:64-85`), and every record goes into one collection, `knowledge` by default (`config.h:863-876`).
   - Collections are `embeddings/<name>.db` (`presentation/operations/collections.cpp:11-13`; `business/tools/rag_query.cpp:19-21`), and graphs are `embeddings/graphs/<name>.db` (`business/agentloop/graph_context.cpp:157-159`), all one namespace.
   - The knowledge admin routes (`mux.cpp:183-213`) and `GET /v1/admin/tasks*` (`:386-393`) answer the whole install.
6. **P2.5 — Personal files are not always written private, and training reads every chat.**
   - A transcript is written at the umask's mode, `harness::write_file_atomically(path, serialize(stamped))` with no `private_mode` (`session.cpp:617`). On the probe it was `-rw-r--r--`, inside a `sessions/` that `check --fix` had made `0700`. The spike's home, made by `config init` then `serve`, had `sessions/` itself at `drwxr-xr-x`.
   - A note is written with `create_directories` and a plain `ofstream` (`business/tools/notes.cpp:80-81`).
   - An analysis is saved under `home / "analyses"`, derived beside the layout rather than through `analyses_dir()` (`presentation/cli/analyze.cpp:335`), and written at the umask's mode (`:358-366`).
   - A dataset built from sessions reads `logger::list_sessions()` whole (`presentation/operations/dataset_core.cpp:22-25`). The cycle's `sessions` source loads the same `load_session_views()` (`presentation/cli/train.cpp:3791`), and `collect_sessions` mines all of it behind one install-wide `log_consent` switch (`business/training/cycle.cpp:335-364`). `SessionFilter` narrows by backend and date only (`business/training/datasets.h:113-119`).
   - A task's ledger names no starter (`business/tasks/task.h:233-266`), and its `person` authority word is the anonymous "person at the terminal" (`:150-161`).

### Split first

This document is one item's whole design, and it is built as three documents, each gated as shown. Final ids are assigned when it is split.

1. **Sessions, attachments, recall and the task ledger carry their person** (`sessions-and-tasks-have-a-person.md`, gated on 63a). This part includes:
   - the layout's scope per row (`personal`, `shared` or `admin`, 7/8/5), and a personal root per member;
   - an additive `person` on `logger::Session`, and the task ledger's person read from `started_by` (63e's field);
   - served sessions, 59b's machine sessions, 59d's command children and 59e's served tasks, each run against the caller's person;
   - one recall index per person;
   - `chats list|show|resume|delete`, `task list|status|attach|halt|cancel|resume` and the admin task routes, filtered to the caller's person;
   - the owner's metadata-only reads (`--person`).

   The two-bearer probe is its regression fixture.
2. **Notes, decisions and collections: personal by default, shared libraries handed out per person** (`notes-knowledge-and-collections-per-person.md`, gated on (1)). This part includes:
   - notes and analyses under the person's root, with the served notes tools bound per request;
   - a member's own folder for the served file tools, which 63a hands to this item ([people-on-one-server.md](people-on-one-server.md), Open questions, "What does each preset hold?": "A folder of a person's own is 63b's");
   - knowledge records, and their raw conversations, in the person's own default collection;
   - collections and graphs with a visibility: a shared library reached by a member only when their allowance hands it out, or a member's own private one;
   - retrieval, `auto_rag`, recalled decisions, the graph tools and graph expansion, each reading through one visibility rule.
3. **Private at every writer, and training reads only the owner's chats** (`private-at-every-writer.md`). This part includes:
   - transcripts, notes, analyses, ledgers, the line history and `config.json` written `0600`, and every private folder created `0700`, by the code that writes it, not only at `check --fix`;
   - `notes` and `analyses` marked private in the layout;
   - session-sourced datasets and the cycle reading the owner's chats only, unless a member's are named, with that member's recorded consent.

   Its writer half carries no gate and may land first. Its training half is gated on (1).

## Acceptance criteria

- [ ] (1) **Given** a sandbox home with the mock backend, the owner plus members `alice` and `bob`, under `serve --port 0` on loopback, **when** alice's session is listed, read, continued and deleted, **then** alice alone can do each, and bob gets `404` on each, as he does for an id that does not exist. Running the two-bearer probe's steps against this build gives that answer, where the probe on `b37b0a9f` gave `200`, `200`, `200` and `204`.
- [ ] (1) **Given** alice's session, **when** its file and the owner's listings are read, **then** the session file is in `people/alice/sessions/`, carries `"person"`, and is `0600`; `apogee chats list` (the owner) does not list it; and `apogee chats list --person alice --output-format json` lists its id, backend, turns and times, and no title, name or message.
- [ ] (1) **Given** 59b's harness plane (where shipped) and 59d's door, **when** alice chats and lists, **then** alice's chat child recalls from `people/alice/memory/chats.db` only; alice's `chats list` and `task list` print hers alone; bob cannot attach to, halt, cancel or resume alice's task; and `GET /v1/admin/tasks` shows alice's task as metadata.
- [ ] (2) **Given** alice's served requests, **when** her model writes a note and a record is auto-captured, **then** the note lands in `people/alice/notes/` and bob's model cannot read it; alice's auto-captured record goes into her own `knowledge` collection; `knowledge share` copies it into a shared collection her allowance lets her write, with `person` set; and a file tool under alice's request works in `people/alice/files/`.
- [ ] (2) **Given** a shared collection outside bob's allowance, one handed to him, and alice's own collection, **when** retrieval runs for bob, **then** the one outside his allowance is not retrieved for him by `--rag`, `auto_rag`, `rag_query`, recalled decisions or the graph tools; the one handed to him is retrieved; and alice's own collection is never retrieved for bob or the owner.
- [ ] (3) **Given** `umask 022`, **when** a chat, a served session, a note and an analysis are written, **then** no file under any personal row is readable beyond the owner, `notes/` and `analyses/` are `0700`, and `config.json` is `0600`.
- [ ] (3) **Given** a home with members, **when** `apogee datasets create --from-sessions` runs, **then** it holds only the owner's chats; `--person bob` is refused naming `datasets consent`, and after bob's consent it includes his.
- [ ] **Given** an install with no members, **when** every existing suite runs, **then** each passes unchanged; `chat`, `complete`, `execute` and `task run` are byte-identical, and a fresh install has no `people/`. The full suite is green on all five targets.
- [ ] Optional live pass, said when skipped: the user runs `apogee-dev serve`, pairs two members' devices from 63a, and sees each one's chats, notes and tasks only their own, and the server window's people screen showing counts and times without contents.

## Constraints

- **Remote use is an add-on (the anti-goal).** Nothing here may make standalone use need a server, a network, an account or a person. An install with no members behaves byte for byte as today. `chat`, `complete`, `execute`, `task`, `acp`, `mcp serve` and the Mac app over pipes run as the owner, and the owner's personal rows stay where they are, with no migration. A file with no `person` reads as the owner's. `people/` is never seeded, so a fresh install's tree is unchanged and `cli.install_parity` stays green. A person is set for a process only when `serve` hands its child one (Technical approach, `paths.h`); otherwise nothing new is read.
- **Never contents to the owner.** The owner's reads of a member's data are metadata: ids, counts, sizes, times, backends and models. They never include messages, a chat's title or name (which are drawn from the conversation), a task's goal or plan, a note's text, a record's fields, or a collection's chunks. This holds on every surface: the CLI, the admin plane, the server window, logs and bus events. **Said honestly in the docs and the people screen:** these files sit on the server Mac's disk, under the owner's OS account. Whoever controls that Mac can read them with any tool. The promise is that Apogee never shows them, not that it encrypts them.
- **One layout declaration** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → One layout declaration). Which rows are personal is a column in the one table. Seeding a person, `check`, `reset` and removing a person enumerate it, and no second list of personal folders exists anywhere.
- **One config mutation path.** A member's own things (their collections, their notes, their consent) are never entries in the shared config. They live in files under their personal root. Owner changes still go through `contracts/config_edit.h`.
- **Secrets are unleakable by construction.** Nothing here carries a key. A person id is not a credential. The metadata views are types with no field for contents, as `backend_view` has no field for `api_key`.
- **Additive** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)). The `person` fields, the `--person` options, the per-person paths and the metadata documents are additions. A stock OpenAI client with no Apogee credential gets the same routes, bodies and errors.
- **Parity** ([ADR 0002](../../../adrs/cli/mode-parity.md); [ADR 0010](../../../adrs/cli/the-shell-is-a-mode.md)). One per-person store and one visibility rule serve the CLI, the inference plane, the harness plane (59b, 59d, 59e) and the admin plane. `--person` is classified for the shell and for 59d's door, where it is owner-only.
- **Never listens; serving = server.** All of this is file layout and filtering in socket-free code. `httpserver/serve.cpp` stays the one object that may reference `listen`/`accept`.
- **The layers** ([ADR 0001](../../../adrs/cli/layer-enforcement.md), [ADR 0008](../../../adrs/cli/module-map.md)). The scope column and the personal root sit in `contracts` (Data). The person on a record sits in `logger` and `tasks`. The visibility rule extends 59f's Business `scope` module. The doors stay in `httpserver` and `cli`.
- **The dev channel** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel)). Probes and tests use a temporary `APOGEE_HOME` and the mock backend. A live pass runs `apogee-dev serve` over `~/.apogee-dev`, never the release `apogee`.

## Technical approach

The seam, by the part of the split it belongs to.

**(1) Sessions, attachments, recall and the task ledger.**

| File | Change |
|---|---|
| `source/data/contracts/layout.h/.cpp` | `LayoutEntry` gains `scope` (`LayoutScope::Personal\|Shared\|Admin`), required on every row of `kDirectories`; `personal_rows()`; `person_home(id)`, which is `<APOGEE_HOME>/people/<id>/`, declared beside the rows and not seeded; `seed_person(id)`, which creates that person's personal rows `0700` from the declaration, plus the person's own `embeddings/` and `files/` for (2); the personal accessors (`sessions_dir`, `attachments_dir`, `memory_dir`, `recall_index_path`, the pending markers' folder, `notes_dir`, `analyses_dir`, `knowledge_raw_dir`, and the task root) resolve through `personal_home()`. |
| `source/data/contracts/paths.h` | `personal_home()` is `apogee_home()`, unless this process was handed a person. A member's process is `person_home(id)`. The person is set once at startup, from the person in 59f's `--scope-file` (63a carries it there), and never from argv or the environment. |
| `source/data/logger/session.h/.cpp` | `std::string person`, additive, written only when set and never rewritten by a resume (as `origin` is). `list_sessions()` reads the personal `sessions_dir()`, so it lists the caller's own by construction. `load` refuses a file whose `person` disagrees with the folder it is in, and one naming a person the registry does not hold (63a's fail-closed rule: absent means the owner, unknown is never the owner). |
| `source/presentation/httpserver/session.h/.cpp` | `SessionStore::create(backend, params, person)` stamps the person and saves under that person's folder. `get`, `list` and `erase` take the caller and answer only that caller's sessions. Every other id, existing or not, is the same `404 session_not_found`. The one exception is the owner ending a member's live session (Open questions), which answers `204` and returns nothing of it. 59f's anonymous bucket stays in the owner's folder with no person (consumed decision). |
| `handler.cpp:347-391,917-957` | The request's caller (59a's gate, then 59f's credential, then 63a's person) is passed to the store on create, continue, list, get and delete. |
| 59b's `machine_sessions`, 59d's `command_door` and 59e's served task | The child is started with the caller's person in its scope file, so its sessions, attachments, recall and ledgers are that person's. A member's chat child recalls from the member's own index, and an app's still never recalls (59f). |
| `source/business/agentloop/recall.h/.cpp`, `presentation/cli/chat_recall.cpp` | No code change beyond the path. The index and the pending markers follow `personal_home()`. |
| `source/business/tasks/task.h`, `ledger.h/.cpp`, `view.h/.cpp` | The task's person is `started_by`, which 63e decides is the person's id (absent means the owner), with `started_via` naming the credential. This item adds no second field. Whichever of (1), 63d and 63e builds first carries that revision of 59f's key and its migration, as 63e records ([task-slots-per-person.md](task-slots-per-person.md): Decision log, 2026-11-14, "`started_by` is the person", and Open questions, "Who answers a question raised inside a running task?": whichever of 63d and 63e builds first carries the `started_by` revision and its migration). Ledgers go under the personal task root. `task.lock` stays install-wide at the owner's `tasks/task.lock` until 63e moves it. A `TaskMetaView` (id, person, status, rounds, backend, started and updated), with no goal, plan, checks or policy. |
| `source/presentation/cli/chat_history.cpp` (`chats list\|show\|resume\|delete`), `task_cmd.cpp` | Read the personal store. The owner gains `chats list --person NAME\|all` and `task list --person NAME\|all`, metadata-only documents (`chats.person_list`, `tasks.person_list`). A member's credential is refused `--person` by 59d's door. `tui_parity.cpp` and 59d's `remote_parity.cpp` classify the option. |
| `source/presentation/httpserver/admin_*` | `GET /v1/admin/tasks` answers the owner's tasks in full and members' as `TaskMetaView`. `GET /v1/admin/tasks/{id}` of a member's task answers the meta view. |
| `source/presentation/cli/check.cpp` | One row per person, checking that their root and each personal row are `0700`, and `--fix` tightens them. `reset` and the removal of a person (63a's verb) enumerate `personal_rows()` under `person_home(id)`. |

**(2) Notes, decisions, collections.**

| File | Change |
|---|---|
| `business/tools/notes.h/.cpp`, `toolsets.cpp:91-94` | The notes tools read their folder at call time from the request's person. They use the per-request binding the todo board already uses on `serve` (`serve_cmd.cpp:370-372`), so one served registry serves every person. |
| `presentation/cli/analyze.cpp:330-336` | The default save folder is `analyses_dir()`, the layout's accessor, so it follows the person. |
| The served file tools (`layout.h`, `seed_person`) | A member's request takes `person_home(id)/files/` as its folder, declared in `layout.h` beside `person_home` and created `0700` by `seed_person`. It is bound per request as the notes tools are. The owner's requests keep `tools.fs_root`, as today. |
| `business/knowledge/`, `presentation/cli/knowledge.cpp` | A member's default collection is their own `knowledge` under `person_home(id)/embeddings/`. Auto-capture writes there, and raw conversations go to their `knowledge/raw`. `Record` gains `person`, additive; it is set when a record is shared, so a shared library says whose it was. `knowledge share ID --into COLLECTION` copies one of the caller's own records into a shared collection their allowance lets them write. |
| 59f's Business `scope` module | The visibility rule: `visible_collections(person, config, allowance)` returns the shared collections the person's allowance hands out (63a's `collections` field) plus the person's own. The owner sees every shared one and their own. `collection_path` and `graph_db_path` (`operations/collections.cpp:11-13`, `tools/rag_query.cpp:19-21`, `agentloop/graph_context.cpp:157-159`) resolve a name through it: the person's own first, then a visible shared one. A named multi-collection graph is visible only when every member collection is. `rag_query`, `auto_rag`, `--rag`, the recalled decisions, the graph toolset, graph expansion and 59g's discovery all call it. 59f's `narrow_config` gains the person's own collections. |
| The knowledge admin routes (`mux.cpp:183-213`) and the `embed`/`graph` admin twins | Answer the owner's and the shared collections only. A member's own appear in the owner's views as a count and a size. |

**(3) Private writers, and training.**

| File | Change |
|---|---|
| `logger/session.cpp:617` and `:714` | Write with `private_mode`. |
| `tools/notes.cpp:80-81` | Uses `platform::create_private_directory` and `write_file_atomically(…, true)`. |
| `cli/analyze.cpp:358-366` | Does the same. |
| `tasks/ledger.cpp:114,209` | The ledger's writes are private. |
| Recall's index folder | Created private. |
| `views/line_reader.cpp:211-213` | Its `chat_history` is written `0600`. |
| The config editor and `config init` | `config.json` is written `0600` by both. |
| `layout.cpp:37,41` | `notes` and `analyses` become `private_mode`. |
| The per-chat prompt cache (`cache/prompt/chats/`) | A member's chat's prompt-cache files are written under `person_home(id)/cache/prompt/chats/`, disposable as today and removed with the person. |
| `operations/dataset_core.cpp:22-25` (and through it the cycle's `train.cpp:3791` and `training/cycle.cpp:335-364`) | Reads `logger::list_sessions()`, which is the owner's sessions, by (1). |
| `datasets create --from-sessions --person NAME` and the cycle's `sessions` source with `person:` | Reads that member's sessions only when their consent file under `person_home(id)` is present. The member gives or withdraws consent themselves, as `datasets consent on\|off`, run as them through 59d's door. |
| `SessionFilter` | Gains `person`. |

**Reference.**

- In-house precedents:
  - The one layout table and its seeding (`layout.cpp:280-298`), which a scope column extends rather than copies.
  - ACP's chats stamped with `origin` and `cwd` and filtered by them (`data/logger/session.h:227-236`; `presentation/cli/acp_cmd.cpp:270-279`): a chat that says who began it, and a listing narrowed by it.
  - 59f's per-credential ownership (a session records its credential, and every other credential gets `404`), which this item widens from credential to person.
  - The todo board bound per request on `serve` (41d), which the notes tools copy.
  - Private chats (`Session.private_chat`, never summarised), the shape of a guest's "nothing kept".
  - The knowledge layer's raw archive, already written `0600` (`business/knowledge/store.cpp:44`), and 57e's "private before the first byte", which (3) carries to every personal writer.
- Siblings:
  - [63a](people-on-one-server.md), the people and the person on every credential; this item is gated on it.
  - [63c](allowances-keys-and-usage.md), the allowance whose `collections` hands out shared libraries.
  - [63d](consent-goes-to-its-person.md), whose session's person is this item's `Session::person`.
  - [63e](task-slots-per-person.md), which moves the one task lock this item leaves in place.
  - [62e](connections-and-activity.md) and [62f](audit-log.md): activity and the audit trail, both metadata.
  - [62h](local-admin-reads.md), the local admin screen's reads.
  - [59a](../v0.2.3/paired-devices.md), [59b](../v0.2.3/machine-mode-over-the-network.md), [59d](../v0.2.3/reads-and-commands-over-the-network.md), [59e](../v0.2.3/tasks-outlive-the-connection.md), [59f](../v0.2.3/paired-apps-with-scopes.md) and [59g](../v0.2.3/capability-discovery.md): the doors and the narrowing this item runs through.
  - [61a](../v0.2.6/sessions-as-training-data.md): exported sessions, held to (3)'s consent.
  - darwin [10c](../../darwin/macos/v0.2.4/people-screens.md): the people screen that shows the metadata reads.
  - GUI [7a](../../darwin/v0.2.4/app-connection-local-or-server.md) and [7c](../../darwin/v0.2.4/every-place-against-a-server.md): a member's app against the server, which sees only their own through the same doors.
- External prior art (the spike's reading of their official docs, 2026-11-14): Jellyfin's per-user library access, and Plex's libraries shared per person with its managed and invited home members. Both are the shape of "shared libraries handed out per person". The deliberate divergence is that the owner here never sees a member's conversations, and the docs say plainly that disk access is the limit of that promise.

**Docs when it ships:**

- [http-api.md](../../../reference/cli/http-api.md): sessions belong to their person, the `404` rule, the meta views.
- [machine-mode.md](../../../reference/cli/machine-mode.md): a served child runs as a person.
- The commands reference: `--person`, `knowledge share` and `datasets consent`.
- CLI CLAUDE.md's layout invariant: the scope column and the personal root.
- DEVELOPER.md: the file rows.

## Testing

Probes and tests use a temporary `APOGEE_HOME` and the mock backend (Constraints, the dev channel).

- **The two-bearer probe is the regression fixture: `cli.personal_sessions`** (`tests/scripts/sh/personal_sessions.sh`). A shell case that runs the real binary under `serve --port 0` on loopback with the mock backend. Two members and their device tokens come from 63a's and 59a's `--print-token`. Alice creates a session. Bob's list omits it, and bob's get, continue and delete each answer `404`, with alice's transcript unchanged and still `200` to her. The owner's `chats list --person alice --output-format json` shows the session's metadata and neither its message nor its title. The file sits in `people/alice/sessions/` at `0600`.
- **`tests/presentation/httpserver/person_sessions_test.cpp`** drives the same matrix through `Mux::dispatch` with no port, including the anonymous bucket and an app's sessions under its person.
- **`layout_test`** — every row has a scope; the counts are 7 personal, 8 shared and 5 admin; `seed_person` creates exactly the personal rows (plus the person's `embeddings/` and `files/`) at `0700`; `cli.install_parity` is unchanged and creates no `people/`.
- **A contents watch, `leak_test`'s twin.** One distinctive string is planted in a member's chat, note, task goal, record and collection. Every owner listing, `--person` document, admin response, `check` row, log line (with `-v`) and bus event is then searched for it, and none may carry it.
- **`recall_test`** — alice's summary is never retrieved in bob's turn, and the owner's index never holds a member's chat.
- **`notes_test` and a served-tools case** — bob's `read_note` of alice's key is not found, and a note written under bob's request lands in bob's root. A file tool under bob's request reads `people/bob/files/`, never `tools.fs_root` or alice's folder.
- **`scope_test`** gains `visible_collections`' table: shared handed out or not, a member's own, the owner, a spanning graph withheld, a name clash refused.
- **A mode census, (3)'s guardrail.** Under `umask 022`, a hermetic run writes a chat, a served session, a note, an analysis, a task ledger, a capture and a recall summary. A walk of every personal row (the owner's and a member's) then finds no file readable or folder enterable beyond the owner, using 57e's watch of temporary files.
- **`dataset_core_test`** — with a member present, a sessions dataset holds only the owner's; `--person bob` is refused without bob's consent and includes his chats with it.
- **The anti-goal** — `chat`, `complete`, `execute` and `task run` with no people configured behave byte for byte as before. A session file with no `person` reads as the owner's, and no `people/` appears.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Where does a member's data live? | `[default]` | Members' personal rows go under `<APOGEE_HOME>/people/<id>/` (`0700`), and the owner's stay where they are. The record also carries an additive `person`. The folder makes every listing, recall and removal structural, and a person's data leaves as one folder. The field makes a misplaced file detectable, and it is refused on load. A per-row owner column alone would leave every reader one forgotten filter away from P2.1. |
| What is the owner's view of a member's data? | `[default]` | Metadata only: id, person, backend and model, turn count, started and updated, size on disk. A chat's title and name are excluded, because they are drawn from the conversation, and so are a task's goal and plan and a collection's names when it is a member's own (a count and a size instead). The owner reaches it with `--person NAME\|all` on `chats list` and `task list`, and the people screen (darwin 10c) reads those documents. |
| What may the owner do with one member chat or task by id? | `[default]` | The owner cannot open, continue, resume or delete one member chat or task by id: it is `404`, as for anyone else. The owner's power over a member's work is administrative, and never shows contents. The owner may end a member's live served or harness session, which drops it from the server as `SessionStore::erase` does today and leaves the transcript the member's (63d's "ending it"). The owner may halt or cancel a member's task (63e's call, a capacity act). Each is audited (62f). The owner removes a member's data whole by removing the person (63a), which is said, confirmed and audited. A member deletes their own. |
| Where do a member's served file tools work? | `[default]` | Under their own `people/<id>/files/` (`0700`), never the owner's `tools.fs_root`, so a Family member's `read` class reads their own files and not the launch folder. Said honestly in the approval line and the people screen: the shell (59f's `write` class) runs as the server Mac's OS account, so a `write` grant reaches whatever that account can. Its folder is where it starts, not a wall, and only the Full preset grants it (63a). |
| What happens to anonymous callers on an open inference plane? | `[default]` | They keep 59f's bucket. Their sessions are stored in the owner's folder with no person and credential `anonymous`, visible only to anonymous callers and to the owner. A member never sees them. |
| How does recall work with people? | `[default]` | Recall is per person. A member's device sessions recall from their own index and summarise into it. Apps never recall (59f). Until (1) lands, 63a runs a member's sessions and tasks with recall off, as 59f does for apps ([people-on-one-server.md](people-on-one-server.md), Technical approach, the doors (`handler.h/.cpp`): "Until 63b ships, a member's sessions and tasks run with recall off"), and `serve` itself never recalls (`recall.h:23-24`). (1) turns a member's recall back on, over their own index. |
| How are collection names kept apart, and where do a member's collections live? | `[default]` | A collection's name is unique between the install's shared collections and one person's own. Creating either is refused on a clash, naming the other. A member's own collections and their graphs are files under their root, never config entries. Named multi-collection graphs stay the owner's (config) in this item. |
| Who may share a record into a shared collection? | `[default]` | Sharing a record is its person's act, `knowledge share ID --into COLLECTION`, allowed where their allowance lets them write that shared collection. The owner shares their own records the same way. The owner never marks a member's record shared, because that would need reading it. |
| What does a guest keep? | `[default]` | A guest (a member on Limited with nothing kept) gets no personal root at all. Their sessions run as private chats (never written, never summarised), and notes, analyses, captures and tasks are refused naming the preset. |
| Whose chats does training read? | `[default]` | Only the owner's. A member's chats join a dataset only when the owner names the member (`--person NAME`) **and** that member has turned on `datasets consent` themselves. The user's call says a member's chats are read when "named", and a dataset is their text, which the owner could then read. So the member's own consent is what keeps "never contents" true. 61a's export follows the same rule. |
| Does the task lock move in this item? | `[default]` | The one task lock stays install-wide in this item. Ledgers move to the person's root, and the lock moves with 63e's per-person queue. A refusal while another person's task runs says only that the server is busy, never that task's goal. |
| Where do the line editor's history and the prompt-cache files live? | `[default]` | The line editor's `chat_history` stays the owner's at the root (only the owner types at the server's terminal), written `0600`. A member's per-chat prompt-cache files go under their root. |
| The person id, the owner's identity, how a credential resolves to a person, the allowance and its presets, the guest, and the verb that removes a person | `(consumed decision)` | 63a. This item supplies `seed_person` and the removal's enumeration. |
| What an allowance's `collections` grants, and whether it grants writing | `(consumed decision)` | 63c |
| Who answers a session's questions and where an "always" answer is kept | `(consumed decision)` | 63d. This item supplies the session's person. |
| The task lock and the per-person queue, `started_by` as the task's person, and the owner's halt and cancel of a member's task | `(consumed decision)` | 63e |
| The per-credential session filter, the anonymous bucket and `--scope-file` | `(consumed decision)` | 59f. This item adds the person to what they carry. |
| What the audit records about the owner's metadata reads and removals | `(consumed decision)` | 62f |
| How the people screen shows a member's data | `(consumed decision)` | darwin 10c |

No `[user]` call blocks this item.

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-14 | Specced from the admin-mode and people spike. The user's calls that shape this item: the people are a household or a small team, so the design serves both; the owner sees members' metadata (devices, apps, activity, which model, when), never chat contents, and whoever controls the server Mac can still read its disk, and that is said honestly; members use the server's local models only, unless the owner grants a household cloud key with a limit (63c); the words are "people" and "access" (Owner, Member), never "roles", which already means model roles in Apogee; administering is a separate "Apogee Server" window in the macOS app, offered only on the Mac that serves, driving the local CLI over pipes (darwin 10a–10c), and it installs the server as a background service (launchd) and starts, stops and restarts it (62d); administering is local by default, and the owner may mark chosen paired devices as allowed to administer remotely (62g), which revises 59d's default that the admin plane keeps one token for any network caller; live changes are applied on the spot by a reload, with what still needs a restart said (62b); members' apps are paired by the owner at the server Mac, capped at that member's allowance (63a); collections are shared libraries handed out per person, plus private ones of a member's own; notes and saved decisions are personal by default; tasks are a per-person queue, not a refusal (63e); the audit keeps 90 days of who, what and when, without request contents (62f); the SPEC amendments are already written ([SPEC.md](../../../assistant/cli/SPEC.md) → Core surfaces 4, "Revised again 2026-11-14"; [GUI SPEC](../../../assistant/gui/SPEC.md) → Core surfaces 3); the standing law: remote use is an add-on, and the CLI alone and the Mac app over pipes stay complete with no server. | the user |
| 2026-11-14 | Placement: both halves land before v0.2.5, both in v0.2.4: the server console (CLI track 62, darwin/macos 10a–10b) and people and access (CLI track 63, darwin/macos 10c). This item is CLI 63b, v0.2.4, track 63, gated on 63a, **split first** into the three documents above. It sits after 63a because nothing can be someone's until a credential names a person. It sits beside 63c–63e, which gate on 63a too, and darwin 10c reads its metadata. Nothing already queued is renumbered. | the user |
| 2026-11-14 | The model comes from the prior art (the agent's default, from the spike's summary). The owner is implicit, as the OS account owning `APOGEE_HOME` on the server Mac. Members each have an allowance in 59f's `AppScope` shape, with presets (Full, Family, Limited) and a guest as a member on Limited with nothing kept. The owner's data stays where it is today, so an owner-only install is untouched. | default |
| 2026-11-14 | This item revises recorded single-person defaults, for members: 59e's "any of them may attach to, halt, cancel or resume any task" ([tasks-outlive-the-connection.md](../v0.2.3/tasks-outlive-the-connection.md), Open questions: which devices may attach to, halt, cancel or resume a task) now holds only for one person's own devices; 59f's "Its saved chats are the person's to see" ([paired-apps-with-scopes.md](../v0.2.3/paired-apps-with-scopes.md), Open questions: what an app may see of what others started) now means the app's person, not the owner; 59b's chat children recall from their own person's index, never the install's. | the spec |
| 2026-11-18 | Moved here from v0.2.7, when the releases were merged; the id is unchanged. | the user |

## Out of scope

- The people registry, allowances and presets (63a).
- Whose keys pay, and usage (63c).
- Consent routing and "always" answers (63d).
- The task queue and the lock (63e).
- The audit log (62f).
- Encrypting a member's data at rest. The owner's OS account can read the disk, and that is said, not solved.
- Confining the shell beneath the server's OS account. A member's shell starts in their own folder, and that is said, not enforced.
- A member's own named multi-collection graphs.
- Moving existing owner data.
- Several OS accounts on one server Mac, which is already a separate install each.

## Links

- **Blocked by:** [63a](people-on-one-server.md) · **Blocks:** —
- **Relates to:** [63c](allowances-keys-and-usage.md), [63d](consent-goes-to-its-person.md), [63e](task-slots-per-person.md), [62e](connections-and-activity.md), [62f](audit-log.md), [62h](local-admin-reads.md), [59a](../v0.2.3/paired-devices.md), [59b](../v0.2.3/machine-mode-over-the-network.md), [59d](../v0.2.3/reads-and-commands-over-the-network.md), [59e](../v0.2.3/tasks-outlive-the-connection.md), [59f](../v0.2.3/paired-apps-with-scopes.md), [59g](../v0.2.3/capability-discovery.md), [61a](../v0.2.6/sessions-as-training-data.md), darwin [10c](../../darwin/macos/v0.2.4/people-screens.md), GUI [7a](../../darwin/v0.2.4/app-connection-local-or-server.md) and [7c](../../darwin/v0.2.4/every-place-against-a-server.md); [SPEC.md](../../../assistant/cli/SPEC.md) → Core surfaces 4; [GUI SPEC](../../../assistant/gui/SPEC.md) → Core surfaces 3; ADRs [0001](../../../adrs/cli/layer-enforcement.md), [0002](../../../adrs/cli/mode-parity.md), [0006](../../../adrs/cli/backwards-compatibility.md), [0008](../../../adrs/cli/module-map.md), [0010](../../../adrs/cli/the-shell-is-a-mode.md).
- **Relates to (maintenance):** [CLI-M57](../maintenance/a-saved-chat-names-only-its-own-files.md), a saved chat's id held to its own file's stem: land it first so the rule carries into this item's per-person session paths.
- **Revises (for members):** 59e's "any of them may attach to, halt, cancel or resume any task", 59f's "Its saved chats are the person's to see", and 59b's chat children recalling from the install's index (Decision log, 2026-11-14).
- **Closes:** wall P2 of the admin-mode and people spike (2026-11-14).
