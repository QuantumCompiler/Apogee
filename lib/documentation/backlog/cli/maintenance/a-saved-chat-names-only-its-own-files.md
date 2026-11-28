# CLI-M57 · A saved chat names only its own files

| Field | Value |
|---|---|
| **Type** | 🐞 Bug |
| **Project** | CLI |
| **Status** | 🟢 Ready |
| **Fix version** | Maintenance (any time) |
| **Component** | Sessions › the saved-chat store (`logger::load`, `list_sessions`, `session_path`) and the `chats` verbs that write and delete through it |
| **Labels** | `sessions` `chats` `safety` |
| **Epic** | — (maintenance has no track) |
| **Blocked by** | — |
| **Blocks** | — |
| **Reporter** | The investigation of the APP-M21 and APP-M22 probes' CLI asides (2026-11-26): found by its sandboxed investigator, then two skeptics and a critic the same day |
| **Created** | 2026-11-26 |

---

## Summary

A saved chat's id is read from inside its session file, and then used as a path at every write and delete. So `apogee chats title <id> x` overwrites any existing `.json` file the id points at, `config.json` included. `apogee chats delete <id>` removes the `.json` file, the attachments index and media folder, and the recall marker that the id points at, and the marker can be any file. A typed path reaches the same loader: it reads a session-shaped file anywhere, and for a file that is not a session it prints a success with nothing done. The item adds one chat-id rule in the logger. A chat is listed, read, written and deleted only when the id inside its file is a plain id that names that very file, and `load` joins a typed name to `sessions/` only when the name passes the rule.

## Description

### User story

**As** someone whose saved chats live in `sessions/`, some of them synced, restored or handed to me,
**I want** every `chats` verb, a resume and the apps to touch only that chat's own files,
**so that** renaming or deleting a chat can never overwrite or remove another file, and no verb says it did something it did not.

### Steps to reproduce

The sandbox, 2026-11-26, with the built binary `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`: `env -i PATH=/usr/bin:/bin`, `APOGEE_HOME` and `HOME` set to `<home>`, a folder three levels deep in the sandbox, stdin from `/dev/null`, and mock backends only.

1. Run `apogee config init` and `apogee config add-backend foo --type mock`, then make `sessions/`, `attachments/` and `memory/pending/` in `<home>`.
2. Write `<home>/sessions/20261126-000001-abce.json` as `{"schema_version": 1, "chat_id": "../victim2", "backend": "foo", "started_at": "2026-11-26T00:00:00Z", "updated_at": "2026-11-26T00:00:00Z", "turns": 0, "compactions": 0, "messages": []}`, and `<home>/victim2.json` as `{"victim": 2}`. Run `apogee chats list`, then `apogee chats title 20261126-000001-abce renamed`.
3. Write `<home>/sessions/20261126-000002-abcf.json` the same way, with `"chat_id": "../../victim.txt"`, and write a file at `<home>/victim.txt`. Run `apogee chats delete 20261126-000002-abcf`.
4. Reset `<home>/victim2.json` to `{"victim": 2}` and write `<home>/victim3.json`. Run `apogee chats title ../victim2 renamed`, then `apogee chats delete ../victim3`.

### Actual result

2. `chats list` prints `../victim2  2026-11-26T00:00:00Z  0 turns  ../victim2`. `chats title` prints `../victim2 renamed to 'renamed'` and exits 0, and `<home>/victim2.json` now holds a session document: `{ "backend": "foo", "chat_id": "../victim2", …, "custom_name": "renamed"`.
3. `deleted ../../victim.txt`, exit 0. `<home>/victim.txt` is gone, and the session file is still in `sessions/`.
4. ` renamed to 'renamed'` and `deleted `, both with an empty id and exit 0. Neither file changes.

### Expected result

2. `chats list` does not list the file. `chats title` exits 1 with one stderr line naming it, `apogee chats: <home>/sessions/20261126-000001-abce.json is not a chat this home saved: its id names another file`, and `victim2.json` keeps its bytes.
3. Exit 1 with the same words for `20261126-000002-abcf.json`. `<home>/victim.txt` and the session file remain.
4. Each exits 1 with `apogee chats: no chat named '../victim2'` (or `'../victim3'`), and nothing changes.

### Background

A chat is a file the home builds: `sessions/<chat_id>.json`, beside `attachments/<chat_id>.db` and `attachments/<chat_id>.media/` (26d) and a recall marker `memory/pending/<chat_id>` (26l). Milestone H (MILESTONES, 2026-08-26) made the id immutable, "renaming sets `custom_name`", and made resume degrade rather than fail. The investigator dates the direct join and the delete by the file's own id to 1b21063c (2026-08-31). Every later piece keys on the id the same way: `save` writes `session_path(chat_id)`, and `delete_chat`, the attachments index and the recall marker each build their paths from it. Nothing checks that the id is a name, or that it is the name of the file it was read from. Every id Apogee itself mints is both, so the gap only shows when a file is planted or edited.

This was found beside APP-M21's CLI asides. That item's probes left asides for the CLI queue, and three of them concern the names of things the CLI makes: agents, symphonies and datasets. The investigation took those three as one seam. Its probes also checked the other verbs that turn a typed name into a file, and found that `chats` takes a path and deletes by the file's own `chat_id`. That is a seam of its own, the logger's one loader and path join, so it is its own item. The first fix proposed guarded the typed name. Later probes measured the same escape from a file inside `sessions/` with no path typed at all, and measured writes and recall-marker deletes as well as attachment deletes. So the reach is low, the effect is high, and the guard sits on the id inside the file (Decision log).

The design, in prose. One rule in the logger says what a chat id may be: not empty, no `/`, `\` or NUL, not `.` or `..`, and no root name. A file is a chat this home saved only when the id inside it passes that rule and `session_path(chat_id)` is the file it was read from. `list_sessions` skips any other file, as it already skips a corrupt one. `load` refuses one by name. `load` joins a typed name to `sessions/` only when the name passes the rule. Any other name goes to the custom-name and title search, which accepts any characters, as it does today. Every write and delete then keys on an id that names the chat's own file, so `chats title`, `compact`, `delete`, `chat --resume`, the attachments index and the recall marker need no change of their own.

### Evidence

All measured 2026-11-26 with the built binary `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`, channel release, by an investigator, two skeptics and a critic, each in its own sandbox. Each probe used `env -i PATH=/usr/bin:/bin`, `APOGEE_HOME` and `HOME` three levels deep, stdin `</dev/null` and mock backends only, and recorded stdout, stderr and exit separately. The one commit after the binary, HEAD 5b8927c9, touches none of `session.cpp`, `chat_history.cpp`, `chat_attachments.cpp` or `chat_recall.cpp`, so the binary has HEAD's code for every path in this seam. Probes are labelled by sandbox: `crit-e` is the critic's; `fix-e`, `fix-e2` and `fix-e3` are the fix skeptic's; `inv-e` is the investigator's. Source lines are read at HEAD under `lib/src/cli/source/`.

1. **The listed id is the `chat_id` itself** (`crit-e`). `chats list` prints `../victim2  2026-11-26T00:00:00Z  0 turns  ../victim2`. By reading, the same id is what every consumer passes back: the JSON row's `id` (`session_row_view`, `presentation/cli/chat_history.cpp:107`), completion (`complete_sources.cpp:398`), the TUI's Chats rows (`tui_workbench.cpp:111`) and the Mac app (`SessionLibraryViewModel.swift:1007`, `:1130`, `:1154`).
2. **A write through the id, with no path typed** (`crit-e`, and again in `fix-e2`). A file `sessions/20261126-000001-abce.json` held the `chat_id` `../victim2`. `chats title 20261126-000001-abce renamed` printed `../victim2 renamed to 'renamed'` and exited 0. `<home>/victim2.json`, which had been `{"victim": 2}`, then began `{ "backend": "foo", "chat_id": "../victim2", …, "custom_name": "renamed"`.
3. **`config.json` replaced** (`fix-e`). A session-shaped file outside the home held the `chat_id` `../config/config`. `chats title -- <its path> pwned` replaced `<home>/config/config.json` with a session document (md5 `ac62ac4f…` → `bde0591d…`). `models status` then printed `chat: (unset -- no models.default configured)`.
4. **A delete through the recall marker** (`crit-e`; `fix-e3`). The `chat_id` was `../../victim.txt`, with `memory/pending/` present. `chats delete 20261126-000002-abcf` printed `deleted ../../victim.txt` and exited 0. `<home>/victim.txt` was gone, and the session file was still in `sessions/`. The marker has no extension, so it can reach a file of any kind. Without `memory/pending/`, the same delete removed nothing (the fix skeptic's run): the path climbs out of a folder, and that folder must exist.
5. **A delete through the session path and the attachments** (`fix-e2`; `inv-e`).
   - A `chat_id` of `../victim`: `chats delete` removed `<home>/victim.json` and `<home>/victim.media`, and left the session file.
   - A session-shaped file outside `sessions/` holding `../target`: `chats show -- <its path>` read it (`../target -- ../target, on foo`). `chats delete -- <its path>` printed `deleted ../target` and exited 0. `<home>/target.json` and the whole `<home>/target.media/` folder were gone, and the named file was still there.

   So `chats delete -- <a copy of a session>` deletes the original in `sessions/`, not the copy.
6. **A typed path prints a false success** (`crit-e`). `victim2.json` was reset to `{"victim": 2}` and `victim3.json` was present.
   - `chats title ../victim2 renamed` printed ` renamed to 'renamed'` and exited 0. `victim2.json` was unchanged.
   - `chats delete ../victim3` printed `deleted ` and exited 0. `victim3.json` was still there.

   Why, by reading:
   - `load` joins the typed name (`data/logger/session.cpp:645-649`) and reads the file, which is not a session. `deserialize` gives it an empty id, with warnings.
   - `title`'s `save` builds `sessions/.json` (`:576`), finds it missing (`:583-586`) and answers `Saved::Deleted`. `chats title` drops that answer and prints the rename (`chat_history.cpp:474-475`).
   - `delete_chat` removes the missing `sessions/.json`, which sets no error (`:712-716`), and prints `deleted ` with the empty id.

   An empty id is not quite a no-op. `recall_marker("")` is the `memory/pending/` folder itself, and `fs::remove` removes it when it is empty (`chat_recall.cpp:60-68`). The `crit-e` sandbox no longer has the `memory/pending/` folder its steps created. That fits this reading, but it was not measured on its own.
7. **Root cause.**
   - `session_path` joins `sessions_dir()` with `chat_id + ".json"` (`session.cpp:247-249`).
   - `load` (`:644-675`) tries that join with the typed name first (`:645-649`). Otherwise it searches `list_sessions()` by custom name and title (`:653-658`) and opens `session_path(candidate.chat_id)` (`:655`), since the listing does not keep each file's own path.
   - `deserialize` takes `chat_id` from the file unchecked (`:395`), though `session.h:138-141` calls the id immutable.
   - `save` (`:569-620`) writes `session_path(session.chat_id)` (`:576`, `:617`) whenever that file exists (`:583-586`).
   - `delete_chat` (`presentation/cli/chat_history.cpp:704-728`) removes `session_path(session.chat_id)` (`:713`). Then `ChatAttachments::remove_index(chat_id)` (`:719`; `chat_attachments.cpp:139-147`) removes `<id>.db` with `-wal`, `-shm` and `-journal`, and `remove_all`s `<id>.media`. Then `forget_recall(chat_id)` (`:721`; `chat_recall.cpp:64-69`) removes `memory/pending/<id>` (`recall_marker`, `:60-62`).
   - `index_for` (`chat_attachments.cpp:135-137`) is also opened by `chat_session.cpp:1319` and `chat_history.cpp:674`.
   - Every id Apogee makes comes from `new_chat_id` (`session.cpp:251-270`). It is assigned at `chat_session.cpp:1124`, `task_cmd.cpp:650` and `httpserver/session.cpp:35`.
8. **Reach is low; the effect is high.** The Mac app and the TUI pass the listed id, which is the `chat_id` itself (point 1). The loader joins that id directly, so a listed `../victim2` reads `<home>/victim2.json` and gets only the false success of point 6. An escape needs either a planted session file named by its file's stem, or a second planted file. No Apogee writer makes such a file. A hostile id can come from a hand edit, a synced or restored `sessions/` folder, or a session file someone hands the person.
9. **Blast radius.**
   - **One loader and one path join** (`logger::load`, `session_path`). They are reached by:
     - `chats info`, `show`, `title`, `compact` and `delete` (`chat_history.cpp:422`, `:444`, `:470`, `:500` and `:631`, `:706`);
     - `chat --resume` and `--continue` (`chat_session.cpp:1101-1108`);
     - `knowledge --from-chat` (`knowledge.cpp:116`);
     - task resume (`task_cmd.cpp:615`, `business/tasks/runner.cpp:97`);
     - recall (`chat_recall.cpp:178`, `:331`);
     - the TUI's Chats rows: details and delete (`tui_workbench.cpp:123-125`, `:127-134`), and by reading also compact (`:136-146`, through `compact_saved_chat`) and open (`:118-121`, which resumes with `--resume <id>`, `tui_session.cpp:204-206`);
     - ACP's `session/load` (`acp_cmd.cpp:282-290`), where it is already guarded (point 10).
   - **What a hostile id reaches.** Writes land only over an existing file (save's check, `session.cpp:583-586`): any `.json` file the id names. Deletes take:
     - `<id>.json` under `sessions/`;
     - `attachments/<id>.db`, with `-wal`, `-shm` and `-journal`;
     - the `attachments/<id>.media` folder, recursively;
     - `memory/pending/<id>`, a file of any extension.
   - **By reading, not measured.** `chats compact` and `chat --resume` write through the same `save`. When recall is due, a resumed chat's turn writes its marker at `memory/pending/<id>`: it creates the parent folders, sets the marker's parent folder to owner-only, then truncates and writes the file (`chat_recall.cpp:277-285`). Once the chat is summarised, the same path is removed (`:164-166`, `:193-194`). The chat's attachments index sits at `attachments/<id>.db` (`chat_session.cpp:1317-1319`).
   - **Every reader of `list_sessions` sees such a file today**: `chats list` (`chat_history.cpp:393`), completion's chat kind (`complete_sources.cpp:392-409`), the TUI's Chats view and session picker (`tui_workbench.cpp:109`, `tui_session.cpp:242`), ACP's `session/list` (`acp_cmd.cpp:273`), `--continue` through `most_recent` (`session.cpp:763-769`), `load`'s own name search (`:653`) and datasets built from sessions (`operations/dataset_core.cpp:25`).
   - **Unaffected.**
     - HTTP `/v1/sessions` mints its own ids (`httpserver/session.cpp:35`) and never loads a file.
     - llama.cpp's saved conversation state refuses an id that is not plain (point 10).
   - **Next.** CLI-59d's door would run `chats delete` and `chats title` as typed, from a paired device.
10. **What already works**, and must stay:
    - every id Apogee writes is `new_chat_id`'s `YYYYmmdd-HHMMSS-xxxx`, and it is its file's stem;
    - `rename_session_backend` rewrites the file it read, not `session_path(chat_id)` (`session.cpp:714`);
    - the custom-name and title lookup finds names holding any characters (`session.cpp:653-658`);
    - `chats delete` refuses a chat that a live task drives (`chat_history.cpp:709-711`);
    - `list_sessions` skips a corrupt file rather than failing (`session.cpp:742-748`), and that is the shape this item's skip takes;
    - by reading, ACP refuses a host's id holding `/` or `\` before it reaches the store, and refuses a chat whose id is not the one asked for (`presentation/acp/server.cpp:264-268`, `:279-282`; [acp.md → Saved sessions](../../../reference/cli/acp.md#saved-sessions)), so a planted mismatch is refused there today;
    - by reading, llama.cpp's prompt cache builds a chat's state path only for a plain id (ASCII letters and digits, `-` and `_`, never `.` or `..`, `data/backends/prompt_cache.cpp:192-202`).
11. **Not pinned.** No test plants a session whose id differs from its file's name, or loads a chat by a path (a grep of `session_test.cpp`, `chat_history_test.cpp` and `chat_test.cpp`, 2026-11-26).
12. **Not measured.**
    - `chats compact` and `chat --resume` through a hostile id. They were read only; both go through the same `save`.
    - A plain id that is not its file's stem (`sessions/C.json` holding `D`). By reading, `chats title C x` writes `session_path("D")` and so overwrites `D.json` today (`session.cpp:576`, `:617`).
    - ACP's `session/load` and `session/list` over stdio. Point 10's guard is read, not driven.
    - An id typed in another case on APFS. By reading, the direct join reaches the file today (Technical approach); the Testing guard pins it.
    - A task ledger's `session_id` naming a hostile chat. Task ids are guarded by `valid_task_id`, but the session id inside a ledger was not probed.
    - Windows, where no host was run: `\` and a root name (`C:x`) as a chat id.

## Acceptance criteria

- [ ] **Given** `sessions/20261126-000001-abce.json` holding the `chat_id` `../victim2` beside `<home>/victim2.json`, **when** `chats title 20261126-000001-abce renamed` runs, **then** it exits 1 with one stderr line naming the file (`is not a chat this home saved: its id names another file`), and `victim2.json`'s md5 is unchanged. Today it prints `../victim2 renamed to 'renamed'` and overwrites the file.
- [ ] **Given** a session file holding the `chat_id` `../../victim.txt`, with `memory/pending/` present, **when** `chats delete <its stem>` runs, **then** it exits 1, and `<home>/victim.txt` and the session file both remain. Today it prints `deleted ../../victim.txt` and the file is gone.
- [ ] **Given** a session file holding the `chat_id` `../victim` beside `<home>/victim.json` and `<home>/victim.media/`, **when** `chats delete <its stem>` runs, **then** it exits 1 and both remain.
- [ ] **Given** a session-shaped file outside `sessions/` holding the `chat_id` `../config/config`, **when** `chats title -- <its path> pwned` runs, **then** it exits 1 with `no chat named`, `config.json`'s md5 is unchanged, and `models status` prints what it printed before. Today `config.json` is replaced and `models status` prints `chat: (unset -- no models.default configured)`. **Given** one holding `../target`, **when** `chats show -- <its path>` or `chats delete -- <its path>` runs, **then** each exits 1, and `<home>/target.json` and `<home>/target.media/` remain.
- [ ] **Given** `chats title ../victim2 renamed` and `chats delete ../victim3`, with both files present, **then** each exits 1 with `apogee chats: no chat named '<name>'`, never ` renamed to` or `deleted ` with exit 0, and nothing changes, `memory/pending/` included.
- [ ] **Given** `sessions/C.json` holding the `chat_id` `D` beside a real `sessions/D.json`, **when** `chats title C x` runs, **then** it exits 1 naming `C.json`, and `D.json` is unchanged.
- [ ] **Given** such files in `sessions/` beside ordinary chats, **when** `chats list` (text and `--output-format json`), `__complete chats show ''`, the TUI's Chats view, ACP's `session/list` or `chat --continue` reads the store, **then** none of them names those files, and every other chat is listed as before.
- [ ] **Given** a chat with the custom name `a/b`, set by `chats title <id> a/b`, **when** `chats show a/b` runs, **then** it shows that chat.
- [ ] **Given** an ordinary chat, **then** list, show, info, title, compact, `chat --resume` and delete behave as today, and delete removes its `.db` (with `-wal`, `-shm` and `-journal`), its `.media` folder and its recall marker.
- [ ] **Given** a session file named by its own id, whether Apogee wrote it or a hand-written test fixture did, **then** it lists, shows and resumes as today. On a filesystem probed as case-insensitive, an id typed in upper case still reaches its chat.

## Constraints

- **A chat is named, not pathed.** One chat-id rule, in the logger, which every surface reaches through `load` and `list_sessions`: the `chats` verbs, `chat --resume` and `--continue`, task resume, `knowledge --from-chat`, recall, the TUI and ACP. No caller keeps its own copy.
- **The id is immutable** (`session.h:138-141`; [Milestone H](../../../assistant/cli/MILESTONES.md#milestone-h--apogee-chat)). The rule only checks it. Nothing rewrites an id, repairs one, or renames a file to match one.
- **Older chats keep working** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)). Every file an Apogee version wrote passes, since `new_chat_id`'s id is its file's stem. A file that fails, such as a hand-renamed copy or a hand-edited id, is skipped by list and refused by name at load. It is never deleted or rewritten. The file check compares files, not spellings, so an id typed in another case on a case-insensitive filesystem keeps reaching its chat, as it does today.
- **Names are free text.** `load` never refuses a name for its characters. Only the direct join is guarded, and a name the rule refuses goes to the custom-name and title search.
- **Resume still degrades for everything else** (Milestone H: "Resume that degrades, never fails"; `session.h:285-291`). `deserialize` stays text-only and lenient. The one new refusal is a file whose id names another file, recorded as a dated revision.
- **The offer is a contract** ([ADR 0007](../../../adrs/cli/tab-completion.md)). Completion's chat kind reads `list_sessions` (`complete_sources.cpp:392-409`), so a file the verbs refuse is never offered.
- **Mode parity** ([ADR 0002](../../../adrs/cli/mode-parity.md)). One rule in the logger reaches every door that loads a saved chat, the Mac app included, since it reaches the store through the CLI. HTTP `/v1/sessions` mints its own ids and never loads a file, so it needs no change; that is stated here rather than left out.
- **Machine mode grows additively** (ADR 0006). No event or field changes. `chats list --output-format json` keeps its shape, without rows for files that are not this home's chats. A refused resume is the same `error` an unknown name gets today.
- **The refusal is one line, and the last on stderr**, in the house voice (`apogee chats: …`, `apogee chat: …`), so the Mac app shows the CLI's own words with no app change.
- **Probes and tests stay hermetic** ([CLAUDE.md → Development runs on the dev channel](../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel)). Tests use a temporary `APOGEE_HOME` three levels deep, with every victim file inside the test's own temp directory. A live check names `apogee-dev` over a scratch home, never `~/.apogee`.
- **Code style.** The rule is a function over `std::string_view` in the existing `session.h`/`.cpp` pair. The Windows root name is `std::filesystem::path(id).has_root_name()`, which fires only on Windows, so no `#ifdef` is needed outside the platform seam. CLI-M45 specifies the same shape for its collection rule.

## Technical approach

Paths under `lib/src/cli/`.

| File | Change |
|---|---|
| `source/data/logger/session.h`, `session.cpp` | `chat_id_error(std::string_view id)`: empty when `id` can name a saved chat's file, otherwise the words. A good id is not empty, holds no `/`, `\` or NUL, is not `.` or `..`, and has no root name (`std::filesystem::path(id).has_root_name()`). In `load` (`:644-675`), the direct join (`:645-649`) runs only for a name the rule passes; any other name goes to the custom-name and title search (`:653-658`). After reading the file it found, `load` refuses a session whose `chat_id` fails the rule, or whose `session_path(chat_id)` is not the file it read (`std::filesystem::equivalent`, its `error_code` form, so a missing file is a refusal, not a throw), with `<file> is not a chat this home saved: its id names another file`. `list_sessions` (`:723-761`) applies the same check and skips a file that fails it, as it skips a corrupt one (`:742-748`). `deserialize` (`:385-567`) is unchanged. |
| `source/presentation/cli/chat_history.cpp` | No code change expected. `info` (`:422`), `show` (`:444`), `title` (`:470`), `compact` (`:500`, `:631`) and `delete` (`:704-728`) refuse through `load`, so no ` renamed to` or `deleted ` line is printed for a chat that was not read from its own file in `sessions/`. |
| `tests/data/logger/session_test.cpp` | The rule's table, and `load` and `list_sessions` over planted files (Testing). |
| `tests/presentation/cli/chat_history_test.cpp` | The fixture under Testing, run through the command tree in process (its `run(home, …)`). |

**Where the check sits.** `deserialize` sees text only, with no file to compare an id against, and its tests call it with ids like `c`. So the check sits where a file is read: `load`, and `list_sessions`, which also feeds `load`'s name search. Once those two admit only a chat whose id names its own file, every id a process holds is checked (a loaded one) or minted by `new_chat_id` (a new one). `save`, `filed_and_gone`, `delete_chat`, `index_for` and `recall_marker` then key on a safe id without a copy of the rule each. In sketch:

```cpp
// data/logger/session.h
/// Why `id` cannot name a saved chat's file, or empty when it can: not empty,
/// no '/', '\\' or NUL, not "." or "..", no root name. A chat is named, never
/// pathed.
[[nodiscard]] std::string chat_id_error(std::string_view id);
```

`load` and `list_sessions` share one private check: the id passes `chat_id_error`, and `std::filesystem::equivalent(session_path(id), path_read)` is true. Comparing files rather than strings keeps a case-insensitive filesystem's today: by reading (not probed), `chats show 20261126-000001-ABCE` reaches `20261126-000001-abce.json` on APFS by the direct join, and it still does. A comparison of the stem's spelling would refuse it.

The rule takes CLI-M45's shape for an escape-only rule (empty, `..`, the separators and a root name), and adds `.` and NUL, as the item's fix names them. It is wider than the character sets of `valid_task_id` and the prompt cache. That way no id that reads today stops reading for its characters: a hand-named `my chat.json` holding `"chat_id": "my chat"` keeps working.

**Reference.**
- `PromptCache::chat_path` (`data/backends/prompt_cache.cpp:192-202`): a consumer of a chat id that already refuses a non-plain one. This item's rule is deliberately wider, as said above.
- ACP's `session/load` (`presentation/acp/server.cpp:264-268`, `:279-282`): it refuses a path, and refuses a chat whose id is not the one asked for. Those are the two halves of this rule, at one door.
- `valid_task_id` (`business/tasks/ledger.cpp:86-94`): a task's directory name, never a path out of `tasks/`.
- `list_sessions`'s corrupt-file skip (`session.cpp:742-748`).

**Docs when it ships:**
- DEVELOPER.md's `logger/` package row and its `session.h/.cpp` row: a chat's id names its own file, checked where a file is read. Also the `logger/session_test.cpp` row.
- `reference/cli/machine-mode.md`'s `chats info` and `chats show` refusal: the new words beside `no chat named '<name>'`.
- MILESTONES: a dated entry, with a dated revision of Milestone H's "Resume that degrades, never fails".

## Testing

Hermetic: a temporary `APOGEE_HOME` three levels deep, with `sessions/`, `attachments/` and `memory/pending/`, and every victim file inside the test's own temp directory. The planted session files take the probes' shape (`schema_version` 1, `backend` `foo`, no turns). Each case fails on today's binary unless it is marked as a guard: fixture cases 1, 2 and 4 and the files outside `sessions/` as measured 2026-11-26 (Evidence 2–6), case 3 and the unit cases by reading (`C.json`'s save writes `session_path("D")`).

- **The bug as a fixture**, in `chat_history_test.cpp`:
  1. `sessions/A.json` holding the `chat_id` `../victim2`, beside `$H/victim2.json`: `chats title A x` exits 1 naming the file, and `victim2.json`'s md5 is unchanged.
  2. `sessions/B.json` holding `../../victim.txt`, beside `$H/victim.txt`: `chats delete B` exits 1, and `victim.txt` and `B.json` remain.
  3. `sessions/C.json` holding `D` (plain, but not its own stem), beside a real `sessions/D.json`: `chats title C x` exits 1, and `D.json` is unchanged.
  4. `chats title ../victim2 x` and `chats delete ../victim3` exit 1 with `no chat named`, and nothing changes, `memory/pending/` included.
  5. Guards: a normal chat lists, shows, titles, compacts and deletes, and the delete removes its `.db`, its `.media` folder and its recall marker. A custom name `a/b`, set by `chats title <id> a/b`, is found by `chats show a/b`.
- **Files outside `sessions/`.** One session-shaped file holding `../config/config`, and one holding `../target`, with `$H/target.json` and `$H/target.media/inside.txt` present. `chats title -- <abs> pwned`, `chats show -- <abs>` and `chats delete -- <abs>` each exit 1, and `config.json` and both target files keep their bytes.
- **The listing.** With A, B and C planted beside two ordinary chats, `chats list` (text and `--output-format json`) and `__complete chats show ''` name only the two ordinary chats and `D`. This extends the offer test at `chat_history_test.cpp:578`, where each offered name is taken.
- **Unit**, in `session_test.cpp`:
  - `chat_id_error` refuses `''`, `.`, `..`, `a/b`, `a\b`, `"a\0b"`, `../x` and `/abs`, and under `_WIN32` `C:x` and `C:`;
  - it passes `20261126-000001-abce`, `my chat`, `a..b`, `café` and `-x`;
  - `load` over a planted mismatch throws naming the file, and `list_sessions` skips it and keeps the rest;
  - a legacy file with no `schema_version`, whose id is its stem, loads with its warning (guard).
- **Case**: on a filesystem probed as case-insensitive, `chats show <the id in upper case>` still shows the chat (guard).
- **Guards already in the suite stay green**:
  - `chat_test.cpp:628` (a delete removes the attachments index) and `:1220` (a delete forgets recall);
  - `machine_mode_e2e.sh`'s hand-written `legacy-chat` and `nocount` sessions, each named by its id;
  - `acp_host_check.py`'s `saved_chat.json` fixture, and its refusals of a name or a path.
- **Gate:** `lib/scripts/cicd.sh --app cli --test`, clang-format, and the MinGW scan of the changed files.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Require a session's `chat_id` to equal its file's stem, or only to be a plain id? | `[default]` | Both. A plain id that differs from its stem would still point every write at another chat's file inside `sessions/` (fixture case 3). No Apogee writer makes a mismatch. "Equals its stem" is checked as the same file (`std::filesystem::equivalent`, Technical approach), so an id typed in another case on a case-insensitive filesystem keeps reaching its chat. |
| Should `check` name skipped session files? | `[default]` | Not in this item. The refusal names the file when it is asked for, and the listing skips it as it already skips a corrupt file. A `check` row is a follow-up. |
| Where does the file check sit: in `deserialize`, or where a file is read? | `[default]` | Where a file is read: `load` and `list_sessions`. `deserialize` sees text only, with no file to compare against. Its tests call it with ids such as `c`, and its "degrades, never fails" contract stays whole for everything but this one refusal. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-26 | Specced from the investigation of the APP-M21 and APP-M22 probes' CLI asides: a sandboxed investigation, two skeptics and a critic (Evidence). Reach is low and the effect is high: an escape needs a planted file, and through one any existing `.json` file is overwritten and the attachments and recall files the id names are removed. | the spec |
| 2026-11-26 | Placed in **Maintenance** as M57, by the user's call to spec the APP-M21/M22 CLI asides as maintenance items from M48, grouped only where they share one seam. This one is its own item because its seam is the logger's one loader and path join (`logger::load`, `session_path`), which no sibling touches. The siblings' seams are other code: the scaffolds ([CLI-M52](../../../assistant/cli/MILESTONES.md#2026-11-27--a-refused-scaffold-writes-nothing-maintenance-item-m52-a-refused-agent-or-mcp-scaffold-writes-nothing) (shipped 2026-11-27)), the symphony branch ([CLI-M53](../../../assistant/cli/MILESTONES.md#2026-11-27--a-symphony-the-cli-writes-is-one-it-can-delete-maintenance-item-m53-a-symphony-the-cli-writes-is-one-it-can-delete) (shipped 2026-11-27)), the folded config lookups ([CLI-M54](../../../assistant/cli/MILESTONES.md#2026-11-27--rename-and-get-find-an-entry-by-its-folded-name-maintenance-item-m54-rename-backend-and-the-agent-get-find-an-entry-by-its-name-in-any-case) (shipped 2026-11-27)) and the dataset rules ([CLI-M55](a-dataset-is-named-not-pathed-at-every-reader.md), [CLI-M56](a-new-dataset-name-is-one-every-verb-reads.md)). CLI-M54 edits only `rename_session_backend` in the same file. | the user (placement) · the spec (grouping) |
| 2026-11-26 | **The guard is on the id inside the file, not on the typed name.** The first fix proposed (`load` refuses a name that is not a plain id) missed the case inside `sessions/`, which needs no typed path. It would also have refused free-text custom names. | the spec |
| 2026-11-26 | **No gate either way, but best landed before CLI-59d and CLI-63b.** 59d (v0.2.3) would run `chats delete` and `chats title` as typed from a paired device. This item does not make that door safe for path arguments, which needs 59d's own rule (Out of scope), so a block would promise what this item cannot give. 63b (v0.2.4) moves sessions, attachments and recall per person: landed first, this rule carries into its per-person paths. | the spec |
| 2026-11-26 | **A dated revision of Milestone H's "Resume that degrades, never fails"** (MILESTONES, 2026-08-26). A file whose id names another file is now refused by name at load and left out of every listing; today it lists and opens. Everything else still degrades with a warning. No file Apogee wrote is affected, since its id is its file's stem. | the spec |

## Out of scope

- **Per-person session stores**: [CLI-63b](../v0.2.4/personal-data-has-an-owner.md). This item's rule should carry into its paths.
- **HTTP `/v1/sessions`**, which mints its ids and never loads a file.
- **The recall index's rows** (`RecallIndex`, keyed by `chat_id`), beyond the marker file. Those are rows in a database, not paths.
- **ACP's own id handling**, beyond what `load`'s rule gives it. It already refuses a path and a mismatched chat.
- **A `check` row naming skipped session files**: a follow-up (Open questions).
- **`chats title` dropping `save`'s answer** (`chat_history.cpp:474`). After this item, `load` admits only a file that `session_path(chat_id)` names, so `save` answers `Saved::Deleted` only when the file is deleted between the load and the save. A note only.
- **The Mac app passing chat ids after `--`**: [APP-M23](../../../assistant/darwin/MILESTONES.md#2026-11-27--every-name-the-app-passes-is-read-as-a-name-maintenance-item-m23-every-name-the-app-passes-is-read-as-a-name). Ids the CLI mints never begin with `-`.
- **CLI-59d's need for its own rule for path arguments** (an aside of the same investigation). Fixing chats here does not make [59d](../v0.2.3/reads-and-commands-over-the-network.md)'s door safe for `datasets prepare --out <path>`, `embed ingest <path>` or `train run --dataset <path>`.
- **The same rule for collections and datasets**: [CLI-M45](../../../assistant/cli/MILESTONES.md#2026-11-27--a-collection-is-named-not-pathed-at-every-reader-maintenance-item-m45-a-collection-is-named-not-pathed-at-every-reader) (shipped 2026-11-27) and [CLI-M55](a-dataset-is-named-not-pathed-at-every-reader.md).

## Links

- **Blocked by:** — · **Blocks:** —
- **Siblings**, from the same investigation (2026-11-26):
  - [CLI-M55](a-dataset-is-named-not-pathed-at-every-reader.md), the same "named, not pathed" fix for datasets, also best built before CLI-59d;
  - [CLI-M54](../../../assistant/cli/MILESTONES.md#2026-11-27--rename-and-get-find-an-entry-by-its-folded-name-maintenance-item-m54-rename-backend-and-the-agent-get-find-an-entry-by-its-name-in-any-case) (shipped 2026-11-27), which edited `rename_session_backend` in the same `session.cpp` (the sweep now folds case); there was no gate;
  - [CLI-M52](../../../assistant/cli/MILESTONES.md#2026-11-27--a-refused-scaffold-writes-nothing-maintenance-item-m52-a-refused-agent-or-mcp-scaffold-writes-nothing) (shipped 2026-11-27), [CLI-M53](../../../assistant/cli/MILESTONES.md#2026-11-27--a-symphony-the-cli-writes-is-one-it-can-delete-maintenance-item-m53-a-symphony-the-cli-writes-is-one-it-can-delete) (shipped 2026-11-27) and [CLI-M56](a-new-dataset-name-is-one-every-verb-reads.md), the investigation's other items, on other seams.
- **Relates to:**
  - [CLI-M45](../../../assistant/cli/MILESTONES.md#2026-11-27--a-collection-is-named-not-pathed-at-every-reader-maintenance-item-m45-a-collection-is-named-not-pathed-at-every-reader) (shipped 2026-11-27), whose escape-only read rule this rule takes its shape from;
  - CLI-59d (v0.2.3, [reads and commands over the network](../v0.2.3/reads-and-commands-over-the-network.md)), which would run `chats delete` and `chats title` from a paired device;
  - CLI-63b (v0.2.4, [personal data has an owner](../v0.2.4/personal-data-has-an-owner.md)), which moves sessions, attachments and recall per person;
  - [APP-M21](../../../assistant/darwin/MILESTONES.md#2026-11-26--listed-name-reaches-the-verb-maintenance-item-m21-a-listed-name-reaches-the-verb-as-listed), whose CLI asides opened this investigation, and [APP-M23](../../../assistant/darwin/MILESTONES.md#2026-11-27--every-name-the-app-passes-is-read-as-a-name-maintenance-item-m23-every-name-the-app-passes-is-read-as-a-name), the Mac app's Sessions argv;
  - [darwin 7c](../../darwin/v0.2.4/every-place-against-a-server.md), Sessions run against a server through 59d's door;
  - [ADR 0006](../../../adrs/cli/backwards-compatibility.md), [ADR 0002](../../../adrs/cli/mode-parity.md) and [ADR 0007](../../../adrs/cli/tab-completion.md).
- **Revises:** [Milestone H](../../../assistant/cli/MILESTONES.md#milestone-h--apogee-chat)'s "Resume that degrades, never fails" (a dated revision), keeping its "`chat_id` is immutable".
