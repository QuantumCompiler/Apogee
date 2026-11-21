# APP-10c · People in the server window

| Field | Value |
|---|---|
| **Type** | ✨ Feature |
| **Project** | Apple apps · macOS |
| **Status** | 🔒 Blocked |
| **Fix version** | v0.2.4 |
| **Component** | Server window › People (`PresentationServer`) |
| **Labels** | `server-window` `people-and-access` `metadata-only` `parity` |
| **Epic** | Track 10 · the server window |
| **Blocked by** | [10b](server-window.md), [CLI 63a](../../../cli/v0.2.4/people-on-one-server.md), [CLI 63c](../../../cli/v0.2.4/allowances-keys-and-usage.md) |
| **Blocks** | — |
| **Reporter** | The admin-mode and people spike (2026-11-14), on the user's calls |
| **Created** | 2026-11-14 |

---

## Summary

The person's Apogee server now serves people, an owner and members, but neither the app nor the binary it drives has any notion of a person: every member would spend the owner's keys, nobody's usage is counted, and what a device or app may do is capped by the config, never by its person. This item builds the **People place in the "Apogee Server" window** on the Mac that serves: the owner and members, each member's preset and allowance, their devices and apps, their usage, the household cloud key granted with its limit, and adding, renaming and removing people, all through the local CLI over pipes. It shows metadata only and never a member's chat contents.

## Description

### User story

**As** the owner of an Apogee server that serves a household or a small team,
**I want** to see and administer the people on it (each member's access, devices and apps, cloud key and usage) in the server window on that Mac,
**so that** every member uses only what I allow them and their use is counted, without Apogee ever showing me their conversations.

### Background

The user's calls, 2026-11-14, from the admin-mode and people spike: the person's Apogee server serves **people**, a household or a small team. The **owner** is whoever owns the server Mac's account. **Members** are added by name at that Mac, and each has an **allowance** of what they may use ([CLI SPEC](../../../../assistant/cli/SPEC.md) → Core surfaces 4, "Revised again 2026-11-14"). The owner administers all of it in the **"Apogee Server" window**, on that Mac only, through the local CLI over pipes ([GUI SPEC](../../../../assistant/gui/SPEC.md) → Core surfaces 3).

[10a](design-the-server-window.md) draws the window's People place and a member's page first. [10b](server-window.md) builds the window earlier in the same release, with People showing its fallback. This item builds People: the owner and members, each member's preset and allowance, their devices and apps, their usage, the household cloud key granted with its limit, and adding, renaming and removing people at the server Mac. It shows metadata only and never a member's chat contents. It is the app's side of three of the spike's walls (P1, P4 and P5, under Evidence).

This item delivers **the People place in the server window**, built as 10a's boards draw it:
- **People**: the owner first (never removable), then the members. Each member shows their preset ("Family", or "Family, edited"), their devices and apps by name, and their usage against their limit, with billed use marked.
- **A member's page**:
  - their access in plain words, in 59f's `AppScope` parts (backends, collections, tool classes, commands, task kinds), edited as `people access` flags;
  - their devices and apps (name, kind, paired, last seen), with Pair a device or app for them through 10b's pairing sheet and `--for`, and each app's scope marked where it reaches beyond their access;
  - the household cloud key: granted or not, with its limit, marked billed;
  - usage over a period, per backend;
  - a guest (Limited) drawn as "nothing kept".
- **Add**, **Rename** (the owner's name too) and **Remove**, each the CLI verb, read back. Remove is asked first and names every device and app it revokes.
- On every page, the honesty line: Apogee never shows anyone's conversations here, and anyone who controls this Mac can still read its disk.

### Evidence

Evidence re-measured 2026-11-14: sources at HEAD (`4acf4a2b`), and a probe on the branch binary (`apogee 0.2.0 (b37b0a9f, 2026-11-09, macos-arm64)`) in a scratch `APOGEE_HOME` under the session scratchpad with `config init`, the `mock` backend and no server. The home was deleted after. The spike's five probes ran the same day on the same binary.

**P1 — the app has no notion of a person, and neither does the binary it drives.**

1. **The binary has no `people` word.** Probed: `apogee people list` and `people list --json` exit 109 ("The following argument was not expected"), as do `devices list` and `apps list`. `__complete ""` answers 30 words, and `people` is not one of them. [63a](../../../cli/v0.2.4/people-on-one-server.md) builds it: `people add|list|rename|remove|access`, `people.json` beside 59a's registry, and a person on every device and app row ([`people-on-one-server.md`](../../../cli/v0.2.4/people-on-one-server.md): Description, the people registry beside 59a's `devices.json`, the verbs and a person bound to every device and app row; Technical approach, the new `people_cmd.h/.cpp` and its five verbs).
2. **The app has no type, read or view for a person.** In `lib/src/darwin/source`, "people" appears only in the sense of "the lines it prints for people" (`business/jobs/JobEvent.swift:42`, `SupervisedJob.swift:10`). `ParityTable` holds 33 rows with no `people` row (`tests/presentation/shell/ParityTests.swift:35-43`, the census: 24 screens, 2 console, 4 carve-outs, 3 plumbing). The app has one `Window` and the `Settings` scene (`presentation/app/Views/ApogeeApp.swift:52-54`, `:99`). The second window is 10b's to build.
3. **No board draws people at HEAD.** A grep of the old one-file prototype for "people" found only two lines of canned video copy (its `main.html:2613`, `:4166` at HEAD, 2026-11-14); the design app that replaced it draws no people either (re-checked 2026-11-21, design 1t: no preset, view or document of a person). 10a adds the `server:people` and `server:member` entries ([`design-the-server-window.md`](design-the-server-window.md), Technical approach: the board list, `server:member` being a member's preset, allowance, devices, apps, key and limit) and the People default this item builds from (the same document, Open questions: the default "People shows the owner first (this Mac's account, never removable, every right), then the members…").
4. **"Role" is taken in the app as in the CLI.** The Models place's Roles tab (`presentation/models/Views/ModelsScreen.swift:41`, `:487`) and the design's Settings pane "Models & roles" (`main.html:5578` at HEAD) mean model roles. The user's call is **people** and **access**.

**P4 — every member spends the owner's keys, and nobody's usage is counted.**

1. **One key per vendor, with no holder.** The credential store is "one slot per API-billing provider type" (`lib/src/cli/source/data/secrets/store.h:9-18`). Probed: after `auth add openai --stdin` with a dummy key, `auth list --output-format json` answered `{"data":[{"provider":"openai","stored_at":"…"}]}`, and `credentials.json` (`0600`) holds `credentials` and `version`, each entry only `key` and `stored_at`. The app's one way to set a key is Settings › Backends & keys, `auth add <provider> --stdin` (`presentation/settings/ViewModels/SettingsViewModel.swift:505-514`). Nothing says who may spend it.
2. **Usage lasts nowhere.** A served turn's tokens are the backend's own or an estimate (`presentation/httpserver/handler.cpp:612-621`) and end up in one log line. 62e's live view per credential "persists nothing" ([connections-and-activity.md](../../../cli/v0.2.4/connections-and-activity.md), Decision log, 2026-11-14: "This item persists nothing", the live view for the server's run). Usage per person that outlives a restart, with limits, is [63c](../../../cli/v0.2.4/allowances-keys-and-usage.md)'s: a ledger read by `apogee usage --person NAME --output-format json` ([`allowances-keys-and-usage.md`](../../../cli/v0.2.4/allowances-keys-and-usage.md): Description, **Usage per person**, a durable ledger `apogee usage --output-format json` reads for the owner; Technical approach, the new `usage_cmd.h/.cpp`).
3. **The server checks one list for everyone.** `resolve_served` checks one server-wide served list (`handler.cpp:252-295`), and nothing between a credential and a backend names a person. In the spike's probe, a second bearer (`person_bob`) got `401` on the admin plane but `200` on every session route, so a member's request would bill the owner's key.

**P5 — what a device or app may do is capped by the config, never by its person.**

1. 59f: "A scope never widens past the config" ([paired-apps-with-scopes.md](../../../cli/v0.2.3/paired-apps-with-scopes.md), Constraints: **A scope never widens past the config.**). `--on DEVICE` is "a label and a cascade, not a check" (the same document, Open questions: the `--on DEVICE` default), and the person's devices "keep the whole harness" (Open questions: the default that the person's devices are not scoped in that item).
2. 63a composes the ceiling: `narrow_config(narrow_config(config, access), scope)`. Approving an app's scope wider than its person's access is refused, and `apps list` marks what lies beyond it ([`people-on-one-server.md`](../../../cli/v0.2.4/people-on-one-server.md): Description, the composed effective view and the refusal of a scope wider than its person's access; Technical approach, `scope_refusal_on_approve` gaining the ceiling, `effective(config, caller)`, and `beyond(scope, access)` for `apps list`).
3. 10a's scope editor already assumes the person's access as the ceiling once 63c answers ([`design-the-server-window.md`](design-the-server-window.md), Open questions: the Apps default, "Once 63c answers, the editor shows the person's allowance as the ceiling"). Until this item, the editor 10b builds is bounded by the config alone.

## Acceptance criteria

- [ ] **Given** a Mac that serves, with a binary carrying 63a and 63c, **when** the owner opens the server window's People (⌘6), **then** it lists the owner first with Rename and no Remove, then each member with their preset, devices, apps and usage against their limit, with billed use marked, and the honesty line is on the list and on each member's page.
- [ ] **Given** the People place, **when** the owner adds a member, **then** Add first shows the CLI's plain words for the preset, creates the member through `people add` and opens their page; **when** the owner renames or removes a member, **then** Rename and Remove run their verbs, and Remove first names every device and app it revokes and the list reads back without them.
- [ ] **Given** a member's page, **when** the owner changes their access in the access editor, **then** it runs one `people access` per Apply; a metered backend for a member is refused in the CLI's words; and narrowing says what it closes and that it applies now.
- [ ] **Given** a member's page, **when** the owner pairs a device or an app for them, **then** `--for NAME` runs through 10b's sheet, the app's scope editor is capped at the person's access, and an app's scope beyond it is marked.
- [ ] **Given** a member's page, **when** the owner opens Cloud key and Usage, **then** Cloud key shows each stored key by provider and date only, and this member's grant and limit; granting runs 63c's verb; and Usage shows 63c's figures per backend, with estimates said.
- [ ] **Given** any state of the People place, **when** it reads, **then** no read it runs returns a member's words (the allowlist test), and no view shows a token, hash, code or key.
- [ ] **Given** a binary that has them, **when** a member's page is shown, **then** grants (63d), personal-data counts and handed-out collections (63b), and task limits (63e) are shown and changed as specified; **given** a binary that does not, each says so in one line; **given** a binary without the `people` word, People shows 10b's fallback.
- [ ] `ParityTable` classifies `people` and 63c's `usage`. The suite is green with `lib/scripts/cicd.sh --app darwin --test`. Every new view has its named previews, and the layering check passes.
- [ ] **Given** a Mac with no server, **when** the app runs, **then** it is unchanged: no People, no menu item and no new key.
- [ ] Optional live pass, said when skipped: with M18 shipped, a development build on the dev channel against `apogee-dev serve` over `~/.apogee-dev` adds a test member, pairs a second test device for them, narrows their access, and removes them. Never the release `apogee` or `~/.apogee`, and never a real cloud key.

## Constraints

- **Nothing makes standalone use need a server or an account (the anti-goal).** People exists only inside the server window, which appears only on a Mac that serves (10b). The harness window, its places, its keys and Settings show no person, member, sign-in or allowance, and behave byte for byte as before on a Mac with no members or no server. There is no login: the owner is proved by the operating system (63a: a process that can read the `0600` config is the owner's account), and the app adds no identity of its own.
- **Never listens; serving = server.** Every read and change is the local `apogee` over pipes, through the runner every place uses. The app never calls `/v1/admin/people` or any port on its own Mac ([GUI SPEC](../../../../assistant/gui/SPEC.md) → Non-goals, No HTTP path for local work). People lives only in the server window, so a Mac connected to a server (7a) has no People place. What `people` typed at a remote console does is the CLI's: [62g](../../../cli/v0.2.4/administering-local-and-marked-devices.md) classifies it `Administer`, run only from a device the owner marked ([`administering-local-and-marked-devices.md`](../../../cli/v0.2.4/administering-local-and-marked-devices.md), Open questions: the `Administer` list, "`people` when 63a ships"), and 63a refuses it for every member's credential ([`people-on-one-server.md`](../../../cli/v0.2.4/people-on-one-server.md), Technical approach: `tui_parity.cpp` and 59d's `remote_parity.cpp`, `people` classified owner-only and refused for a member's credential). The app draws that refusal in the CLI's words.
- **One config mutation path.** A person's access lives in `people.json` and changes only through `people add|rename|remove|access` (63a). The household key's grant, its limit and the member's files root change only through 63c's flags on that verb, `people access NAME --cloud BACKEND --limit tokens:N/PERIOD|cost:N/PERIOD`, `--no-cloud` and `--files-root` ([`allowances-keys-and-usage.md`](../../../cli/v0.2.4/allowances-keys-and-usage.md), Technical approach: `people_cmd.cpp`, 63a's `people access` gaining `--cloud`, `--limit`, `--no-cloud` and `--files-root`). The key itself is stored only through `auth add <provider> --stdin`. Task limits change only through `config set tasks.*` ([63e](../../../cli/v0.2.4/task-slots-per-person.md)). Each change is read back with `people list --json`, as Settings' panes read back theirs. The app writes no file.
- **Secrets are unleakable.** No People view shows a token, a hash, a pairing code or a key. The household key is drawn by provider name, `stored_at`, grant and limit only (`auth list`'s fields, probed above). A key typed in this window goes to `auth add --stdin` on standard input and is held nowhere, as `SettingsViewModel.addKey` does (`:505-514`). Pairing codes appear only on 10b's sheet.
- **Metadata, never contents (the user's call).** The People place never runs a read that returns a member's words: `chats list|show`, `knowledge`, recall, notes, a task's goal or plan, a session's title, or a website a member granted ([63d](../../../cli/v0.2.4/consent-goes-to-its-person.md) gives the owner tools and a website count only). It shows counts, names of devices and apps, backends, times and token figures. The honesty line is on the list and on every member's page.
- **Honest by default** ([the design's guide](../../../../assistant/design/CLAUDE.md) → The design's rules). Every figure (usage, limit, last seen, counts) is one the CLI reports, in its words where it has them, with "estimated" carried when the CLI says so. Billed use is marked with the `model.api` token. A refusal (a metered backend for a member, a family that is never a member's) is drawn in the CLI's words.
- **Additive** (the CLI's [ADR 0006](../../../../adrs/cli/backwards-compatibility.md); the app's tolerant client). Unknown fields in `people list --json` and 63c's documents are kept, never fatal. Against an older binary, People keeps 10b's fallback (the owner alone, members arriving with a later apogee) from the CLI's own exit-109 refusal. A part whose verb the binary lacks (63b's counts, 63d's grants, 63e's limits) says so in one line and is never drawn empty.
- **Parity** (the CLI's [ADR 0002](../../../../adrs/cli/mode-parity.md); the GUI's [ADR 0003](../../../../adrs/gui/parity.md)). Every act maps to a verb a person can type at the server Mac. `people`, and any top-level word 63c adds, are classified in `ParityTable` in the same change, and `scripts/refresh-parity-fixture.sh` is re-run against that CLI.
- **The layers and MVVM** ([ADR 0001](../../../../adrs/gui/layer-enforcement.md), [ADR 0002](../../../../adrs/darwin/presentation-mvvm.md)). The typed reads sit in Business (`reads`), the place in 10b's Presentation module, with a SwiftUI-free view-model and the shared `styles`.
- **Every view is seen on its own** ([ADR 0004](../../../../adrs/darwin/module-schemes-and-previews.md)). Each state a person sees differently has a named `#Preview` over recorded documents, never the binary, `~/.apogee` or the network.
- **The dev channel** ([GUI CLAUDE.md](../../../../assistant/darwin/CLAUDE.md#development-runs-on-the-dev-channel); [M18](../maintenance/app-dev-channel.md)). A development build's window drives `apogee-dev` over `~/.apogee-dev`. Fixtures are recorded in a scratch `APOGEE_HOME` with the mock backend. Nothing here touches `~/.apogee`, loads a launchd service, pulls a model or calls a cloud API.

## Technical approach

| File | Change |
|---|---|
| `lib/src/darwin/source/business/reads/People.swift` (new) | The typed reads, each over `JSONValue` and keeping every field: `Person`, `PersonAccess`, `PersonAllowance`, `PersonUsage` and `PeopleVerbs` (below). |
| `lib/src/darwin/source/business/reads/CommandRunning.swift` | Unchanged. Changes run as commands with their outcome, as Settings' do. |
| `lib/src/darwin/source/presentation/server/` (10b's `PresentationServer`) | The People place, in the files below. |
| `ViewModels/PeopleViewModel.swift` | The list and the chosen member, read on focus and after each change. It holds the reads it may run, an allowlist (Testing). Each change waits for the one before it, as Settings' changes do since 6c. |
| `ViewModels/PersonEditing.swift` | The add, rename, access and remove forms as typed, the plain-words preview before writing, and what a narrowed access will close (the member's live sessions and running task, from 62e's activity rows for that person). |
| `Views/PeopleView.swift`, `Views/MemberPage.swift`, `Views/AddPersonSheet.swift`, `Views/AccessEditor.swift`, `Views/RemovePersonSheet.swift`, `Views/KeyGrantSheet.swift` | The views. |
| `Previews/` | The fakes and slices of the recorded documents. |
| 10b's `AppsView` scope editor | Its ceiling becomes the app's person's access where the binary reports one, and `apps list`'s beyond-access parts are marked. |
| 10b's `DevicesView` and pairing sheet | Pair for a person passes `--for NAME`, and the lists gain the person column. |
| `lib/src/darwin/source/presentation/shell/ViewModels/Parity.swift` | `people` as a screen of `PresentationServer`, its remote meaning 62g's `Administer`, and 63c's `usage` beside it. |
| `lib/src/darwin/tests/business/reads/Fixtures/` | The recorded documents (below). |
| `lib/design/prototype/` | Only if the build needs a state 10a's boards do not draw. Each such state is drawn there first (a preset, its document and its gallery entry, held by the design's local gate), per the design's law (Decision log, 2026-11-14). |

The typed reads in `People.swift`:
- `Person`, from `people list --json` (63a): id, name, owner flag, preset, edited, access, keeps, devices and apps.
- `PersonAccess`, the `AppScope` parts. They are read as sent and written back only as `--allow` and `--deny` `KIND:NAME` flags.
- `PersonAllowance` and `PersonUsage`, from 63c's documents: the metered backends granted (`cloud`) and the limit (`people list --json`), and `usage --person NAME --output-format json` per backend over a period, with billed and estimated as the CLI states them.
- `PeopleVerbs`: the argv for `people add|rename|remove|access` (through `Argv`, a name always placed after `--`), with 63c's `--cloud`, `--limit`, `--no-cloud` and `--files-root`.

The fixtures in `lib/src/darwin/tests/business/reads/Fixtures/`, recorded from the binary in a scratch home with the mock backend and two configured backends (one local, one `openai`-type entry never called):
- `people-list-owner.json` (no members);
- `people-list.json` (the owner named, a Family member, a Family-edited member, a Limited guest, with devices and apps);
- `people-add-family.json`;
- `people-access-refused-metered.err` and `people-access-refused-family.err`;
- `people-remove.txt`;
- 63c's allowance and usage documents (granted and not, under and at the limit);
- `people-older.err` (exit 109 from a binary without the `people` word).

**Reference.**
- Siblings:
  - [10a](design-the-server-window.md), the boards this item builds: the `server:people` and `server:member` entries and the People default (its Open questions: "People shows the owner first…").
  - [10b](server-window.md), the window, its module, its Devices and Apps places, the pairing sheet, its fallbacks and its parity rows. This item is gated on it.
  - [7a](../../v0.2.4/app-connection-local-or-server.md), the client side. Its Connections pane never shows people.
- The CLI words People reads and runs:
  - [63a](../../../cli/v0.2.4/people-on-one-server.md): `people add|list|rename|remove|access`, presets, the composed ceiling, `--for`, fail-closed on an unknown person. Gate.
  - [63c](../../../cli/v0.2.4/allowances-keys-and-usage.md): the household key's grant and limit (`people access --cloud … --limit …`), the files root, and usage per person (`apogee usage`; its Description, **Usage per person**; its Technical approach, `people_cmd.cpp`'s new flags and the new `usage_cmd.h/.cpp`). It leaves how they look to this item (its Open questions: "(consumed decision: darwin 10c) How usage, limits and the grant look in the server window"). Gate.
  - [63b](../../../cli/v0.2.4/personal-data-has-an-owner.md): a member's personal data as counts, and shared collections handed out per person. Drawn where the binary has it.
  - [63d](../../../cli/v0.2.4/consent-goes-to-its-person.md): `people grants NAME --json` and `--forget-all`, tools and a website count (its Open questions: the default that the owner sees a member's grants as tool names and a count of websites), which this item shows and clears (its Reference: "darwin 10c, people in the server window, where the owner sees and clears a member's grants"). Drawn where the binary has it.
  - [63e](../../../cli/v0.2.4/task-slots-per-person.md): `tasks.owner_slots`, `member_slots`, `per_member` and `queue_per_member` (its Technical approach: the config's `tasks` section; its Open questions: the default limits, 1, 1, 1 and 10), which the owner sets here (its Reference: "darwin 10c (the owner sets the limits in the server window)"). Drawn where the binary has it.
  - [62e](../../../cli/v0.2.4/connections-and-activity.md), live activity with the person on each row. [62f](../../../cli/v0.2.4/audit-log.md), the audit, whose actor carries the person. [62h](../../../cli/v0.2.4/local-admin-reads.md), the local admin reads `people list --json` joins. [62g](../../../cli/v0.2.4/administering-local-and-marked-devices.md), `people`'s remote meaning.
- In-house precedents:
  - Settings' panes, each over the verbs a person types with each change read back, and a key sent on standard input and held nowhere (`SettingsViewModel.swift:505-514`).
  - The Models store's asked deletes, its plan read first and shown (`DeletePlan`), the shape of Remove.
  - The Tasks screen's refusal drawn in the CLI's words (6g).
- Prior art (the spike's `roles-prior-art`, 2026-11-14):
  - Plex Home: one owner and up to 15 members, libraries shared per person. The closest household shape, and the reason People lists a person with their libraries and devices on one page.
  - Nextcloud's "Devices & sessions": name, created and last used, never the secret. The shape of a member's devices.
  - Home Assistant: one owner who cannot be removed. The owner's row offers no Remove.
  - Jellyfin's per-user library policy and admin flag, and its server-wide keys running as admin with no user behind them: the cautionary case for an app tied to no person.

**Docs when it ships:** the darwin CLAUDE.md codebase map and DEVELOPER.md (the new files), MILESTONES and ROADMAP.

## Testing

`lib/src/darwin/tests/business/reads/PeopleTests.swift` and `tests/presentation/server/PeopleViewModelTests.swift`, hermetic over scripted runners.

- **The contents allowlist.** `PeopleViewModel`'s runner is a scripted fake that records every argv. A test drives every state (the list, each member's page, add, access, grant, remove) and fails if any argv begins with `chats` (recall included), `knowledge`, `task status` or `task list`, or names a session id. The allowlist holds `people`, `devices list`, `apps list`, `auth list`, 63c's `usage`, 63d's `people grants`, `config get tasks` and 62e's `serve activity`. A new read joins it only in a change that says why it carries no contents.
- **Secrets.** A test feeds documents with planted token-shaped, hash-shaped and key-shaped strings in unknown fields and checks that no view-model output, toast or accessibility label carries them. The key sheet's view-model test checks that the key reaches only the runner's standard input and is gone after.
- **Additive.** Every recorded document is decoded with an unknown field added and one optional field removed, and still draws. `people-older.err` draws 10b's fallback, never an empty list.
- **Previews.** One named `#Preview` per state: the owner alone; a household; a small team; a member on Family, Family edited and Limited; an app beyond access; a key granted under and at its limit; Usage estimated; the add sheet; a metered refusal; remove naming devices and apps; an older apogee; a failed read. All are over the recorded fixtures (ADR 0004), and each `Previews/` slice is updated in the change that re-records its fixture.
- **Parity.** `ParityTests`' census gains the `people` row, and the fixture is refreshed from the 63a binary. `people`'s remote meaning (62g's `Administer`) is pinned.
- **The anti-goal.** A test builds the app's composition with no server and no `people` word (the fixture from a binary without it) and checks that the harness window's destinations, keys and Settings panes are unchanged.
- **The probe as the regression fixture.** The sandboxed probe in Evidence (scratch `APOGEE_HOME`, `config init`, mock backend) is re-run against the gate binaries when fixtures are recorded. `people list --json` must exit 0 with the owner's row, and `auth list --output-format json` must still carry no key. Its outputs are the fixtures.
- **Gate:** `lib/scripts/cicd.sh --app darwin --test`, the layering check included.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Who owns every CLI word and its JSON shape? | `(consumed decision)` | Each belongs to its CLI item: 63a's verbs, presets, `PersonView` fields and fail-closed rule; 63c's grant verb, limit units, period and usage document; 63b's counts and collection hand-outs; 63d's grants read and clearing; 63e's limit keys. The app reads them as recorded fixtures. 63c is written (2026-11-14) but not built; its parts are built against its recorded documents once it ships, never against a guessed shape. |
| Who owns the window? | `(consumed decision)` | 10b: the `Window` scene, `PresentationServer` and its scheme, the sidebar and its keys (⌘6 is People), the Devices and Apps places, the pairing sheet, the older-binary fallbacks, and the parity rows for `devices`, `apps` and `serve`. |
| What sets the look? | `(consumed decision)` | 10a's boards: the `server:people` and `server:member` entries, and the People default ([`design-the-server-window.md`](design-the-server-window.md), Open questions: "People shows the owner first…"). |
| Where do the plain words shown before a write come from? | `[default]` | From the CLI, never from the app. People needs to show what a preset or an `--allow`/`--deny` will hold **before** it is written, and 63a prints those words only as the verb runs, then writes ([`people-on-one-server.md`](../../../cli/v0.2.4/people-on-one-server.md): Open questions, the default "what each preset holds, printed in plain words before the row is written"; Acceptance criteria, `people add bob --json` printing Family's access in plain words, then writing `people.json`). The app does not expand presets itself, which would be a second capability. This item asks 63a for a `--dry-run` on `people add` and `people access`, printing the plain words and the JSON access and writing nothing (folded into 63a as an amendment if 63a is not yet built, or a small CLI sibling if it is). Until the binary has it, the sheet shows each preset's one-line description from `people --help` and says "Apogee will show exactly what this allows once it's added". After the write, it shows the CLI's printed words with the read-back. |
| How does the People list order and show people? | `[default]` | People lists the owner first, named as `people list` names them ("Owner" until `people rename owner NAME`), with Rename and no Remove. Members follow in the CLI's order. Each row shows the name, the preset ("Family, edited"), counts of devices and apps, and usage this period against the limit, with the billed part marked. A preset whose expansion has drifted from the row shows the CLI's own note ("Family now also covers …") with the `people access --preset` it names as a button. |
| What parts does a member's page have? | `[default]` | Six parts in 10a's order: Access, Devices and apps, Cloud key, Usage, Personal data, Tasks. Each part whose verb the binary lacks shows one line naming the release that brings it. Personal data shows counts only (chats, notes, tasks, from 63b), and the collections handed out to this person by name. It shows no titles and no previews. |
| How does the access editor work? | `[default]` | One row per `AppScope` part. Each value is a toggle whose change becomes 63a's `--allow KIND:NAME` or `--deny KIND:NAME`, applied as one `people access` per Apply, never per toggle. The member's files root (63c's `--files-root`) is a row of its own, in the CLI's words. A metered backend shows as unavailable for a member, with 63a's refusal line ("a member uses this server's local models; a cloud backend needs a household key with a limit") and a link to Cloud key. The families that are never a member's are not offered. Narrowing first shows what it closes (the member's live sessions and running task, as counts from 62e) and that it applies now. Widening shows that it applies to what they start next. |
| What does Cloud key show and do? | `[default]` | Cloud key shows the owner's stored keys by provider and `stored_at` (from `auth list`), and for this member: granted or not, which metered backends, the limit, and usage against it, all marked billed. The key that pays is the one the resolver finds for that backend ([`allowances-keys-and-usage.md`](../../../cli/v0.2.4/allowances-keys-and-usage.md), Description: **The household key**). Grant opens a sheet with the backends, the limit in tokens or estimated cost and its period (day, week, month), and runs `people access NAME --cloud BACKEND --limit UNIT:N/PERIOD`. A cost limit refused for a backend with no price is drawn in the CLI's words. A key not yet stored is added from the same sheet through `auth add <provider> --stdin`, the key held nowhere. Revoking the grant (`--no-cloud`) is asked first. The key is never shown, and "household" is the CLI's word for it. |
| What does Usage show? | `[default]` | 63c's ledger for one person over the periods 63c offers (default the current one), from `usage --person NAME --period P --by backend --output-format json`: requests and tokens per backend, remaining against the limit, billed marked, "estimated" where the CLI says so. Activity (10b) stays the live view per credential, and Usage here is the kept view per person. The app adds nothing up that the CLI does not report. |
| How do Add, Rename and Remove run? | `[default]` | Add asks for a name and a preset (Family preselected, as 63a's `people add` defaults). It shows the CLI's plain words (the `--dry-run` call above) and the disk sentence, then runs `people add NAME --preset P --json` and opens the new member's page with Pair a device for them. Rename runs `people rename NAME NEW` and says that every binding is kept. Remove reads `people list --json` for that person, names every device and app it revokes, and states what happens to their data in 63b's words where the binary says, else "their saved data stays on this Mac". On Yes it runs `people remove NAME --yes`, because the app asked itself and a pipe is not a terminal. |
| How are a member's devices and apps listed and paired? | `[default]` | They list name, kind, paired and last seen, from 63a's `PersonView` and 59a's and 59f's views. Pair a device for them opens 10b's sheet with `devices pair --for NAME --json`. Pair an app for them opens 10b's app sheet with `apps pair --for NAME`, its scope editor capped at the person's access. Revoke is 10b's, asked first. An app's scope part beyond the person's access is drawn struck through with "beyond <name>'s access" from `apps list`. |
| Where do grants (63d) and task limits (63e) appear? | `[default]` | Grants appear under Access as tool names and a website count, never the websites, with Forget all (`people grants NAME --forget-all`) asked first. Beside each tool the config allows install-wide, the page says "allowed for everyone by this server's Permissions", as 63d asks ([`consent-goes-to-its-person.md`](../../../cli/v0.2.4/consent-goes-to-its-person.md), Open questions: the layer-order default, the config's `allow` staying install-wide and the server window saying so beside each `allow`). Task limits appear under Tasks as the four `tasks.*` keys, set through `config set` and read back, with 62b's "applies now". The per-person limit is install-wide, and the page says so. Both are drawn in the design app first. |
| How is a guest drawn? | `[default]` | A guest is a member on Limited. Their page says "Nothing kept: their chats are never saved, recalled or captured", and Usage still shows counts, because usage is metadata. There is no Guest label of the app's own. The preset's name is the CLI's. |
| How does the UI tell a household from a team? | `[default]` | It does not: the difference is in the data, not in the UI. People uses no family-only words beyond the preset name "Family", which is the CLI's. Previews include a household (the owner and three members, one a guest) and a small team (the owner and eight members on Full), so the list and pages are checked at both sizes. |
| What is `people`'s remote meaning? | `(consumed decision)` | 62g and 63a. A Mac connected to a server is offered no People place, because the server window exists only on the Mac that serves (the user's call). `people` typed at a remote console is 62g's `Administer` ([`administering-local-and-marked-devices.md`](../../../cli/v0.2.4/administering-local-and-marked-devices.md), Open questions: the `Administer` list, "`people` when 63a ships"): it runs from a device the owner marked and is refused from any other, and 63a refuses it for every member's credential ([`people-on-one-server.md`](../../../cli/v0.2.4/people-on-one-server.md), Technical approach: `people` classified owner-only in `tui_parity.cpp` and `remote_parity.cpp`). The parity row carries that meaning, and the app draws the refusal in the CLI's words. Pairing a device or app for a member stays at the server Mac, because `devices pair` and `apps pair` are local-only for every caller (the same 62g document, Open questions: every `devices` and `apps` leaf except `devices pair`, `devices admin` and `apps pair`, which are local-only for every caller). |

No `[user]` call blocks this item.

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-14 | Specced from the admin-mode and people spike, on the user's calls below. | — |
| 2026-11-14 | **An admin mode** is a separate "Apogee Server" window in the macOS app, offered only on the Mac that serves, driving the local CLI over pipes. | the user |
| 2026-11-14 | **Administering is local by default.** The owner may mark chosen paired devices as allowed to administer remotely (62g). This revises 59d's default that the admin plane keeps one token for any network caller. | the user |
| 2026-11-14 | **The window installs the server as a background service** (launchd) and starts, stops and restarts it (10b, 62d). | the user |
| 2026-11-14 | **Placement:** the server console (CLI track 62; darwin/macos 10a and 10b) and people and access (CLI track 63; darwin/macos 10c), both in v0.2.4. Nothing already queued is renumbered. | the user |
| 2026-11-14 | **People:** design for a household **and** a small team. The owner sees members' metadata (devices, apps, activity, which model, when), **never chat contents**, and Apogee says honestly that whoever controls the server Mac can still read its disk. Members use the server's local models only, unless the owner grants a household cloud key with a limit. | the user |
| 2026-11-14 | **Defaults the user took:** the words "people" and "access" (Owner, Member), never "roles", which means model roles in Apogee; live changes applied on the spot (reload), with what still needs a restart said; an audit kept 90 days of who, what and when, without request contents; collections as shared libraries handed out per person, plus a member's own private ones; notes and saved decisions personal by default; members' apps paired by the owner at the server Mac, capped at that member's allowance; a task queue per person, not a refusal. | the user |
| 2026-11-14 | **The model:** an implicit owner (the OS account that owns `APOGEE_HOME` on the server Mac); members, each with an allowance in 59f's `AppScope` shape; presets (Full, Family, Limited) in place of groups; a guest is a member on Limited with nothing kept. | the user |
| 2026-11-14 | **Already written:** the SPEC amendments (CLI SPEC → Core surfaces 4, "Revised again 2026-11-14"; GUI SPEC → Core surfaces 3). | — |
| 2026-11-14 | **The standing law:** remote use is an add-on. The CLI alone and the Mac app over pipes stay complete and need no server. | the user |
| 2026-11-14 | darwin/macos 10c, v0.2.4, last of track 10. It is gated on 10b (the window and module it builds into), CLI 63a (people) and CLI 63c (whose keys pay, limits and usage), because those are the walls it closes on the app's side: P1, P4 and P5. 63b, 63d and 63e are not gates. Their parts appear where the binary answers, so the item ships with 63a and 63c alone. It is macOS only (`backlog/darwin/macos/`), because the window is offered only on the Mac that serves. | the spec |
| 2026-11-14 | The design comes first. 10a draws People earlier in the same release so that this item only builds. A state the build finds undrawn (63d's grants, 63e's limits, or a refusal 10a did not foresee) is drawn in the design app before it is built, per the design's law ([the design's guide](../../../../assistant/design/CLAUDE.md) → What a change touches, The design's rules; re-worded by design 1t, 2026-11-21). | the spec |
| 2026-11-18 | Moved here from v0.2.7, when the releases were merged; the id is unchanged. | the user |
| 2026-11-21 | Re-worded by design 1t, which retired the old one-file prototype and the claude.ai canvas: a state this item finds undrawn is drawn in the design app (a preset, its document and its gallery entry, checked by its local gate), no longer in `lib/design`'s old folder and on the canvas. | design 1t (default) |


## Out of scope

- Every CLI word, record and rule (CLI track 63).
- The window itself, Devices, Apps, Activity, Audit and Server (10b), and drawing the boards (10a), except a state this item finds undrawn.
- Reading or showing any member's conversation, notes, decisions, task goals or granted websites, in any form.
- Members administering anything, or a second admin beside the owner.
- Groups, custom roles, accounts, passwords and invitations.
- People on iPhone or iPad, or on a Mac connected to a server (7a, 7c).
- The Linux and Windows apps, which follow v0.2.5.

## Links

- **Blocked by:** [10b](server-window.md), [CLI 63a](../../../cli/v0.2.4/people-on-one-server.md), [CLI 63c](../../../cli/v0.2.4/allowances-keys-and-usage.md) · **Blocks:** —
- **Follows from:** the admin-mode and people spike (2026-11-14); [10a](design-the-server-window.md), which draws the boards this item builds.
- **Relates to:** [7a](../../v0.2.4/app-connection-local-or-server.md) and 7c (the client side); [63b](../../../cli/v0.2.4/personal-data-has-an-owner.md), [63d](../../../cli/v0.2.4/consent-goes-to-its-person.md) and [63e](../../../cli/v0.2.4/task-slots-per-person.md) (drawn where the binary has them, not gates); [62e](../../../cli/v0.2.4/connections-and-activity.md), [62f](../../../cli/v0.2.4/audit-log.md), [62g](../../../cli/v0.2.4/administering-local-and-marked-devices.md) and [62h](../../../cli/v0.2.4/local-admin-reads.md); [M18](../maintenance/app-dev-channel.md) (the dev channel for the optional live pass); [CLI SPEC](../../../../assistant/cli/SPEC.md) → Core surfaces 4 and [GUI SPEC](../../../../assistant/gui/SPEC.md) → Core surfaces 3.
