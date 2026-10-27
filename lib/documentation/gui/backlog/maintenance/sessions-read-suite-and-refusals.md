# Sessions and media read the suite and the refusals the CLI now sends

**What / why.** The CLI's M29 (shipped 2026-10-26, [Milestone AO](../../../cli/assistant/MILESTONES.md#milestone-ao--the-desktop-clients-cli-growths)) made the session opening and the media verbs tell a driver what they used to keep to stderr, or not say at all. The app was built before it and still works from its own fallbacks:
- **The suite.** `session` now carries `suite` (absent when none) and, for execute, `orchestrating`, with the vocabulary date `2026-10-26`.
  - **Today the app knows a session's suite only from what it launched** (4i's chooser and `--suite`), so a resumed chat's header can't say which suite it runs under. That was the CLI gap 4i recorded.
  - **Execute's banner** can't say whether the session orchestrates.
- **The suite's admission line and warnings** reach machine mode as `notice`s after `session`, where they went to stderr alone. 4i's switch writes its own line into the transcript, so the two may now say the same thing twice.
- **A model named with `-m` that can't start is refused at launch,** exit 1, its own problem on stderr. Before, the default answered under its label. The app launches with `-m` from Models' Use in a new session, from the settings sheet, and from 4i's suite switch, which drops `-m`. Each must draw the launch refusal as the failed session, in the CLI's words, with Try again (1g).
- **A runtime refusal now ends the job stream with `error`.** `video generate`, `video compose` and `voice say` with their runtime missing send the CLI's words as the stream's last event. 4f's `InstallConsent` reads the failure message and stderr together, but never the stream's `error`.
- **Compose's two-voice-models refusal names `--voice-model`.** The app's picker shows with two or more voice models; any words or fixture pinned to "name one with --model" for compose are the older binary's.

**Core constraint(s).**
- **The binary's word decides,** in its own words: the suite from `session`, the refusals from the stream or stderr.
- **An older binary keeps today's behaviour.** A `session` with no `suite` is read as "not said", never "none". The app's own knowledge of what it launched stays the fallback, and stderr stays the refusal's fallback where no `error` event arrives.
- **The protocol is tolerated additively** (the CLI's ADR 0006): unknown fields pass through, and nothing breaks on a binary announcing `2026-10-24` or `2026-10-25`.
- **ADR 0004:** each changed state has a preview over recorded documents.

**Seam + files.**
- **`data/protocol/ProtocolEvent.swift` and `EventDecoder.swift`:** the `session` event's optional `suite` and `orchestrating`.
- **`presentation/session/ViewModels/SessionViewModel.swift` and `Transcript.swift`:** the header's suite from `session` when said, else what the app launched. The suite notices drawn as notices, the app's own switch line not doubled. A launch refusal drawn as the failed state in the CLI's words.
- **`presentation/sessions/ViewModels/SessionLibraryViewModel.swift`:** the resumed session's suite in the inspector, and the execute banner's orchestrating.
- **`business/media/InstallConsent.swift`,** with the media view-models: the refusal read from the stream's `error` first, then stderr.
- **Fixtures:** recorded from the branch binary, which has M29:
  - a resumed chat under a suite (`session.suite`);
  - the suite notices;
  - a refused `-m` launch (its stderr);
  - a runtime refusal under `stream-json` ending in `error`;
  - compose's `--voice-model` refusal.

**Reference.**
- GUI 4i (the suite chooser and switch) and 4f (`InstallConsent`) in [Milestone E](../../assistant/MILESTONES.md#milestone-e--the-audits-gui-findings).
- M10's `chat_id` adoption in [Milestone A](../../assistant/MILESTONES.md#milestone-a--the-macos-application): the precedent for reading a new `session` field with a fallback.
- The CLI's `machine-mode.md` launch contract.

**Decisions made** (dated):
- 2026-10-26 — **Opened from the CLI and GUI Maintenance run's record** (the user's call: "spec them into the backlog and then execute them"). The placement follows the GUI's M11 and M12, GUI Maintenance.

**Open calls:**
- [default: where the CLI's suite notice and the app's own switch line say the same thing, the app keeps the CLI's and drops its own, on a binary that sends the notice.]
- [default: a launch refusal is drawn as 1g's failed state with Try again, its words the stderr tail's last line, as an older binary's failures already are.]

**Guardrail(s).**
- **Recorded streams:** the header and inspector say the suite on a resumed chat; execute's banner says it orchestrates; no line is doubled.
- **The refused `-m` launch** is drawn in the CLI's words, nothing answering under the model's label.
- **A runtime refusal's consent** comes from the `error` event, and an older recording still from stderr.
- **Every older recording** still decodes and draws as today.

**Acceptance criteria:**
- [ ] A resumed chat's window says the suite it runs under, and an execute session whether it orchestrates.
- [ ] A suite's admission and warnings appear once, in the CLI's words.
- [ ] A model that can't start is shown refused, never quietly replaced.
- [ ] The install consent works from the stream's `error`, and older binaries still work.
- [ ] Previews, `check-previews`, the layering check and `lib/scripts/cicd.sh --test` pass, with no Swift warning.

**Scope note.** GUI Maintenance, claimable by name, no gate. The app's adoption of the CLI's M29. Its siblings for M30–M33 are M14–M16.
