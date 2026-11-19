# Design the server window

**What / why.** The user's call, 2026-11-14, from the admin-mode and people spike: the Mac that runs the person's Apogee server gets an **admin mode**, a separate **"Apogee Server"** window in the macOS app, offered only on that Mac. It administers that Mac's server by driving the **local** CLI over pipes ([GUI SPEC](../../../../assistant/gui/SPEC.md) → Core surfaces, item 3, added the same day). Its places are the server itself (status; installing it as a background service and starting, stopping and restarting it; how clients connect, 62c's settings), devices, apps, activity, the audit trail and people. The design's own law says nothing is built before it is drawn: "A new screen or state worth its own board" needs a start preset, a `Screen<Name>.dc.html`, a `canvas.json` entry and a README row ([design CLAUDE.md](../../../../../design/spec/CLAUDE.md):27), and "Every part maps to the CLI … If none exists, draw it with its fallback and add the gap to the contract board" (`:37`). This item draws the whole window in `lib/design/spec` and on the Apogee Desktop canvas, every part against the CLI words it maps to and every missing word added to the BuildContract board's gap list, so [10b](server-window.md) builds the window and [10c](people-screens.md) builds People from boards that already exist.

**A9 — no admin screen is designed.** Re-measured 2026-11-14 at `4acf4a2b`:

1. **The canvas has no board for any of it.** At HEAD, `json/canvas.json` holds 71 boards (60 on the Build page) and 56 `html/Screen*.dc.html` files. The working checkout holds three more session boards another session is drawing (74 boards, 59 screen files; the spike counted these). In either count, the only board title matching server, connect, device, pair, admin, people or apps is "Library · MCP servers and presets". The Build page has five rows of boards, at y 0, 1400, 2720, 4040 and 5360; the last four have headings 300 above them (y 1100, 2420, 3740 and 5060), and the Settings row is last, its 900-high boards ending at y 6260. The prototype's `start` options (`html/main.html`'s `data-props`) name no server, device, app or people state, and its only "pair" is a drafter's pairing.
2. **The contract board has no row or gap about serving.** `html/BuildContract.dc.html` has eleven map sections (Sessions through Settings and system), 80 rows marked ready, and gaps G1–G24 (16 closed, 4 "Before v1", 4 "Later"). None is about a server, a device, an app or a person.
3. **The client side is undrawn too, and must stay distinct.** GUI 7a's Connections pane, pair sheet and indicator, the side of a Mac that *connects to* a server, are still to be drawn ([7a](../../v0.2.4/app-connection-local-or-server.md):66). The server window is the other side: the Mac that *is* the server. The two must never be confused in words or in place.
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

This item delivers: a **way into the window** (the Apogee menu's item, present only on a Mac that serves); the window's own chrome, with a sidebar of six places and its own keys; **Server** (running or stopped, the background service, how clients connect, and a change applied now or waiting for a restart); **Devices** (paired, pending with the code and QR, which may administer remotely, rename, revoke); **Apps** (each app's scope, what it asked for beside what was approved, pairing one, revoke); **Activity** (who is connected and what is running, with usage per credential); **Audit** (who changed, approved or used what, and when); and **People** (the owner and members, presets, allowances, keys and limits), drawn now and built by 10c later in the same release. Each part gets its contract-map row, each missing CLI word its gap and fallback, the window gets its layout and keys on BuildLayout, and the service gets its place on BuildProcesses.

**Core constraint(s).**
- **Nothing makes standalone use need a server or an account (the anti-goal).** The user's standing law: remote use is an add-on, and the CLI alone and the Mac app over pipes stay complete and need no server ([GUI SPEC](../../../../assistant/gui/SPEC.md) → Non-goals, No server requirement). On a Mac that does not serve, the harness window, its places, its keys and every existing board stay exactly as they are. There is no menu item, no sidebar entry, no sign-in and no "people" anywhere in the harness window.
- **Never listens; serving = server.** The window drives the local `apogee` over pipes: reads and commands, as every place does. It **never connects to the port on its own Mac** ([GUI SPEC](../../../../assistant/gui/SPEC.md):34, No HTTP path for local work; the CLI's Interactive turns never open a listening socket). Only `apogee serve` listens. Under the background service, that process is launchd's child, not the app's, so the design draws it outside the app's children on BuildProcesses. Quitting the app leaves the server running, which answers the carve-out's reason (`Parity.swift:83`).
- **One config mutation path.** Every change drawn in the window is the CLI verb that makes it: `config set serve.*` (62c), `apps scope` (59f), `devices revoke` (59a), and the people verbs (63a, 63c). Each change is read back, as Settings' panes are today. The design never draws a field that writes a file.
- **Secrets are unleakable** (the CLI CLAUDE.md). The pairing code and the QR appear only on the pairing sheet, which is the local caller of `devices pair --json` (59a: "on a local front end over pipes", [paired-devices.md](../../../cli/v0.2.3/paired-devices.md):89). No list, activity row, audit row or toast ever shows a token, a hash, a code or a key. A household cloud key is entered through the CLI's own key flow (`auth add --stdin`, the key on standard input, `auth_cmd.cpp:160`; the household grant and its limit are 63c's) and drawn by name and limit only.
- **Honest by default** ([design CLAUDE.md](../../../../../design/spec/CLAUDE.md) → The design's rules). Every status, count, expiry and usage figure is one the CLI reports, in its words where it has them. Anything billed or sent off the machine is marked, using the `model.api` token ("Cloud API · billed"). The owner sees members' metadata, never their chat contents. The People and Activity boards also say plainly that whoever controls this Mac can still read its disk.
- **Additive** (the CLI's [ADR 0006](../../../../adrs/cli/backwards-compatibility.md)). The design is drawn for the CLI that will exist, and against an older binary it draws each fallback. A place whose verb the binary lacks says so in one line naming the release that brings it. It never pretends the place is empty.
- **Parity** (the CLI's [ADR 0002](../../../../adrs/cli/mode-parity.md); the GUI's [ADR 0003](../../../../adrs/gui/parity.md)). Every part maps to a CLI verb that a person can also type. The contract map names each verb, and nothing in the window is a capability of its own.
- **The design's own rules.** Every color is a token (the existing 25 and nothing new unless a color is genuinely missing). Type sizes are `var(--t-…)`. The design is keyboard first, with real buttons, inputs and labels, an `aria-label` on each icon-only button, and text at 4.5:1 in all three themes. Copy is plain words.
- **The repo and the canvas move together** ([design CLAUDE.md](../../../../../design/spec/CLAUDE.md) → The repo and the canvas move together). Every changed file is published to <https://claude.ai/artifact/HRGyHM1ZkRL3D1bWGXQGk7> in the same piece of work, after first reading down anything changed on the canvas since its last publish. `support.js` and the two `.md` files stay repo-only, and nothing is committed unless asked.
- **The layers and the dev channel, as drawn.** The window's places are drawn as one future Presentation module (10b's `PresentationServer`). The window's reads go through the same runner as every place. A development build's window drives `apogee-dev` over `~/.apogee-dev` ([GUI CLAUDE.md](../../../../assistant/darwin/CLAUDE.md#development-runs-on-the-dev-channel); [M18](../maintenance/app-dev-channel.md)). Any probe this item runs uses a scratch `APOGEE_HOME`, never `~/.apogee` or `~/.apogee-dev`, and never `launchctl`.

**Seam + files.** Design only. No code, no CLI, no app source changes.
- `lib/design/spec/html/main.html`:
  - A second window drawn in the prototype, shown when the `start` prop names a `server…` state. It has its own chrome titled "Apogee Server", its own sidebar (Server, Devices, Apps, Activity, Audit, People) and its own key list, beside the harness window's markup, never inside its sidebar.
  - The markup: one `<sc-if value="{{ui.isServer…}}">` block per place, plus the pairing, scope, confirmation and member sheets.
  - The data: canned state in the constructor (this Mac's server, the service, two or three devices with one pending pairing, two apps with one asking for more, a few live connections, audit rows, an owner and two members on presets).
  - What moves: `tick()` drives a pairing redeemed while the sheet is open and the activity rows.
  - The values: a `/* ---------- server ---------- */` section in `renderVals()` exposes `srv`.
  - The entry: the harness window's Apogee menu drawn open, showing "Apogee Server…" with its key. The design has no menu bar today, so this is the one menu drawn.
  - The start presets, added to `applyStart()` and to `data-props`' `start` options: `server:menu`, `server`, `server:stopped`, `server:pending`, `server:devices`, `server:pair`, `server:apps`, `server:scope`, `server:activity`, `server:audit`, `server:people`, `server:member` and `server:older` (the window against an older apogee, each place's fallback).
  - Settings › Keyboard's list (`set.keys`) gains the window's key, marked as on the Mac that serves.
- `lib/design/spec/html/ScreenServer*.dc.html`: one board per preset, each copied from a sibling and mounting `App` at its `start`:
  - `ScreenServerMenu`: the way in;
  - `ScreenServer`: running, how clients connect;
  - `ScreenServerStopped`: stopped, or the service not installed;
  - `ScreenServerPending`: a change applied now beside one waiting for a restart;
  - `ScreenServerDevices`;
  - `ScreenServerPair`: the code, the QR, waiting, then paired as a name;
  - `ScreenServerApps`;
  - `ScreenServerScope`: an app's scope beside what it asked for;
  - `ScreenServerActivity`;
  - `ScreenServerAudit`;
  - `ScreenServerPeople`;
  - `ScreenServerMember`: a member's preset, allowance, devices, apps, key and limit;
  - `ScreenServerOlder`.
- `lib/design/spec/json/canvas.json`: a sixth row on the Build page, with the heading note "Screens — the server window, on the Mac that serves" at y 6380 (120 below the Settings row's end, as each heading sits below the row before) and its boards at y 6680, x stepping 1520 from 0 as the Settings row does, page `build`, added to `order`.
- `lib/design/spec/html/BuildContract.dc.html`:
  - A **SERVER WINDOW** section in the contract map: one row per part, naming the verb behind it and its chip (`ready` for what the binary has, `gap` for the rest).
  - The gap list gains the missing words, from the next free number (G25 at HEAD), each with its "Until then:" fallback and the CLI item that closes it (the table under Decisions below).
  - "Decisions the design makes" gains the window's: a second window, offered only on the Mac that serves; pipes, never the port; the service outside the app's children; metadata, never contents; the words people and access, never roles.
- `lib/design/spec/html/BuildLayout.dc.html`: the second window's regions and sizes (its sidebar and place), its keys, and its states (running, stopped, older apogee, empty lists), with each piece named in SwiftUI. `json/tokens.json`'s layout values get only sizes that are genuinely new.
- `lib/design/spec/html/BuildProcesses.dc.html`: launchd starting `apogee serve` as its own process, with the port its only listener, and the app's server window reaching the local `apogee` over pipes beside the harness window's children. Nothing goes from the app to the port.
- `lib/design/spec/README.md`: the screen-board count, the gap range, and a row for the server window's boards.
- The canvas: each changed file at its canvas path (`project/App.dc.html`, `project/<name>.dc.html`, `project/canvas.json`), published from a scratch folder.

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
  - [59f](../../../cli/v0.2.3/paired-apps-with-scopes.md), apps and `AppScope` (`backends`, `collections`, `tools`, `commands`, `tasks`, with `requested` beside it, `:39`);
  - [59d](../../../cli/v0.2.3/reads-and-commands-over-the-network.md), what a network caller may run;
  - [63a](../../../cli/v0.2.4/people-on-one-server.md), people;
  - [63c](../../../cli/v0.2.4/allowances-keys-and-usage.md), allowances, keys and usage;
  - [63e](../../../cli/v0.2.4/task-slots-per-person.md), each person's task queue, shown on Activity.
- The client side: [7a](../../v0.2.4/app-connection-local-or-server.md), the Connections pane and pair sheet of a Mac that connects to a server. Its "New Window on <server>" (`:60`) is the precedent for a second window. Its default carving `devices` out of the app (`:111`) is amended by 10b.
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

**Decisions made** (dated):
- 2026-11-14: Gated on design [1e](../../../design/v0.2.1/styles-shell-and-the-first-screen.md) (the user's calls that day, design track 1): the design prototype is being rebuilt in React, and the old `main.html` is frozen from design 1c, taking fixes only. So this window is drawn in the React app once its first screen exists, never in the old format. Where this document names `main.html`, its boards or the canvas, read the React app's server module, its gallery entries and `lib/design/main.html`; design 1t re-words it.
- 2026-11-14: Specced from the admin-mode and people spike. The user's calls:
  - **An admin mode** is a separate "Apogee Server" window in the macOS app, offered only on the Mac that serves, driving the local CLI over pipes.
  - **Administering is local by default.** The owner may mark chosen paired devices as allowed to administer remotely (62g). This revises 59d's default that the admin plane keeps one token for any network caller.
  - **The window installs the server as a background service** (launchd) and starts, stops and restarts it.
  - **Placement:** the server console (CLI track 62; darwin/macos track 10's 10a and 10b) and people and access (CLI track 63; darwin/macos 10c), both in v0.2.4, with nothing already queued renumbered.
  - **People:** design for both a household and a small team. The owner sees members' metadata (devices, apps, activity, which model, when), never chat contents, and says honestly that whoever controls the server Mac can still read its disk. Members use the server's local models only, unless the owner grants a household cloud key, with a limit.
  - **Defaults the user took:**
    - the words "people" and "access" (Owner, Member), never "roles", which means model roles in Apogee;
    - live changes applied on the spot (reload), with what still needs a restart said;
    - an audit kept 90 days of who, what and when, without request contents;
    - collections as shared libraries handed out per person, plus a member's own private ones;
    - notes and saved decisions personal by default;
    - members' apps paired by the owner at the server Mac, capped at that member's allowance;
    - a task queue per person, not a refusal.
  - **The model:**
    - an implicit owner (the OS account that owns `APOGEE_HOME` on the server Mac);
    - members, each with an allowance in 59f's `AppScope` shape;
    - presets (Full, Family, Limited) in place of groups;
    - a guest is a member on Limited with nothing kept.
  - **Already written:** the SPEC amendments (CLI SPEC → Core surfaces, item 4, "Revised again 2026-11-14"; GUI SPEC → Core surfaces, item 3).
- 2026-11-14: First in darwin/macos track 10, ungated. The design's law draws every part with its fallback and adds each missing CLI word to the contract board, so it waits on no CLI item. [10b](server-window.md) is gated on it. It sits in v0.2.4 with CLI track 62 because the boards are the specification 10b builds from and the gap list CLI 62a–62h close. People is drawn now, ahead of 10c in the same release, so the window's sidebar has its place from the first build and 10c only builds.
- 2026-11-14: macOS only (`backlog/darwin/macos/`). The window runs only on the Mac that serves. iPhone and iPad never show it (GUI SPEC → Core surfaces, item 3).
- 2026-11-14: The contract board's new gaps, each with the item that closes it and its fallback (numbers assigned from the next free one when drawn):

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

**Open calls:**
- (consumed decision) Every CLI word and its JSON shape belongs to its CLI item:
  - [62a](../../../cli/v0.2.4/serve-status-and-runtime-record.md): what "this Mac serves" means, and the status fields;
  - [62b](../../../cli/v0.2.4/serve-reload-and-served-vs-disk.md): which changes reload and which need a restart;
  - [62c](../../../cli/v0.2.4/serve-section-in-config.md): the `serve.*` keys;
  - [62d](../../../cli/v0.2.4/serve-as-a-background-service.md): the service verbs and the LaunchAgent;
  - [62e](../../../cli/v0.2.4/connections-and-activity.md) and [62f](../../../cli/v0.2.4/audit-log.md): the activity and audit records;
  - [62g](../../../cli/v0.2.4/administering-local-and-marked-devices.md): the mark's verb and what a marked device may do;
  - [62h](../../../cli/v0.2.4/local-admin-reads.md): the pairing payload and the pending rows;
  - [63a](../../../cli/v0.2.4/people-on-one-server.md) and [63c](../../../cli/v0.2.4/allowances-keys-and-usage.md): people, presets, allowances, keys and limits.

  The boards draw the words those documents record. Where a document is not yet written when this item is worked, the board draws the spike's capability wording and the gap row says it is provisional.
- (consumed decision) The window's build is [10b](server-window.md)'s:
  - the `Window` scene, its composition and module;
  - how `ParityTable` classifies `devices`, `apps` and `serve`'s new verbs while bare `serve` stays carved out;
  - amending 7a's carve-out of `devices` (`:111`).

  People's build is [10c](people-screens.md)'s.
- (consumed decision) The client side's Connections pane, pair sheet and indicator are [7a](../../v0.2.4/app-connection-local-or-server.md)'s to draw.
- [default: the window is drawn inside `main.html`, as a second window chrome shown by the `server…` start presets, so every board mounts `App` as the others do and the prototype stays one file. A separate prototype file would split the canned data and the tick.]
- [default: the menu item is "Apogee Server…" in the Apogee menu, on ⌥⌘S (free: the shell's keys are ⌘1–⌘0, ⇧⌘R, ⇧⌘L, ⌥⌘M, ⇧⌘K, ⌃⌘S and ⌘,; `KeyMap.swift:84-91`, `:99-123`). It is drawn present when the local CLI says this Mac serves (62a's meaning, consumed above), and absent otherwise. Because the window is offered only on the Mac that serves (the user's call), the harness window offers no "make this Mac a server" path: the first opt-in to serving (`apogee serve` or 62c's `config set serve.*` at a prompt) stays the CLI's. Once this Mac serves, installing the background service is the window's, by the user's call (Server, below). The menu board draws both the present and absent states.]
- [default: inside the window, ⌘1–⌘6 go to Server, Devices, Apps, Activity, Audit and People, in that order. They are the window's own keys and never collide with the harness's, since only the focused window takes them.]
- [default: the place names are Server, Devices, Apps, Activity, Audit and People. "Connections" stays 7a's word for the servers a Mac connects to, so the two sides never share a label. Members are "people", and what they may use is their "access". The word "role" appears nowhere in the window.]
- [default: Server draws three things:
  - the status as 62a reports it: running or stopped, since when, the address and port, TLS and the pin, devices-only, the backends served, tools;
  - the service: installed or not, starts at login, with Install as a background service (62d's install verb) when it is not installed, and Start, Stop, Restart and Remove when it is;
  - How clients connect as a form over 62c's keys. Each change is read back and labelled "applied now" or "needs a restart" in 62b's words. A pending restart shows what is served beside what is on disk.

  Stop, Restart and removing the service are asked first, and each says which connected devices and apps it will drop (62e's count).]
- [default: Devices lists paired devices (name, kind, paired, last seen, and whose once 63a answers) and the pending pairings with their expiry. Pair opens a sheet with the typed code, the QR drawn by the app from 62h's payload, the addresses, port and pin, and the expiry counted down from the CLI's own expiry (never invented). The sheet turns to "Paired as <name>" when the read-back shows the pairing redeemed. Rename and Revoke are offered, Revoke asked first with the apps on that device named (59f's cascade). "May administer from this device" is a mark, off by default, asked first, saying what it allows (62g).]
- [default: Apps lists each app by name and the device it runs on. Its scope shows in the plain words `apps pair` prints (59f, `:137`), with what the app asked for beside what was approved, and Grant what it asked for. The scope editor has one row per `AppScope` part (backends, collections, tool classes, commands, task kinds), each an allow or deny that becomes `apps scope`'s flags. Once 63c answers, the editor shows the person's allowance as the ceiling, so it cannot widen past it.]
- [default: Activity shows who is connected now (device or app, and person once 63a answers), what each is running (sessions, jobs, tasks, with a person's queued tasks shown as queued, 63e), and usage per credential over a chosen period (requests and tokens per backend, billed backends marked). It shows metadata only, with no prompts, answers or file names. The honesty line reads: "Apogee doesn't show anyone's conversations here. Anyone who controls this Mac can still read its disk."]
- [default: Audit is a list, newest first, of who (owner, member, device, app), did what (changed, approved, revoked, used), to what, and when. It is filtered by who and by kind, and says it keeps 90 days and never what was asked. It is read-only, and the window offers no clearing.]
- [default: People shows the owner first (this Mac's account, never removable, every right), then the members, each with a preset (Full, Family, Limited), their devices and apps, and their usage against their limit. A member's page shows their allowance in the `AppScope` shape, their personal data as counts only (chats, notes, tasks; never their contents), their shared collections handed out, and the household cloud key: granted or not, with its limit, marked billed. Adding a member starts with pairing their first device at this Mac. A guest is drawn as a member on Limited with "nothing kept". Removing a member is asked first and names their devices and apps.]
- [default: the gap rows' "when" chips name the release that closes them (v0.2.4) rather than "Before v1". Their text names the CLI item, so a gap is closed by name when its item ships.]
- [default: no new color token. Status uses `status.accent`, `status.warning` and `status.error`, and billed uses `model.api`. A missing color goes through the theme table, `ColorTokens` and `tokens.json` together, as the design's law asks.]
- [default: an older apogee (`server:older`) draws each place's fallback line naming its release, from the CLI's own refusal (exit 109, "The following arguments were not expected"), so the window is honest against 0.2.0 through 0.2.3.]

**Guardrail(s).**
- The design's own check ([design CLAUDE.md](../../../../../design/spec/CLAUDE.md) → Checking a change): the node script runs every start preset, the new `server…` ones included, for 30 ticks and reports "N start screens, no problems".
- A before-and-after snapshot of `renderVals()` for every start preset that exists before this item shows them unchanged. The harness window is untouched on a Mac that doesn't serve (the anti-goal). It is taken in the scratchpad with the same `new Component({ start, still: true })` setup.
- A consistency pass:
  - every `ScreenServer*.dc.html` mounts a start that `data-props` lists;
  - every new board has its `canvas.json` entry on page `build` and its README row;
  - every `{{hole}}` in the new markup resolves to a name `renderVals()` returns.
- Each new board is opened at `main.html?start=…&still=true` in all three themes, and its text is checked at 4.5:1. A secrets read of the new markup and canned data finds no token, hash, code or key outside the pairing sheet.
- The spike's probe is the gap list's fixture. When a CLI item ships, the same sandboxed probe (scratch `APOGEE_HOME`, mock backend, `serve --port 0` on loopback, killed after) is re-run, and its gap is marked closed only when the binary answers:
  - `__complete ""` gains `devices` and `apps`;
  - `serve status --output-format json` exits 0;
  - `config get serve` answers;
  - `/v1/admin/status` stops answering 404.
- After publishing, the canvas files are read back and match the repo's byte for byte (`support.js` excepted, since it is never sent).

**Acceptance criteria:**
- [ ] `main.html` draws the Apogee Server window at each `server…` preset: the menu entry (present and absent), Server running, stopped (the service installed or not, with Install offered) and with a change pending a restart, Devices, the pairing sheet (code, QR, waiting, paired), Apps, an app's scope beside what it asked for, Activity, Audit, People, a member's page, and the older-apogee fallbacks. The design's node check reports no problems for every start screen.
- [ ] The thirteen `ScreenServer*.dc.html` boards exist, each on a new Build-page row titled "Screens — the server window, on the Mac that serves" in `canvas.json`, with README's layout table and counts updated.
- [ ] BuildContract has a SERVER WINDOW section mapping every part to its CLI verb. Each missing verb has a gap row with its "Until then:" fallback and the item that closes it (62a–62h, 59a, 59f, 63a, 63c). The window's decisions are in "Decisions the design makes".
- [ ] BuildLayout draws the second window's regions, sizes, keys (⌥⌘S into it; ⌘1–⌘6 inside it) and states. Settings › Keyboard lists the way in. BuildProcesses draws launchd's `apogee serve` outside the app's children, and the window reaching the local `apogee` over pipes, never the port.
- [ ] No board shows a token, code or key outside the pairing sheet. Activity, Audit and People show metadata only and carry the honesty line about the Mac's disk. Billed use is marked. The word "role" appears nowhere in the window.
- [ ] Every pre-existing start preset renders exactly as before (the snapshot comparison), and the harness window has no new item on a Mac that doesn't serve.
- [ ] The changed files are published to the Apogee Desktop canvas and read back identical. Nothing is committed unless the user asks.

**Scope note.** Earmarked for v0.2.4 (darwin/macos track 10, item 10a); gated on design 1e. [10b](server-window.md) is gated on it, and [10c](people-screens.md) builds its People boards. Out of scope: Moved here from v0.2.6 on 2026-11-18, when the releases were merged (the user's call); the id is unchanged.
- any Swift, CLI or test change (10b, 10c and CLI tracks 62 and 63);
- the client side's Connections pane, pair sheet and indicator (7a);
- the window on iPhone or iPad (never offered);
- a way to make a Mac that doesn't serve into a server from the app (the window is offered only on the Mac that serves, the user's call, so the first opt-in stays the CLI's; installing the service on a Mac that serves is in scope);
- the Linux and Windows apps' server windows, which follow v0.2.5;
- network administering beyond 62g's marked devices.
