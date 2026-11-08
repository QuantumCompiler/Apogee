# The server runs many sessions at once

**What / why.** The user's vision, 2026-11-07: Apogee as a private, self-hosted harness that feels like Claude on every device the person owns. One strong machine (512 GB of memory, say, and a finely tuned large model) serves the person's desktops and phones. A phone, a Mac and an iPad each have a session open, and an autonomous task runs. Today the server answers them one at a time. Measured 2026-11-07, at `4f92316d`, with the branch binary over a scratch `APOGEE_HOME` and two `mock` backends whose script pauses 400 ms per chunk:

**W3 — serve answers one inference at a time.**

1. **One lock for the whole server.** "Inference requests serialise behind one mutex" (`presentation/httpserver/handler.h:31-34`). It is `turn_mutex_` (`handler.h:164`). It is held "from the first model call to the persisted result" (`handler.cpp:374-376`), so it covers the tool calls in between, and `/v1/completions` takes it too (`handler.cpp:784`, `:815`). The reference says so: "The server serves **one inference at a time**. A second chat request waits for the first" ([http-api.md](../../reference/http-api.md):33-34). Milestone T chose it: "Per-backend concurrency is a later item; a second request waiting beats a first one corrupted" ([MILESTONES.md](../../assistant/MILESTONES.md):4591).
2. **Even two different backends wait.** One request alone took 0.85 s. Two at once, to `slowa` and `slowb`, finished at 0.83 s and 1.64 s. Seventeen at once across the two finished one after another: the last at 13.76 s, 0.81 s apart. `/health` answered in 0.02 s meanwhile, so the waiting is the lock, not the listener. Re-run the same day at the same commit: one request alone 0.81 s; two at once, to `slowa` and `slowb`, 0.81 s and 1.62 s.
3. **Below the lock, each model already has its own.** A loaded llama.cpp base is driven by one thread at a time (`SharedBase::use`, `data/backends/llama_bases.h:60-68`), and a turn runs under it (`llamacpp.cpp:1622-1626`). An MLX child takes one request at a time "and a served deployment may ask from several threads" (`mlx_local.h:638-640`). So the server lock adds nothing for two local models on different bases. For the cloud API backends it is the only thing in the way, and those clients "were never audited for concurrent use" (`handler.h:33-34`). The Harness's own state (`providers_`, `held_`, the turn observer: `business/harness/harness.h:404-420`) has no lock.
4. **A second process-wide gate.** Every member call (a `consult`, a verifier, a symphony stage) passes `member_call_gate()` (`business/agentloop/member_call.cpp:24-28`, held at `:177`). Two sessions' consults wait on each other even on different backends.
5. **The waiting turn is told nothing.** A second streamed request got its first frame (the empty `role` delta) at once, then silence for the length of the first turn. No meta-frame says it is waiting: the vocabulary has no such word (`harness::StatusEvent::Type`, `data/contracts/types.h:367-389`; http-api.md:149-160). A non-streamed request just hangs. Nothing bounds the wait either: all seventeen waited, and none was refused.
6. **Nothing prices a second model before loading it.** Neither `serve_cmd.cpp` nor any `httpserver` file calls `price_suite`, `admission_refusal`, `machine_budget` or `offload_memory_total` (grep, 2026-11-07). With `--all-backends`, a second local model loads on its first request. 26a fits only its window to free memory (`llamacpp.cpp:989-1013`). An idle model frees itself only at its own next request (`expire_if_idle`, `llamacpp.h:353-357`), so a model past its idle window still holds its memory while another loads. The arithmetic that would say "does not fit" exists, for suites (`data/modelstore/footprint.h`, 27e/53d/M25; `presentation/cli/suite_residency.h`).
7. **A network session would load its own copy.** 59b runs each network session as a child `apogee chat` or `execute` in machine mode, owned by `serve` ([59b](machine-mode-over-the-network.md), its 2026-11-07 decision). llama.cpp runs in-process, so each child with a local model loads the weights again. 59b hands this item the call: "whether a local model is loaded once and shared across session children" (its open calls).

This item lets the server run many sessions at once:

- **Scheduling per backend.** A lane per loaded model, with a bounded queue and a fair turn order. A local model is shared, and its sessions' calls take turns on it. Cloud backends run side by side.
- **Memory honesty.** Before a model loads beside others, the footprint check runs. It frees what has idled out, waits for memory a busy model will free, and refuses with the arithmetic what can never fit.
- **The waiting turn is told.** A `queued` status with its place in line, on every surface that carries status.
- **One copy of each model.** A model channel through which 59b's session children use the server's resident models instead of loading their own.

The HTTP inference plane gets all of this the day it ships, with no pairing and no network session needed.

**Core constraint(s).**
- **The anti-goal: standalone use never needs a server** (the user's call, 2026-11-07; [SPEC.md](../../assistant/SPEC.md):31 and GUI [SPEC.md](../../../gui/assistant/SPEC.md):34). The scheduler is composed only by `serve`. `apogee chat`, `execute`, `complete`, `task`, `acp`, the shell, and the Mac app over pipes compose none, and stay byte-identical: no lane, no queue, no pre-load check, no model channel. A process with one session has nothing to wait for. The full suite, unchanged, is the proof.
- **Interactive = pipes, serving = server** ([CLAUDE.md](../../assistant/CLAUDE.md) → Interactive turns never open a listening socket; SPEC.md:66). Nothing new listens. The model channel is a pipe pair between `serve` and its own child, never a loopback port. A child never reaches `serve`'s HTTP plane. `no_listen_symbols.cmake` and `cli.complete_opens_no_listening_socket` stay green.
- **Secrets are unleakable** (CLAUDE.md → Secrets are unleakable by construction). A `queued` status names the backend and the place in line. It never names another session, its device, its id or its content. The model channel carries requests and answers, never a key. A session child that uses it reads no credential, because its providers are the parent's.
- **Additive** ([ADR 0006](../../adrs/backwards-compatibility.md)). A stock OpenAI client works as before: a stateless request is still one request, and the error at the bound is the OpenAI envelope. The `queued` meta-frame is sent only with `apogee_events`. The machine-mode `queued` event is a new type a host ignores (machine-mode.md:214). The new `/v1/model/status` fields are new fields.
- **Parity** ([ADR 0002](../../adrs/mode-parity.md)). One scheduler in Business. The HTTP plane, 59b's sessions (through the channel) and 59e's served task take their model calls through it, and the admin plane's model-calling jobs too: graph builds and community summaries run on worker threads today, outside `turn_mutex_` (`admin_graph.cpp:825-832`, `jobs.cpp:242`). The standalone modes are the skip, named: one session, nothing to schedule.
- **The four layers** ([ADR 0001](../../adrs/layer-enforcement.md)). A provider says what it can run at once through a capability in `contracts/provider.h`, discovered by the Harness, never a cast (CLAUDE.md → Capability probes never leak a cast). The pricing stays in `modelstore/footprint`: "composition, not new math" (`footprint.h:18-25`). The lanes live in Business. The frames and errors live in Presentation.
- **A failure fails one turn** (57c). A call that dies on its lane frees the lane for the next. A client that hangs up while waiting leaves the line, through the turn's cancellation, as a streamed client's disconnect already cancels its turn.
- **The dev channel** (CLAUDE.md → [Development runs on the dev channel](../../assistant/CLAUDE.md#development-runs-on-the-dev-channel)). Tests are hermetic. A live pass names `apogee-dev serve` over `~/.apogee-dev`, never the release `apogee` or `~/.apogee`.

**Seam + files.**
- `data/contracts/provider.h`: a `ConcurrentCalls` capability. `lane()` returns a key and a width. A llamacpp provider answers its base's key (53d's `SharedBaseKey`, so two entries on one base share a lane) and width 1, its one context. An `mlx` provider answers its child and 1. The HTTPS API providers answer their entry and the server's cloud width. A provider that does not answer gets its own entry's key and width 1, the safe default. The `mock` provider takes its width from a top-level `"parallel"` in its script (default 1, so every existing test is unchanged).
- `data/contracts/types.h`: `StatusEvent::Type::Queued`, phase `Start` when a call waits and `Done` when it gets its lane. Its fields: `position` (calls ahead of it), `name` (the backend) and `detail` (`busy` or `memory`).
- `business/harness/lanes.h/.cpp` (new, in the `harness` module): `Lanes`. Each lane is a FIFO per caller, served round robin across callers, with at most its width running and a bound on waiting. It reports `Queued` through the call's `on_status` and again when the position changes. A cancelled waiter leaves its place. It is held for **one model call**, never across a tool call, a permission wait or a member call. So a session waiting on its person's answer holds no model, and a turn that consults a member on its own base cannot deadlock on its own lane.
- `business/harness/harness.h/.cpp`: `Harness::set_lanes(Lanes*)` and `lane_for(backend)`. With lanes set, `chat`, `stream_chat`, `embed` and `count_tokens` take the backend's lane. With none set, they run as today. The concurrency audit: `held_`, the turn observer (`heard`) and the residency hold go under one lock. The router and `providers_` are read-only after construction, and a test says so.
- `business/harness/admission.h/.cpp` (new): the pre-load check. Before a lane runs a call on a local backend that is not resident (`Harness::resident`), it prices the resident set plus the candidate against the machine's budget (an injected source, as `MachineBudgetSource` is in `suite_residency.h`). On `OverBudget` it frees unheld local models with no call running or waiting, least recently used first, and prices again. Still over while another model is busy: the call waits (`Queued`, `memory`). Over on its own, with nothing else resident: refused at once.
- `data/modelstore/footprint.h/.cpp`: `served_footprint(config, resident, candidate, machine)`. It prices a set of backends the way `suite_footprint` does, with one base counted once (53d), drafters counted (M25), and unknown said, never guessed and never the reason a load is refused (`footprint.h:27-29`). No new estimator.
- `business/agentloop/member_call.cpp`: the process-wide gate becomes per turn when lanes are set. A turn's member calls still never overlap (27f's property). Two sessions' member calls take their members' lanes.
- **The model channel** (59b's call). The child side: `data/backends/channel_provider.h/.cpp` and `data/backends/model_channel_protocol.h/.cpp`, JSONL over a pipe pair, framed as `mlx_protocol` is. At start the parent sends a `describe` for each served backend: its window, behavior, media, in-text tool calls and metering. Then calls: `chat` (streamed `token`, `thinking` and `status` frames, `Queued` included), `count`, `embed`, `cancel`. `data/backends/factory.cpp`: given a channel, every backend the parent describes is built as a channel provider, and no other. The parent side: `presentation/httpserver/model_channel.h/.cpp`, which answers a child's calls through the Harness and its lanes, one caller per child. `infrastructure/platform/child_process.h/.cpp`: `start_child` gains an optional extra pipe pair, inherited by the child alone, named to it by `APOGEE_MODEL_CHANNEL`, and closed on exec for anything the child starts. `presentation/cli/` (chat, execute): a hidden `--model-channel` flag, refused when no channel is present.
- `presentation/httpserver/handler.h/.cpp`: `turn_mutex_` goes. A lock per session keeps one session's turns in order. A second request on a busy session waits behind it, as today. A stateless request is its own caller. At the bound: `503`, `type: server_busy`, with the backend and the bound in the message. `sse_reporter.cpp`: the `queued` meta-frame.
- `presentation/httpserver/mux.cpp` (`GET /v1/model/status`): each local entry gains `running` and `waiting`.
- `presentation/cli/serve_cmd.cpp`: it composes `Lanes`, the admission check (its budget from `commands::machine_budget()`) and the model-channel host, and passes `set_lanes`. Flags: `--cloud-parallel N` and `--queue-limit N`. The start line names them.
- `presentation/machine/`: the `queued` event, `{"type":"queued","backend":…,"position":…,"reason":"busy"|"memory"}` while waiting, `"position":0` when running. It is in `apogee __machine-schema`. Only a served session's child, whose calls go through the channel, ever sends it.
- Docs: [http-api.md](../../reference/http-api.md) (the one-inference line replaced: the lanes, the bound, `server_busy` in the errors table, the `queued` meta-frame, the new status fields, the flags), [machine-mode.md](../../reference/machine-mode.md) (`queued`), the CLI `CLAUDE.md` (the listening-socket rule's sub-rules name the model channel as a pipe), MILESTONES.
- Tests, hermetic: a temp `APOGEE_HOME`, the mock backend with `delay_ms` and `"parallel"`, injected budgets. No real model, no cloud call.
  - `tests/business/harness/lanes_test.cpp`: two callers on one lane run in turn, and two lanes overlap (a mock that records how many calls it has open at once). Round robin: caller A has five calls waiting and B has one, and B runs second. The bound refuses the next. A cancelled waiter leaves and the positions move up. Each waiter is told its position, and no event names another caller.
  - `tests/business/harness/admission_test.cpp`: a test provider that reports itself local, its footprint planted. Fits, then loads. Over, so the LRU idle one is freed and said. Over while the other is busy, so the call waits as `memory`. Over alone, so it is refused with the arithmetic. A held model (`--ignore-timeout`) is never freed for another. An unknown size loads, said.
  - `tests/data/modelstore/footprint_test.cpp`: `served_footprint` with a shared base counted once, a drafter counted, and an unknown member.
  - `tests/business/harness/provider_capability_test.cpp`: `lane_for` asked of providers the factory built, no type named (as `[factory][embed][capability]`).
  - `tests/business/agentloop/member_call_test.cpp`: one turn's member calls never overlap. Two turns' calls on two backends do.
  - `tests/data/backends/channel_provider_test.cpp`: the protocol over in-memory pipes. Describe, a streamed chat with `Queued` frames, count, embed, cancel mid-stream, the parent gone (the turn fails, said), a malformed line.
  - `tests/presentation/httpserver/handler_test.cpp`: two sessions on two backends overlap. One session's two requests stay in order. `503 server_busy` at the bound. `queued` only with `apogee_events`. A stock request's body is unchanged.
  - `tests/scripts/sh/serve_e2e.sh`: W3's probe as a case, two backends at once both done near one turn's time. A child `apogee chat --machine --model-channel` against a parent with the mock: its answer comes from the parent's provider, and the child's environment and argv carry no key.
  - `cli.http_api_conformance`, `cli.machine_schema_conformance`, `cli.complete_opens_no_listening_socket`.

**Reference.**
- Siblings:
  - [59a](paired-devices.md): who may connect. This item does not need it.
  - [59b](machine-mode-over-the-network.md): the network session as a child. It consumes the model channel and lifts its one-session default.
  - [59d](reads-and-commands-over-the-network.md): reads answer beside the lanes, as `/health` and the session routes do today.
  - [59e](tasks-outlive-the-connection.md): a served task runs in `serve`'s own process, one task at a time. Its model calls are one more caller on the lanes.
  - [60b](../v0.2.4/device-tools.md): a desktop's tools run on the device, so they hold no lane.
  - GUI [7a](../../../gui/backlog/v0.2.4/app-connection-local-or-server.md) and [7c](../../../gui/backlog/v0.2.4/every-place-against-a-server.md): the clients that draw `queued`.
- Precedents:
  - Milestone T: the plane and its one-turn decision, superseded here.
  - 27e/53d/M25: the footprint, the shared base and the drafter.
  - 26a: the window fitted to free memory.
  - 27f: the member gate.
  - 57c: a failure fails the turn.
  - 27a: the MLX child over pipes, the shape of the model channel.
  - 56a: a session as a captured child.
- External: Ollama's FAQ (docs.ollama.com/faq, read 2026-11-07).
  - What it does: each model takes one request at a time by default (`OLLAMA_NUM_PARALLEL`). Several models stay loaded if they fit in memory (`OLLAMA_MAX_LOADED_MODELS`). At most 512 requests wait, and past that it answers 503 (`OLLAMA_MAX_QUEUE`). A new model that does not fit waits while idle models are unloaded to make room.
  - Taken: the per-model width, the bounded wait answered with 503, and unloading idle models to make room.
  - Changed: Apogee prices a load from the store's own records before trying it, and refuses at once what can never fit. Its bound is per lane and small, because the clients are one person's devices. Fairness is round robin across sessions, not arrival order. The waiter is told its place.

**Decisions made** (dated):
- 2026-11-07: Specced from the Apple-platforms spike and brainstorm. The user's calls that shape this item:
  - The vision: a private, self-hosted harness that feels like Claude on every device the person owns. One strong machine serves the person's own desktops and phones, and Apogee mimics frontier harnesses (Claude Code, Codex, Gemini CLI, Ollama, vLLM) with local models.
  - Remote mode is an add-on, never the only way the application runs. The CLI alone, and the Mac app over pipes to a local CLI, stay complete and the default. Anything that makes them need a server, a network or an account is a failure.
  - iPhone and iPad are clients of the person's server only. The Mac and the CLI stay standalone.
  - Where the work happens: the server's tools by default, and a connected desktop offers its own files and shell (60b). Those run on the device, so they hold no lane here.
  - Paid features are a someday. The server admits the person and their devices only, so fairness here is between one person's sessions and tasks: no tenants, quotas, accounting or metering.
  - Placement: successive versions, with v0.3.0 the target where macOS, iPadOS and iOS fully work.

  Track 59, v0.2.3, ungated.
- 2026-11-07: It sits in v0.2.3, before the first client (GUI 7a, v0.2.4), because a server that answers one device at a time does not feel like Claude on every device. It is ungated: the lanes, the memory check and `queued` need neither pairing nor the network session, and the HTTP plane gets them the day they ship.
- 2026-11-07: The model channel is this item's, handed over by 59b. Its network sessions are children, and a child must not load a second copy of a large local model. 59e made the same call for tasks: in `serve`'s process, for the same reason.
- 2026-11-07: Supersedes Milestone T's "One turn at a time" (MILESTONES.md:4591), which named per-backend concurrency as a later item.

**Open calls:**
- [default: a local lane's width is 1, its one context ("one context per provider is one context per chat", `llamacpp.h:565-569`). Two sessions on one model alternate per model call, and each switch re-reads past the shared prefix, which the prompt cache shortens (26j). Several sequences decoding at once over one model is out of scope.]
- [default: a cloud lane's width is 4 per backend entry, set by `serve --cloud-parallel N`. A per-entry config key waits until someone asks.]
- [default: the bound is 16 waiting calls per lane, set by `--queue-limit N`. The next is `503 server_busy`: `slowa: 16 calls are waiting (the queue limit) -- try again`. There is no `Retry-After` header.]
- [default: fairness is round robin by caller. A caller is a served session, a stateless request, a network session's child or the served task. Per-device weighting waits for 59a's device identity and a request for it.]
- [default: a lane is held for one model call. Embedding and token counting take the lane of the backend they run on. Retrieval and tools run outside any lane.]
- [default: memory. Unheld idle local models are freed least recently used first, each said in `serve`'s log and as a `notice` to the waiting turn. A call waits for memory at most 120 s, then gets `503 server_busy` with the arithmetic. A model alone over the budget is refused at once, in `admission_line`'s words. A held model (`--ignore-timeout`, `--preload`'s hold) is never freed for another. A budget that is not known, or a size that is not known, loads and says so.]
- [default: a non-streamed request gets no word while it waits: it has no channel for one. Event-aware clients stream.]
- [default: the model channel forwards every model call of a session child, cloud included. The child holds no provider and reads no key, and one scheduler counts everything. Forwarding local calls only would keep keys in the child and split the accounting.]
- [default: the channel is a third pipe pair, not new lines on the machine-mode pipes, so a device's lines and the model's traffic cannot be mixed by construction.]
- [default: at most 16 live network sessions (`serve --max-sessions N`). The next `net_connect` is refused by name. The lanes bound the work, and this bounds the processes.]
- (consumed decision: 59b) How a network session starts, attaches and resumes. Once this ships, 59b's launch passes the channel. Whichever of the two lands second wires it, and 59b's one-session default lifts.
- (consumed decision: 59e) One served task at a time. Its model calls are a caller here.
- (consumed decision: 59a) Device identity and admission.

**Guardrail(s).**
- The overlap tests are the main ones. A mock counts how many calls it has open at once: two lanes reach 2, one lane never passes its width, and a stateless stock request's bytes match a golden.
- The standalone suite runs unchanged: nothing composes lanes there, and a test asserts `Harness` without lanes takes no lane.
- The admission cases pin "never a load over a known budget".
- The no-leak case pins that a `queued` event and a channel frame carry no other session's id, device or content, and that no key crosses the channel.
- The listening-socket checks stay green.

**Acceptance criteria:**
- [ ] W3's probe: two requests to `slowa` and `slowb` at once both finish within 1.2× one turn's time. Seventeen across two backends take about half of today's 13.76 s, and with `--cloud-parallel` on a mock with `"parallel": 4`, about a quarter.
- [ ] Two sessions on one lane alternate per model call: a five-call turn on one session does not hold back the other's one-call turn until it ends.
- [ ] A streamed request with `apogee_events` gets `queued` with its position before its first content, then `position: 0`. A stock client sees no new frame.
- [ ] The call past the bound gets `503 server_busy` in the OpenAI envelope. A client that hangs up while waiting leaves the line.
- [ ] Loading a second local model over a known budget frees an idle one first (said), waits while the other is busy, refuses with the arithmetic when it can never fit, and is never loaded over the budget.
- [ ] A session child started with `--model-channel` answers from the parent's provider, holds no key, and loads no model of its own.
- [ ] http-api.md no longer says one inference at a time. Every standalone surface's tests pass unchanged, and the full suite is green.
- [ ] The user's live pass, optional and said: `apogee-dev serve` with a local model and a cloud API backend, two clients at once, the second told it waits on the local model while the cloud one answers alongside.

**Scope note.** Earmarked for v0.2.3 (track 59). Ungated. Out of scope:
- several sequences decoding at once on one local model (a ROADMAP idea);
- serving a vendor-CLI backend (still never served);
- per-device priorities, quotas or accounting;
- a pre-load check in the standalone surfaces;
- several served tasks at once (59e);
- training runs, which stay CLI-only over HTTP (SPEC.md:51).
