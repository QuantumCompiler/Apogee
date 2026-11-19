# People on one server: the owner, members, and every credential belonging to a person

**What / why.** The user's calls, 2026-11-14: the harness server serves **people**, not only one person's devices. That means a household or a small team, and the design must suit both. The **owner** is whoever owns the server Mac's account. **Members** are added by name at that Mac. Each member has an **allowance** of what they may use, and their devices and apps never exceed it ([SPEC.md](../../../assistant/cli/SPEC.md) → Core surfaces 4, "Revised again 2026-11-14"). This item builds the identity every other item in track 63 stands on:
- a people registry beside 59a's `devices.json`;
- `apogee people add|list|rename|remove|access`;
- a person bound to every device and app row;
- the composed ceiling, so an app's scope is narrowed by its person's access before its own scope narrows it again;
- the person added to 62f's audit actor.

What a person's own data is, and who may read it, is [63b](personal-data-has-an-owner.md)'s. Whose keys pay is [63c](allowances-keys-and-usage.md)'s. Who answers a question is [63d](consent-goes-to-its-person.md)'s. How tasks queue is [63e](task-slots-per-person.md)'s. Each of them reads the person this item resolves.

Evidence re-measured 2026-11-14: sources at HEAD (`4acf4a2b`), and a probe on the branch binary (`apogee 0.2.0 (b37b0a9f)`) in a scratch `APOGEE_HOME` under the scratchpad with the `mock` backend, `serve --port 0` on loopback, killed after. The admin-mode and people spike ran the same day on the same binary. Its two-bearer probe is quoted where it is the evidence.

**P1 — Apogee has no notion of a person, and the word "role" is already taken.**
1. **Nothing asks who anyone is.** A grep of `lib/src/cli/source` for `getuid`, `getlogin`, `getpwuid`, `"USER"` and `"USERNAME"` finds nothing. The one `geteuid` is `platform::owned_by_this_user` (`infrastructure/platform/platform.h:251-256`, `platform.cpp:672-684`), and its one caller is a sweep of a temporary folder (`presentation/cli/chat_session.cpp:3133`). The install root comes from the OS home directory (`data/contracts/paths.h:170-174`), so the only boundary between people today is the macOS account.
2. **No stored record names a person.** `logger::Session` (`data/logger/session.h:137-253`) has `chat_id`, `custom_name`, `title`, `backend`, `model`, `params`, `messages`, `provider_session_id`, the times, `schema_version`, `turns`, `compactions`, `private_chat` and ACP's `origin`, and no owner. Probed: a chat sent under `Authorization: Bearer apd_alice` with `"session_id":"new"` was saved as `sessions/20261114-200336-a16a.json` with the keys `['backend','chat_id','compactions','messages','schema_version','started_at','turns','updated_at']`. `GET /v1/sessions` under `Bearer apd_bob` answered `200` and listed it. The spike's probe went further: bob read alice's session, wrote a turn into it (`200`) and deleted it (`204`). Fixing that is 63b's job. It cannot be fixed until a credential resolves to a person. A task ledger records authority only as the anonymous word `person`, "the person at the terminal" (`business/tasks/task.h:155-157`).
3. **The registry has devices but no people.** 59a's row holds "name, kind, paired at, last seen, token hash" ([paired-devices.md](../v0.2.3/paired-devices.md):27), and its scope note says "The server admits one person's devices" (`:116`). 59d keeps the admin plane on "its one token for scripts and the operator" ([reads-and-commands-over-the-network.md](../v0.2.3/reads-and-commands-over-the-network.md):84). 59e's recorded default is that any device may steer any task, because "A per-device owner adds nothing when every device belongs to one person" ([tasks-outlive-the-connection.md](../v0.2.3/tasks-outlive-the-connection.md):77).
4. **"Role" means model roles.** `harness/roles.h` is "The one resolver for `models:` role pointers" (`business/harness/roles.h:9`). Suites name members by role (`data/contracts/config.h:343-344`). Probed: `apogee models --help` reads "show role assignments", and `apogee models status` printed six roles (`chat`, `embedding`, `extraction`, `vision`, `transcription`, `utility`). A "member role" would be read as a model. The user's call: **people** and **access**, never roles.

**P5 — what a credential may do is capped by the config, never by its person.**
1. **59f's ceiling is the config.** "A scope never widens past the config" ([paired-apps-with-scopes.md](../v0.2.3/paired-apps-with-scopes.md):29). `narrow_config(config, scope)` takes no person (`:43`). `--on DEVICE` is "a label and a cascade, not a check" (`:146`). So an app paired for a member could be granted any backend, collection or tool class the config holds.
2. **Devices are unscoped by recorded default.** "The person's devices are not scoped in this item. Their rows have no scope and keep the whole harness" (`:148`). Through 59d's door "the server runs exactly that command" a device sends ([reads-and-commands-over-the-network.md](../v0.2.3/reads-and-commands-over-the-network.md):13). A member's phone would reach `config`, `auth`, `devices` and `apps`, and so could widen its own access.
3. **The narrowing pieces exist, keyed to agents, tasks and apps.** `apply_tool_policy` filters the registry, so "a tool that is not registered is not advertised, whatever the model asks" (`presentation/cli/helpers.h:198-203`). `grant_refusal` holds a task's grants to the config (`business/tasks/policy.h:75-101`). 59f's `AppScope` and its one narrowing ([paired-apps-with-scopes.md](../v0.2.3/paired-apps-with-scopes.md):39-47) are the shape an allowance reuses unchanged.
4. **Metered is already a fact a provider states.** `Harness::generation_is_metered` answers `true` for an unroutable model ("unknown is metered", `business/harness/harness.h:318-322`, `harness.cpp:564-570`). That is the existing test for "the server's local models only".

This item delivers **people, and a person behind every credential**:
- `<config dir>/people.json` (`0600`): one row per member, with an id, a name, the preset the access came from, the access itself (59f's `AppScope`) and whether anything is kept. The owner is implicit and never a row. A process running the CLI against this home on the server Mac (a terminal, the Mac app over pipes, `serve` itself) is the owner.
- `apogee people add|list|rename|remove|access`, run at the server Mac. The presets are **Full**, **Family** and **Limited**. Limited is the guest, and nothing of theirs is kept.
- Every device and app row gains `person`. A row without it is the owner's, so older rows read as today. `devices pair --for NAME` and `apps pair --for NAME` bind a member's credential. Members never pair anything themselves.
- A credential resolves to a person and a credential. The effective view is `narrow_config(narrow_config(config, access), scope)`: a member's device gets the first narrowing, a member's app both, and the owner's credentials neither. Approving an app's scope wider than its person's access is refused.
- 59f's ownership filter is keyed by person for a member's credentials. The families that decide who may do what are never a member's. A member uses the server's local models only, until 63c's household key.
- 62f's audit actor gains the person.

**Core constraint(s).**
- **Remote use is an add-on (the anti-goal).** Nothing here may make standalone use need a server, an account or a person. `chat`, `complete`, `execute`, `task`, `acp`, `mcp serve` and the Mac app over pipes never read `people.json`, and behave byte for byte as today. They run as the owner, as they always have. The file does not exist until the first `people add`. Until then `check` shows an Ok row saying "not created yet", as it does for 59a's registry. A server with no members behaves exactly as 59f leaves it.
- **No accounts, no passwords, no login.** A person is a name in the owner's registry plus the credentials the owner paired for them. A member proves who they are only by holding a device or app token minted at the server Mac (59a's exchange). The owner is proved by the operating system: a process that can read the `0600` config is the account that owns it.
- **Fail closed on an unknown person.** "Absent means the owner" applies only to a row with **no** `person` field (rows written before this item). A row that names a person the registry does not hold is refused, never read as the owner: `401` on every door, and `check` names it. This covers a removed person, a hand-edited id and an unreadable `people.json`. No path from a member's credential widens to the owner's.
- **A credential never exceeds its person.** One narrowing (59f's `scope` module), applied twice, at every door. No door keeps its own idea of a person's access.
- **Secrets are unleakable by construction** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → Secrets are unleakable by construction). `people.json` holds no secret, but it is personal metadata about other people, so it is written `0600` through `write_file_atomically(…, /*private_mode=*/true)` (`data/contracts/config_edit.h:478`). `PersonView` names a person's devices and apps through 59a and 59f's views, which structurally lack the token hash.
- **One config mutation path** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → One config mutation path). A person's access lives in `people.json`, never in `config.json`. This item adds no config key and edits no config file. The people registry is written under its own lock, as 59a's registry is (`devices.json.lock`, modeled on `lock_config_file`, `config_edit.h:500`).
- **Older versions keep working** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)). The `person` field, the new file, the verbs, the `--for` flags and the admin routes are additive. A 59a or 59f row with no `person` is the owner's. The dated revisions of 59a, 59e and 59f defaults below are member-only: the owner's devices keep the whole harness.
- **Parity** ([ADR 0002](../../../adrs/cli/mode-parity.md); [ADR 0010](../../../adrs/cli/the-shell-is-a-mode.md)). `people list|add|rename|remove|access` answer from the CLI (`--json`) and on the admin plane, and are classified in the shell's table (`cli/tui_parity.cpp`) and in 59d's network door, where they are owner-only. Adding a person mints no credential: binding one is `devices pair --for` or `apps pair --for`, which stay terminal-only (59a). So nothing here needs a carve-out.
- **Never listens; serving = server** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → Interactive turns never open a listening socket). `people` writes a file and opens no socket. The person is resolved in the socket-free gate and handlers. `httpserver/serve.cpp` stays the one object that may reference `listen`/`accept`.
- **The layers** ([ADR 0001](../../../adrs/cli/layer-enforcement.md), [ADR 0008](../../../adrs/cli/module-map.md)). The registry sits in Data (`secrets`, beside 59a's `devices`). The presets and the double narrowing sit in 59f's Business module `scope`. The gate and the routes sit in `httpserver`, and the verb in `cli`. No new module and no new link.
- **UTF-8 where it enters** (CLI CLAUDE.md). A person's name is mended and bounded before it is stored or printed, whether it came from the terminal or the admin plane.
- **The dev channel** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel)). Probes and tests use a temporary `APOGEE_HOME` under the scratchpad and the mock backend. A live pass runs `apogee-dev serve` over `~/.apogee-dev`, never the release `apogee` or `~/.apogee`, and never loads a launchd service.

**Seam + files.**
- `source/data/secrets/people.h/.cpp` (new, in the `secrets` module): the people registry over `<config dir>/people.json`, `0600`.
  - A row (`Person`) holds:
    - `id`: `p_` plus 16 random hex, never reused;
    - `name`;
    - `preset`: `full`, `family` or `limited`;
    - `edited`: true once the access differs from the preset it came from;
    - `access`: 59f's `AppScope`;
    - `keeps`: false for Limited;
    - `added_at`.
  - The file also holds an optional `owner_name`.
  - Every write holds `people.json.lock` and lands through `write_file_atomically(…, true)`. The file is read fresh on change (by mtime), as 59a's registry is, so a running server applies a change at the next request.
  - A corrupt file is read as **no members**, with a warning, and is never overwritten. Every member row then fails closed by the rule above.
  - `PersonView` adds the names of the person's devices and apps, read from 59a's registry.
  - Locks are always taken in one order, `people` then `devices`, so a removal can cascade without deadlock.
- `source/data/secrets/devices.h/.cpp` (59a's registry, grown by 59f): a row gains `person`, absent for the owner. A pending pairing carries the person, so the exchange mints a bound row. The views gain the person's id and name.
- `source/business/scope/scope.h/.cpp` (59f's module):
  - `preset_access(preset, config, harness)` expands a preset against the config.
  - `access_refusal(access, config, harness)` refuses:
    - a family that is never a member's;
    - a backend `Harness::generation_is_metered` calls metered, until 63c;
    - a vendor-CLI backend;
    - any name 59f's approval already refuses.
  - `scope_refusal_on_approve(scope, config, ceiling)` gains the ceiling, which is the person's access. Anything outside it is refused, and the refusal names the `people access` line that would widen it.
  - `effective(config, caller)` is the one composition: `narrow_config` by the access, then by the app's scope. `narrow_tools`, `command_refusal` and `task_refusal` take the composed scope.
  - `beyond(scope, access)` lists what an app's stored scope holds that its person's access no longer allows, for `apps list`.
- `source/presentation/httpserver/device_auth.h/.cpp` (59a's gate, grown by 59f): a bearer resolves to a `Caller`. The caller carries the person (an id, or the owner), the credential name and kind, and the composed scope. The owner's devices have no scope, as today. A member's device carries their access. A member's app carries both narrowings. A row naming an unknown person is `401`.
- `source/presentation/httpserver/handler.h/.cpp` and 59b's, 59d's and 59e's doors: the `Caller` travels with each request.
  - 59f's ownership filter is keyed by **person** for a member's credentials. A member sees and steers what any of their own credentials started, and gets `404 session_not_found` for anything else. The owner's credentials reach the owner's own records (and anything with no person), as today. What a member's credential started is `404` to them too, because a session's turns and a task's goal are contents. The owner sees a member's activity as metadata (62e, 63b's views), and may halt or cancel a member's task (63e).
  - A member's session and command children get 59f's `--scope-file`, carrying the composed scope and the person id. A child started without one reads nothing new, as in 59f.
  - Until 63b ships, a member's sessions and tasks run with recall off, as 59f's app sessions do. A Limited person's sessions run as `chat --private` does: never written (`Session::never_written`, `session.h:184-189`).
  - `GET /v1/harness/device` answers the caller's person and their access in plain words.
- 62f's audit writer ([audit-log.md](audit-log.md)): the actor gains `person` (id, and the name at that moment). The `people` verbs and every `--for` binding are audited actions.
- `source/presentation/cli/people_cmd.h/.cpp` (new):
  - `apogee people add NAME [--preset full|family|limited] [--allow KIND:NAME]… [--deny KIND:NAME]… [--json]`;
  - `people list [--json]`;
  - `people rename NAME NEW` (`owner` names the owner);
  - `people remove NAME [-y|--yes]`;
  - `people access NAME [--preset P] [--allow KIND:NAME]… [--deny KIND:NAME]… [--json]`.

  The `KIND:NAME` forms are 59f's. Each verb has help and completion ([ADR 0007](../../../adrs/cli/tab-completion.md)), completing people, presets, backends, collections, classes, families, agents and recipes from live state.
- `source/presentation/cli/devices_cmd.cpp` and `apps_cmd.cpp`:
  - `devices pair --for NAME` and `apps pair --for NAME`;
  - `devices list` and `apps list` gain the person column;
  - `apps scope` is held to the person's access;
  - `apps list` shows the parts of a scope that are beyond the person's access.
- `source/presentation/httpserver/admin_people.h/.cpp` (new): `GET /v1/admin/people` (views), `POST /v1/admin/people`, `PATCH /v1/admin/people/{name}` (rename and access, validated by the same functions) and `DELETE /v1/admin/people/{name}`. They are shaped as 59f's app routes and reach only the admin plane, local by default (62g).
- `source/presentation/cli/tui_parity.cpp` and 59d's `remote_parity.cpp`: `people` classified, owner-only. For a member's credential, `people`, `devices`, `apps`, `auth`, `config`, `reset`, `uninstall`, `serve` and `train` are refused whatever the access.
- `source/presentation/cli/check.cpp`:
  - `check_secrets` gains the `people.json` row (`0600`, or skipped on Windows), and `--fix` tightens it.
  - A row confirms that the home and `people.json` belong to the account running `check` (`platform::owned_by_this_user`).
  - A row names any credential bound to an unknown person.
- Docs:
  - [http-api.md](../../../reference/cli/http-api.md) gets a chapter, "People and access". It covers the owner and members, binding, the composed ceiling, the person-keyed ownership filter, the never-a-member families, the admin routes and the fail-closed rule.
  - The commands reference gets `people`, and `--for` on `devices pair` and `apps pair`.
  - CLI CLAUDE.md's secrets sub-rules get "a credential resolves to a person; an unknown person fails closed".
  - DEVELOPER.md gets the file rows.
- Tests (hermetic: a temporary `APOGEE_HOME`, the mock backend, handlers driven through `Mux::dispatch` with no port wherever a port is not the point):
  - `tests/data/secrets/people_test.cpp`:
    - a round trip;
    - `0600` after every write;
    - ids never reused after a remove;
    - a rename keeping the id;
    - names unique, ignoring case, and mended;
    - a corrupt file read as no members and never overwritten;
    - a change seen by a second reader;
    - the lock order.
  - `tests/data/secrets/devices_test.cpp`: a row with `person` round-trips; a 59a or 59f row with none reads as the owner's; removing a person revokes their devices and apps in one operation.
  - `tests/business/scope/scope_test.cpp`:
    - each preset's expansion against a config with local and metered backends;
    - `access_refusal` (never-a-member families, a metered backend);
    - the double narrowing's table;
    - an app scope beyond its person refused at approval;
    - `beyond` after the access narrows.
  - `tests/presentation/httpserver/person_access_test.cpp`:
    - a member's device sees only the backends in their access;
    - a member's app is held to the intersection;
    - the owner's device is unchanged;
    - a member gets `404` on the owner's session, and the owner's device gets `404` on the member's (never its turns);
    - a row naming an unknown person gets `401`;
    - a narrowed access applies at the next request;
    - a Limited person's served session leaves no file;
    - recall is off for a member.
  - `tests/presentation/httpserver/admin_people_test.cpp`: the views, add, rename, access, remove, and the same refusals as the CLI.
  - `tests/presentation/cli/people_cmd_test.cpp`: each verb's output and `--json`; the plain-words access printed before the row is written; `remove` naming what it revokes and asking without `--yes` on a terminal; an unknown name.
  - The existing `leak_test.cpp` (no token or hash in any people view or audit line) and `check_test.cpp` are extended.
  - A shell case, `cli.people_access` (`tests/scripts/sh/people_access.sh`). It is the spike's two-bearer probe turned into a regression: the real binary under `serve --port 0 --tls on --devices-only` on loopback, with two mock backends. The owner's device and member `bob`'s device are each paired with `--print-token`. bob's `GET` of the owner's session is `404`, and a `DELETE` of it is `404`. A backend outside bob's access is `403 scope_refused`. `people access bob --allow backend:other` makes the next request `200`, and `people remove bob` makes the next one `401`.
  - `cli.http_api_conformance` covers the new routes. `cli.install_parity` stays green, since nothing is seeded.

**Reference.**
- In-house precedents:
  - 59a's registry, lock, mtime re-read and fail-closed gate, which this item grows by a person.
  - 59f's `AppScope`, its one narrowing, its scope files and its never-an-app families. An allowance is the same type, and the families gain `people` and `train`.
  - `chat_id`'s immutability (`session.h:138-141`: "An id that changed would break every reference"). Records name a person by id, and a rename changes only the name.
  - `chat --private` (52d, `never_written`), which is what Limited's "nothing kept" means.
  - `Harness::generation_is_metered`, the provider's own word on cost, already the gate for 54b's `mcp_serve` and for spending on Apogee's initiative.
  - `owned_by_this_user` (57a), the existing proof of "this account's".
- Siblings:
  - [59a](../v0.2.3/paired-devices.md), [59b](../v0.2.3/machine-mode-over-the-network.md), [59d](../v0.2.3/reads-and-commands-over-the-network.md), [59e](../v0.2.3/tasks-outlive-the-connection.md) and [59f](../v0.2.3/paired-apps-with-scopes.md): the gate, the doors and the scope this item keys by person. This item is gated on 59f.
  - [59g](../v0.2.3/capability-discovery.md): capability discovery, filtered by the caller's scope, which becomes the composed one.
  - [62f](audit-log.md): the audit, whose actor gains the person.
  - [62g](administering-local-and-marked-devices.md): administering is local, plus devices the owner marks. Only the owner's devices may be marked.
  - [62h](local-admin-reads.md): what a local admin screen reads, which `people list --json` joins.
  - [62b](serve-reload-and-served-vs-disk.md): reload. People need none, being read fresh by mtime.
  - [62e](connections-and-activity.md): connections and activity, which gain the person column.
  - [63b](personal-data-has-an-owner.md), [63c](allowances-keys-and-usage.md), [63d](consent-goes-to-its-person.md) and [63e](task-slots-per-person.md): the rest of track 63, each gated on this item.
  - darwin [10c](../../darwin/macos/v0.2.4/people-screens.md): the people screens in the server window, gated on this item and 63c.
  - darwin [7a](../../darwin/v0.2.4/app-connection-local-or-server.md): the app's side of a paired connection, unchanged by whose it is.
- External prior art, from the spike (2026-11-14). It checked Open WebUI, LibreChat and Home Assistant in their official documentation, and Jellyfin, Plex Home and Nextcloud from third-party pages.
  - **Plex Home** is the closest match: one owner, up to 15 managed or invited members, and libraries shared per person.
  - **Jellyfin** gives each user a per-user library policy and an admin flag. Its server-wide API keys run as admin with no user behind them, and are the cautionary case.
  - **Home Assistant** has one owner who cannot be removed, and per-user tokens carrying that user's full rights with no way to narrow them.
  - **Open WebUI** makes its first account the primary admin. **LibreChat** has admin and user plus custom roles, built for organisations.
  - The divergences:
    - No accounts or passwords: a person is the credentials the owner pairs for them.
    - No admin tier beside the owner: administering stays at the server Mac (62g).
    - Presets, not groups or custom roles: groups pay off only with many people.
    - An app is narrowed below its person, which Home Assistant's users have asked for.
    - The word is "access", since "role" is taken.

**Decisions made** (dated):
- 2026-11-14: Specced from the admin-mode and people spike. The user's calls that shape this item:
  - The server serves a household **or** a small team, and the design suits both.
  - The owner sees members' metadata (devices, apps, activity, which model, when), **never chat contents**. Whoever controls the server Mac can still read its disk, and Apogee says so honestly. `people add` prints it once, and the reference states it.
  - A member uses the server's local models only, unless the owner grants a household cloud key with a limit (63c).
  - The words are **people** and **access** (Owner, Member), never "roles", which in Apogee means model roles.
  - Members' apps are paired by the owner at the server Mac, capped at that member's access.
  - Live changes apply on the spot, and what still needs a restart is said.
  - An admin mode: a separate **Apogee Server** window in the macOS app, offered only on the Mac that serves, which drives the local CLI over pipes (darwin 10a–10c). Administering happens there or at that Mac's terminal. Remote administering is off by default, and the owner may allow it from chosen paired devices (62g, revising 59d's one admin token for any network caller).
  - The window installs the server as a background service (launchd) and starts, stops and restarts it (62d). The service runs under the owner's account, so a served request runs as the owner's process and the person is the credential's, never the process's.
  - The defaults the user took: live changes applied on the spot, with what still needs a restart said; an audit kept 90 days of who, what and when, without request contents (62f); collections as shared libraries handed out per person plus a member's private ones, and notes and saved decisions personal by default (63b); members' apps paired by the owner at the server Mac, capped at that member's access (here); a per-person task queue, not a refusal (63e).
  - The SPEC amendments are already written ([SPEC.md](../../../assistant/cli/SPEC.md) → Core surfaces 4, "Revised again 2026-11-14"; [GUI SPEC](../../../assistant/gui/SPEC.md) → Core surfaces 3, the server window).
  - The standing law: remote use is an add-on, and the CLI alone and the Mac app over pipes stay complete with no server.
- 2026-11-14: The model, the agent's default from the prior art, recorded with the spike:
  - The owner is implicit: the OS account that owns `APOGEE_HOME` on the server Mac.
  - Members each have an allowance in 59f's `AppScope` shape.
  - Presets (Full, Family, Limited) are used instead of groups.
  - A guest is a member on the Limited preset with nothing kept.
- 2026-11-14: Placement, the user's call: both halves land before v0.2.5, both in v0.2.4: the server console (CLI track 62, darwin/macos 10a–10b) and people and access (CLI track 63, darwin/macos 10c). This item is CLI 63a, v0.2.4, first of track 63, gated on 59f. 63b–63e gate on it, and darwin 10c gates on it and 63c. Nothing already queued is renumbered. It collapses two walls into one item: who a person is (P1), and the ceiling a person puts on their credentials (P5).
- 2026-11-14: This item revises three recorded defaults, **for members only**:
  - 59a's "The server admits one person's devices" ([paired-devices.md](../v0.2.3/paired-devices.md):116) becomes "the owner's and the members' devices, each bound to its person".
  - 59f's "the person's devices are not scoped" ([paired-apps-with-scopes.md](../v0.2.3/paired-apps-with-scopes.md):148) stays true of the owner's devices. A member's devices are held to that member's access.
  - 59e's "any of them may attach to, halt, cancel or resume any task" ([tasks-outlive-the-connection.md](../v0.2.3/tasks-outlive-the-connection.md):77) stays true of the owner's devices for the owner's tasks. A member's devices reach only that member's tasks. The owner may halt or cancel a member's task (63e) but never attach to it or read it, since its goal and plan are contents (63b).

  59d's "the admin plane keeps its one token" is revised by 62g, not here. This item adds that a member's device is never marked.

**Open calls:**
- [default: `people.json` sits beside `devices.json` in the config directory, `0600`, created by the first `people add`. A row's id is `p_` plus 16 random hex, never reused. Records (63b's, the audit's) name a person by id, so a rename rewrites nothing. A name is mended, cut to 64 characters, and unique ignoring case. `owner` is reserved for the owner.]
- [default: the owner is never a row and cannot be removed. Anything with no `person` is the owner's. The owner is shown as "Owner" until `people rename owner NAME` stores `owner_name`. No OS-user lookup is added, because a process that can read the `0600` config is the owner's account. `check` confirms that with `owned_by_this_user`. Someone at the server Mac under their own macOS account has their own, separate Apogee install, not a member's seat. A member exists only as the credentials the owner pairs for them.]
- [default: a preset is a **starting point**, written out when it is applied, never a live pointer. This follows 59f's rule that changing `models.default` never widens an app. The row records the preset and whether it has been edited since, shown as "Family, edited". A config that later gains a backend never widens anyone. `people list` says when a preset's expansion today differs from a row ("Family now also covers backend 'local-2'; `people access bob --preset family` applies it").]
- [default: what each preset holds, printed in plain words before the row is written:
  - **Full**: every local backend, every collection, the tool classes `read`, `edit`, `write` and `outbound`, every command family that is not on the never list, and `task:goal` with every recipe and agent.
  - **Family**: every local backend, every collection, the `read` and `outbound` tool classes, the read-only command families (`models list`, `collections list`, `graph query`, `search`), and `task:goal`.
  - **Limited**: the default backend only, and only when it is local. No collections, tools, commands or tasks. Nothing kept: its sessions are never written, never recalled and never captured.

  `edit` and `write` act under the folder `serve` gives served tools, as 59f's approval line says. A folder of a person's own is 63b's. Until 63b ships, every preset leaves out the families and tools that read personal records (`chats`, `knowledge`, `task list`, and the notes tools), because those records are not yet separated.]
- [default: `people add` with no `--preset` uses **Family**. It is a household's everyday member, and the owner names the person at the server Mac and reads the plain-words access before it is written. Limited is the guest. 59f's least-by-default rule stays for apps, which arrive from code, not from the owner's own hand.]
- [default: the families that decide who may do what, and what costs the owner, are **never a member's**, whatever their access: `people`, `devices`, `apps`, `auth`, `config`, `reset`, `uninstall`, `serve` and `train`, and the admin plane. 59f's never-an-app list gains `people` and `train`. A member who could edit the config or the registry could widen their own access. `models pull|delete|convert|quantize`, `mcp create` and `review` are in no preset, and the owner may grant them by name with `--allow command:…`.]
- [default: a member's access may name only backends the Harness calls unmetered (`generation_is_metered` false, so unknown is metered). `--allow backend:<metered>` for a member is refused: "a member uses this server's local models; a cloud backend needs a household key with a limit". 63c lifts this with its grant.]
- [default: a device's or app's person is set at pairing and never changes. `devices pair --for NAME` binds a member's device, and with no `--for` the device is the owner's. An app's person is its device's person (`--on DEVICE`). `apps pair --for NAME` is required for a member's app on `this machine`, and is refused when it contradicts the device. Rebinding means revoking and pairing again. There is no move verb.]
- [default: narrowing a person's access leaves their apps' stored scopes as they are. The effective view is the intersection, so nothing reaches past the new access. `apps list` marks what is "beyond Bob's access", and widening Bob again restores it without re-approving each app. A narrowed access closes the member's live harness sessions at their next event and halts a running task, through 59f's narrowed-scope path. A widened one applies to what they start next. Every change applies at the next request with no restart, because the registry is read by mtime. The verb says "applies now".]
- [default: `people remove NAME` names the devices and apps it will revoke. On a terminal it asks once, and `-y|--yes` skips the question, as `reset` does. The revocations and the row's removal land under both locks in one operation. The id is never reused, so anything stamped with it reads as "a removed person". What happens to a removed person's data is 63b's.]
- [default: 59f's ownership filter (sessions, harness sessions, jobs and tasks record their credential) is keyed by person for a member's credentials. A member reaches what any of their own devices or apps started. An app still reaches only its own, as in 59f. The owner's credentials reach the owner's records and anything with no person, and get `404` on what a member's credential started (the never-contents call; the owner's metadata views are 62e's and 63b's). Records are stamped on disk by 63b. Until then the filter works over 59f's in-process credential record.]
- [default: until 63b ships, a member's sessions and tasks run with recall off, as an app's do. Recall is the leak that happens without anyone asking. A Limited person's sessions are never written at any time.]
- [default: `people add|rename|remove|access` and each `--for` binding are audit actions, and every audit line's actor carries `person` (its id, and its name at that moment). If 62f has not shipped when this item builds, 62f takes the field as part of its own build.]
- [default: `GET /v1/harness/device` answers the caller's person and their access, so a member's device can show what it may use. 59g's capability read is filtered by the composed scope with no change of shape.]
- [default: the admin plane serves `people` as views plus add, rename, access and remove, validated by the CLI's functions. Adding a person mints nothing, so no credential is ever created over HTTP. Binding stays on the server's terminal (59a).]
- (consumed decision: 59a) The registry's lock, the mtime re-read, pairing, the exchange and TLS. A member's device pairs over the same `POST /v1/pair`.
- (consumed decision: 59f) The `AppScope` shape, the `KIND:NAME` flags, the narrowing, scope files for children, `scope_refused`, and the narrowed-scope path for live sessions.
- (consumed decision: 62f) The audit's format, retention (90 days) and readers.
- (consumed decision: 62g) Which of the owner's devices may administer remotely. This item adds only that a member's device is never one.
- (consumed decision: 63b) Personal data on disk: per-person records, recall, notes, knowledge, collections (shared libraries handed out per person, plus a member's private ones), training, and a removed person's data.
- (consumed decision: 63c) The household cloud key, its limit, and usage per person.
- (consumed decision: 63d) Which devices a session's questions go to.
- (consumed decision: 63e) The per-person task queue.
- (consumed decision: darwin 10c) How people and access look in the server window.

No **[user]** call blocks this item.

**Guardrail(s).**
- `person_access_test` pins the composed ceiling at every door through `Mux::dispatch`. A test plants a door path that narrows once, or not at all, for a member credential, and it fails.
- `scope_test` pins each preset's expansion, the never-a-member families, the metered refusal and the double narrowing's table.
- A row naming an unknown person, and an unreadable `people.json`, are `401` on every door. A test fails if either is ever read as the owner.
- `cli.people_access` turns the spike's two-bearer probe into a regression fixture on the real binary: a member's device can no longer read, write into or delete the owner's session.
- A conformance case shows the inference plane byte-identical for a stock client (no bearer, and a dummy `sk-` bearer) with and without people added.
- A test drives `chat`, `complete`, `execute` and `task run` with `people.json` absent, then unreadable, and checks they are unchanged (the anti-goal). A child started without `--scope-file` reads no registry.
- `leak_test` checks that no person view, `check` row or audit line carries a token or a hash. `cli.install_parity` stays green.

**Acceptance criteria:**
- [ ] In a sandbox home with backends `mock` (the default) and `other`, and an `openai`-type entry `cloud` (no network call is made), `apogee people add bob --json` prints Family's access in plain words (backends `mock` and `other`, not `cloud`) and the "your disk" sentence, then writes `people.json` `0600`. `people add carol --preset limited` says nothing of hers is kept.
- [ ] `apogee people access bob --allow backend:cloud` is refused, naming the local-models rule. `--allow command:config` is refused as never a member's.
- [ ] `apogee devices pair --for bob --name "Bob's phone" --print-token` mints a token bound to bob. `devices list` shows the person column. A device paired with no `--for` shows as the owner's.
- [ ] `apogee apps pair --name helper --on "Bob's phone" --allow backend:other --print-token` succeeds. After `people access bob --deny backend:other`, its next request on `other` is `403 scope_refused`, and `apps list` marks `backend:other` as beyond bob's access.
- [ ] Under `serve --port 0 --tls on --devices-only` on loopback: the owner's device starts a session. bob's device gets `404` for it on `GET` and `DELETE`, and does not see it in `GET /v1/sessions`. bob's session is `404` to the owner's device too (the owner never sees contents). carol's session leaves no file under `sessions/`.
- [ ] `apogee people rename bob robert` keeps every binding. `people remove robert --yes` names and revokes `Bob's phone` and `helper`, and their next requests are `401`. A hand-edited row naming an unknown person is `401`, and `check` names it.
- [ ] An audit line for each `people` change carries the actor's person. `people list --json` gives every view 62h and darwin 10c read: id, name, owner flag, preset, edited, access, keeps, devices and apps.
- [ ] `chat`, `complete`, `execute` and `task run` behave byte for byte as before with no `people.json`. A stock OpenAI client gets the same responses with people added as without. The full suite is green on all five targets.
- [ ] Optional live pass, said when skipped: the user runs `apogee-dev serve --bind 0.0.0.0 --allow-remote --devices-only`, adds a test member, pairs a second machine for them, and sees the owner's sessions invisible to it and a backend outside its access refused.

**Scope note.** Earmarked for v0.2.4 (track 63); gated on 59f. 63b, 63c, 63d and 63e gate on it, and darwin 10c gates on it and 63c. Out of scope: Moved here from v0.2.7 on 2026-11-18, when the releases were merged (the user's call); the id is unchanged.
- Personal data on disk, recall per person, notes, knowledge, collections and training (63b).
- Household and per-person keys, limits and usage (63c).
- Routing questions and approvals to the session's person (63d).
- The per-person task queue (63e).
- The people screens (darwin 10c).
- Groups, custom roles, and a second admin beside the owner.
- Accounts, passwords, invitations, and members pairing or administering anything themselves.
- Moving a device or app to another person.
- OS-level separation (a macOS account or sandbox per member).
- Any change to standalone use.
