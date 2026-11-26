# Backlog

The work queue, seen one release at a time. This page is the **overview**: what each release plans across every product, with every item linked to its document. Each product's own index (its `README.md`, linked below) stays the **authority** on its rows, ids, gates and format. This page mirrors it and is updated in the same pass. The [documentation router](../CLAUDE.md) says which product a change belongs to.

## v0.2.1, the release in progress

The branch is `v0.2.1`; the last published release is v0.2.0.

**Still to build: nothing.** Every queue's v0.2.1 tracks have shipped in development (2026-11-21).
- **The design:** none; track 1 is complete in development (2026-11-21). The prototype is rebuilt in React; its law (1a), its scaffold (1b), the old prototype's baseline (1c), the CLI stand-in (1d), its first screen (1e), the session screen's two halves (1u, 1v), the Sessions place around it with the console (1g), the Models place (1h), Settings (1i), the Training place (1j), the Video place with the runtime consent (1k), the compositions (1l), the Voice place (1m), the Library and Review places (1n), the Knowledge and Code graph places (1o), the Suites and Tasks places (1p), the gallery with the foundations pages (1q), the build boards as pages (1r), the parity proof (1s) and retiring the old prototype (1t) have shipped: every place and every Build and Foundations board is ported (the canvas's 5 Exploration boards are not, 1q's default), the built file is the one review surface, after 1s's report was presented you wrote "Go ahead and continue" (2026-11-21), and the old one-file prototype and the claude.ai canvas are retired.
- **The mod:** none; track 1 is complete in development (2026-11-21). The Claude plugin is born: its scaffold, 1a, its tools, slash commands and status line, 1b, its pane, 1c, and its pipeline, 1d, have shipped.
- **The Apple apps:** none; their three tracks for this release have shipped.
- **The CLI:** none; its three tracks for this release have shipped.

Outside the release, 15 maintenance items can be picked up any time: the CLI's M36–M47 (2026-11-22) and the Apple apps' M21–M23 (2026-11-26). Five wait on another maintenance item: the CLI's M42 on M37 and M41, M43 on M40, M44 on M43 and M46 on M45, and the Apple apps' M23 on M21. None of the 15 waits on a decision from you.

**Can start today** (nothing gates it): no release item; any maintenance item below that waits on nothing (the CLI's M36–M41, M45 and M47; the Apple apps' M21 and M22).

### The design ([queue](design/README.md#index))

**Track 1:** the design prototype is rebuilt in React and TypeScript, using the same layers and screen modules as the Mac app. It reads the CLI's own recorded output, and it builds into one `lib/design/main.html` that opens with a double-click. The old one-file prototype, frozen at `b375fac9` (1c's baseline), is retired. New screens are drawn in the React app. The law, 1a, has shipped: the design's [ADR 0001](../adrs/design/react-design-prototype.md). So has the scaffold, 1b: the empty app, its module map and layering check, and one `npm run check` ([DEVELOPER.md](../assistant/design/DEVELOPER.md)). So has the old prototype's baseline, 1c: its still frames at that revision, re-made by `npm run baseline` ([DEVELOPER.md](../assistant/design/DEVELOPER.md#the-old-prototypes-baseline)). So has the CLI stand-in, 1d: the app's three lower layers, answering every read, command, job and chat from the CLI's own recordings ([DEVELOPER.md](../assistant/design/DEVELOPER.md#the-cli-stand-in)). So has the first screen, 1e: the tokens generated from tokens.json, the shell with its 11 places, the composition root, and the system monitor on the CLI's recorded `system` document, so the built file opens at the monitor ([DEVELOPER.md](../assistant/design/DEVELOPER.md#styles-the-shell-and-the-first-screen)). The session screen, 1f, was split on 2026-11-19 into 1u and 1v, and 1f stays spent. Its first half, 1u, has shipped too: the session's turn grown only from the CLI's recorded streams, its header and composer, the answer's markdown and code, and the presets `chat:new`, `chat:private`, `chat:play` and `chat:suite`. So has its second, 1v: the five scenarios and the session's states replayed from captured or hand-made streams, the nine presets from `chat` to `chat:compacting`, and `chat` the default start again ([DEVELOPER.md](../assistant/design/DEVELOPER.md#the-session)). So has 1g: the sidebar's list of the CLI's saved chats on every screen, the inspector beside the session, the execute door and the console, with the presets `execute` and `console` ([DEVELOPER.md](../assistant/design/DEVELOPER.md#the-sessions-place-and-the-console)). So has 1h: the Models place -- its four tabs, its sheets and five confirms -- on the CLI's recordings, a pull and a conversion landing in Stored weights, and its nine presets ([DEVELOPER.md](../assistant/design/DEVELOPER.md#the-models-place)). So has 1i: Settings -- its ten panes over the CLI's reads and verbs, Appearance restyling the whole app, Keyboard listing the shell's keys -- and its thirteen presets ([DEVELOPER.md](../assistant/design/DEVELOPER.md#settings)). So has 1j: the Training place -- its five tabs, the promote and pull sheets, a pipeline, a cycle pass, a promote and a pull replayed from the CLI's streams -- and its ten presets ([DEVELOPER.md](../assistant/design/DEVELOPER.md#the-training-place)). So has 1k: the Video place's clips over the records the CLI wrote, a clip's page with its player, the generate form, a run replayed from its stream, and the one-time install consent and card, with the player, the phase row, the install card and the consent sheet in the shared styles module, and its four presets ([DEVELOPER.md](../assistant/design/DEVELOPER.md#the-video-place-and-the-runtime-consent)). So has 1l: the compositions in Video's second tab -- a brief planned on paper and rendered scene by scene from the project the composer saves, Stop and Resume, an edit previewed and rendered as a new video -- the chosen voice shared through `styles`, and its five presets ([DEVELOPER.md](../assistant/design/DEVELOPER.md#the-compositions)). So has 1m: the Voice place -- the speeches over the records the CLI wrote beside their WAVs, the speak card with the voice model's own voices, the selected speech with its waveform, player and sentence timings, a speech spoken from a hand-made stream and read back from the folder -- and its preset `voice` ([DEVELOPER.md](../assistant/design/DEVELOPER.md#the-voice-place)). So has 1n: the Library and Review places -- the agents, symphonies and MCP servers, the person's own definition files edited in place and closed asked first, Review's dry run, its targets, the last sweep and its skills -- and their ten presets ([DEVELOPER.md](../assistant/design/DEVELOPER.md#library-and-review)). So has 1o: the Knowledge and Code graph places -- the decision records and the collections on the CLI's recordings, the code graph on the recorded `demo` graph with its Functions and Files views, the notes prose graph, the More menu's export, dedupe and delete, and Ask about this landing in a new session -- and their six presets ([DEVELOPER.md](../assistant/design/DEVELOPER.md#knowledge-and-the-code-graph)). So has 1p: the Suites and Tasks places -- the recorded suites and the CLI's verdict on each, a chat opened under one and the session switched to it, the ledger's tasks, a run followed live from its recorded stream with halt, cancel, resume and Run again, a refusal's alert and the New task sheet -- and their nine presets ([DEVELOPER.md](../assistant/design/DEVELOPER.md#suites-and-tasks)); every place is ported. So has 1q: the gallery, the canvas's Build and Foundations pages and rows inside the built file on `?gallery` or ⌥⌘G -- every screen board the app at its preset and still, App live, every preset a frame -- with the color, type, components and logo pages generated from the tokens and the modules' own specimens, and it replaces the claude.ai canvas as the review surface ([DEVELOPER.md](../assistant/design/DEVELOPER.md#the-gallery)). So has 1r: the contract, layout and processes boards as pages in the gallery's Build row -- a contract row one data edit, the stat cards counted, the gaps the monitor and the Models place found added as G25-G32, every size the layout page shows read from the tokens and every key from the key map, the native controls under the real floor, `SWIFTUI · MACOS 27`, and every color a token ([DEVELOPER.md](../assistant/design/DEVELOPER.md#the-build-pages)). So has 1s: the parity proof, every preset 1c kept and every rebuilt board shot beside the built file and each of the 4,606 differences classed as the CLI's data or a fix a shipped item records, none open; after its report was presented you wrote "Go ahead and continue" (2026-11-21) ([DEVELOPER.md](../assistant/design/DEVELOPER.md#the-parity-proof)). So has 1t: the old one-file prototype and its boards deleted after `npm run parity` ran green (kept in git history, 1c's baseline re-making their pictures), `tokens.json` moved beside the built file, the canvas closed with a last note, [the design's guide](../assistant/design/CLAUDE.md) the one guide, and every Swift comment, doc line and pending Apple-apps item re-pointed at the design app. All twenty-one are in [Milestone A](../assistant/design/MILESTONES.md#milestone-a--the-design-prototype-in-react); **track 1 is complete in development.**

Nothing is left to build.

### The mod ([queue](mod/README.md#index))

**Track 1, the mod is born:** the Claude plugin, driving the same `apogee` binary through its MCP server and JSON documents. The scaffold, 1a, has shipped ([Milestone A](../assistant/mod/MILESTONES.md#milestone-a--the-mod-is-born)): the plugin at `lib/src/mod/` with its four layers under the layering check, binary discovery and the not-installed guard, and `changed.sh mod`. So have its working surfaces, 1b: Apogee's served tools and `apogee_complete` for Claude, the `/apogee` set with promote and rollback confirmed, and the status line. So has its pane, 1c: backends and models, training and the version ledger beside the chat, read back after every confirmed command, polled only while shown, with promote and rollback as confirmed buttons. So has its pipeline, 1d: its own workflow, `mod.yml`, gated by `changed.sh mod`, with `tsc` on every runner, validate where the `claude` CLI installs headlessly (the skip said otherwise), `claude plugin test` a dev-host gate, and the plugin zipped as the artifact `apogee-mod`. All four are in [Milestone A](../assistant/mod/MILESTONES.md#milestone-a--the-mod-is-born); **track 1 is complete in development.** Your live look (`claude --plugin-dir lib/src/mod`, on Claude Code 2.1.326 or newer) is still to come.

Nothing is left to build.

### The Apple apps ([queue](darwin/README.md#index))

**macOS.** Nothing is left to build. Apogee in Xcode (track 5), the second bug sweep's app findings (track 6) and the app's logo (track 11) have shipped ([Milestone F](../assistant/darwin/MILESTONES.md#milestone-f--apogee-in-xcode-the-apps-side), [Milestone G](../assistant/darwin/MILESTONES.md#milestone-g--the-second-sweeps-gui-findings), [Milestone H](../assistant/darwin/MILESTONES.md#milestone-h--the-apps-logo)).

### The CLI ([queue](cli/README.md#index))

Nothing is left to build. This release's three CLI tracks have shipped:
- the mod's CLI siblings, `apogee mcp serve` (track 54, [Milestone W](../assistant/cli/MILESTONES.md#milestone-w--the-mcp-client));
- Apogee in Xcode (track 56, [Milestone AS](../assistant/cli/MILESTONES.md#milestone-as--apogee-in-xcode));
- the second bug sweep's CLI findings (track 57, [Milestone AT](../assistant/cli/MILESTONES.md#milestone-at--the-second-sweeps-cli-findings)).

**One step is yours.** After the release's build, you turn on the merge ruleset so CI gates merges into `stable` (57m, [the ROADMAP line](../assistant/cli/ROADMAP.md#v021--the-mods-cli-siblings-track-54-apogee-in-xcode-track-56-and-the-second-bug-sweep-track-57)). Run it against the release pull request after its first green run (`required-checks.py --pr <N> --apply`), so the mod's two new jobs, `what changed (mod)` and `build mod` (mod 1d), are required beside the CLI's and the app's.

### Ties to later releases

None now: [mod 2b](mod/v0.2.2/mod-graphs-and-knowledge.md)'s gate on mod 1c was satisfied when 1c shipped (2026-11-21); it waits on CLI 58b alone, in v0.2.2.

## Any time: maintenance

Maintenance items aren't tied to a release; any one can be picked up by name.

| Product | # | Item | Waits on |
|---|---|---|---|
| CLI | M36 | [Grant and deny flags offer what they take](cli/maintenance/grant-and-deny-flags-offer-what-they-take.md) | — |
| CLI | M37 | [PowerShell completes the word at the cursor](cli/maintenance/powershell-completes-the-word-at-the-cursor.md) | — |
| CLI | M38 | [A refused run starts no MCP server](cli/maintenance/a-refused-run-starts-no-mcp-server.md) | — |
| CLI | M39 | [Deny takes every host a yes takes, and says when a tool's name became a website](cli/maintenance/deny-takes-every-host-a-yes-takes.md) | — |
| CLI | M40 | [Delete-allowed-host removes what the list holds](cli/maintenance/delete-allowed-host-removes-what-the-list-holds.md) | — |
| CLI | M41 | [macOS's bash 3.2 completes every word](cli/maintenance/bash-3-2-completes-every-word.md) | — |
| CLI | M42 | [The completion stubs read the words as the shell does](cli/maintenance/stubs-read-the-words-as-the-shell-does.md) | M37, M41 |
| CLI | M43 | [The gate's config edits land, or say they did not](cli/maintenance/gate-edits-land-or-say-they-did-not.md) | M40 |
| CLI | M44 | [The YAML editor refuses, by name, the layouts it does not write](cli/maintenance/yaml-editor-refuses-the-layouts-it-does-not-write.md) | M43 |
| CLI | M45 | [A collection is named, not pathed, at every reader](cli/maintenance/a-collection-is-named-not-pathed-at-every-reader.md) | — |
| CLI | M46 | [A collection the config can't hold is refused before it is written](cli/maintenance/a-collection-the-config-cannot-hold-is-refused-before-it-is-written.md) | M45 |
| CLI | M47 | [Config migrate carries every line the loader reads, or names the one it can't](cli/maintenance/config-migrate-carries-every-line-or-names-it.md) | — |
| Apple apps | M21 | [A listed name reaches the verb as listed](darwin/macos/maintenance/a-listed-name-reaches-the-verb-as-listed.md) | — |
| Apple apps | M22 | [A job's notes reach the person](darwin/macos/maintenance/a-jobs-notes-reach-the-person.md) | — |
| Apple apps | M23 | [Every name the app passes is read as a name](darwin/macos/maintenance/every-name-the-app-passes-is-read-as-a-name.md) | M21 |

## Later releases

One row per release, with each track linked. Follow a link to see the items and their gates in that product's queue.

| Release | CLI | Apple apps | Mod |
|---|---|---|---|
| v0.2.2 | [Track 58, Claude aware of Apogee](cli/README.md#v022) (58a–58d) | — | [Track 2, Claude aware of Apogee](mod/README.md#v022) (2a–2b) |
| v0.2.3 | [Track 59, the harness server](cli/README.md#v023) (59a–59h) | — | — |
| v0.2.4 | [Track 60, one tool definition and device tools](cli/README.md#v024) (60a–60e); [track 62, the server console](cli/README.md#v024) (62a–62h); [track 63, people and access](cli/README.md#v024) (63a–63e) | [Track 7, the Mac as a client](darwin/README.md#v024) (7a–7e); [track 10, the server window and its People place](darwin/README.md#v024) (10a–10c, macOS) | — |
| v0.2.5 | — | [Track 8, iPad](darwin/README.md#v025) (8a–8c); [track 9, iPhone, the foldable and the three platforms whole](darwin/README.md#v025) (9a–9c) | — |
| v0.2.6 | [Track 61, Apogee-tuned training](cli/README.md#v026) (61a–61b) | — | — |

On 2026-11-18 (the user's call), v0.2.4 took in v0.2.6 and v0.2.7, v0.2.5 took in v0.3.0, and v0.3.1 became v0.2.6. The items kept their ids.

The design has nothing planned after v0.2.1 yet.

## The queues

| Folder | Product |
|---|---|
| [`cli/`](cli/) | **The CLI**, the harness itself: commands, backends, the TUI, `serve`, machine mode |
| `gui/` | **Every GUI app**, what binds every graphical app alike, whatever its language |
| [`darwin/`](darwin/) | **The Apple apps**, macOS, iPadOS and iOS, one Swift codebase; an item or note for one OS sits in its subfolder |
| [`mod/`](mod/) | **The mod**, the Claude plugin, driving the same binary |
| [`design/`](design/) | **The design**, the prototype every GUI app is built from (`lib/design/`) |
| `unix/` | **The Linux app**, not built yet |
| `windows/` | **The Windows app**, not built yet |

There is no Android queue yet.

## Keeping this page true

A skill or session that changes a product's index changes this page in the same pass:
- **An item is added:** its row goes into its release's section here, or into Later releases as a track line.
- **An item ships:** its row leaves, and the counts and "can start today" are updated.
- **An item moves:** its row moves.
- **An item is split:** its sub-items replace its row.
- **A gate changes:** the row's "Waits on" changes.
- **A release ships:** the next release becomes "in progress".

The docs check catches a stale link here, but not a missing row. So the backlog skills' validation step also compares this page with the product indexes.
