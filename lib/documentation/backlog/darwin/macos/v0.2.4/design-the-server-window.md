# APP-10a · Design the server window

| Field | Value |
|---|---|
| **Type** | 🛠️ Task |
| **Project** | Apple apps · macOS |
| **Status** | 🟢 Ready |
| **Fix version** | v0.2.4 |
| **Component** | Design · the Apogee Server window (the design app, `lib/design/prototype/`) |
| **Labels** | `design-first` `server-window` `admin-mode` |
| **Epic** | Track 10 · the server window |
| **Blocked by** | — |
| **Blocks** | [10b](server-window.md) |
| **Reporter** | The admin-mode and people spike (2026-11-14), on the user's call |
| **Created** | 2026-11-14 |

---

## Summary

The Mac that runs the person's Apogee server gets an admin mode, a separate **"Apogee Server"** window, and nothing of it is drawn yet (A9). This item draws the whole window in the design app, every part against the CLI words it maps to and every missing word added to the contract page's gap list with its fallback, so [10b](server-window.md) builds the window and [10c](people-screens.md) builds People from boards that already exist.

## Description

### User story

**As** a contributor building the Apogee Server window (10b, and 10c for People),
**I want** every part of the window drawn first, each against the CLI word it maps to and each missing word recorded as a gap with its fallback,
**so that** the window is built from boards that already exist, as the design's law requires, and the CLI items know which words close which gaps.

### Background

The user's call, 2026-11-14, from the admin-mode and people spike: the Mac that runs the person's Apogee server gets an **admin mode**, a separate **"Apogee Server"** window in the macOS app, offered only on that Mac. It administers that Mac's server by driving the **local** CLI over pipes ([GUI SPEC](../../../../assistant/gui/SPEC.md) → Core surfaces, item 3, added the same day). Its places are the server itself (status; installing it as a background service and starting, stopping and restarting it; how clients connect, 62c's settings), devices, apps, activity, the audit trail and people.

The design's own law says nothing is built before it is drawn: "A new screen or state worth its own board" needs a start preset, its fixture, its test and its gallery entry ([the design's guide](../../../../assistant/design/CLAUDE.md) → What a change touches), and "Every part maps to the CLI … If none exists, draw it with its fallback and add the gap to the contract board" (→ The design's rules). This item draws the whole window in the design app, every part against the CLI words it maps to and every missing word added to the contract page's gap list, so [10b](server-window.md) builds the window and [10c](people-screens.md) builds People from boards that already exist.

This item delivers:

- a **way into the window** (the Apogee menu's item, present only on a Mac that serves);
- the window's own chrome, with a sidebar of six places and its own keys;
- **Server** (running or stopped, the background service, how clients connect, and a change applied now or waiting for a restart);
- **Devices** (paired, pending with the code and QR, which may administer remotely, rename, revoke);
- **Apps** (each app's scope, what it asked for beside what was approved, pairing one, revoke);
- **Activity** (who is connected and what is running, with usage per credential);
- **Audit** (who changed, approved or used what, and when);
- **People** (the owner and members, presets, allowances, keys and limits), drawn now and built by 10c later in the same release.

Each part gets its contract-map row, each missing CLI word its gap and fallback, the window gets its layout and keys on the layout page, and the service gets its place on the processes page.

The design prototype is the React app: design 1e (the gate recorded 2026-11-14) shipped 2026-11-18, and design 1r, whose data this item's contract, layout and processes additions land in, shipped 2026-11-21 (the design's [Milestone A](../../../../assistant/design/MILESTONES.md#milestone-a--the-design-prototype-in-react)). Design 1t retired the old one-file prototype and the claude.ai canvas on 2026-11-21 and re-worded this document for the design app (Decision log, 2026-11-21): the window is a module, presets and hand-made documents in the CLI's shape there, its boards the gallery's entries, and its check the design's local gate.

### Evidence

**A9 — no admin screen is designed.** Re-measured 2026-11-14 at `4acf4a2b`:

1. **The canvas has no board for any of it.** At HEAD, the old prototype's canvas index held 71 boards (60 on the Build page) and 56 Screen boards. The working checkout holds three more session boards another session is drawing (74 boards, 59 screen files; the spike counted these). In either count, the only board title matching server, connect, device, pair, admin, people or apps is "Library · MCP servers and presets". The Build page has five rows of boards, at y 0, 1400, 2720, 4040 and 5360; the last four have headings 300 above them (y 1100, 2420, 3740 and 5060), and the Settings row is last, its 900-high boards ending at y 6260. The prototype's `start` options name no server, device, app or people state, and its only "pair" is a drafter's pairing. Re-checked 2026-11-21 in the design app (design 1t): no start preset in `app/Previews/Presets.ts` names a server state, and the gallery's Build page has its five rows under the same headings.
2. **The contract board has no row or gap about serving.** The old BuildContract board had eleven map sections (Sessions through Settings and system), 80 rows marked ready, and gaps G1–G24 (16 closed, 4 "Before v1", 4 "Later"). None is about a server, a device, an app or a person. Since design 1r the contract page's data (`gallery/Models/contract.ts`) holds the same eleven parts and gaps G1–G32 (G25–G32 handed it by the monitor and the Models place), none about serving.
3. **The client side is undrawn too, and must stay distinct.** GUI 7a's Connections pane, pair sheet and indicator, the side of a Mac that *connects to* a server, are still to be drawn ([7a](../../v0.2.4/app-connection-local-or-server.md) (Technical approach: the Connections pane, the pair sheet, the indicator and the unreachable state drawn first in the design app)). The server window is the other side: the Mac that *is* the server. The two must never be confused in words or in place.
4. **The app has nowhere to put it yet.** `ApogeeApp.swift:52-54`: "One window: the app holds one live session" and one `Window(model.title, id: "main")`, plus the `Settings` scene (`:99`). `Destination` has 11 cases (`Destination.swift:8-20`) and the number keys are spent (⌘1–⌘9, ⌘0, Review on ⇧⌘R). `serve` is carved out of the app as "a daemon the app would orphan when it quits" (`Parity.swift:83`). Building the window is 10b's; drawing where it lives is this item's.
5. **The CLI words the window needs are mostly unshipped.** Probed 2026-11-14 with the branch binary (`apogee 0.2.0 (b37b0a9f, 2026-11-09, macos-arm64)`), a scratch `APOGEE_HOME` under the session scratchpad, the `mock` backend, and `serve --bind 127.0.0.1 --port 0` killed after:
   - `__complete ""` answers 30 words; `devices`, `apps` and `people` are not among them, and `devices list`, `apps list` and `people list` exit 109.
   - `serve --help` has start flags and `--print-admin-token` only; `serve status` exits 109.
   - `config get serve` answers `no such config key: 'serve'` (exit 1).
   - While the server ran, the home held `config/` (`admin-token`, `config.json`, `config.json.lock`) and `logs/` only: nothing a local reader could find to learn that a server is running, or on what port.
   - With the bearer, `GET /v1/admin/status`, `/connections`, `/devices` and `/apps` answered 404, and `/v1/admin/backends` 200; without it, 401.
   - `check --output-format json` reports one row about serving, "Admin token: not generated yet -- created at the first 'apogee serve'".
   - The reference says "The server does not hot-reload" ([http-api.md](../../../../reference/cli/http-api.md):327).

   The window is therefore drawn mostly against CLI words that tracks 59, 62 and 63 will add. Each is drawn with its fallback, and each gap is added to the contract board, as the design's law asks.

## Acceptance criteria

- [ ] **Given** the built `lib/design/main.html`, **when** it starts at each `server…` preset, **then** it draws the Apogee Server window: the menu entry (present and absent), Server running, stopped (the service installed or not, with Install offered) and with a change pending a restart, Devices, the pairing sheet (code, QR, waiting, paired), Apps, an app's scope beside what it asked for, Activity, Audit, People, a member's page, and the older-apogee fallbacks. The design's local gate, `npm run check`, passes, its walk over every start preset included.
- [ ] The thirteen `server…` presets each have a gallery entry (`gallery/Models/Boards.ts`) with `id` equal to its preset and `noOldBoard: true`, titled `Server · <state>`, on a new Build-page row under the heading "Screens — the server window, on the Mac that serves".
- [ ] The contract page has a SERVER WINDOW part (`contract.ts`) mapping every part to its CLI verb. Each missing verb has a gap row with its "Until then:" fallback and the item that closes it (62a–62h, 59a, 59f, 63a, 63c). The window's decisions are in "Decisions the design makes".
- [ ] The layout page draws the second window's regions, sizes, keys (⌥⌘S into it; ⌘1–⌘6 inside it) and states, from tokens.json and the key map. Settings › Keyboard lists the way in. The processes page draws launchd's `apogee serve` outside the app's children, and the window reaching the local `apogee` over pipes, never the port.
- [ ] No board shows a token, code or key outside the pairing sheet. Activity, Audit and People show metadata only and carry the honesty line about the Mac's disk. Billed use is marked. The word "role" appears nowhere in the window.
- [ ] **Given** the presets that exist before this item, **when** each renders, **then** it renders exactly as before: `npm run parity` (design 1s) reads no new difference on any screen pair (`<preset>@<mode>`) it judged before, other than Settings › Keyboard's added line for the window's key. The new differences on the BuildContract, BuildLayout and BuildProcesses pairs and on `settings:keyboard@installed` are exactly this item's additions, each classed by a verdict in `parity/verdicts.json`: a `fix` citing `10a`, which the proof accepts only once this item's design record is written in the design's [MILESTONES.md](../../../../assistant/design/MILESTONES.md) under a heading holding "(backlog item 10a)". On a Mac that doesn't serve, the harness window has no new item.
- [ ] `lib/design/main.html` is rebuilt from the item's sources, and the staleness check passes on it. Nothing is committed unless the user asks.

## Constraints

- **Nothing makes standalone use need a server or an account (the anti-goal).** The user's standing law: remote use is an add-on, and the CLI alone and the Mac app over pipes stay complete and need no server ([GUI SPEC](../../../../assistant/gui/SPEC.md) → Non-goals, No server requirement). On a Mac that does not serve, the harness window, its places, its keys and every existing board stay exactly as they are. There is no menu item, no sidebar entry, no sign-in and no "people" anywhere in the harness window.
- **Never listens; serving = server.** The window drives the local `apogee` over pipes: reads and commands, as every place does. It **never connects to the port on its own Mac** ([GUI SPEC](../../../../assistant/gui/SPEC.md):34, No HTTP path for local work; the CLI's Interactive turns never open a listening socket). Only `apogee serve` listens. Under the background service, that process is launchd's child, not the app's, so the design draws it outside the app's children on the processes page. Quitting the app leaves the server running, which answers the carve-out's reason (`Parity.swift:83`).
- **One config mutation path.** Every change drawn in the window is the CLI verb that makes it: `config set serve.*` (62c), `apps scope` (59f), `devices revoke` (59a), and the people verbs (63a, 63c). Each change is read back, as Settings' panes are today. The design never draws a field that writes a file.
- **Secrets are unleakable** (the CLI CLAUDE.md). The pairing code and the QR appear only on the pairing sheet, which is the local caller of `devices pair --json` (59a: "on a local front end over pipes", [`paired-devices.md`](../../../cli/v0.2.3/paired-devices.md) (Open questions: the default that a pairing code is never minted over HTTP, and appears only on the server's own terminal or a local front end over pipes)). No list, activity row, audit row or toast ever shows a token, a hash, a code or a key. A household cloud key is entered through the CLI's own key flow (`auth add --stdin`, the key on standard input, `auth_cmd.cpp:160`; the household grant and its limit are 63c's) and drawn by name and limit only.
- **Honest by default** ([the design's guide](../../../../assistant/design/CLAUDE.md) → The design's rules). Every status, count, expiry and usage figure is one the CLI reports, in its words where it has them. Anything billed or sent off the machine is marked, using the `model.api` token ("Cloud API · billed"). The owner sees members' metadata, never their chat contents. The People and Activity boards also say plainly that whoever controls this Mac can still read its disk.
- **Additive** (the CLI's [ADR 0006](../../../../adrs/cli/backwards-compatibility.md)). The design is drawn for the CLI that will exist, and against an older binary it draws each fallback. A place whose verb the binary lacks says so in one line naming the release that brings it. It never pretends the place is empty.
- **Parity** (the CLI's [ADR 0002](../../../../adrs/cli/mode-parity.md); the GUI's [ADR 0003](../../../../adrs/gui/parity.md)). Every part maps to a CLI verb that a person can also type. The contract map names each verb, and nothing in the window is a capability of its own.
- **The design's own rules.** Every color is a token (the existing 25 and nothing new unless a color is genuinely missing). Type sizes are `var(--t-…)`. The design is keyboard first, with real buttons, inputs and labels, an `aria-label` on each icon-only button, and text at 4.5:1 in all three themes. Copy is plain words.
- **Drawn in the design app** ([the design's guide](../../../../assistant/design/CLAUDE.md) → What a change touches). The window is a module, its presets, their documents and their gallery entries, checked by the local gate; the claude.ai canvas is retired (design 1t), so nothing is published. Nothing is committed unless asked.
- **The layers and the dev channel, as drawn.** The window's places are drawn as one future Presentation module (10b's `PresentationServer`). The window's reads go through the same runner as every place. A development build's window drives `apogee-dev` over `~/.apogee-dev` ([GUI CLAUDE.md](../../../../assistant/darwin/CLAUDE.md#development-runs-on-the-dev-channel); [M18](../../../../assistant/darwin/MILESTONES.md#2026-11-21--app-dev-channel-maintenance-item-m18-a-development-build-of-the-app-runs-on-the-dev-channel)). Any probe this item runs uses a scratch `APOGEE_HOME`, never `~/.apogee` or `~/.apogee-dev`, and never `launchctl`.

## Technical approach

Design only. No Swift, no CLI, no app source changes. Paths under `lib/design/prototype/` (its `source/presentation/` unless said), and the rebuilt `lib/design/main.html`.

| File | Change |
|---|---|
| `source/presentation/server/` (new module) | the second window (chrome, sidebar, keys): its view-models over the stand-in, its views, a preview for each state, its mirroring tests; its entry in `modules.json` (10b's `PresentationServer`, drawn first) and its name on the Presentation layer card |
| `app/Previews/Presets.ts`, `app/ViewModels/AppViewModel.ts` | the thirteen start presets, each with what it opens, and the menu entry |
| `fixtures/` | the documents the window reads: a recording where the binary answers, else a hand-made file in the CLI's shape from its item's spec, named `*-handmade*` |
| `gallery/Models/Boards.ts` | a sixth Build row, under the heading "Screens — the server window, on the Mac that serves", its thirteen entries in order, each keyed by its preset: `{ id: '<preset>', start: '<preset>', title: 'Server · <state>', page: 'build', row, height: 900, draws: 'screen', noOldBoard: true }` |
| `gallery/Models/contract.ts` | a SERVER WINDOW part, the new gaps from G33, and the window's entries in the decisions (detail below) |
| `gallery/Models/layout.ts`, `lib/design/tokens.json`, `shell/ViewModels/KeyMap.ts` | the second window's regions and sizes (its sidebar and place), its keys, and its states (running, stopped, older apogee, empty lists), with each piece named in SwiftUI (`gallery/Models/nativeControls.ts`); tokens.json gets only sizes that are genuinely new |
| `gallery/Models/processes.ts` | launchd starting `apogee serve` as its own process, with the port its only listener, and the app's server window reaching the local `apogee` over pipes beside the harness window's children; nothing goes from the app to the port |
| `parity/verdicts.json` | the verdicts classing the new differences on the BuildContract, BuildLayout and BuildProcesses pairs and on `settings:keyboard@installed`, each a `fix` citing `10a`, standing on this item's design record in the design's `MILESTONES.md` (a heading holding "(backlog item 10a)") |
| `lib/design/main.html` | rebuilt by `npm run build` |

**The window in detail.**

- A second window drawn by the design app, shown when the start preset names a `server…` state. It has its own chrome titled "Apogee Server", its own sidebar (Server, Devices, Apps, Activity, Audit, People) and its own key list, beside the harness window, never inside its sidebar.
- The views: one per place, plus the pairing, scope, confirmation and member sheets, through the shell's hosts.
- The data: the documents above (this Mac's server, the service, two or three devices with one pending pairing, two apps with one asking for more, a few live connections, audit rows, an owner and two members on presets), read through the stand-in as every place reads.
- What moves: the stand-in's clock replays a pairing redeemed while the sheet is open and the activity rows.
- The entry: the harness window's Apogee menu drawn open, showing "Apogee Server…" with its key. The design has no menu bar today, so this is the one menu drawn.
- The start presets: `server:menu`, `server`, `server:stopped`, `server:pending`, `server:devices`, `server:pair`, `server:apps`, `server:scope`, `server:activity`, `server:audit`, `server:people`, `server:member` and `server:older` (the window against an older apogee, each place's fallback).
- Settings › Keyboard's list (the shell's `KeyMap.reference`) gains the window's key, marked as on the Mac that serves.

**The boards** (the gallery's entries), one per preset, each keyed by it:

- `server:menu`: the way in;
- `server`: running, how clients connect;
- `server:stopped`: stopped, or the service not installed;
- `server:pending`: a change applied now beside one waiting for a restart;
- `server:devices`;
- `server:pair`: the code, the QR, waiting, then paired as a name;
- `server:apps`;
- `server:scope`: an app's scope beside what it asked for;
- `server:activity`;
- `server:audit`;
- `server:people`;
- `server:member`: a member's preset, allowance, devices, apps, key and limit;
- `server:older`.

**The contract page in detail** (`gallery/Models/contract.ts`).

- A **SERVER WINDOW** part in the contract map (`contractParts`, `contractRows`): one row per part, naming the verb behind it and its status (`ready` for what the binary has, `gap` for the rest).
- The gap list (`contractGaps`) gains the missing words, from the next free number (G33 since design 1r; G25 at the spec), each with its "Until then:" fallback and the CLI item that closes it (the table below, recorded in the Decision log 2026-11-14).
- "Decisions the design makes" (`contractDecisions`) gains the window's: a second window, offered only on the Mac that serves; pipes, never the port; the service outside the app's children; metadata, never contents; the words people and access, never roles.

**The contract board's new gaps**, each with the item that closes it and its fallback (numbers assigned from the next free one when drawn):

| Gap | Closed by | Until then (drawn) |
|---|---|---|
| Whether this Mac serves, and what the running server is doing: `serve status --output-format json` | 62a | No menu item and no window. `serve` stays a carve-out. |
| Reload, and what the server serves against what is on disk | 62b | Every change says "applies when the server restarts". |
| How clients connect, kept in the config: `config set serve.*` | 62c | How clients connect is read-only, from status, and says to restart with the flags. |
| The background service: install, start, stop, restart, remove | 62d | Install, Start, Stop and Restart are not drawn. The place says to run `apogee serve` at a prompt. |
| Connections and activity per credential | 62e | Activity says this apogee doesn't report it yet. |
| The audit log | 62f | Audit says nothing is recorded yet. |
| A device marked to administer remotely | 62g | No mark. Administering is this Mac only. |
| Pending pairings, requested scopes, rename, the QR payload, JSON policy reads | 62h | The sheet shows the typed code only, with no QR, no pending row and no Rename. |
| `devices` and `apps` themselves | 59a, 59f | Devices and Apps say this apogee doesn't pair yet. |
| People, presets and allowances; whose keys pay, limits and usage per person | 63a, 63c | People shows the owner alone and says members arrive with a later apogee. |

**Reference.**

- The siblings in this track:
  - [10b](server-window.md), the window built from these boards (gated on this item, CLI 62a, 62c and 62h).
  - [10c](people-screens.md), People built from this item's People and member boards.
- The CLI words the window maps to:
  - [62a](../../../cli/v0.2.4/serve-status-and-runtime-record.md), `serve status`;
  - [62b](../../../cli/v0.2.4/serve-reload-and-served-vs-disk.md), reload, and served against disk;
  - [62c](../../../cli/v0.2.4/serve-section-in-config.md), the config's `serve` section;
  - [62d](../../../cli/v0.2.4/serve-as-a-background-service.md), the background service;
  - [62e](../../../cli/v0.2.4/connections-and-activity.md), connections and activity;
  - [62f](../../../cli/v0.2.4/audit-log.md), the audit log;
  - [62g](../../../cli/v0.2.4/administering-local-and-marked-devices.md), administering locally plus marked devices;
  - [62h](../../../cli/v0.2.4/local-admin-reads.md), what a local admin screen reads (pending pairings, requested scopes, rename, the QR payload, JSON policy reads);
  - [59a](../../../cli/v0.2.3/paired-devices.md), devices and pairing;
  - [59f](../../../cli/v0.2.3/paired-apps-with-scopes.md), apps and `AppScope` (`backends`, `collections`, `tools`, `commands`, `tasks`, with `requested` beside it; 59f's Technical approach, the `devices.h/.cpp` registry row);
  - [59d](../../../cli/v0.2.3/reads-and-commands-over-the-network.md), what a network caller may run;
  - [63a](../../../cli/v0.2.4/people-on-one-server.md), people;
  - [63c](../../../cli/v0.2.4/allowances-keys-and-usage.md), allowances, keys and usage;
  - [63e](../../../cli/v0.2.4/task-slots-per-person.md), each person's task queue, shown on Activity.
- The client side: [7a](../../v0.2.4/app-connection-local-or-server.md), the Connections pane and pair sheet of a Mac that connects to a server. Its "New Window on <server>" (7a's Technical approach: it opens a second composition over that server, retiring `ApogeeApp`'s one-window rule for a server window only) is the precedent for a second window. Its default carving `devices` out of the app (7a's Open questions: `devices` classified as carved out beside `serve`, since a pairing code is minted only on the server's own terminal) is amended by 10b.
- In-house precedents:
  - Settings' panes, each over the verbs a person types with each change read back (`SettingsViewModel.Pane`, 6 panes; the design's six Settings boards);
  - the Models store's asked deletes and read-back (`DeletePlan`);
  - 1q's design-completion boards, drawn before the screens they specified.
- Prior art (the spike's `roles-prior-art`, 2026-11-14, from each product's official documentation unless marked):
  - Plex Home: one owner and up to 15 members, with libraries shared per person;
  - Home Assistant: an owner, admins and users;
  - Jellyfin (community pages): access to libraries per user and an admin flag;
  - Open WebUI's pending users, which an admin approves;
  - Nextcloud (community pages): a "Devices & sessions" list showing name, created and last used, never the secret.

  These shaped the People, Devices and pending-pairing boards.

## Testing

- **The design's local gate** ([the design's guide](../../../../assistant/design/CLAUDE.md) → Checking a change): `npm run check` in `lib/design/prototype/`, its walk over every start preset (the new `server…` ones included) for 30 ticks with nothing `undefined` or `NaN`, the new module's tests, the layering check over it, the token scan, and the Playwright tests that open the built file from disk.
- **Before and after, by the parity proof.** `npm run parity` (design 1s) on the item's build reads no new difference on any screen pair it judged before but Settings › Keyboard's added line for the window's key, so every other pre-existing preset is unchanged, and the harness window is untouched on a Mac that doesn't serve (the anti-goal). The new differences on the BuildContract, BuildLayout and BuildProcesses pairs and on `settings:keyboard@installed` are exactly this item's additions, each classed by a `fix` verdict citing `10a` in `parity/verdicts.json`; the proof accepts such a verdict only once the design's MILESTONES.md holds this item's design record, headed "(backlog item 10a)", and it exits 1 on any difference no verdict classes.
- **Consistency, by the gallery's and the Build pages' tests.** "Every preset has a frame" holds each new preset to its gallery entry; the contract page's gaps test holds every `gap` row to an open gap and every open gap to a row; the layout page's keys test holds the new keys to its key rows.
- **Themes and contrast.** Each new board is opened at `main.html?gallery&board=server:…&theme=<dark|light|contrast>`, and its text is checked at 4.5:1.
- **Secrets read.** A read of the new module and its documents finds no token, hash, code or key outside the pairing sheet.
- **The spike's probe is the gap list's fixture.** When a CLI item ships, the same sandboxed probe (scratch `APOGEE_HOME`, mock backend, `serve --port 0` on loopback, killed after) is re-run, and its gap is marked closed only when the binary answers:
  - `__complete ""` gains `devices` and `apps`;
  - `serve status --output-format json` exits 0;
  - `config get serve` answers;
  - `/v1/admin/status` stops answering 404.
- **The built file.** `npm run build` rewrites `lib/design/main.html`, and the staleness check passes on it.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Who owns each CLI word and its JSON shape? | `(consumed decision)` | Its CLI item: [62a](../../../cli/v0.2.4/serve-status-and-runtime-record.md), what "this Mac serves" means, and the status fields; [62b](../../../cli/v0.2.4/serve-reload-and-served-vs-disk.md), which changes reload and which need a restart; [62c](../../../cli/v0.2.4/serve-section-in-config.md), the `serve.*` keys; [62d](../../../cli/v0.2.4/serve-as-a-background-service.md), the service verbs and the LaunchAgent; [62e](../../../cli/v0.2.4/connections-and-activity.md) and [62f](../../../cli/v0.2.4/audit-log.md), the activity and audit records; [62g](../../../cli/v0.2.4/administering-local-and-marked-devices.md), the mark's verb and what a marked device may do; [62h](../../../cli/v0.2.4/local-admin-reads.md), the pairing payload and the pending rows; [63a](../../../cli/v0.2.4/people-on-one-server.md) and [63c](../../../cli/v0.2.4/allowances-keys-and-usage.md), people, presets, allowances, keys and limits. The boards draw the words those documents record. Where a document is not yet written when this item is worked, the board draws the spike's capability wording and the gap row says it is provisional. |
| Who owns the window's build? | `(consumed decision)` | [10b](server-window.md): the `Window` scene, its composition and module; how `ParityTable` classifies `devices`, `apps` and `serve`'s new verbs while bare `serve` stays carved out; amending 7a's carve-out of `devices` ([7a](../../v0.2.4/app-connection-local-or-server.md), Open questions: `devices` classified as carved out beside `serve`). People's build is [10c](people-screens.md)'s. |
| Who draws the client side's Connections pane, pair sheet and indicator? | `(consumed decision)` | [7a](../../v0.2.4/app-connection-local-or-server.md). |
| Where is the window drawn? | `[default]` | In the design app, as a second window shown by the `server…` start presets, its places one new Presentation module (`server`), so every gallery frame is the app at its preset and the built file stays one file. A separate prototype would split the documents and the clock. |
| What is the way in, and when is it shown? | `[default]` | The menu item is "Apogee Server…" in the Apogee menu, on ⌥⌘S (free: the shell's keys are ⌘1–⌘0, ⇧⌘R, ⇧⌘L, ⌥⌘M, ⇧⌘K, ⌃⌘S and ⌘,; `KeyMap.swift:84-91`, `:99-123`). It is drawn present when the local CLI says this Mac serves (62a's meaning, consumed above), and absent otherwise. Because the window is offered only on the Mac that serves (the user's call), the harness window offers no "make this Mac a server" path: the first opt-in to serving (`apogee serve` or 62c's `config set serve.*` at a prompt) stays the CLI's. Once this Mac serves, installing the background service is the window's, by the user's call (Server, below). The menu board draws both the present and absent states. |
| What are the window's own keys? | `[default]` | Inside the window, ⌘1–⌘6 go to Server, Devices, Apps, Activity, Audit and People, in that order. They are the window's own keys and never collide with the harness's, since only the focused window takes them. |
| What are the places and their words called? | `[default]` | The place names are Server, Devices, Apps, Activity, Audit and People. "Connections" stays 7a's word for the servers a Mac connects to, so the two sides never share a label. Members are "people", and what they may use is their "access". The word "role" appears nowhere in the window. |
| What does Server draw? | `[default]` | Three things. (1) The status as 62a reports it: running or stopped, since when, the address and port, TLS and the pin, devices-only, the backends served, tools. (2) The service: installed or not, starts at login, with Install as a background service (62d's install verb) when it is not installed, and Start, Stop, Restart and Remove when it is. (3) How clients connect as a form over 62c's keys; each change is read back and labelled "applied now" or "needs a restart" in 62b's words, and a pending restart shows what is served beside what is on disk. Stop, Restart and removing the service are asked first, and each says which connected devices and apps it will drop (62e's count). |
| What does Devices draw? | `[default]` | Devices lists paired devices (name, kind, paired, last seen, and whose once 63a answers) and the pending pairings with their expiry. Pair opens a sheet with the typed code, the QR drawn by the app from 62h's payload, the addresses, port and pin, and the expiry counted down from the CLI's own expiry (never invented). The sheet turns to "Paired as <name>" when the read-back shows the pairing redeemed. Rename and Revoke are offered, Revoke asked first with the apps on that device named (59f's cascade). "May administer from this device" is a mark, off by default, asked first, saying what it allows (62g). |
| What does Apps draw? | `[default]` | Apps lists each app by name and the device it runs on. Its scope shows in the plain words `apps pair` prints ([59f](../../../cli/v0.2.3/paired-apps-with-scopes.md), Open questions: the default that `apps pair` prints what the app may and may not do in plain words before the code, with "asks for: …" beside it), with what the app asked for beside what was approved, and Grant what it asked for. The scope editor has one row per `AppScope` part (backends, collections, tool classes, commands, task kinds), each an allow or deny that becomes `apps scope`'s flags. Once 63c answers, the editor shows the person's allowance as the ceiling, so it cannot widen past it. |
| What does Activity draw? | `[default]` | Who is connected now (device or app, and person once 63a answers), what each is running (sessions, jobs, tasks, with a person's queued tasks shown as queued, 63e), and usage per credential over a chosen period (requests and tokens per backend, billed backends marked). It shows metadata only, with no prompts, answers or file names. The honesty line reads: "Apogee doesn't show anyone's conversations here. Anyone who controls this Mac can still read its disk." |
| What does Audit draw? | `[default]` | A list, newest first, of who (owner, member, device, app), did what (changed, approved, revoked, used), to what, and when. It is filtered by who and by kind, and says it keeps 90 days and never what was asked. It is read-only, and the window offers no clearing. |
| What does People draw? | `[default]` | The owner first (this Mac's account, never removable, every right), then the members, each with a preset (Full, Family, Limited), their devices and apps, and their usage against their limit. A member's page shows their allowance in the `AppScope` shape, their personal data as counts only (chats, notes, tasks; never their contents), their shared collections handed out, and the household cloud key: granted or not, with its limit, marked billed. Adding a member starts with pairing their first device at this Mac. A guest is drawn as a member on Limited with "nothing kept". Removing a member is asked first and names their devices and apps. |
| What do the gap rows' "when" chips say? | `[default]` | They name the release that closes them (v0.2.4) rather than "Before v1" (a value `contract.ts`' `GapWhen` gains). Their text names the CLI item, so a gap is closed by name when its item ships. |
| Does the window need a new color token? | `[default]` | No new color token. Status uses `status.accent`, `status.warning` and `status.error`, and billed uses `model.api`. A missing color goes into `lib/design/tokens.json`'s colors in all three themes, whose color page computes its contrast, as the design's law asks. |
| How is the window drawn against an older apogee? | `[default]` | An older apogee (`server:older`) draws each place's fallback line naming its release, from the CLI's own refusal (exit 109, "The following arguments were not expected"), so the window is honest against 0.2.0 through 0.2.3. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-14 | Gated on design [1e](../../../../assistant/design/DEVELOPER.md#styles-the-shell-and-the-first-screen) (the user's calls that day, design track 1): the design prototype is being rebuilt in React, and the old `main.html` is frozen from design 1c, taking fixes only. So this window is drawn in the React app once its first screen exists, never in the old format. Where this document names `main.html`, its boards or the canvas, read the React app's server module, its gallery entries and `lib/design/main.html`; design 1t re-words it. | the user |
| 2026-11-14 | Specced from the admin-mode and people spike. **An admin mode** is a separate "Apogee Server" window in the macOS app, offered only on the Mac that serves, driving the local CLI over pipes. | the user |
| 2026-11-14 | **Administering is local by default.** The owner may mark chosen paired devices as allowed to administer remotely (62g). This revises 59d's default that the admin plane keeps one token for any network caller. | the user |
| 2026-11-14 | **The window installs the server as a background service** (launchd) and starts, stops and restarts it. | the user |
| 2026-11-14 | **Placement:** the server console (CLI track 62; darwin/macos track 10's 10a and 10b) and people and access (CLI track 63; darwin/macos 10c), both in v0.2.4, with nothing already queued renumbered. | the user |
| 2026-11-14 | **People:** design for both a household and a small team. The owner sees members' metadata (devices, apps, activity, which model, when), never chat contents, and says honestly that whoever controls the server Mac can still read its disk. Members use the server's local models only, unless the owner grants a household cloud key, with a limit. | the user |
| 2026-11-14 | **Defaults the user took:** the words "people" and "access" (Owner, Member), never "roles", which means model roles in Apogee; live changes applied on the spot (reload), with what still needs a restart said; an audit kept 90 days of who, what and when, without request contents; collections as shared libraries handed out per person, plus a member's own private ones; notes and saved decisions personal by default; members' apps paired by the owner at the server Mac, capped at that member's allowance; a task queue per person, not a refusal. | the user |
| 2026-11-14 | **The model:** an implicit owner (the OS account that owns `APOGEE_HOME` on the server Mac); members, each with an allowance in 59f's `AppScope` shape; presets (Full, Family, Limited) in place of groups; a guest is a member on Limited with nothing kept. | the user |
| 2026-11-14 | **Already written:** the SPEC amendments (CLI SPEC → Core surfaces, item 4, "Revised again 2026-11-14"; GUI SPEC → Core surfaces, item 3). | — |
| 2026-11-14 | First in darwin/macos track 10, ungated. The design's law draws every part with its fallback and adds each missing CLI word to the contract board, so it waits on no CLI item. [10b](server-window.md) is gated on it. It sits in v0.2.4 with CLI track 62 because the boards are the specification 10b builds from and the gap list CLI 62a–62h close. People is drawn now, ahead of 10c in the same release, so the window's sidebar has its place from the first build and 10c only builds. | the spec |
| 2026-11-14 | macOS only (`backlog/darwin/macos/`). The window runs only on the Mac that serves. iPhone and iPad never show it (GUI SPEC → Core surfaces, item 3). | the spec |
| 2026-11-14 | The contract board's new gaps, each with the item that closes it and its fallback, numbers assigned from the next free one when drawn (the gap table in Technical approach). | the spec |
| 2026-11-18 | The gate, design 1e, shipped (the design's [Milestone A](../../../../assistant/design/MILESTONES.md#milestone-a--the-design-prototype-in-react)), so the window is drawn in the React design app. | — |
| 2026-11-18 | Moved here from v0.2.6 when the releases were merged; the id is unchanged. | the user |
| 2026-11-21 | Re-worded by design 1t, which retired the old one-file prototype and the claude.ai canvas: the window is drawn in the design app (a module, its presets, hand-made documents in the CLI's shape, gallery entries) instead of the old prototype's one file, its boards and its canvas index; the canvas read-back is replaced by the design's local gate and 1s's parity proof: every screen pair it judged before unchanged but Settings › Keyboard's added line, the BuildContract, BuildLayout and BuildProcesses pairs and `settings:keyboard@installed` being where this item's intended changes land, each classed by a `fix` verdict citing 10a, which stands on this item's design record in the design's MILESTONES.md; and the contract, layout and processes additions land in design 1r's data (`gallery/Models/contract.ts`, `layout.ts`, `processes.ts`; 1r's default), its gaps numbered from G33. 1r is named as a gate beside 1e; both shipped, so the item stays 🟢. | design 1t (default) |
| 2026-11-21 | The boards keyed by their presets: the gallery's entries are `server:menu` … `server:older`, each with `id` equal to its `start` and `noOldBoard: true`, titled `Server · <state>`, in place of the old prototype's board names (`ScreenServerMenu` … `ScreenServerOlder`, carried over from its board files), since the gallery's rule for an entry with no old board requires it (`Boards.test.ts`; the design's DEVELOPER.md, the gallery's "a new start preset"). 10b and 10c name the entries the same way. | design 1t (a review's finding) |

## Out of scope

- Any Swift, CLI or test change (10b, 10c and CLI tracks 62 and 63).
- The client side's Connections pane, pair sheet and indicator (7a).
- The window on iPhone or iPad (never offered).
- A way to make a Mac that doesn't serve into a server from the app (the window is offered only on the Mac that serves, the user's call, so the first opt-in stays the CLI's; installing the service on a Mac that serves is in scope).
- The Linux and Windows apps' server windows, which follow v0.2.5.
- Network administering beyond 62g's marked devices.

## Links

- **Blocked by:** — · **Blocks:** [10b](server-window.md)
- **Relates to:** [10c](people-screens.md) (builds this item's People and member boards); [7a](../../v0.2.4/app-connection-local-or-server.md) (the client side); CLI [62a](../../../cli/v0.2.4/serve-status-and-runtime-record.md)–[62h](../../../cli/v0.2.4/local-admin-reads.md), [59a](../../../cli/v0.2.3/paired-devices.md), [59d](../../../cli/v0.2.3/reads-and-commands-over-the-network.md), [59f](../../../cli/v0.2.3/paired-apps-with-scopes.md), [63a](../../../cli/v0.2.4/people-on-one-server.md), [63c](../../../cli/v0.2.4/allowances-keys-and-usage.md), [63e](../../../cli/v0.2.4/task-slots-per-person.md); design [1e](../../../../assistant/design/DEVELOPER.md#styles-the-shell-and-the-first-screen) and [1r](../../../../assistant/design/DEVELOPER.md#the-build-pages) (its gates, both shipped), the design's [guide](../../../../assistant/design/CLAUDE.md) and [Milestone A](../../../../assistant/design/MILESTONES.md#milestone-a--the-design-prototype-in-react); [M18](../../../../assistant/darwin/MILESTONES.md#2026-11-21--app-dev-channel-maintenance-item-m18-a-development-build-of-the-app-runs-on-the-dev-channel) (the dev channel)
- **Follows from:** the admin-mode and people spike (2026-11-14); the [GUI SPEC](../../../../assistant/gui/SPEC.md) → Core surfaces, item 3
