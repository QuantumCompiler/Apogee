# Stdio machine mode: structured JSONL event stream for front-end drivers

**What / why.** The GUI's contract with the harness (decided 2026-08-24): when the CLI runs — including when the GUI runs it — output routes **directly through stdin and stdout, never over localhost**. A GUI therefore powers Apogee by spawning the executable as a child process and speaking a machine-readable protocol over its pipes, which this item builds: `--output-format stream-json` on `complete` and `chat` emitting the typed event stream the Reporter seam already carries — answer tokens, thinking deltas and token estimates, tool start/result, status notices, and a terminal result object with usage/session metadata — as JSONL on stdout, plus `--input-format stream-json` so a persistent `chat` child accepts JSONL user messages on stdin (one process per session, many turns). This is precisely the contract Apogee consumes *from* the claude CLI (see the appendix of [claude-cli-backend.md](claude-cli-backend.md)), now offered from the other side — those design notes double as the emitter's spec: persistent child per session, mid-turn message injection, clean shutdown that drains queued output, resume by session id, diagnostics on stderr only. A front-end gets everything the terminal renderer gets, as data, with no HTTP layer in between. Mutating operations need no second protocol: the GUI shells out to the same CLI commands (`apogee config …`, `apogee auth …`, `apogee models …`) — the CLI **is** the parity contract, which is why this mode replaces any localhost control plane for local front-ends.

**Core constraint(s).**
- Machine mode is pipes only: it opens no sockets, ever (the lsof test extends here) — the strengthened interactive-=-pipes rule: local front-ends included, serving reserved for genuine server deployments
- The JSONL vocabulary mirrors the Reporter seam 1:1 — no second event vocabulary is invented; the terminal renderer, the future sseReporter, and this jsonReporter are sibling adapters over one seam
- In machine mode stdout carries only JSONL; every diagnostic goes to stderr (the same discipline Apogee demands of the vendor CLIs it drives)
- Thinking events are distinctly typed so a driver can discard them; thinking never appears in the result text or persisted history (harness-wide rule)
- The event schema is documented and versioned; drivers are told to ignore unknown event types (the tolerance we practice as a CLI consumer, granted to our own consumers)

**Seam + files.** lib/src/cli/source/commands/json_reporter.cpp (Reporter→JSONL adapter, sibling of cliReporter), `--output-format`/`--input-format` flags on complete.cpp and chat.cpp, a stdin JSONL reader for driven chat sessions (mutex-guarded single writer on the child side mirrors the notes' process model), an event-schema reference doc shipped with the repo, lib/src/cli/tests/commands/machine_mode_test.cpp (exec-style: drive the real binary over pipes with a mock backend; fixture round-trips; an adversarial-chunk consumer test proving events parse identically at any read granularity; the lsof no-LISTEN assertion).

**Reference (Ommi).** No stdio machine mode existed in Ommi — its GUI direction ran through HTTP. Nearest analog: serve's sseReporter meta-frames (a Reporter→wire adapter, the sibling pattern this reuses). The in-repo template is the adopted claude-CLI design notes: Apogee emits what it expects to consume, so the notes' reader/framing/testing discipline applies symmetrically to the emitter.

**Decisions made** (dated):
- 2026-08-24 — User rule: CLI (and GUI-driving-the-CLI) output routes over stdin/stdout, never localhost; serving applies only when the executable runs on a server and a remote client (mobile/desktop) makes REST calls. This item is that rule's enabling work, and the GUI project gates on it.

**Open calls:**
- [default: include — wire AskFn to a question event on stdout answered by a message on stdin, so a driving GUI renders native question/permission dialogs instead of the tool being silently unavailable] ask_user and the write-tool permission gate over the machine protocol (agentloop's "nil AskFn ⇔ never advertised" rule extends naturally: a machine-mode driver that declares interactivity provides the AskFn)
- [user] Does the GUI need anything beyond event streaming + shelling to CLI commands (e.g. a push-style config-changed notification), or is poll-after-mutate acceptable for v1 of the GUI contract?
- [default: mirror the claude CLI's event naming where the concepts align] JSONL schema naming, so anyone who has driven claude can drive apogee

**Guardrail(s).** The adversarial-chunk consumer suite (1-byte/3-byte/4KiB/whole-stream reads yield identical event sequences); a schema-conformance test pinning every emitted event type against the documented schema; the machine-mode lsof assertion in CI; a grep test that stdout in machine mode contains only `{`-opening lines.

**Acceptance criteria:**
- [ ] `apogee complete --output-format stream-json` emits the full typed event stream as JSONL and nothing else on stdout; stderr carries all diagnostics
- [ ] A driven `chat` child serves multiple turns over stdin JSONL without respawning; closing stdin drains queued output and exits cleanly with the terminal result emitted
- [ ] Thinking arrives as distinctly-typed events a driver can drop; the result object's text contains no thinking (test-locked)
- [ ] A reference driver script (test fixture) reconstructs the identical conversation from the JSONL that the terminal renderer displayed
- [ ] Zero listening sockets in machine mode (lsof, CI)

**Scope note.** gated ring (ring convention: assumes the complete v0.1.0 set; no ring-internal gate — it rides the agentloop Reporter seam). The GUI sibling project gates on this item. Out of scope: the GUI itself, and `serve` (which remains the *server-deployment* surface — see [serve-public-plane.md](serve-public-plane.md)).
