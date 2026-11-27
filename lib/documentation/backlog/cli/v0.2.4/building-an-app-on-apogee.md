# CLI-60d · Building an app on Apogee: the guide and the compatibility promise

| Field | Value |
|---|---|
| **Type** | 🛠️ Task |
| **Project** | CLI |
| **Status** | 🔒 Blocked |
| **Fix version** | v0.2.4 |
| **Component** | Reference docs (`reference/cli/apps.md`) and its conformance check |
| **Labels** | `docs` `app-integration` `compatibility` `conformance` |
| **Epic** | Track 60 · one tool definition and device tools |
| **Blocked by** | [59g](../v0.2.3/capability-discovery.md), [60c](app-tools-on-every-door.md), [darwin 7d](../../darwin/v0.2.4/apogeekit-swift-package.md) |
| **Blocks** | [60e](maintaining-the-app-integration.md) |
| **Reporter** | The user's goal and the apps-as-clients spike (2026-11-14) |
| **Created** | 2026-11-14 |

---

## Summary

Tracks 59 and 60 and darwin 7d build what an app needs to use Apogee as its AI back end, but nothing is written for app builders and nothing promises that what they build keeps working. This item writes the guide, `reference/cli/apps.md`, states the compatibility promise, and adds a conformance check that runs the guide's examples against the binary.

## Description

### User story

**As** an app developer using Apogee as my app's AI back end (the first being Taylor'd Portfolio),
**I want** one guide that tells me which door to use, what my app may reach and how to use each capability, with examples that are known to run, and a stated promise of compatibility,
**so that** I can build on Apogee without reading its internals, and what I build keeps working as Apogee grows.

### Background

The user's goal, 2026-11-14: other apps use Apogee as their AI back end, the first being Taylor'd Portfolio (QuantumCompiler/Taylor-d-Portfolio), the user's SwiftUI job hunter for macOS. Tracks 59 and 60 and darwin 7d build what such an app needs: a paired app held to a scope ([59f](../v0.2.3/paired-apps-with-scopes.md)), capability discovery ([59g](../v0.2.3/capability-discovery.md)), structured output on every door ([59h](../v0.2.3/structured-output-on-every-door.md)), an app's own tools ([60c](app-tools-on-every-door.md)) and ApogeeKit, the client core as a Swift package ([darwin 7d](../../darwin/v0.2.4/apogeekit-swift-package.md)). None of that is useful to an app builder until it is written down for them, and nothing promises that what they build keeps working.

This item delivers three things:

- **The guide**, `reference/cli/apps.md`. It covers pairing and scope, which door to use, discovery, structured output, an app's own tools, tasks, retrieval, the Swift package with a worked example, local against remote, and errors.
- **The compatibility promise**: the protocol grows only by addition, and the package's version is tied to the protocol's schema date.
- **A conformance check** that runs the guide's examples against the binary, as `cli.http_api_conformance` holds the HTTP reference to the route table.

### Evidence

**W7 — nothing is written for integrators.** This is the apps-as-clients spike's W7 (2026-11-14). Re-verified at HEAD `231fb049`, with probes on the branch binary (`apogee 0.2.0 (b37b0a9f, 2026-11-09, macos-arm64)`), a scratch `APOGEE_HOME`, the `mock` backend, and `serve` on loopback `--port 0`, killed after:

1. **The references are written for Apogee's own app and for stock OpenAI clients.** `reference/cli/` holds five contracts: `acp.md`, `http-api.md`, `machine-mode.md`, `tools.md` and `training.md`. None is an app-building guide. Machine mode is "Apogee's contract with a front-end. A GUI powers Apogee by spawning the executable…" ([machine-mode.md](../../../reference/cli/machine-mode.md):3-6). Its worked example is a Python driver for a GUI author, `reference_driver.py` (:8-10). The HTTP API is "Apogee's contract with a remote client", and "It is not for local front-ends" ([http-api.md](../../../reference/cli/http-api.md):3-7). Neither page says which door an app should pick, what an app may reach, or how an app finds out what it may use.
2. **Only the machine door has a stated version, and no package exists.** [ADR 0006](../../../adrs/cli/backwards-compatibility.md) promises that the machine protocol "grows additively" within a `protocol_version` (:14). It is enforced by `cli.machine_schema_conformance` and by `__machine-schema`, the schema shipped in every archive. The machine door announces its vocabulary's date as `capabilities.schema` ([machine-mode.md](../../../reference/cli/machine-mode.md):101) and `x-apogee.schema` (:601-602). The probe printed `{'protocol_version': 1, 'schema': '2026-11-01'}`. The HTTP door states no version. [http-api.md](../../../reference/cli/http-api.md) has no versioning section (its `##` sections are at :13, :92, :115, :242, :293 and :1611), and `GET /health` answers `{"status": "ok"}` (:240). The probe's `/v1/models` and `/v1/chat/completions` responses carried only `Content-Type`, `Content-Length` and `Keep-Alive`, with no version header. `git ls-files | grep Package.swift` is empty: there is no package to promise anything about.
3. **What the references say today would teach an app the wrong thing.** The tool rules, "**A client cannot send `tools`**" ([http-api.md](../../../reference/cli/http-api.md):143) and "It does not run tools the client defines … refused rather than silently ignored" (:1626-1627), are reversed by 60c on the user's call. In the probe, a `tools` request got `400` "client-side tools are not supported: the server runs its own tool loop and returns only the final answer (tool_mode selects it)" (`presentation/httpserver/handler.cpp:666-671`). A `response_format: {type: json_schema, …}` request got `200` and plain `"mock response"`, with no error. The handler reads `model`, `messages`, `tools` (to refuse it), `stream`, `temperature`, `max_tokens`, `system` and `tool_mode` (`handler.cpp:661-676`), and never reads `response_format`. The only code that writes a schema is outbound, to a provider (`data/backends/openai_wire.cpp:194-209`, from a request's `transient.response_schema`, which agents' output schemas fill); nothing carries a served request's field there. 59h fixes that. Until the guide exists, an app that asks for JSON this way gets no warning.
4. **What already works, which the guide will show.** `apogee complete -q -s "Answer in JSON" -m mock "rank this job"` printed `mock response`, rc 0. `complete -m mock --output-format stream-json hi` ended on `{"finish_reason":"stop","model":"mock","text":"mock response","type":"result"}`. `--output-format json` was refused: "expected 'text' or 'stream-json'", rc 105. `complete --help` lists `--rag COLLECTION` and no schema flag. The spike's probes (2026-11-14) add these: `--rag` answers after `embed ingest`, `task run` takes `--output-format stream-json`, `--allow`, `--agent` and `--tools`, `agents create` scaffolds a prompt with an output schema, and a request with `Origin: https://evil.example` gets `403`.
5. **Apogee's own precedents for examples that are tests check no example's output.** `reference_driver.py` is "the **worked example** a GUI author reads … And it is the **test** that the example stays true" (`tests/scripts/py/reference_driver.py:4-9`; registered as `cli.reference_driver`, `tests/CMakeLists.txt:1247-1256`). `http_api_conformance.cmake` pins every route to a heading in both directions and refuses to run against an empty list (`:1-11`, `:41-46`), but it never runs a request. The app already gates features on the schema date: `ProseReads.documentsSince = "2026-10-26"` and `ProseReads.asksDocuments(vocabulary:)` (`lib/src/darwin/source/business/reads/DocumentOrText.swift:9-24`), over `MachineSchema.schema` read from `__machine-schema` (`data/protocol/MachineSchema.swift:11-13`, `:36-38`). The Mac app's real-binary pass runs on the mock backend in a throwaway home, and is skipped, with a reason given, when the machine has no install (`lib/src/darwin/tests/business/session/RealBinaryTests.swift:8-21`).
6. **The shape the worked example follows.** Taylor'd Portfolio was read at its HEAD `ce04df4`; it is the model for the example, and none of its code is used. `LLMProvider` (`lib/src/Data/LLM/LLMProvider.swift:16-87`) is a task-oriented protocol: `buildProfile`, `rank` (a batch and one job), `extractPosting`, `enrichPosting`, `cleanPostingText`, `tidyDocument`, `refineSummary`, `buildTargetBrief`, `generateApplication` (with grounding) and `searchJobs`. Its Claude engine asks for JSON and decodes it. `generateJSON` appends a JSON-only suffix and fails with `decodingFailed` on anything else (`ClaudeCodeProvider.swift:127-145`). The process client runs `claude -p <prompt> --output-format json` (`lib/src/Infrastructure/LLM/ClaudeProcessClient.swift:59-60`) and strips code fences from the reply (`:92`). Everything sits behind a `TextGenerating` port, `generate(prompt:instructions:) -> String` (`TextGenerating.swift:15-18`). The guide's worked example is an engine of that shape over ApogeeKit: typed tasks answered with structured output instead of "ask and hope", grounding by retrieval over a collection, and a long search run as a task.

## Acceptance criteria

- [ ] `reference/cli/apps.md` exists, with the twelve chapters in order. The router, `reference/README.md`, and the intros of `machine-mode.md` and `http-api.md` point at it.
- [ ] **Given** a sandbox home with the mock backend, **when** an app builder follows the guide, **then** they get, from the binary: a local plain completion, a local structured answer that decodes, a retrieval-grounded answer after `embed ingest`, a local `task run` stream, a paired app's discovery answer listing only what its scope grants, a scope refusal, a served structured answer, and a served `tool_calls` round trip. Each one is a checked block, and `cli.apps_guide_conformance` passes on macOS and Linux.
- [ ] **Given** the guide, **when** any expected block in it is changed, **then** `cli.apps_guide_conformance` fails, naming the block.
- [ ] The guide states the compatibility promise in plain words, and no ADR is edited (60e records it).
- [ ] The Swift chapter quotes `JobMatchEngine.swift` exactly. The package's example test passes hermetically, and against an installed binary on the mock backend where one exists.
- [ ] **Given** a home with no registry and no TLS identity, **when** the local chapter runs, **then** it passes: a local example in the guide never needs `serve`, a pairing or a network.
- [ ] Optional live pass, said when skipped: the user follows the guide's local chapter and its remote chapter against `apogee-dev serve` on the dev channel, from a scratch Swift app that depends on ApogeeKit.

## Constraints

- **Remote use is an add-on (the anti-goal).** Nothing here may make standalone use need a server. The guide's first chapter is the local door: an app on the same Mac spawns `apogee` and speaks over its pipes, with no `serve`, no pairing, no TLS identity and no network. The conformance check runs every local example in a home where `devices.json` and the TLS directory are absent. The guide never tells a local app to start `serve` or to reach `localhost`.
- **Serving = server** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → Interactive turns never open a listening socket). The guide follows the references' rule that a local front end never talks to a port ([http-api.md](../../../reference/cli/http-api.md):3-7). The check runs `serve` only on loopback `--port 0`, inside its own sandbox, and kills it when its examples end. It never binds a non-loopback address.
- **Secrets are unleakable by construction** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → Secrets are unleakable by construction). An app's token is returned once and kept by the app: in the Keychain for a Swift app (7d's), or in the app's own secret store. The guide says so and shows no other place. Its examples use a fixed placeholder token that cannot be a real one. The check pairs its own app in the sandbox to run the remote examples, never prints that token, and fails if the guide's text contains anything shaped like a live token other than the placeholder.
- **The protocol grows only by addition** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)). The promise the guide states is ADR 0006's rule applied to its new counterparties: apps, the HTTP door and ApogeeKit. ADRs are append-only ([the ADR index](../../../adrs/cli/README.md):3), so this item edits no ADR; the record that restates the rule for outside apps is [60e](maintaining-the-app-integration.md)'s ADR 0013, built after this. Meanwhile the guide is a reference doc, so what it states is already a "behavior contract named in records" under ADR 0006 (:16): changing it later takes a dated revision, never a quiet edit.
- **Parity** ([ADR 0002](../../../adrs/cli/mode-parity.md)). Each capability is shown on every door that has it: the CLI line, the machine-mode line and the HTTP request. Where a door lacks a capability, the guide says which door and why. It never implies that one door is the full product.
- **Nothing links the CLI executable** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → Nothing links the CLI executable). An app spawns or connects; it never links the binary or the core. The guide says so, and the worked example depends on ApogeeKit alone.
- **The layers** ([ADR 0001](../../../adrs/cli/layer-enforcement.md), [ADR 0008](../../../adrs/cli/module-map.md)). This item changes no CLI module. It adds a document and a test script. The Swift example sits beside ApogeeKit as a consumer of its public products, with no UI framework import, under 7d's layering check.
- **The dev channel** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel)). Any live pass runs `apogee-dev` over `~/.apogee-dev`. The check and the package's test use a throwaway `APOGEE_HOME`, never `~/.apogee`.

## Technical approach

| File | Change |
|---|---|
| `lib/documentation/reference/cli/apps.md` (new) | **Building an app on Apogee**, the guide, in twelve chapters (listed below). It links to the two contracts rather than copying them. |
| [machine-mode.md](../../../reference/cli/machine-mode.md) | The intro gains one line: building an app? start at `apps.md`. |
| [http-api.md](../../../reference/cli/http-api.md) | The intro gains the same line. Its "What this server does not do" (:1611) gains a dated line saying the HTTP door is now versioned by the same schema date, if 59g's answer carries it (Open questions). The tool rules (:143, :1626-1627) are 60c's to revise, not this item's. |
| `lib/documentation/CLAUDE.md` (the router's contracts line, :24) | The guide named among the integrator-facing contracts. |
| `lib/documentation/reference/README.md` | The guide named among the integrator-facing contracts. |
| `lib/src/cli/tests/scripts/py/apps_guide_conformance.py` (new) | The conformance check (Testing). |
| `tests/CMakeLists.txt` | Registers the script as `cli.apps_guide_conformance`, under the same guard as `cli.reference_driver` (`if(UNIX)`, `Python3_Interpreter_FOUND`). |
| The worked example, in ApogeeKit's tree where 7d puts the package (consumed) | An example target holding `JobMatchEngine.swift` and its test (Testing). |

The guide's chapters, in order:

1. *Which door.* The OpenAI-compatible door for plain model calls from any OpenAI client library. Apogee's own protocol for the full harness: stdio locally, the paired connection remotely. ApogeeKit for Swift apps. A decision table maps "I want to…" to a door.
2. *Local first.* An app on the same machine spawns `apogee`; no server is involved. Whether a scope applies there is 59f's (consumed): as specced, 59f leaves a local stdio app unscoped, running as the person as the Mac app does, and the chapter says so plainly, so an app builder knows the scope is the remote door's.
3. *Pairing an app and its scope.* 59f's verbs, the scope's parts (backends, collections, tools, commands, kinds of task), approval, listing and revoking by name, and where the token lives.
4. *What may I use.* 59g's discovery: the models, collections, tools and recipes this credential may use, and the schema date.
5. *Structured output.* 59h on each door: the CLI's flag, the machine-mode field and `response_format`. Shows the refusal when a schema cannot be honored.
6. *An app's own tools.* 60c: the standard `tool_calls` round trip on the OpenAI door and its equivalent on Apogee's protocol, within the scope. Contrasted with 60b's device tools, which are the CLI's own tools run by a device.
7. *Retrieval.* `embed ingest` into a collection the scope grants, `--rag` on a turn, and `?retriever=` on a served request.
8. *Tasks.* `task run` locally, a served task that outlives the connection (59e), attaching from a cursor, and the grants a task starts with.
9. *The Swift package.* Adding ApogeeKit, opening a connection (local or paired), and the worked example: a `JobMatchEngine` of the Taylor'd Portfolio shape. It returns typed `extractPosting` and `rank` results through structured output, grounds `generateApplication` in a collection, and runs `searchJobs` as a task.
10. *Local against remote.* The same code over either connection, what changes (the token, the pin, the scope), and what stays the same.
11. *Errors.* The OpenAI error envelope ([http-api.md](../../../reference/cli/http-api.md):92-100), machine mode's `error` event and a refused launch ([machine-mode.md](../../../reference/cli/machine-mode.md) → A launch refused, :228), a scope refusal (59f), a revoked token, a dropped connection, and the rule that Apogee refuses rather than silently ignores.
12. *The compatibility promise.* Stated in plain words (the Open questions' default), with links to the ADR and the schema.

The guide links to the two contracts rather than copying them. A rule stated twice drifts.

**No ADR edit.** ADRs are append-only; the rule for outside apps, with `cli.apps_guide_conformance` and the package's example test in its Enforcement, is [60e](maintaining-the-app-integration.md)'s new ADR 0013, which cites ADR 0006 as the rule it extends.

**Reference.**

- In-house precedents:
  - `reference_driver.py`, a worked example that is also its test (`cli.reference_driver`).
  - `http_api_conformance.cmake`, a document pinned to the code in both directions, refusing to pass vacuously.
  - `schema_conformance.py` and `__machine-schema`, the protocol as a shipped artifact with a date.
  - `ProseReads.documentsSince` (`DocumentOrText.swift`), a client gating a feature on the schema date, not on a release number.
  - `RealBinaryTests.swift`, a real-binary pass on the mock backend, skipped and said when there is no install.
  - [ADR 0006](../../../adrs/cli/backwards-compatibility.md), additive growth, and behavior contracts in the reference docs that hold until a dated revision says otherwise (:16).
- External prior art:
  - The OpenAI SDKs' client-side function-calling round trip (`tools`, `tool_calls`, a `tool` message), which 60c adopts and the guide shows unchanged.
  - Rust's doctests and Python's `doctest`: a document's examples run as tests. This item diverges by comparing whole outputs after normalization, not interleaved REPL lines, because Apogee's examples are commands and HTTP exchanges.
  - Semantic versioning for the package. This item diverges by making the schema date, not the package's number, what decides compatibility with a binary. The number says which package release; the date says which vocabulary.

**Docs when it ships:** Release notes: one line for the guide and the promise.

## Testing

- **`cli.apps_guide_conformance`** (`lib/src/cli/tests/scripts/py/apps_guide_conformance.py`, new). It reads `apps.md`, collects every checked block (marked as in the Open questions' default), and runs each one in order against the built binary. It uses a fresh `APOGEE_HOME` with the `mock` backend registered, as `serve_e2e.sh:54-55` does. The local blocks run with no registry or TLS identity present. For the remote blocks it pairs an app with 59f's scriptable mint, starts `serve --bind 127.0.0.1 --port 0`, and kills it after. Each block's output is compared with its paired expected block, after placeholders (ids, times, ports, the token) are normalized. It fails if no checked blocks are found, if a chapter in the required list has none (the Swift chapter is checked by quoting, below), or if the guide contains a token-shaped string other than the placeholder.
- **The worked example's test** (beside `JobMatchEngine.swift`, in ApogeeKit's tree). It runs hermetically against a scripted fake connection every time. Against the installed binary on the mock backend in a throwaway home, it runs only where an install exists, and is skipped with a reason otherwise, the way `RealBinaryTests.swift:8-21` does. The guide quotes `JobMatchEngine.swift` byte for byte, and `cli.apps_guide_conformance` checks the quote against the file (a text comparison, so it runs on every target).

Guardrails:

- **Every checked block runs.** `cli.apps_guide_conformance` runs every checked block and fails on any mismatch. A mutation check, run once while building: change one expected block and confirm the test goes red.
- **No vacuous pass.** Zero checked blocks, or a required chapter with none, fails.
- **The anti-goal.** It runs the local chapter's blocks with no `devices.json` and no TLS directory in the home, then with both unreadable. They still pass.
- **No token leaks.** It fails on any token-shaped string in the guide other than the placeholder, and it never writes the sandbox's minted token to its own output.
- **Loopback only.** `serve` runs only on `127.0.0.1 --port 0` inside the check's sandbox and is killed in a `finally` path. A leaked server process fails the test.
- **The quote is exact.** The quoted Swift example equals its file byte for byte.
- **Pinned contracts.** `cli.http_api_conformance` and `cli.machine_schema_conformance` stay green, so the guide links to contracts that are themselves pinned.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Where does the guide live, and what is it called? | `[default]` | The guide is `reference/cli/apps.md`, titled "Building an app on Apogee". It sits under `cli/` because both doors are the CLI's contract. ApogeeKit's own API documentation (its README or DocC) lives with the package (7d's), and the guide links it. |
| How is a checked block marked, run and compared? | `[default]` | A checked block is a fenced block preceded by `<!-- apps-check: <name> -->`, with its expected output in the next fenced block, marked `<!-- apps-expect: <name> -->`. A block's language says how it runs. `sh` runs in the sandbox shell with `apogee` on `PATH`. `http` is a raw request sent to the sandbox server. `jsonl` is lines written to a spawned machine-mode child, and the expected block is the events it writes back, with unknown fields ignored per [machine-mode.md](../../../reference/cli/machine-mode.md)'s rule 1. Normalized: ids, timestamps, ports, durations, the token (to `apa_PLACEHOLDER`) and the version line. Prose-only illustrations stay unmarked and must not look like a command (no `$` prompt). |
| What is the compatibility promise, as the guide states it? | `[default]` | (1) Within `protocol_version` 1, both doors grow only by addition: new routes, events, fields and inbound lines. Never a changed meaning, never a removal. An app must ignore what it does not know. (2) The vocabulary's version is its schema date, announced by `__machine-schema`, by `capabilities.schema` on every session, and remotely by the discovery answer. (3) ApogeeKit names the date it speaks (`ApogeeKit.schema`). It works against any binary announcing that date or a later one. Against an earlier binary, a feature that needs a later date is reported as unavailable with the date it needs, never a crash or a silent fallback (the `documentsSince` pattern). (4) A breaking change is a `protocol_version` 2, a major package version and a dated revision with a migration. It is never shipped silently. |
| Which ADR records the promise for outside apps? | `(consumed decision)` | [60e](maintaining-the-app-integration.md)'s ADR 0013, a new record citing ADR 0006, since ADRs are append-only. This item states the promise in the guide only; 60e's record takes its wording from here. |
| Where does the HTTP door announce its schema date? | `[default]` | The `schema` field of 59g's discovery answer. If 59g ships without it, this item adds it there as an additive field and pins it in `cli.apps_guide_conformance`. Whether discovery carries it is 59g's to decide first. |
| How is the Swift example checked? | `[default]` | Twice. The package's test runs it (always hermetically, and against an installed binary where there is one). The CLI check compares the guide's quote with the file. A CI with no install says the real-binary half was skipped, as `darwin.yml`'s build step already does for the app. |
| How does the guide show the OpenAI door, and which of it is checked? | `[default]` | The guide shows the OpenAI door with `curl` and the Python `openai` package's request shape as text. Only the `curl` lines are checked, since the check does not install any client library. |
| What is the guide's placeholder token, and what does the token-shape scan match? | `[default]` | The placeholder is `apa_PLACEHOLDER`, which no real token can be: an app's token is `apa_`-prefixed ([59f](../v0.2.3/paired-apps-with-scopes.md)), a device's `apd_` ([59a](../v0.2.3/paired-devices.md)), and both are hex after the prefix. The check's token-shape scan is `ap[ad]_[0-9a-f]{16,}`; if 59f or 59a settles a different prefix, the placeholder and the scan follow it. |
| The pairing verbs, the scope's vocabulary, how an app's token is minted for a script, and whether a local stdio app is held to a scope | `(consumed decision)` | [59f](../v0.2.3/paired-apps-with-scopes.md)'s |
| The discovery answer's route, fields and machine-mode form | `(consumed decision)` | [59g](../v0.2.3/capability-discovery.md)'s |
| The structured-output flag, field and refusal | `(consumed decision)` | [59h](../v0.2.3/structured-output-on-every-door.md)'s |
| An app's tools on each door, and the dated revision of [http-api.md](../../../reference/cli/http-api.md):143 and :1626-1627 | `(consumed decision)` | [60c](app-tools-on-every-door.md)'s |
| ApogeeKit's location, its products, how it is tagged and versioned, and where its example target lives | `(consumed decision)` | [darwin 7d](../../darwin/v0.2.4/apogeekit-swift-package.md)'s |
| A remote task's grants and its attach | `(consumed decision)` | [59e](../v0.2.3/tasks-outlive-the-connection.md)'s |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-14 | Specced from the apps-as-clients spike (probes on the branch binary `b37b0a9f`, a scratch `APOGEE_HOME`, the `mock` backend). The user's calls that shape it follow. | — |
| 2026-11-14 | Taylor'd Portfolio uses the full harness (models, retrieval over its documents, autonomous tasks on the server), so the guide covers the harness, not only model calls. | the user |
| 2026-11-14 | Apps reach Apogee through both doors: the OpenAI-compatible endpoints for plain model calls, and Apogee's own protocol (stdio locally, the paired connection remotely) for the harness. The guide's first job is saying which to choose. | the user |
| 2026-11-14 | Each paired app is held to a scope the person approves, and it never reaches files, shell or other apps' data unless granted. The guide teaches the scope before any capability. | the user |
| 2026-11-14 | Apogee ships a Swift package, factored from the Mac app's client core, that other apps embed and the Mac app depends on too. The guide's Swift chapter is written against it. | the user |
| 2026-11-14 | The CLI SPEC amendment is approved and written ([SPEC.md](../../../assistant/cli/SPEC.md) → Core surfaces, item 4, "Revised 2026-11-14"). | the user |
| 2026-11-14 | On the OpenAI door, an app's own tools work the standard way, within the app's scope. This reverses serve's recorded "does not run tools the client defines"; that revision is 60c's, and the guide describes the result. | the user |
| 2026-11-14 | Taylor'd Portfolio's own side is not specced now. The worked example is Apogee's own code of that shape, and none of the app's code is used. | the user |
| 2026-11-14 | Remote use is an add-on: the CLI alone and the Mac app over pipes stay complete and need no server. | the user |
| 2026-11-14 | Placed in v0.2.4 (track 60), after 60c, as 60d; [60e](maintaining-the-app-integration.md), the integration's ADR and contributor rules, follows it. It is gated on 59g, 60c and darwin 7d: the guide documents what they build, so it comes after them. 59h and 59e sit in v0.2.3, whose table precedes this one, so they are built first in table order. If either is still pending when this item is taken, the item waits rather than documenting a shape that does not exist. | the spec |
| 2026-11-14 | The guide is one page for every app, not one per language. A non-Swift app uses the OpenAI door with its own client library, or the protocol directly, generating its types from `machine-schema.json`. Only Swift gets a package (the user's call above). | the spec |
| 2026-11-14 | Every example the guide shows is either run against the binary or quoted from a file that is itself tested. An untested example is not allowed in the page. That is the spike's W7 lesson: a reference written today would already be wrong about `tools` and `response_format`. | the spec |

## Out of scope

- Taylor'd Portfolio's own side (its engine over ApogeeKit, its settings, its migration off `claude -p`), not specced now.
- Client packages for other languages.
- Any new capability. Where the guide finds a gap, the gap becomes its own item, not a change made here.
- Hosting the guide anywhere but the repository.
- The design of scopes, discovery, structured output, app tools and the package themselves, which belong to their siblings.

## Links

- **Blocked by:** [59g](../v0.2.3/capability-discovery.md), [60c](app-tools-on-every-door.md), [darwin 7d](../../darwin/v0.2.4/apogeekit-swift-package.md) · **Blocks:** [60e](maintaining-the-app-integration.md)
- **Built before it in table order:** [59h](../v0.2.3/structured-output-on-every-door.md) and [59e](../v0.2.3/tasks-outlive-the-connection.md) (v0.2.3, whose table precedes this one); the item waits if either is still pending.
- **Follow-on:** [60e](maintaining-the-app-integration.md), the integration's ADR 0013 and contributor rules, which records this guide's promise and names its conformance check.
- **Relates to (siblings):**
  - [59f](../v0.2.3/paired-apps-with-scopes.md), paired apps with scopes.
  - [59g](../v0.2.3/capability-discovery.md), capability discovery.
  - [59h](../v0.2.3/structured-output-on-every-door.md), structured output on every door.
  - [60c](app-tools-on-every-door.md), an app's own tools on every door.
  - [darwin 7d](../../darwin/v0.2.4/apogeekit-swift-package.md), ApogeeKit.
  - [59a](../v0.2.3/paired-devices.md), pairing, TLS and the pin.
  - [59b](../v0.2.3/machine-mode-over-the-network.md), the machine mode over the network.
  - [59c](../v0.2.3/many-sessions-at-once.md), many sessions at once.
  - [59d](../v0.2.3/reads-and-commands-over-the-network.md), reads and commands.
  - [59e](../v0.2.3/tasks-outlive-the-connection.md), tasks that outlive the connection.
  - [60a](one-tool-definition.md), one tool definition.
  - [60b](device-tools.md), device tools.
  - [darwin 7a](../../darwin/v0.2.4/app-connection-local-or-server.md), the app's connection seam, which ApogeeKit carries.
  - [darwin 8a](../../darwin/v0.2.5/app-builds-for-ios-and-ipados.md), the code built for iOS, the platforms the package must build for.
  - [60e](maintaining-the-app-integration.md), the integration's ADR 0013 and contributor rules, which records this guide's promise and names its conformance check.
  - [darwin 7e](../../darwin/v0.2.4/maintaining-apogeekit.md), the same duty for the package's side.
- **Relates to (records):** [ADR 0006](../../../adrs/cli/backwards-compatibility.md), [ADR 0002](../../../adrs/cli/mode-parity.md), [ADR 0001](../../../adrs/cli/layer-enforcement.md), [ADR 0008](../../../adrs/cli/module-map.md), [SPEC.md](../../../assistant/cli/SPEC.md) → Core surfaces, item 4.
