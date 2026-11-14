# Structured output on every door: a schema per request, checked, corrected once, and refused out loud

**What / why.** The user's goal, 2026-11-14: other apps use Apogee as their AI back end, Taylor'd Portfolio (the user's SwiftUI job hunter) first. That app's engine asks a model for JSON and decodes it: `ClaudeCodeProvider` runs about five of its tasks that way (`generateJSON` into `CandidateProfile`, `JobMatchBatch`, `JobMatch`, `ExtractedPosting`, `PostingDetails`, …), through `ClaudeProcessClient`, which runs `claude -p <prompt> --output-format json` and decodes the envelope's `result` (read from its repo, 2026-11-14). Apogee already holds an answer to a schema well: it asks the provider natively where the provider can, validates every answer itself, and gives one correction. But that machinery is reachable only through an agent's files. This item puts a **per-request schema** on every door: `complete` (with a one-document `--output-format json` result like the reads), a machine-mode turn, and the OpenAI-compatible door. Each door runs the same machinery and reports the same verdict, and none ignores a schema it was sent.

**The wall this item closes: W4, no per-request structured output.** Re-measured 2026-11-14 at HEAD (`231fb049`; the CLI source is unchanged since the branch binary, `apogee 0.2.0 (b37b0a9f)`), in a scratch `APOGEE_HOME` with the `mock` backend and a second mock entry whose script echoes `{{response_schema:json}}` and `{{system:json}}` (`data/backends/mock.cpp:108-111`):

1. **`complete` takes no schema and prints no document.** `apogee complete -q -m mock --json-schema '{"type":"object"}' hi` exits `109`: "The following arguments were not expected: hi --json-schema". `--output-format json` exits `105`: "--output-format: expected 'text' or 'stream-json'" (`presentation/cli/complete.cpp:693-704`; the enum is `OutputFormat {Text, StreamJson}`, `presentation/machine/json_reporter.h:309-314`). The reads already have the one-document form: `ReadFormat::Json`, "A read prints one JSON document, never a stream" (`json_reporter.h:323-333`). Plain `complete -q -s … -m mock hi` printed `mock response`, rc `0`, and `--output-format stream-json` ended `{"finish_reason":"stop","model":"mock","text":"mock response","type":"result"}`. So the turn works; only the schema and the document are missing.
2. **The protocol's turn carries no schema, and its result carries no verdict.** The `user` line has one field, `text` (`presentation/machine/protocol.cpp:398-400`). `result` carries `text`, `model`, `finish_reason` and `usage` (`protocol.cpp:134`; written at `json_reporter.cpp:240-257`). Probed through the one surface that validates today, `analyze -m mock --prompt p.txt --schema s.json --output-format stream-json hi`: the answer did not conform, yet the `result` line was the plain one above. The verdict reached only stderr (`[apogee] the answer does not conform to the schema (conforms: false): /: the answer is not a JSON object`), and the exit code was `0`.
3. **The OpenAI door silently ignores `response_format`.** `chat_completions` reads `model`, `messages`, `tools`, `stream`, `temperature`, `max_tokens`, `system`, `tool_mode`, `apogee_events` and `session_id`, and nothing else (`presentation/httpserver/handler.cpp:659-686`). `response_format` appears nowhere under `presentation/`. Probed on loopback `--port 0` (killed after), every one of these answered `200`: `{"type":"json_schema",…}` with `"mock response"`; `{"type":"json_object"}`; `{"type":"nonsense"}`; the same streamed; and `response_format` on `/v1/completions`. Served from the echoing entry, a `json_schema` request answered `schema="" system=""`: the schema never reached the backend, and no prompt mentioned it. The same handler refuses `tools` with a 400 (`handler.cpp:666-670`), and its reference says why: "a request with `tools` is refused rather than silently ignored" ([http-api.md](../../../reference/cli/http-api.md):1626-1627; the 400 row, :103). The user's call of 2026-11-14 reverses that bullet's tools half ([60c](../v0.2.4/app-tools-on-every-door.md) makes an app's own tools work the standard way); its principle, honored or refused and never ignored, is the one this item applies to `response_format`. An unknown key (`"frobnicate":1`) is also ignored with a `200`.
4. **Structured output exists only behind `analyze`.** `agents create sumr` scaffolds a prompt, `schemas/sumr-output.json` and a config entry. `analyze --schema PATH` overrides it, but only with `--agent` or `--prompt PATH`; without either it exits `1`: "provide --agent <name> (see --list) or --prompt <file>". A non-conforming answer under `--json` prints `{"conforms": false, "errors": […], "raw": …}` (`presentation/cli/analyze.cpp:300-307`) and exits `0`.

**The machinery this item reuses, never copies** (all at HEAD):
- `agentloop::run_structured` (`business/agentloop/structured.h:70-89`, `structured.cpp:160-213`) sets `Options::response_schema`. That asks the schema of the provider on every request (`agentloop/loop.h:113-118`, carried at `loop.cpp:448` as `ChatRequest::Transient::response_schema`). It then extracts the answer's JSON (`extract_json`: fences and surrounding prose tolerated) and validates it as draft-07 (`validate_against`, collecting every violation). A miss gets **exactly one** corrective turn carrying the validator's message (`correction_message`). A second miss comes back raw with `conforms: false`: "never dropped, never silently passed" (`structured.h:12-21`). The string overload keeps the author's property order for a grammar (26f, `structured.h:78-89`). `nlohmann::json_schema::json_validator` appears in exactly one source file, `structured.cpp`.
- Native modes, already wired per provider:
  - OpenAI's Responses API: `text.format` `json_schema`, `strict` off, alongside tools (`data/backends/openai_wire.cpp:194-209`).
  - Anthropic: `output_format` where the model supports it, with its beta header (`anthropic_wire.cpp:271-280`, `anthropic.cpp:376-380`), else one forced tool whose input is the answer.
  - Gemini: `responseSchema`, on a tools-less request only (`google_wire.cpp:183-196`).
  - llama.cpp: a grammar on the tools-less pass (`llamacpp.cpp:1070-1076`).
  - MLX: the prompt alone, said once (`mlx_local.cpp:1362-1368`).
  - The local backends add an `OUTPUT FORMAT` block unless one is already there (`local_prompt.cpp:11-40`).
  - The vendor CLIs (`claude_cli.cpp`, `codex_cli.cpp`, `gemini_cli.cpp`, `ollama_cli.cpp`) never read `response_schema`. The schema reaches them only in words, which is why `analyze` appends `agentloop::schema_instruction` to the system prompt for every backend (`analyze.cpp:241-245`).

This item delivers the **per-request schema** on three doors:
- `apogee complete --json-schema SCHEMA`, with a single-document `--output-format json`.
- An optional `schema` on a machine-mode `user` line, whose `result` carries the verdict.
- `response_format` on `POST /v1/chat/completions`, honored for `json_schema` and `json_object`, a no-op for `text`, and **refused with a 400** for anything else. It is never ignored.

Every door runs `run_structured` and appends `schema_instruction`. A backend with a native mode holds the answer that way, and every other backend gets the prompt; either way the answer is validated, corrected once, and then either delivered or failed clearly. It is ungated and useful locally today: Taylor'd Portfolio's "ask for JSON, decode" tasks map onto `apogee complete --json-schema … --output-format json` over pipes, with no server.

**Core constraint(s).**
- **Remote use is an add-on (the anti-goal).** Nothing here may make standalone use need a server, a network, a pairing or an account. `complete --json-schema` and a driven `chat` turn with a `schema` work over pipes with no `serve`, no device registry and no TLS identity. The OpenAI door gains the same behavior; it is not the door the feature lives behind.
- **One source of truth** ([SPEC.md](../../../assistant/cli/SPEC.md) → Principles: "One agent loop … shared by every surface"). The three doors call `agentloop::run_structured` and `agentloop::schema_instruction`. No door gets its own validator, extractor, retry or correction text. The JSON Schema validator stays in `structured.cpp` alone, and a check holds that (Guardrails).
- **Refused out loud, never silently ignored** (the OpenAI door's own rule, [http-api.md](../../../reference/cli/http-api.md):1626-1627; [60c](../v0.2.4/app-tools-on-every-door.md) rewrites that bullet's tools half, by the user's call, and keeps the rule). A `response_format` the door cannot honor (an unknown `type`, a schema that is not a valid draft-07 schema, or one on `/v1/completions`) is a 400 that says why. A request with no `response_format` is byte-identical to today's.
- **Older versions keep working** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md); [machine-mode.md](../../../reference/cli/machine-mode.md):285-290, "Growth is by addition only"). `--json-schema`, the `json` word, the `user` line's `schema`, the `result`'s verdict fields and `capabilities.structured_output` are all additions. `protocol_version` stays `1`, `capabilities.schema` (the vocabulary's date) moves, and the verdict fields appear only on a turn that carried a schema. The protocol ignores an unknown inbound key, so an older binary would drop a `schema` without saying so. The session's `capabilities.structured_output` is how a driver knows before it sends one, and a `result` without `conforms` is how it knows after.
- **Parity** ([ADR 0002](../../../adrs/cli/mode-parity.md); [ADR 0010](../../../adrs/cli/the-shell-is-a-mode.md)). The per-request schema answers from the CLI (`complete`), the protocol (`chat`/`execute` driven, and through it 59b's network connection) and HTTP (`response_format`). Two places get carve-outs with their reasons: the interactive `chat` and the shell, because a person typing does not hand a turn a JSON Schema, and `acp`, because ACP's prompt request has no schema field to carry one.
- **Secrets are unleakable by construction** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → Secrets are unleakable by construction). A schema and an answer are request data, never secrets, but they never reach a log either. serve's request logger stays method, path and status (`presentation/httpserver/serve.cpp:180-184`). The validator's messages name JSON pointers and the schema's own words, nothing from the config.
- **Serving = server** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → Interactive turns never open a listening socket). No new socket, route or listener. `complete` and the machine mode stay pipes.
- **The layers** ([ADR 0001](../../../adrs/cli/layer-enforcement.md), [ADR 0008](../../../adrs/cli/module-map.md)). The machinery stays in Business (`agentloop`). The doors' parsing and rendering sit in Presentation (`cli`, `machine`, `httpserver`). No new module.
- **UTF-8 where it enters** (CLI CLAUDE.md → Text is UTF-8 where it enters). A schema arriving from a flag, a driver line or a request body is mended before it is validated or echoed in a message. So is the model's raw answer before a document or an error body carries it.
- **The dev channel** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel)). Any live pass runs `apogee-dev` over `~/.apogee-dev`, never the release `apogee` or `~/.apogee`.

**Seam + files.**
- `source/business/agentloop/structured.h/.cpp`: `run_structured`'s behavior is unchanged. It gains one entry for a schema given by a caller, `parse_request_schema(std::string_view text)`, used by all three doors. It parses the text, requires an object, checks it with `validate_schema`, bounds its size, and returns the schema text as written (the 26f overload keeps its order) or the validator's reason.
- `source/presentation/cli/complete.cpp`:
  - `--json-schema SCHEMA`: inline JSON, or a path to a `.json` file.
  - `--output-format` takes `json` as a third word on `complete` only (`chat`, `execute` and `task` keep refusing it).
  - With a schema, the system prompt ends with `schema_instruction`, and both run paths call `run_structured`: the terminal one (`:547`) and the machine one (`:352`).
  - A schema given with `--tools` is allowed; the loop's rule already holds the schema to the final, tools-less pass where a provider needs that.
  - A `--json-schema` that fails `parse_request_schema` exits `1` (`kUserError`) before any model call.
- `source/presentation/machine/json_reporter.h/.cpp`:
  - The `result` event gains `json`, `conforms`, `attempts` and `errors`, present only when the turn carried a schema.
  - `MachineCapabilities` gains `structured_output`.
  - The `user` line's parse (`json_reporter.cpp:645-646`) reads an optional `schema` into `DriverMessage`.
  - The one-document `json` form for `complete` is the `result` event's object without `type`.
- `source/presentation/machine/protocol.cpp`: `kUserLine` gains the optional `schema` (object); `kResult` gains the four verdict fields; the capabilities definition gains `structured_output`; the `schema` date moves. `apogee __machine-schema` prints them, and `cli.machine_schema_conformance` holds them.
- `source/presentation/cli/chat_session.cpp` (the driven loop's `user`-line check, `:1971`) and `chat_turn.cpp` (`:340`): a `user` line with a `schema` runs that turn through `run_structured`, with `schema_instruction` added to that turn's system prompt only. The correction is durable history, as `run_structured` already records it (`structured.cpp:189-192`).
- `source/presentation/cli/analyze.cpp`: its `--output-format stream-json` `result` carries the verdict through the same emit path, closing item 2's gap. Its `--json` document keeps its shape, and its exit code stays `0`. Changing `analyze`'s exit code is out (below).
- `source/presentation/httpserver/handler.h/.cpp`:
  - `chat_completions` reads `response_format`:
    - `text` is today's prose.
    - `json_object` is the schema `{"type":"object"}`.
    - `json_schema` takes `json_schema.schema`, accepting `name`, `description` and `strict`. `strict` is moot, because Apogee validates every answer, and is not passed through.
    - Anything else is a 400 naming the accepted types.
  - `run_turn` (`:374`; its `agentloop::run` call at `:550`) calls `run_structured` when a schema is set.
  - The non-streamed answer's `content` is the conforming JSON, re-serialized compactly.
  - A streamed answer is held until its verdict: the live `apogee_events` meta-frames still flow, then the conforming JSON goes out as content chunks. A non-conforming final goes out as the existing streamed error frame.
  - `completions` (`:760`) refuses `response_format` with a 400 naming `/v1/chat/completions`.
- Docs:
  - [http-api.md](../../../reference/cli/http-api.md):
    - the `response_format` row and example;
    - two error rows, the 400 for a refused `response_format` and the 502 for an answer that did not conform after its correction;
    - a dated line in "What this server does not do": `response_format` is honored or refused, never ignored.
  - [machine-mode.md](../../../reference/cli/machine-mode.md):
    - `user.schema`, with an example;
    - the verdict fields on `result`;
    - `capabilities.structured_output`;
    - `complete --output-format json`'s document.
  - `machine-schema.json`, regenerated.
  - `complete --help`.
- Tests (hermetic: a temp `APOGEE_HOME`, the mock backend with scripted turns, handlers driven through `Mux::dispatch` with no port wherever a port is not the point):
  - `tests/business/agentloop/structured_test.cpp`: `parse_request_schema`'s refusals (not JSON, not an object, not a valid schema, over the size bound) and its order-keeping return.
  - `tests/presentation/cli/complete_structured_test.cpp`:
    - a scripted mock answering prose, then JSON: conforms, `attempts: 2`, rc `0`;
    - one answering prose twice: rc `2`, the verdict on stderr, an empty stdout in text mode, and the document's `conforms: false` with `text` raw under `json`;
    - the echoing mock proves `{{response_schema:json}}` and the `OUTPUT FORMAT` block both reached the request;
    - a bad `--json-schema` exits `1` with no model call.
  - `tests/presentation/machine/machine_mode_test.cpp`: a `user` line with a `schema`, then one without, in one session. The verdict appears only on the first `result`, `capabilities.structured_output` is `true`, and the schema covers only its own turn.
  - `tests/presentation/httpserver/handler_test.cpp`: the `response_format` matrix covers `text`, `json_object`, `json_schema` conforming, conforming after one correction, and failing twice (the 502); an unknown `type` and an invalid schema (400s); streamed (no first attempt's text in any chunk, and the error frame on failure); `/v1/completions` (400); and a request without `response_format` byte-identical to today's.
  - `tests/scripts/sh/machine_mode_e2e.sh` and `serve_e2e.sh` gain the real binary's case, with serve on loopback `--port 0`.

**Reference.**
- In-house precedents:
  - 26f, the schema held by a grammar on the tools-less pass, with the author's order kept.
  - The 2026-09-13 decision recorded in `structured.h`: a native JSON mode lowers the retry rate and never replaces the check.
  - `analyze`'s schema block and run (`analyze.cpp:241-245`, `:681-690`).
  - The reads' one-document `--output-format json` (27j, 28h).
  - The knowledge clerk's "returned no record" as a 502 `backend_error` ([http-api.md](../../../reference/cli/http-api.md):109), the precedent for a model that could not produce the structure asked of it.
  - `ExitCode::kBackendError`, "The provider failed: transport, API rejection, malformed response" (`presentation/cli/helpers.h:53-65`).
- External prior art:
  - OpenAI's Chat Completions `response_format` (`json_schema` with `name`/`schema`/`strict`, and `json_object`), the shape this door accepts. Its divergence: Apogee validates every answer itself and corrects once, whatever `strict` says.
  - `claude -p --output-format json`, which Taylor'd Portfolio decodes today, with its result under `result`. Apogee's document keeps the names of its own `result` event (`text`, `json`) rather than that envelope's, so one shape serves every Apogee door. The mapping is for the Swift package and the guide.
- Siblings:
  - [59a](paired-devices.md), paired devices.
  - [59b](machine-mode-over-the-network.md): the `user` line's `schema` rides the network connection unchanged.
  - [59c](many-sessions-at-once.md), lanes per model.
  - [59d](reads-and-commands-over-the-network.md): `complete --output-format json` is a command a paired credential may run.
  - [59f](paired-apps-with-scopes.md), an app's scope.
  - [59g](capability-discovery.md), what an app may use.
  - [60c](../v0.2.4/app-tools-on-every-door.md), an app's own tools on both doors.
  - [60d](../v0.2.4/building-an-app-on-apogee.md), the integrator's guide.
  - [60e](../v0.2.4/maintaining-the-app-integration.md), the integration's ADR and contributor rules, which name this door's growth-by-addition.
  - darwin [7d](../../darwin/v0.2.4/apogeekit-swift-package.md), ApogeeKit.

**Decisions made** (dated):
- 2026-11-14: Specced from the apps-as-clients spike. The user's calls that shape it:
  - Taylor'd Portfolio uses the full harness, not only model calls.
  - Apps reach Apogee through both doors: the OpenAI-compatible endpoints for plain model calls, and Apogee's own protocol (stdio locally, the paired connection remotely) for the harness.
  - The CLI SPEC amendment is written ([SPEC.md](../../../assistant/cli/SPEC.md) → Core surfaces, item 4, "Revised 2026-11-14").
  - Each paired app is held to a scope the person approves, and never reaches files, shell or other apps' data unless granted ([59f](paired-apps-with-scopes.md)). A schema rides inside that scope and widens nothing (the default below).
  - On the OpenAI door, an app's own tools work the standard way (Apogee returns `tool_calls`, the app runs them, the turn continues), within its scope. This reverses serve's recorded rule "It does not run tools the client defines" ([http-api.md](../../../reference/cli/http-api.md):1626-1627), and [60c](../v0.2.4/app-tools-on-every-door.md) changes the reference with it. The half this item relies on, a field the door cannot honor is refused and never ignored, stands.
  - Apogee ships a Swift package other apps embed, factored from the Mac app's client core, which the Mac app depends on too (darwin [7d](../../darwin/v0.2.4/apogeekit-swift-package.md)). It decodes this item's verdict; it does not re-validate.
  - Taylor'd Portfolio's own side is not specced now.
  - Standing law: remote use is an add-on, and the CLI alone and the Mac app over pipes stay complete and need no server.
- 2026-11-14: Placed in v0.2.3, track 59, as 59h, **ungated** (accepted with "execute the spike"). It needs no pairing, scope or network: the local door is useful today, and the OpenAI door's change is a fix to its own rule.
- 2026-11-14: One machinery. `agentloop/structured` is reused at every door, never a second copy. Native structured output where the backend has it, the prompt plus the validator everywhere else, and the validator always.
- 2026-11-14: On the OpenAI door, a `response_format` is honored (`json_schema`, `json_object`), a no-op (`text`), or refused with a 400. Silently ignoring it, as the probe saw, is the defect this item closes.

**Open calls:**
- [default: `--json-schema SCHEMA` takes inline JSON when the value starts with `{`, else a path to a file. It is bounded at 64 KiB, and the same bound applies to a driver line's and a request's schema. `analyze`'s `--schema PATH` is unchanged.]
- [default: with `--json-schema` and the text format, stdout carries the conforming JSON, compact, and nothing else, so `| jq` works. On failure, stdout is empty and stderr carries the validator's errors and the raw answer, cut to 4 KiB. The exit code is `2` (`kBackendError`, "malformed response", worth retrying unchanged), so no new exit code is minted.]
- [default: `complete --output-format json` prints one document, the `result` event's object without `type`: `text`, `model`, `finish_reason`, `usage` when reported, and with a schema `json`, `conforms`, `attempts` and `errors`. It prints the document on failure too (`conforms: false`, `text` raw), with exit `2`. Without a schema it is the plain answer's document, useful on its own.]
- [default: the corrective turn is part of the same turn on every door. It is one `result`, one HTTP response and one lane occupancy under [59c](many-sessions-at-once.md). The deltas of both attempts stream on the protocol, each attempt in its own `answer_start`/`answer_end`, as `analyze` streams today.]
- [default: a non-conforming final on the OpenAI door is a 502 `backend_error`, as the knowledge clerk's empty record is. The message names the first validator errors, and the error object adds `apogee_errors` and `apogee_raw`. A stock SDK's own retry policy may retry it; each retry is a fresh request with its own correction.]
- [default: on the protocol, a non-conforming final is still a `result` (`conforms: false`), never an `error`. The turn happened and its history holds it, and the stability promise of exactly one `result` or `error` per `user` line holds.]
- [default: the conforming `content` on the OpenAI door is the parsed object re-serialized compactly, without fences or prose. Key order is not part of JSON's meaning, and a client decodes it as it decodes OpenAI's.]
- [default: structured output is **not** a scope class. It is a shape of a turn, not a capability, so an app's scope ([59f](paired-apps-with-scopes.md)) gates the backend and the tools as for any turn, and nothing more.]
- [default: the vendor-CLI backends take the prompt path in this item. Passing a vendor CLI's own schema option would be per-process, while `claude_cli` keeps one persistent child per session; that is its own item if wanted.]
- [default: no per-backend structured-output fact is recorded. Every backend can carry a schema (natively, by grammar, or by the prompt) and every answer is validated, so there is no backend that "cannot"; `capabilities.structured_output` on the session says the door accepts one. [59g](capability-discovery.md) consumes this: its conditional `backends.structured_output` field is not added.]
- [default: when `response_format` and an app's own `tools` meet on the OpenAI door, the schema governs the final answer only; a response that ends in `tool_calls` has nothing to check. The handler keeps the two fields independent so [60c](../v0.2.4/app-tools-on-every-door.md), which consumes this rule, composes them.]
- (consumed decision) The Swift API over the verdict (a `Decodable`-typed call, say) is darwin [7d](../../darwin/v0.2.4/apogeekit-swift-package.md)'s, and the integrator-facing write-up is [60d](../v0.2.4/building-an-app-on-apogee.md)'s.

**Guardrail(s).**
- A source check fails if `json_schema::json_validator` (or the `json-schema.hpp` include) appears outside `business/agentloop/structured.cpp`. One validator, mechanically.
- The handler matrix pins every `response_format` outcome: honored, no-op, 400 and 502. A conformance case pins that a request without `response_format` answers byte-identically, streamed and not.
- The streamed case pins that no chunk carries a first, discarded attempt's text.
- `cli.machine_schema_conformance` holds the new fields, and they appear only on a turn that carried a schema.
- A test drives `complete --json-schema` with no registry, no TLS identity and no server present, and it answers (the anti-goal).
- `leak_test.cpp` extended: with a schema set and `-v`, serve's log lines carry no schema text and no answer text.

**Acceptance criteria:**
- [ ] In a sandbox home with a scripted mock, `apogee complete -m mock --json-schema s.json --output-format json hi` prints one document with `conforms: true` and the parsed object under `json`. When the script answers prose first, the document says `attempts: 2`. When it answers prose twice, it says `conforms: false` with the raw `text`, and the exit code is `2`.
- [ ] `apogee complete -q -m mock --json-schema '{"type":"object"}' hi | jq .` succeeds on a conforming script. A malformed schema exits `1` naming the validator's reason, with no model call.
- [ ] A driven `apogee chat --output-format stream-json` session announces `capabilities.structured_output: true`. A `{"type":"user","text":…,"schema":{…}}` line ends in a `result` carrying `json`, `conforms` and `attempts`, and the next plain `user` line's `result` carries none of them.
- [ ] `apogee serve` on loopback: `response_format` `json_schema` and `json_object` answer conforming JSON, or a 502 after one correction; `{"type":"nonsense"}` and an invalid schema are 400s; `response_format` on `/v1/completions` is a 400. Served from the echoing mock, the request shows the schema reached the backend.
- [ ] A stock OpenAI client library's structured-output call (`response_format` `json_schema`) against `apogee serve` with the mock decodes the answer unmodified.
- [ ] `analyze --output-format stream-json`'s `result` carries the verdict.
- [ ] http-api.md, machine-mode.md and `machine-schema.json` say all of the above. The full suite is green on all five targets.
- [ ] Optional live pass, said when skipped: on the dev channel, `apogee-dev complete --json-schema` against one installed model per family ([the model families](../../../assistant/cli/DEVELOPER.md#on-real-weights-the-model-families)), measuring how often the answer conforms on the first attempt and after the correction.

**Scope note.** Earmarked for v0.2.3 (track 59), ungated. Out of scope:
- Taylor'd Portfolio's own side: its `LLMProvider` engine over Apogee is not specced now (the user's call, 2026-11-14).
- A schema on `task run` (a task's goal and acceptance are its own contract), on `acp` and on `mcp serve`.
- A vendor CLI's own schema option (a recorded default).
- Changing `analyze`'s `--json` shape or exit code.
- Refusing the OpenAI door's other unhonored standard fields (`n`, `stop`, `seed`, `logprobs`, …). The probe shows the handler ignores every key it does not name, and a sweep of them is its own item if wanted.
- Scopes, discovery and an app's own tools (59f, 59g, 60c).
