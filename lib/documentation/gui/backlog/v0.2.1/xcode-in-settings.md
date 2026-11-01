# Xcode in Settings

**What / why.** The user asked whether Apogee could plug into Xcode. The research of 2026-10-31 (five angles, each checked by a skeptic, then a completeness critic) found two routes. Both run over pipes, and both belong to the CLI:
- **Apogee as an agent in Xcode.** Xcode 26.6 and later starts any Agent Client Protocol (ACP) agent that a person adds under Xcode › Settings › Intelligence › Agents › "Add an Agent" (Apple, *Setting up coding intelligence*). The CLI's track 56 makes Apogee one: `apogee acp`, the fifth core surface the CLI SPEC gained that day ([SPEC.md](../../../cli/assistant/SPEC.md), line 32; [56a](../../../cli/backlog/v0.2.1/acp-agent-mode.md)).
- **Xcode's tools in Apogee.** Since Xcode 26.3, Xcode is an MCP server, reached through `xcrun mcpbridge` once Xcode's "Allow external agents to use Xcode tools" is on and a project is open (Apple, *Giving external agents access to Xcode*). The CLI's [56d](../../../cli/backlog/v0.2.1/xcode-setup-from-apogee.md) adds an `xcode` MCP preset and `check` rows for Xcode. It also adds `apogee acp registration`, which prints the fields Xcode's sheet asks for.

The app has no place for any of this (wall **W7**). Measured, not guessed (2026-10-31, by reading the source on `v0.2.1`):

1. **There is no pane for it.** Settings has five panes (`SettingsViewModel.Pane`, `lib/src/darwin/source/presentation/settings/ViewModels/SettingsViewModel.swift:17-45`). The design's Settings lists nine (`lib/design/spec/html/main.html:5484`). Neither has one for Xcode.
2. **Health hides what passes.** `check`'s rows already reach the app (`readCheck`, `:423-437`). But `healthRows` draws only rows that fail, rows that warn, and skipped rows that carry a remedy (`:155-158`). A row saying Xcode was found would never be drawn.
3. **The app drops the preset's state.** `mcp presets --output-format json` prints each preset's `runnable`, `missing` and `remedy` (`lib/src/cli/source/presentation/operations/mcp_presets.cpp:138-166`). The app's `MCPPreset` decodes only its name, description, provides, register and refusal (`lib/src/darwin/source/business/reads/Library.swift:71-91`). Library › MCP servers would list an `xcode` preset like any other, with nothing about Xcode's switch or its approval.
4. **Library's read connects.** `MCPViewModel.refresh` reads `mcp list` (`lib/src/darwin/source/presentation/library/ViewModels/LibraryViewModels.swift:460-469`), which lists the servers "and connect[s] to each" (`lib/src/cli/source/presentation/cli/mcp_cmd.cpp:163`). Once `xcode` is registered, that read starts `xcrun mcpbridge`, and Xcode asks the person to approve that binary if it has not yet. Every Apogee session does the same at its start: the MCP registry holds "Every configured server, connected once at startup" (`lib/src/cli/source/business/mcp/registry.h:15`). Whether the entry exists is a plain config read: `config get mcp_servers.<name>` prints the entry's command, or `no such config key` when there is none (`lib/src/cli/source/presentation/cli/config_cmd.cpp:328-347`, `:1443`). Registering connects to nothing: `mcp create` writes the entry and prints a `Test it:` line (`mcp_cmd.cpp:124-129`).
5. **`acp` has no place in the parity table.** `ParityTable` has three carve-outs (`lib/src/darwin/source/presentation/shell/ViewModels/Parity.swift:83-85`). `ParityTests` pins 24 screens and 3 carve-outs (`lib/src/darwin/tests/presentation/shell/ParityTests.swift:35-43`). The word fixture holds 29 words, and `acp` is not one of them. Under [ADR 0003](../../adrs/parity.md), the suite fails as soon as the fixture is refreshed against a CLI that has `acp`, and stays failing until `acp` is classified.
6. **A refusal matches by prefix.** The console refuses any command path that *starts with* a refusal's words (`Parity.swift:122-124`), as the TUI's exec line does (`lib/src/cli/source/presentation/cli/tui_runner.cpp:54-71`). So refusing `acp` also refuses `acp registration`.

From the research (its sources: Apple's documents, release notes, and `strings` run on Xcode 27.1, build 27A9275, the version on the development Mac):
- **Xcode's "Add an ACP Agent" sheet** asks for a Name, an Executable, an optional Interpreter ("Specify an interpreter if the executable is a script…"), Arguments, and Environment Variables (Apple's document, and `strings` on `IDEIntelligenceACP`). Devin's guide alone says the Executable must be an absolute path, with no `~` and no `PATH` lookup; Xcode's placeholder for it is `/absolute/path/to/agent`.
- **Xcode's trust is tied to the binary.**
  - Access to Xcode's tools is gated by the switch (Apple's document) and by a prompt for each agent. Xcode hashes the binary (SHA-256) and asks again when it changes: "The agent binary has been updated since you last approved it" (Xcode's strings, confirmed by the skeptic).
  - Xcode's strings also suggest an unsigned agent's approval has a time limit (`unsignedRequiresExpiration`, `unsignedTTL`). This was read from strings, not seen in use.
  - `apogee` is ad hoc and linker-signed (`codesign -dv` on `~/.local/bin/apogee`: `flags=0x20002(adhoc,linker-signed)`, `TeamIdentifier=not set`; re-checked 2026-10-31).
  - So, as those strings read (not yet seen in use), every install asks for approval again, and `apogee-dev` is likely approved apart from `apogee` (wall W3). Nothing fixes this short of Developer ID signing; 56d states it to the person.
- **Every Mac the app runs on can run these Xcodes.** Xcode 26.3 needs macOS Sequoia 15.6 or later, 26.6 needs Tahoe 26.2 or later, and 27 needs Tahoe 26.6 or later on Apple silicon. All of these are at or below the app's floor of macOS 27 on Apple silicon (`MACOSX_DEPLOYMENT_TARGET = 27.0`, `ARCHS = arm64`).
- **Nobody has seen how Xcode stores a custom agent.** There are `ACPRegistryPlist` and `ACPAgentPlistPayload` symbols, but the research never found the file: adding a test agent would have changed Xcode's state. Xcode's own sheet is the only documented way in.

**This item gives Xcode a pane in Settings.** The pane has three parts:
- **(a) What `check` says of Xcode:** its Xcode rows, worded as the CLI words them, ok rows included. The pane never probes Xcode itself.
- **(b) "Let Apogee use Xcode's tools":** registers the `xcode` preset (`mcp create xcode --from xcode`) and removes it (`config delete-mcp-server xcode`). Each change is read back, and the CLI's words are shown. Xcode's switch, the open project and Xcode's approval are explained by the preset's `note`, which 56d adds, in the CLI's words.
- **(c) "Use Apogee as an agent in Xcode":** the fields `apogee acp registration` prints, each with a Copy button, and where they go in Xcode (the document's `where`). The note that approval is asked again after each update is the document's `approval`, in the CLI's words.

The app writes nothing into Xcode's settings: the person adds the agent in Xcode's own sheet.

**Core constraint(s).**
- **The machine-mode client rule** ([GUI CLAUDE.md](../../assistant/CLAUDE.md), "The GUI is a machine-mode client"): every fact on the pane is something the binary printed. The app has no probe of Xcode, no copy of the preset's command, and does not compute the binary's path. The CLI prints its own path, as the `self` preset already does (`mcp_presets.cpp:84-96`). On the development build that path is `apogee-dev`'s, and the app never adds or edits it.
- **No HTTP path for local work** (the GUI SPEC's non-goal; the CLI [SPEC.md](../../../cli/assistant/SPEC.md), line 66: `serve` "is never a localhost backend for a local front-end"). The pane sends no one to `serve` or Xcode's "Locally Hosted" chat provider.
- **Nothing is connected to just to show the pane.** The pane reads `check`, `mcp presets`, `config get` and `acp registration`. It never runs `mcp list` or `mcp test` against `xcode`, and Register only writes config. Xcode is first reached when an Apogee session starts the registered server, and then Xcode asks its own approval; the pane says so before Register.
- **Honest about older binaries** (GUI 4k's rule, `configSetHint`, `SettingsViewModel.swift:416-421`). What a binary lacks is said only from what that binary answered, and nothing is drawn that it has not printed.
- **Parity** ([ADR 0003](../../adrs/parity.md)): `acp` is classified in the same change, and the console's refusals stay the TUI exec line's, word for word.
- **Shape** ([ADR 0002](../../adrs/presentation-mvvm.md), [ADR 0004](../../adrs/module-schemes-and-previews.md)): the view-model is free of SwiftUI. Every view has a named, hermetic `#Preview` for each state, built over recorded documents.
- **The protocol grows additively** (the CLI's [ADR 0006](../../../cli/adrs/backwards-compatibility.md)): unknown fields in the new documents are tolerated.
- **Development runs on the dev channel** ([the law](../../../cli/assistant/CLAUDE.md#development-runs-on-the-dev-channel)): recordings and probes use `apogee-dev` (`make -C lib/src/cli install MODE=dev`) over a temporary `APOGEE_HOME`, and `~/.apogee` is never touched.

**Seam + files.**
- **`lib/src/darwin/source/business/reads/`**:
  - `Library.swift`: `MCPPreset` additionally decodes `runnable`, `missing`, `remedy` and 56d's `note`, each optional. Library's screen is unchanged; the Xcode pane reads them.
  - A new `XcodeSetup.swift`: `AgentRegistration`, the `acp.registration` document 56d records (`acp registration`, read through `runner.read`, which appends `--output-format json`), and the rows of `check`'s Xcode section by the names 56d gives them (today `xcode`, `mcpbridge`, `acp agents`, `approval`).
- **`lib/src/darwin/source/presentation/settings/`**:
  - `ViewModels/SettingsViewModel.swift`: `Pane` gains `.xcode`.
  - A new `ViewModels/XcodeSettings.swift`, owned by `SettingsViewModel` as `ThemeSettings` is (the `theme` property, `:192`; the class, `:645`). It holds the reads, `register()`, `askToRemove()`/`remove()`, and the facts of what the binary lacks.
  - `Views/SettingsView.swift`: the pane switch (`:45-51`) gains `XcodePane`.
  - A new `Views/XcodePane.swift`: the rows, Register/Remove, and the fields with Copy. Copy writes the pasteboard from the view, never from the view-model, as `MarkdownCode.copy(to:)` does (`lib/src/darwin/source/presentation/styles/Components/MarkdownText.swift:59`, its pasteboard nameable by a test).
  - `Previews/`: the recorded documents and the runner's rules.
- **`lib/src/darwin/source/presentation/shell/ViewModels/Parity.swift`**:
  - A new `acp` row, mirroring 56a's row in `tui_parity.cpp` (the open calls below). Either way its words say that Xcode, or another agent host, starts `acp` over its own stdio and the app never does, and that its registration is in Settings › Xcode.
  - The notes on the `mcp`, `config` and `check` rows (`:75`, `:58`, `:62`) name Settings › Xcode.
  - The refusals follow (`:100-111`).
- **`lib/src/darwin/tests/presentation/shell/Fixtures/command-words.txt`**: refreshed by `lib/src/darwin/scripts/refresh-parity-fixture.sh`, passing the branch's `apogee-dev` as its argument. Its default is the release binary (`:16`).
- **Tests** (under `lib/src/darwin/tests/`):
  - A new `presentation/settings/XcodeSettingsTests.swift`.
  - `presentation/settings/SettingsViewModelTests.swift:170`, which pins the five pane titles.
  - Decoder tests and recordings in `business/reads/` and its `Fixtures/`.
  - `presentation/shell/ParityTests.swift`.
  - `presentation/console/ConsoleSafetyTests.swift`: what the console does with `acp` and `acp registration`, behind every root option.
- **Docs:**
  - The GUI CLAUDE.md codebase map (the `settings` entry) and DEVELOPER.md.
  - [ADR 0003](../../adrs/parity.md): a dated note, and `acp` in its carve-outs list (today "the TUI's three") if 56a refuses it.
  - The Presentation layer card's Modules line (`lib/src/darwin/source/presentation/CLAUDE.md`), whose `settings` entry lists the panes. The darwin layering check reads every backticked word on that line as a module name, so write commands there in plain text.
- **The design** (`lib/design/spec`, under its [CLAUDE.md](../../../../design/spec/CLAUDE.md) law: the repository and the canvas move together):
  - `html/main.html`: the pane list (`:5484`), its markup, the `settings` section of `renderVals()` (the `set` object it returns), and a `settings:xcode` start option (`:2438`; `applyStart` already routes `settings:<pane>`, `:2692`).
  - A new board `html/ScreenSettingsXcode.dc.html`, copied from `ScreenSettingsPermissions.dc.html`.
  - Its entry in `json/canvas.json` (page `build`), and `README.md`'s board count (53 today).
  - The contract map's row in `html/BuildContract.dc.html`.
  - The changed files published to the Apogee Desktop canvas, kept identical to the repository.

**Decisions made** (dated):
- 2026-10-31 — The user's call: the CLI SPEC gains `apogee acp`, the agent over ACP on stdio, with the same session core and agent loop as `execute` ([SPEC.md](../../../cli/assistant/SPEC.md), line 32). `serve` stays server-only, so Xcode's "Locally Hosted" chat provider pointed at `apogee serve` is not pursued. Nothing on this pane points at a port.
- 2026-10-31 — The user's call: the subscription backends (`claude-cli`, `codex-cli`, `gemini-cli`, `ollama-cli`) are offered to Xcode through `apogee acp`, as this app offers them and `serve` does not. The pane chooses no backend; which backends the agent offers is 56a's.
- 2026-10-31 — The user's call: no Xcode plug-in for now. People add Apogee by hand in Xcode's "Add an Agent" sheet, using the fields 56d prints, and the plug-in stays a ROADMAP idea. So this pane shows those fields and writes nothing into Xcode.
- 2026-10-31 — The user's call: the GUI queue holds 5a only. 5b, which would have marked chats begun in Xcode in the Sessions library, was proposed and not queued.
- 2026-10-31 — Specced from the Xcode research as GUI v0.2.1 track 5, "Apogee in Xcode, the app's side". It is gated on the CLI's 56d, which supplies every document the pane reads. 56d is itself gated on 56a.

**Open calls:**
- [default: a new pane, "Xcode" (symbol `hammer`), listed last in the app (after Permissions) and in the design (after Keyboard), and always listed. It is macOS-only by nature, as this app is. With Xcode absent, the pane says so in `check`'s words and offers nothing that needs Xcode. Recommended: always listing it is how a person finds the route.]
- [default: the pane's own reads (`mcp presets`, `config get mcp_servers.xcode`, `acp registration`) run when it is chosen and after each of its changes, never in `refresh()`, so the other panes' reads are unchanged. Its `check` rows come from the `checks` that `refresh()` already reads (`readCheck`). Health keeps its filter, so a failing Xcode row also appears there. Recommended: no second `check` run for one pane.]
- [default: the pane draws every row of `check`'s Xcode section, by the section name 56d records, ok rows included and each row's remedy kept. 56d's approval row (W3) appears here in its words. Recommended: take it.]
- [default: whether the preset is registered is read with `config get mcp_servers.xcode` (registered: its command; `no such config key`: not registered), never with `mcp list`. Recommended: take it, since `mcp list` connects and would raise Xcode's approval.]
- [default: Register runs `mcp create xcode --from xcode`: the argv of the preset row's own `register` line (`apogee mcp create <server> --from <preset>`, `mcp_presets.cpp:157-159`), built as Library's `register(_:as:)` builds it (`LibraryViewModels.swift:483-488`), with the server name the line gives. It is offered only when the row says `runnable`; otherwise the row's `missing` and `remedy` are shown. Remove is asked first, as Library's is (`:503`), then runs `config delete-mcp-server xcode`. Recommended: the preset's own line, never a server entry the app composes.]
- [default: the registration fields are drawn as the document gives them, each with Copy. Xcode's menu path is the document's `where` and the approval note its `approval` (56d's shape), so the app composes neither. The app's own words are only the labels. Recommended: take it.]
- [default: what an older binary lacks comes from its own answers. No `xcode` row in `mcp presets` means "this apogee ships no Xcode preset". A refused `acp registration` is said in its words, and the fields are not drawn. No Xcode section in `check` means "this apogee checks nothing of Xcode". A binary not yet read asserts nothing. There is no version or date compare. M16's `ConfigVerbs.has` (`lib/src/darwin/source/business/reads/ConfigVerbs.swift:18-20`) chose between two paths that both worked: `providers register` on a dated binary, and the `config add-backend` the app already ran on an older one. Here there is no older path to keep. There is no fallback the app composes, such as `mcp create xcode --command xcrun --args mcpbridge`: that would be the app's own copy of the preset, which the machine-mode client rule forbids. Recommended: the binary's own answers are enough.]
- (consumed decision) `acp`'s place in the shell's parity table (`tui_parity.cpp`, the CLI's [ADR 0010](../../../cli/adrs/the-shell-is-a-mode.md)) is 56a's. The app's refusals are the exec line's `kRefusals` (`tui_parity.cpp:96-120`), word for word (`refusalSet`, `ParityTests.swift:119-132`, which pins the app's own order: the doors, the `$EDITOR` verbs, then the carve-outs in the table's row order). So the app cannot refuse `acp` unless the exec line does. 56a's default is a carve-out refused as `serve` is, and [56d](../../../cli/backlog/v0.2.1/xcode-setup-from-apogee.md)'s keeps that prefix refusal for `acp registration`, adding to its reason that the fields print at a prompt and show in Settings › Xcode (both open calls). A runner-covered row, as `__mcp-tools` has, was the other outcome weighed.
- [default: the app mirrors whichever 56a records. **If the exec line refuses `acp` (56a's and 56d's default):** a carve-out row; the prefix match also refuses `acp registration`, and the reason names Settings › Xcode. Census: 24 screens, 4 carve-outs. **If 56a makes it runner-covered:** a screen row, `.screen(module: "PresentationSettings")`, its note naming Settings › Xcode for `acp registration` and saying an agent host spawns `acp` itself. No refusal is added, so the console runs `acp registration` as typed, and a bare `acp`, a child on the console's closed stdin, "would read nothing and end" (`Parity.swift:95-97`). Census: 25 screens, 3 carve-outs. Recommended: mirror 56a, never a refusal the exec line lacks.]
- [default: the design canvas draws the pane in this item, under lib/design/spec's law. The contract row names its five sources: `check`, `mcp presets`, `config get`, `mcp create --from` / `config delete-mcp-server`, and `acp registration`. Recommended: take it.]

**Guardrail(s).**
- **View-model tests over scripted reads (`XcodeSettingsTests`):**
  - the `check` document with and without Xcode rows;
  - the presets document with no `xcode` row, a row not runnable, and a runnable row;
  - `config get` registered and not;
  - `acp registration`, both as a document and as a refusal.
  - Register's and Remove's argv byte for byte, each read back, and the CLI's words shown.
  - The runner's log never holds `mcp list` or `mcp test`, and holds nothing outside the named verbs.
  - A binary not yet read asserts nothing.
- **Decoder tests** over the recorded fixtures, recorded from `apogee-dev` in a sandbox with the home written `/Users/someone`, as M16's were (the Executable field and any path in `check`'s Xcode rows included).
- **`ParityTests`:** the census as 56a's classification makes it, the `acp` row, `refusalSet` still equal to the exec line's set, and the `mcp`/`config`/`check` rows naming Settings › Xcode.
- **`ConsoleSafetyTests`:** if `acp` is refused, `acp` and `acp registration` join its `refused` table, so the root-option matrix covers them (`--release acp` among them). If not, `acp registration` joins the neighbours that run. Either way `--config acp` runs.
- **The layering check:** its preview rule (`lib/src/darwin/tests/architecture/layering/LayeringCheck.swift:31-33`) and the `PresentationSettings` scheme.
- **The design's start-screen check** (`lib/design/spec/CLAUDE.md`, "Checking a change") passes over `settings:xcode`.

**Acceptance criteria:**
- [ ] On a binary with 56d, Settings › Xcode shows `check`'s Xcode rows, the preset's state and the registration's fields, all in the CLI's words. Copy puts each field's value on the pasteboard exactly as printed, the Executable an absolute path.
- [ ] Register runs `mcp create xcode --from xcode` and Remove (asked first) runs `config delete-mcp-server xcode`. Each is read back by `config get` and its output shown. Showing the pane never starts `xcrun mcpbridge`.
- [ ] On a binary without these verbs, the pane says what that binary said it lacks, and draws no field or row it never printed.
- [ ] `ParityTable` classifies `acp` as 56a's row in `tui_parity.cpp` does, and the console refuses exactly what the exec line refuses. The fixture is refreshed from `apogee-dev`, and the full suite (`lib/scripts/cicd.sh --app darwin --test`) is green.
- [ ] Each state has a named preview over recorded documents: Xcode found, Xcode absent, an older binary, the preset registered, and Remove asked. Xcode absent is recorded where 56d's `check` can be made to find no Xcode; until then it has no preview (ADR 0004's rule for a state no recording shows).
- [ ] The prototype and its new board draw the pane, the contract map has its row, and the canvas matches the repository.
- [ ] The user looks at the pane in a development build of the app driving `apogee-dev` (GUI [M18](../maintenance/app-dev-channel.md); until M18 ships, ask first, per the GUI CLAUDE.md). Pasting the fields into Xcode's sheet, which registers `apogee-dev`'s absolute path as the development build prints it, never `apogee`'s, is the user's run.

**Scope note.** Earmarked for v0.2.1 (GUI track 5, "Apogee in Xcode, the app's side"). 🔒 Gated on the CLI's [56d](../../../cli/backlog/v0.2.1/xcode-setup-from-apogee.md) (cross-queue).

This item closes wall **W7**. W3, Xcode's trust tied to signing, is not fixed here; it is shown here in the CLI's words.

Its CLI siblings are [56a](../../../cli/backlog/v0.2.1/acp-agent-mode.md) (`apogee acp`), [56b](../../../cli/backlog/v0.2.1/acp-host-tools.md) (Xcode's tools in an ACP session), [56c](../../../cli/backlog/v0.2.1/acp-sessions-saved.md) (ACP sessions saved) and 56d (the documents read here).

Out of scope:
- writing Xcode's settings or its `~/Library/Developer/Xcode/CodingAssistant` folders;
- an Xcode plug-in, a Source Editor Extension, or signing (the ad-hoc decision stands);
- the headless `xcrun mcp-server`;
- Library › MCP servers, which still reads `mcp list` and so connects to every server, `xcode` included, as it does today;
- marking chats begun in Xcode in Sessions (5b, not queued);
- Linux and Windows, which have no Xcode.
