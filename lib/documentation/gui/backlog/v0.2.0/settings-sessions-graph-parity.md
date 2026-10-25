# Settings, Sessions, Graph and Library reach their remaining verbs

**What / why.** Under the full-parity law (gui SPEC; ADR 0003), the CLI/GUI audit (2026-10-22, three-lens verified) found seven screen-level gaps where a classified screen doesn't reach a verb its command owns. Each is console-reachable today, which is the law's floor, not its bar:

1. **Saved chats cannot be compacted, inspected or recalled from Sessions.** The app never runs `chats compact`, `chats info` or `chats recall` without `--forget`. Sessions covers list, title, delete and resume (`SessionLibraryViewModel.swift:215`, `:436`, `:489`, `:514`). Settings offers only Forget (`SettingsViewModel.swift:281-283`), so recall summaries can be erased without ever being seen. The TUI's Chats view compacts any saved chat on `c` (`tui_workbench.cpp:135-146`).
2. **A scanned provider cannot be registered from Settings.** Settings › Backends & keys lists providers (`SettingsView.swift:194-201`) with no row action. Its only registering control is the bootstrap button (`:218`), which also installs runtimes. The TUI registers one chosen provider on `r` (`tui_doctor.cpp:178-188` → `register_provider`, `providers_cmd.cpp:222`).
3. **No surface shows a backend's recorded vendor-tool consent (`server_tools`).** 47a's `server_tools` is recorded consent for a vendor to run tools under the user's key. No app source mentions it, and `check` has no `server_tools` section. A person cannot see which entries they consented for.
4. **Graph's export, dedupe and delete are unreached.** The screen covers stats, query, explain, update, build, the report and communities (`GraphViewModel.swift:71-171`). `graph export html|graphml|mermaid`, `graph report --out`, `graph dedupe` and `graph delete` have no button. The parity row (`Parity.swift:50`) is silent on them.
5. **Library can add an MCP server but never remove one.** `config delete-mcp-server <name>` is reached nowhere (`LibraryViewModels.swift:352-375`), though every other Library resource has a delete.
6. **Knowledge never shows a record's archived raw conversation.** `knowledge info --raw` prints it, but only in text mode (`knowledge.cpp:725`), and the app reads JSON only. Records never decodes `raw_ref` (`RecordsViewModel.swift:93`).
7. **Track-52 upgrades wait for Sessions, and Settings asserts something false.** Every track-52 capability flag (`streamsJobs`, `hasTrack52`, `SessionLibraryViewModel.swift:312-316`) is set only from a chat session's `session` event (`:463-465`), and a session starts only when Sessions is in view (`AppViewModel.swift:230`). A launch that restores onto another place runs a track-52 binary on fallbacks: pulls without stream-json, Compose unable to plan alone, and Settings saying "this apogee has no config set" while hiding Set, On/Off and Forget (`SettingsView.swift:334`, `:348`). The design's startup step 2 checks the vocabulary with `apogee __machine-schema` (`BuildContract.dc.html:62`). The app never runs it. That verb answers with no config or backend (`chat` does not).

**Core constraint(s).** **The screen reaches the verb**, every addition through the binary's own command with read-back. **Capabilities are known at launch**: the app reads `__machine-schema` once at startup (and on binary change) to set its capability flags, so no screen runs on a fallback against a capable binary, and no sentence asserts what the binary can't do without having asked. **Consent is visible**: an entry's `server_tools` are listed where its backend is shown, read through `config get` once CLI 55g makes it readable (via the admin view's fields or the config read until then). **Text-only reads are run as text**: the raw conversation comes from `knowledge info --raw` run as a command (the read runner's text path), or from `raw_ref` read through the CLI, never parsed by the app from private paths. ADR 0002.

**Seam + files.** `presentation/sessions/` (compact with confirm, info, the recall list), `presentation/settings/` (per-provider register; a backend's `server_tools`; capability-truthful wording), `presentation/app/ViewModels/AppViewModel.swift` (a startup `__machine-schema` read setting capabilities), `presentation/graph/` (export with a save panel, dedupe, delete with confirm), `presentation/library/` (MCP remove), `presentation/knowledge/` (the raw view), `presentation/shell/ViewModels/Parity.swift` (rows updated). Tests: argv pins per verb; capabilities set from a recorded schema fixture before any session; the Settings wording against capable and incapable fixtures; destructive confirms.

**Reference.** The gui SPEC's Full-parity principle, ADR 0003; the TUI's Chats and Providers keys; CLI 47a (`server_tools`), 52d/52e (compact, recall, `config set`); [CLI 55g](../../../cli/assistant/MILESTONES.md#milestone-ar--the-audits-cli-findings) (shipped 2026-10-25), referenced and not gated; the CLI/GUI audit (2026-10-22).

**Decisions made** (dated):
- 2026-10-22: Specced from the CLI/GUI audit (the user's call). The startup schema read supersedes per-session detection as the source of capability flags. Per-session detection (GUI MILESTONES:526, :565) stays as a refresh.

**Open calls:**
- [default: graph exports write through a save panel to a path the person picks, never a default location.]

**Guardrail(s).** Argv pins; fixture-driven capability and wording tests; confirms on every destructive act.

**Acceptance criteria:**
- [ ] Saved chats can be compacted, inspected and their recall reviewed; one provider registers from its row; `server_tools` consent is visible.
- [ ] Graph exports, dedupes and deletes; MCP servers can be removed; a record's raw conversation is viewable.
- [ ] Capabilities are known from launch, whatever place is restored; the full suite is green.

**Scope note.** Earmarked for v0.2.0 (track 4); ungated.
