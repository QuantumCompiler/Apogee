# The app reads chat details, recall, dedupe, raw records and the footprint as documents

**What / why.** The CLI's M31 (shipped 2026-10-26, [Milestone AO](../../../cli/assistant/MILESTONES.md#milestone-ao--the-desktop-clients-cli-growths)) gave five prose reads a JSON face. The app reads each as text today, a fallback ADR 0006 tolerates:

| Read | Where the app reads it as text | What M31 gave it |
|---|---|---|
| **A saved chat's Details…** (4k) | `chats info <id>` as a command, `label: value` lines (`presentation/sessions/ViewModels/SessionLibraryViewModel.swift:958`) | `--output-format json`: `chats list`'s row plus every fact typed (`object: "chat.info"`) |
| **Recall** (4k's inspector section; Settings' Forget, `SettingsViewModel.swift:338`) | the count line, `recall holds N chat summaries in …` (`SessionLibraryViewModel.swift:1018`) | `--output-format json` lists the summaries themselves (`{data:[{chat, name, updated, summary}], object:"list", index, pending}`), so the person sees what Forget erases |
| **Graph's Dedupe…** (4k) | `graph dedupe --dry-run` read as prose for whether it names groups (`GraphViewModel.swift:287`) | the admin route's JSON body byte for byte: `groups`, `merged_nodes`, `threshold`, `dry_run` |
| **A record's raw conversation** (4k) | `knowledge info --raw` text, cut at `-- Raw conversation --` | `knowledge info --raw --output-format json` carries `raw` (or `null`) |
| **The Suites fit** (4i) | `models status --suite`'s `footprint` field, parsed from its text (`business/reads/System.swift:138–142`) | a typed `footprint` object beside it: `{suite, members:[{backend, roles, bytes, known, local, shares, note}], margin_bytes, total_bytes, budget_bytes, verdict}` |

**Core constraint(s).**
- **The binary's word decides,** typed where it is typed. The app stops parsing prose wherever the document answers.
- **An older binary keeps today's reading.** A refused `--output-format`, or a document without the new field, falls back to the text read, each fallback pinned by its existing recording.
- **Text-only commands stay commands.** Each read either runs as a JSON read (`ReadRunner`) on a binary that takes it, or as today's command; never both for the same answer.
- **ADR 0004:** each changed state has a preview over recorded documents.

**Seam + files.**
- **`business/reads/`:** decoders for `chat.info`, the recall listing, the dedupe body, `raw`, and the typed footprint. `SuiteFootprint` is built from the object when present, else from the text as today.
- **`presentation/sessions/`:** Details… and the inspector's Recall from the documents.
- **`presentation/settings/`:** Forget's confirm naming the summaries it erases.
- **`presentation/graph/`:** Dedupe… from the body's `groups`, Merge offered when it names any.
- **`presentation/knowledge/`:** the raw conversation from `raw`.
- **`presentation/suites/`:** the fit from the typed footprint.
- **Fixtures:** each JSON document recorded from the branch binary over a sandbox, the home written `/Users/someone`, beside the existing text recordings.

**Reference.**
- GUI 4i and 4k in [Milestone E](../../assistant/MILESTONES.md#milestone-e--the-audits-gui-findings).
- The CLI's M31 record: the shapes and its decisions, such as `roles` as an array and the exact bytes.

**Decisions made** (dated):
- 2026-10-26 — **Opened from the CLI and GUI Maintenance run's record** (the user's call: "spec them into the backlog and then execute them").

**Open calls:**
- [default: the app asks for JSON first on a binary whose machine schema is dated `2026-10-26` or later, and falls back to the text read on a refusal (exit 109, arguments not expected) whatever the date.]
- [default: Recall lists each summary's name and date, its text shown on selection; Forget's confirm says how many and names them.]

**Guardrail(s).**
- **Over recorded JSON:** each read draws its typed facts.
- **Over the existing text recordings:** each read draws as today.
- **The typed footprint and the text footprint** give the same verdict for the same suite.

**Acceptance criteria:**
- [ ] Details…, Recall, Dedupe…, the raw conversation and the Suites fit read the CLI's documents where the binary has them.
- [ ] Recall shows the summaries themselves, and Forget names what it erases.
- [ ] Older binaries read as today; previews, `check-previews`, the layering check and `lib/scripts/cicd.sh --test` pass, with no Swift warning.

**Scope note.** GUI Maintenance, claimable by name, no gate. The app's adoption of the CLI's M31.
