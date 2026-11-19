# What a local admin screen reads

**What / why.** The user's calls, 2026-11-14 (the admin-mode and people spike): the macOS app gains a separate "Apogee Server" window, offered only on the Mac that serves, and it administers that Mac's server by driving the **local CLI over pipes** ([GUI SPEC](../../../assistant/gui/SPEC.md) → Core surfaces 3; [CLI SPEC](../../../assistant/cli/SPEC.md) → Core surfaces 4, "Revised again 2026-11-14"). A screen over pipes can only show what a verb prints, and it should never parse prose for the facts it exists to change. This item gives the server window the reads and the small acts it needs for devices, apps and policy that 59a and 59f do not provide:
- `devices pending` lists pairing codes that are still waiting, and `devices pending cancel` withdraws one. The same pending rows appear in `devices list --output-format json`.
- `apps list --output-format json` shows what each app asked for beside what the person approved.
- `devices rename` and `apps rename` rename a paired device or app.
- `devices pair --output-format json` and `apps pair --output-format json` carry the exact QR payload and the expiry. The window then draws the QR itself and sees the pairing complete.
- `config get permissions` and `config get tools.allowed_hosts` answer `--output-format json` with the admin plane's own documents, byte for byte.

It closes the spike's wall **A8**: a local admin screen lacks pending pairings, requested scopes, rename, the QR payload and JSON policy reads.

Evidence re-measured 2026-11-14: sources at HEAD (`4acf4a2b`), and a probe on the built binary (`apogee 0.2.0 (b37b0a9f)`, `lib/src/cli/build/macos-arm64/source/apogee`) in a scratch `APOGEE_HOME` under the scratchpad. The probe used the mock backend, with `serve --bind 127.0.0.1 --port 0` killed after each read.

**A8.1: pending pairings cannot be seen or cancelled.** 59a's registry holds pending pairings ("code hash, expiry, attempts left", [paired-devices.md](../v0.2.3/paired-devices.md):27), but its verbs are only `devices pair|list|revoke` (`:33`), and nothing says `list` shows a pending code. 59f adds `apps pair|list|scope|revoke` ([paired-apps-with-scopes.md](../v0.2.3/paired-apps-with-scopes.md):59-62). Neither document has a verb that lists or withdraws a code still waiting. A code is single-use and valid for 10 minutes (paired-devices.md:84), and once used the pending row is gone, so a front end has no read that tells it a pairing has completed.

**A8.2: the QR payload exists only as terminal glyphs.** 59a's payload, `{"apogee_pair":1,"addresses":[…],"port":N,"pin":"sha256/…","secret":"…"}`, "is drawn only on a terminal; `--no-qr` or a pipe prints the code and the address lines only" ([paired-devices.md](../v0.2.3/paired-devices.md):85). The `--json` acceptance names "a code, the addresses, the port and the pin" (`:105`), but not the payload or the expiry. A window that runs `devices pair` over a pipe gets neither glyphs nor payload. It can show the typed code, but no QR code to scan.

**A8.3: what an app asks for is text only.** 59f keeps an app's `requested` scope beside the approved one, never applied (paired-apps-with-scopes.md:39). Its text shows that as "asks for: …" in `apps list` and `apps scope NAME` (`:137`). 59f's `AppView` is only "the app's view with no hash" (`:39`), and its acceptance names kind, device and scope (`:167`), so no JSON field for what an app asked for is specified.

**A8.4: nothing can be renamed.** Names are what `revoke` takes, unique ignoring case (paired-devices.md:87; paired-apps-with-scopes.md:146). 59f records an app's device by that device's name (`:39`). Neither item has a rename, so a device named at pairing ("iPhone (2)") keeps that name for as long as it is paired.

**A8.5: policy reads over pipes are prose, and differ from the admin plane's.** Probed:
1. `config get permissions --output-format json` exits `109` ("The following arguments were not expected: json --output-format"). The binary's own list of the verbs that take `--output-format json` does not include `config get`. The verb's registration has no format option (`presentation/cli/config_cmd.cpp:1459-1483`).
2. The two reads disagree on what they show. With `permissions.write_file` removed from the file and `"https://example.com/path"` added to `tools.allowed_hosts`:
   - `config get permissions` printed five lines, sorted by key, with no `write_file`.
   - `GET /v1/admin/permissions` printed all six destructive tools in declared order, `write_file` at its effective `ask`: `{"data":[{"level":"ask","tool":"write_file"},…,{"level":"ask","tool":"delete_note"}],"object":"list"}`.
   - `config get tools.allowed_hosts` printed both entries, with nothing to mark the bad one.
   - `GET /v1/admin/allowed-hosts` answered `{"data":[{"host":"docs.python.org","valid":true},{"host":"https://example.com/path","valid":false}],"object":"list"}`.

   The cause is two builders. The text read iterates only the levels the file lists (`config_cmd.cpp:227-234`) and prints the hosts one per line (`:182-183`). The route builds `permissions_view`: every destructive tool at its effective level first, then any other key (`presentation/httpserver/admin_config.cpp:507-522`, served at `:687-694`). It also marks each host `valid` (`:771-781`; [http-api.md](../../../reference/cli/http-api.md):1465-1472, :1506-1512).
3. The Mac app parses the prose today. Settings runs `config get permissions` and `config get tools.allowed_hosts` as commands (`lib/src/darwin/source/presentation/settings/ViewModels/SettingsViewModel.swift:358`, `:378`) and splits lines (`lib/src/darwin/source/business/reads/ConfigVerbs.swift:95-104`). It cannot see an unlisted tool's level or an invalid host.
4. `devices list` and `apps list` exit `109` on this binary, and `GET /v1/admin/devices` and `/v1/admin/apps` answer `404`. 59a and 59f are queued, not built, so this item is gated on both.

**Core constraint(s).**
- **Remote mode is an add-on (the anti-goal).** Nothing here may make standalone use need a server, a network, a pairing or an account. `chat`, `complete`, `execute`, `task`, `acp`, `mcp serve` and the Mac app's harness window never read the registry. A read of an absent registry answers an empty list and creates no file, and `config get` in either format needs no server.
- **One config mutation path** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → One config mutation path). This item writes no config. `config get --output-format json` is a read. Renames and cancels write the device registry, which is not the config, through 59a's advisory lock and `write_file_atomically(…, true)`.
- **Secrets are unleakable by construction** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → Secrets are unleakable by construction).
  - A pending row is a view. It has no field for the code, the code's hash, the QR secret or its hash.
  - The pairing code and the QR payload's `secret` appear in exactly one place: the stdout of the `pair` that minted them, printed once to the local caller.
  - They are never served on the admin plane, never in a listing, an event or a log line, and never in a path or a query.
  - **A pairing code is never minted over HTTP** ([paired-devices.md](../v0.2.3/paired-devices.md):89), and never through the network door ([reads-and-commands-over-the-network.md](../v0.2.3/reads-and-commands-over-the-network.md):117).
- **Older versions keep working** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)). Every change is additive:
  - New verbs and routes.
  - New fields on 59a's and 59f's documents.
  - A `pending` array beside each list's `data`, never mixed into it.
  - `--output-format json` beside 59a's `--json`.

  A reader written against 59a or 59f sees the same `data` it saw. Text output is unchanged except for new lines after the existing ones.
- **Parity** ([ADR 0002](../../../adrs/cli/mode-parity.md); [ADR 0010](../../../adrs/cli/the-shell-is-a-mode.md)). Each new leaf answers from the CLI and has its admin twin. Each is classified in `tui_parity.cpp` and `parity_test.cpp`. A JSON read and its route are one builder, so they cannot differ by a byte, as 28h's `read_views` made `agents list` and `GET /v1/admin/agents`.
- **Never listens; serving = server** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → Interactive turns never open a listening socket). Every verb here reads or writes a file and opens no socket. A pairing completes because a running `serve` writes the redemption into the registry, and the reads only look.
- **The layers** ([ADR 0001](../../../adrs/cli/layer-enforcement.md), [ADR 0008](../../../adrs/cli/module-map.md)):
  - Data: the registry's pending, cancel and rename operations (`secrets`).
  - Business: what an app asks for beyond its scope, in 59f's `scope` module.
  - Presentation: the documents, in `operations/read_views`, which both `httpserver` and `cli` already link (`cmake/modules.cmake:72-73`, `:77-78`). The verbs sit in `cli` and the routes in `httpserver`.
  - No new module and no new link.
- **UTF-8 where it enters** (CLI CLAUDE.md). A new name is mended and bounded before it is stored or printed, as a name at pairing is.
- **The dev channel** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel)). Probes and tests use a temporary `APOGEE_HOME` under the scratchpad and the mock backend. A live pass runs `apogee-dev` over `~/.apogee-dev`, never the release `apogee` or `~/.apogee`.

**Seam + files.**
- `source/data/secrets/devices.h/.cpp` (59a's registry, with 59f's app rows):
  - **Pending rows.**
    - A pending row gains a non-secret `id` and the `name`, `kind`, `on` and approved `scope` it will mint.
    - Its `state` is `waiting`, `redeemed`, `cancelled`, `expired` or `burned`, and a redeemed row records `paired_as`.
    - The exchange marks the row `redeemed` instead of deleting it, in the same locked write that adds the paired row. A row in any end state is pruned at the next locked write after its original expiry.
    - `PendingView` is the row with no code hash and no QR-secret hash.
  - **Cancel.** `cancel_pending(id)` sets `cancelled` and drops both hashes, so the exchange then refuses the code exactly as it refuses an unknown one.
  - **Rename.** `rename(old, new)` changes the name in one locked write. Renaming a device also rewrites the `device` field of the apps recorded on it, so an app's row never names a device that no longer exists.
  - **Stable id.** Each paired row gains an `id`, minted for an older row at the first locked write. Ownership records (59f's sessions, jobs and `started_by`) and later records (62e, 62f) can then refer to a row by its `id`, so a rename never breaks them (Open calls).
- `source/business/scope/scope.h/.cpp` (59f's module): `asks_for(requested, scope)`. It returns the part of a requested scope not already approved, in `AppScope`'s shape, so every surface computes "asks for" the same way.
- `source/presentation/operations/read_views.h/.cpp`:
  - `permissions_document(config)` and `allowed_hosts_document(config)`. They move out of `admin_config.cpp:507-522` and `:771-781` unchanged, and the route and the CLI both call them.
  - `devices_document(registry)`: 59a's `{object, data}` plus `pending`.
  - `apps_document(registry, config)`: 59f's views plus `requested`, `asks_for` and the app-kind `pending`.
  - `pairings_document(registry)`.
  - `pair_document(…)`: 59a's `--json` fields plus `pending`, `expires` and `qr_payload`, built by the same function that feeds the terminal QR.
- `source/presentation/cli/config_cmd.cpp` `bind_get` (`:1459-1483`): gains `add_read_format` (the 27j convention, `helpers.h:259-262`).
  - For `permissions` and `tools.allowed_hosts`, `--output-format json` prints the shared document through `write_document`: the route's body and one newline (`machine/json_reporter.cpp:613-620`).
  - Any other key is refused by name. `--reveal` with JSON is refused. Text is unchanged.
- `source/presentation/cli/devices_cmd.h/.cpp` (59a):
  - `devices pending [--output-format json]` and `devices pending cancel ID|--all`.
  - `devices rename OLD NEW`.
  - `--output-format json` on `pair`, `list` and `pending`, with `--json` kept as its alias.
  - `devices list` text gains a "waiting" block after the paired rows.
- `source/presentation/cli/apps_cmd.h/.cpp` (59f):
  - `apps rename OLD NEW`.
  - `--output-format json` on `pair`, `list` and `scope NAME`, with `--json` kept.
- `source/presentation/httpserver/admin_devices.h/.cpp` (59a/59f):
  - `GET /v1/admin/pairings` (views) and `DELETE /v1/admin/pairings/{id}`.
  - `PATCH /v1/admin/devices/{name}` with `{"name": NEW}`. 59f's `PATCH /v1/admin/apps/{name}` gains `name` beside its allow and deny.
  - `GET /v1/admin/devices` and `/v1/admin/apps` serve the same documents as the CLI.
  - No route mints a code.
- `source/presentation/httpserver/admin_config.cpp`: `admin_list_permissions` and `admin_list_allowed_hosts` call the moved builders. `mux.cpp` gets the new rows.
- `source/presentation/cli/tui_parity.cpp`, `tests/presentation/httpserver/parity_test.cpp`: the new leaves classified. `devices pending` is served (`GET /v1/admin/pairings`). `devices pending cancel`, `devices rename` and `apps rename` are twins. `config get` stays read-only as a leaf, and its two JSON keys are held to their routes by the test below.
- Completion ([ADR 0007](../../../adrs/cli/tab-completion.md)): `devices rename` and `apps rename` complete live names, and `devices pending cancel` completes waiting ids.
- Docs:
  - [http-api.md](../../../reference/cli/http-api.md): the pairings routes, rename, the new fields, and lines under `GET /v1/admin/permissions` and `/allowed-hosts` naming their CLI twins.
  - DEVELOPER.md's file rows.
  - CLI CLAUDE.md's secrets sub-rules gain "a pending pairing's view has no code, secret or hash, and a code reaches only the stdout of the `pair` that minted it".
- Tests (hermetic: a temporary `APOGEE_HOME`, the mock backend, handlers driven through `Mux::dispatch` with no port wherever a port is not the point):
  - `tests/data/secrets/devices_test.cpp`:
    - a pending row's id and states;
    - a redeemed row keeping `paired_as` and pruned after expiry;
    - cancel dropping both hashes, and the exchange then refusing the code;
    - rename unique ignoring case, cascading to the device's apps in one write;
    - an id minted for an older row;
    - a 59a/59f-shaped file read unchanged;
    - `0600` after every write.
  - `tests/business/scope/scope_test.cpp`: the `asks_for` table (nothing asked, a subset, more of each kind, a name the config dropped).
  - `tests/presentation/operations/read_views_test.cpp`: the two policy documents over the spike's drift fixture (`permissions.write_file` absent, an invalid host listed), and the pending and pair documents with no secret field.
  - `tests/presentation/cli/config_cmd_test.cpp`: `config get permissions --output-format json` and `config get tools.allowed_hosts --output-format json` equal the route's body (`Mux::dispatch`) plus `\n`, byte for byte, over the same fixture. Another key is refused, `--reveal` with JSON is refused, and text is unchanged.
  - `tests/presentation/cli/devices_cmd_test.cpp`, `apps_cmd_test.cpp`: `pending`, `cancel`, `rename`, `--output-format json` equal to `--json`, `pair`'s `qr_payload` equal to the terminal QR's input bytes, and `--no-qr` minting no secret.
  - `tests/presentation/httpserver/admin_devices_test.cpp`: the pairings routes, rename, no secret in any body, and no mint route.
  - `leak_test.cpp` extended (Guardrails).
  - `cli.serve_lifecycle` (`tests/scripts/sh/serve_e2e.sh`) extended: under `serve --port 0` on loopback, `curl` of the two policy routes equals the CLI's JSON reads of the same home.

**Reference.**
- In-house precedents:
  - 28h's shared read views (`operations/read_views.h:10-16`): "`agents list --output-format json` prints the very body `GET /v1/admin/agents` serves". The policy documents move there the same way.
  - M31, "prose reads gain JSON" ([MILESTONES.md](../../../assistant/cli/MILESTONES.md) → Milestone AO): `graph dedupe --output-format json` "prints the admin route's body byte for byte", from one pass both call.
  - 46a's renames (`operations/renames.h`): one edit or none, with every reference moved in the same write.
  - 59a's registry lock and private write, and 59f's `AppScope` and `requested`.
- Siblings:
  - [59a](../v0.2.3/paired-devices.md) and [59f](../v0.2.3/paired-apps-with-scopes.md): the registry, pairing and scopes this item extends; it is gated on both.
  - [59d](../v0.2.3/reads-and-commands-over-the-network.md): the network door, which never mints a pairing code (`:117`).
  - [62a](serve-status-and-runtime-record.md): whether a server is running to redeem a code.
  - [62b](serve-reload-and-served-vs-disk.md): what the running server applies against what the file says.
  - [62e](connections-and-activity.md): connections and activity keyed by credential.
  - [62f](audit-log.md): the audit log, which records renames and cancels.
  - [62g](administering-local-and-marked-devices.md): which credentials may administer.
  - [63a](people-on-one-server.md): people that devices and apps belong to.
  - darwin [10a](../../darwin/macos/v0.2.4/design-the-server-window.md): the pairing sheet, the devices and apps lists and the policy pane, drawn against these words.
  - darwin [10b](../../darwin/macos/v0.2.4/server-window.md): the server window, gated on this item.
  - darwin [7a](../../darwin/v0.2.4/app-connection-local-or-server.md): the client side of pairing, whose `PairingCode` decodes the payload this item prints.

**Decisions made** (dated):
- 2026-11-14: Specced from the admin-mode and people spike (five probes on `b37b0a9f` in scratch homes, mock backend, serve on loopback). The user's calls that shape this item:
  - **An admin mode:** a separate "Apogee Server" window in the macOS app, offered only on the Mac that serves. It drives the local CLI over pipes. So every fact the window shows must be a CLI document.
  - **Remote administering:** local by default. The owner may mark chosen paired devices as allowed to administer remotely, which revises 59d's default. The admin plane stays local-only otherwise. Which of this item's verbs such a device may run is 62g's.
  - **Server lifecycle:** the window installs the server as a background service (launchd) and starts, stops and restarts it (62d).
  - **Placement:** both before v0.2.5, both in v0.2.4: the server console (CLI track 62, darwin/macos track 10's 10a-10b) and people and access (CLI track 63, darwin/macos 10c). Nothing already queued is renumbered.
  - **People:** a household or a small team (designed for both), owner and members, in the words "people" and "access" (Owner, Member), never "roles", which in Apogee means model roles (63a). The owner sees members' metadata (devices, apps, activity, which model, when), never chat contents, and the docs say honestly that whoever controls the server Mac can still read its disk. Members use the server's local models only, unless the owner grants a household cloud key, with a limit (63c).
  - **The model** (from the prior art, the agent's default): the owner is implicit, the OS account that owns `APOGEE_HOME` on the server Mac, plus members, each with an allowance in 59f's `AppScope` shape. Presets (Full, Family, Limited) stand in for groups, and a guest is a member on the Limited preset with nothing kept (63a, 63c).
  - **The defaults the user took:** live changes are applied on the spot (a reload), saying what still needs a restart (62b); an audit is kept 90 days of who, what and when, without request contents (62f); collections are shared libraries handed out per person, plus a member's own private ones; notes and saved decisions are personal by default; a member's apps are paired by the owner at the server Mac, capped at that member's allowance (63a, 63c), so `apps pair` here stays a verb run on the server's own Mac; tasks queue per person rather than being refused (63e).
  - **The SPEC amendments are already written:** CLI SPEC Core surfaces 4, "Revised again 2026-11-14", and GUI SPEC Core surfaces 3, the server window.
  - **The standing law:** remote use is an add-on. The CLI alone and the Mac app over pipes stay complete and need no server.
- 2026-11-14: Placement: CLI 62h, v0.2.4, track 62, gated on 59a and 59f. darwin 10b gates on it, as on 62a and 62c.
  - The spike proposed folding the device and app pieces into 59a and 59f as amendments. This item stands apart instead, so those two build as written in v0.2.3, and the admin reads land with the server console.
  - A builder of 59a or 59f who reads this first may lay down a row `id` and a kept pending row at no cost. That saves the migration here.
- 2026-11-14: The `config get` half needs neither 59a nor 59f, and may be built first within this item. It stays here because one pane of the server window consumes it.
- 2026-11-14: This item reads the file. A policy document says what the config says, which is what the CLI applies now and what a server applies at its next start or reload. Showing what a running server enforces against the file is 62b's served-vs-disk read. The window shows both, labelled.

**Open calls:**
- [default: the pending verbs are `devices pending`, which lists the waiting codes of both kinds as `devices list` shows both, and `devices pending cancel ID` (or `--all`), as `task recipes` and `task recipes show` sit together. There is no `apps pending`: `apps list` carries the app-kind pending rows, and the cancel is the one verb.]
- [default: a pending row's `id` is 8 random hex characters, not derived from the code, and printed by `pair`. A pending row's view is `{id, kind, name|null, on|null, scope|null, created, expires, attempts_left, state, paired_as|null}`, with timestamps in RFC 3339 UTC. A row in an end state is kept until its original expiry, then pruned at the next locked write, so a window that opened the sheet sees how the pairing ended. A cancel of a redeemed row is refused, naming `devices revoke NAME`.]
- [default: the list documents keep 59a's and 59f's `data` exactly, and add a top-level `pending` array beside it, never mixed in. A reader that counts `data` counts paired rows, as it did. The text gains a "waiting" block after the paired rows.]
- [default: the window sees a pairing complete by reading, not by waiting. It reads `devices pending --output-format json` for its id while the sheet is open and flips when the state is `redeemed`, showing `paired_as` (and, for an app, the scope and what it asked for). A blocking `pair --wait` is not added, because a GUI read is one document from a process that exits. The polling interval is the window's (10b).]
- [default: `pair --output-format json` adds `pending`, `expires` and `qr_payload` to 59a's fields. `qr_payload` is a **string**: the exact bytes the terminal QR encodes, built by one function, so a QR the app draws decodes to the same bytes as the terminal's. Under `--no-qr`, no QR secret is minted at all and `qr_payload` is `null`. The secret is one more live credential, and a caller that will not draw a QR does not need one.]
- [default: every `devices` and `apps` verb that prints a document takes `--output-format json` through `add_read_format` and appears in the binary's list of verbs that take it. The app's reads append that flag. 59a's `--json` stays as an alias and prints the same bytes.]
- [default: `rename OLD NEW` takes 59a's name rules: mended, cut to 64 characters, unique ignoring case. A name another row holds is refused, naming the holder, with no " (2)" on a rename, because the person chose this name. The phrase `this machine` is refused as a name, since 59f's `--on` reads it. A rename changes no token, applies at the next request, and is visible in the device's own `GET /v1/harness/device`.]
- [default: each row gains a stable `id`, and records that refer to a row (59f's served sessions, jobs and the ledger's `started_by`; 62e; 62f) use it. If 59f shipped keying its records by name, this item moves them to the id in the same build. A record that still carries a name resolves through the row's current name or a former one, kept as `aliases` in the row. A rename never orphans a session or a task.]
- [default: `apps list --output-format json` gives each app `requested` (its asked-for scope, or `null`) and `asks_for` (`scope::asks_for`, empty when nothing more is asked), beside 59f's approved `scope`. "Asking for more" after pairing is not added: `requested` is what the exchange carried.]
- [default: only `permissions` and `tools.allowed_hosts` take `config get --output-format json` in this item. Their documents are the admin plane's, byte for byte, through one builder. Any other key with JSON is refused, naming the two that take it. `--reveal` with JSON is refused, so a JSON read can never carry a key. JSON for further keys is a later item, each with its route.]
- [default: the admin twins are `GET /v1/admin/pairings`, `DELETE /v1/admin/pairings/{id}`, and `PATCH /v1/admin/devices/{name}` and `/v1/admin/apps/{name}` with `name`. Pairings get their own collection so a device named `pending` cannot collide with a route.]
- (consumed decision: 62g) Which of these verbs a network credential, or a device the owner marks, may run. Whatever 62g decides, `devices pair` and `apps pair` stay local-only, because a pairing code is never minted over the network (59a:89; 59d:117).
- (consumed decision: 62a) Whether a server is running to redeem a code. The sheet says when none is, from `serve status`.
- (consumed decision: 62b) What the running server applies against the file's policy.
- (consumed decision: 62f) A rename and a cancel are audited admin acts. Their records carry the row's `id`, the old and new names, and never a code.
- (consumed decision: darwin 10a, 10b) Drawing the QR (CoreImage), the polling cadence, and classifying `devices` and `apps` as server-window screens, which amends 7a's carved-out default (app-connection-local-or-server.md:111). The app must never log or keep the code or `qr_payload`.
- (consumed decision: 63a) The person a device or app belongs to, and a pairing minted for a member.

No **[user]** call blocks this item.

**Guardrail(s).**
- **The spike's probe as the fixture:** a config with `permissions.write_file` absent and an invalid host listed. `config_cmd_test` holds each JSON read equal to its route's body plus one newline, and `cli.serve_lifecycle` holds the same on the real binary against a loopback `serve --port 0`. A second builder for either document fails both.
- `leak_test.cpp` plants a distinctive pairing code and QR secret. It finds them only in the minting `pair`'s stdout, and searches every other listing, `devices pending`, both list documents, every admin response, `-v` log line and bus event for them.
- `devices_test` pins that a pending view has no secret field and that a cancelled code is refused like an unknown one.
- A test reads a 59a/59f-shaped registry and checks `data` is unchanged (ADR 0006).
- `tui_parity_test` and `parity_test` fail on an unclassified new leaf. The binary's `--output-format json` list includes the new verbs.
- A test drives `chat` and `complete`, and runs `devices list`, `devices pending` and `config get permissions --output-format json`, with no registry present. The answers are empty or file-based, and no file is created (the anti-goal).

**Acceptance criteria:**
- [ ] In a sandbox home with the mock backend, `apogee config get permissions --output-format json` and `apogee config get tools.allowed_hosts --output-format json` print, byte for byte, the bodies of `GET /v1/admin/permissions` and `GET /v1/admin/allowed-hosts` from a loopback `serve --port 0` over the same home (plus one newline). On the spike's fixture, that includes `write_file` at `ask` and the invalid host with `"valid":false`. Text output is unchanged.
- [ ] `apogee devices pair --name phone --output-format json` prints the code, `pending`, `expires` and a `qr_payload` string that decodes as 59a's payload. With `--no-qr`, `qr_payload` is `null`.
- [ ] `apogee devices pending --output-format json` lists that row as `waiting`, with no code, secret or hash. `devices list --output-format json` carries it under `pending`, with `data` unchanged.
- [ ] After a scripted client redeems the code against a loopback `serve`, `devices pending` shows the row `redeemed` with `paired_as: "phone"` until its expiry.
- [ ] `devices pending cancel ID` makes the exchange refuse that code as it refuses an unknown one.
- [ ] `apogee apps pair --name portfolio --on "this machine" --output-format json` behaves the same for an app. After an exchange carrying a `requested_scope` wider than the approved one, `apogee apps list --output-format json` shows `scope`, `requested` and a non-empty `asks_for`.
- [ ] `apogee devices rename phone "Taylor's iPhone"` and `apogee apps rename portfolio job-hunter` apply at the next request. The device's apps name the new device, the token still works, and a name already held is refused, naming its holder.
- [ ] `GET /v1/admin/pairings`, `DELETE /v1/admin/pairings/{id}` and the rename `PATCH`es answer as their CLI twins, and no route mints a code.
- [ ] `chat` and `complete` behave as before with no registry, and the full suite is green on all five targets.
- [ ] Optional live pass, said when skipped: the user runs `apogee-dev devices pair --output-format json` on the dev channel, renders `qr_payload` as a QR with any generator, scans it from a second device into a running `apogee-dev serve`, and sees `devices pending` turn `redeemed`.

**Scope note.** Earmarked for v0.2.4 (track 62); gated on 59a, 59f. darwin 10b gates on it. Out of scope: Moved here from v0.2.6 on 2026-11-18, when the releases were merged (the user's call); the id is unchanged.
- Every screen: drawing the QR, the sheet, the lists and the policy pane (darwin 10a, 10b).
- What a running server applies against the file (62b), whether one is running (62a), connections and activity (62e), the audit log (62f), and who may administer over the network (62g).
- People, and devices or apps belonging to a person (63a).
- An app asking for more after pairing.
- Pairing over the network, or without the server's own Mac.
- LAN discovery.
- JSON for `config get` keys other than the two.
- Rotating or re-issuing a token.
