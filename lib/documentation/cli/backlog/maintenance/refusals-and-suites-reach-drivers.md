# Refusals and suites reach drivers: the named backend, the runtime, the suite

**What / why.** The macOS app's v0.2.0 run (GUI track 4 and its Maintenance items) drives the binary as its only client, and its probes found five places where the session opening, or a media run's refusal, tells a driver less than the truth. Each was reproduced again on the branch binary, `apogee 0.2.0 (4b828e91)`, over sandbox homes on 2026-10-26.
1. **A named backend that can't run is quietly replaced by the default.** With mock `good` as the default and an `mlxgone` entry whose runtime is missing:
   - `chat -m mlxgone --output-format stream-json` exits 0 with stderr empty, and its result reads `{"model":"mlxgone","text":"GOOD-SAYS"}`, the default's answer under the named backend's label. The saved chat records `backend: mlxgone`.
   - `execute -m` does the same, and `chat -m nope` (a name configured nowhere) is answered by `good` too.
   - Only when nothing can start does it refuse, and then with every backend's problem: `no usable backend is configured -- …`.

   The cause is `presentation/cli/chat_session.cpp:829–840`: only `constructed_count() == 0` is checked, never the named backend, so the router falls back (`business/harness/harness.cpp:241–251`). `complete -m mlxgone` already refuses correctly (`complete.cpp:744–760`, whose comment names this hazard), but `complete.cpp:716–723` has the same ordering bug when nothing starts.
2. **A runtime-not-installed refusal sends only the `job` line.** `video generate`, `video compose` and `voice say … --output-format stream-json` with torch and diffusers missing exit 1, their stdout only the `job` line and the words (`torch and diffusers are not installed … -- apogee train setup --with video`) on stderr. Other pre-run refusals, such as an existing `-o`, send `job` then `error`. The cause is `presentation/cli/video_runtime.cpp:69–74`: `ready_runtime` writes to cerr and throws without telling the `JobStream`; `train.cpp:1937–1942` (`require_python_env`) is its sibling.
3. **Compose's two-voice-models refusal names the wrong flag:** `the store holds 2 voice models -- name one with --model:`, where compose's flag is `--voice-model`. The cause is `voice_say.cpp:79–82`, which hard-codes the flag in `resolve_voice_model`, called by compose at `video_compose.cpp:452–459`.
4. **A suite's launch refusal and admission lines reach stderr only.**
   - `chat --suite vast --output-format stream-json` (and `execute`) exits 1, stdout empty, stderr `suite vast needs … it does not fit`.
   - A driven `chat --suite research` puts `[apogee] suite research: no member holds memory …` on stderr and no `notice` in the stream.

   Sources: `chat_session.cpp:1391–1394` (the admission line), `:948–952` (`activate_suite`'s warnings), and `:966–975` (the refusal, before `begin_session` at `:1484`).
5. **Nothing says which suite a session runs under.** After `chat --suite fast`, a `chat --resume <id>`'s `session` event carries no `suite`, though the saved chat holds `suite: fast`; only stderr's admission line names it. `execute` is the same and carries no `orchestrating` either. The design's contract board calls this gap G23. The suite is resolved at `chat_session.cpp:927–947`, and `begin_session` (`:1483`) isn't given it.

Findings 1, 4 and 5 all edit the one session opening in `chat_session.cpp`, so they are one item. Finding 1 comes first: the wrong model answers under the named one's name.

**Core constraint(s).**
- **The protocol grows additively** (ADR 0006).
  - **`suite` on `session`:** an optional field on an existing event, so `protocol_version` stays 1. It moves `kMachineSchemaVersion` (`machine/protocol.h:47`, today `2026-10-25`) and the goldens `tests/fixtures/cli/chat_session.golden` and `execute_session.golden`. `cli.machine_schema_conformance` pins it (`schema_conformance.py:145–155`).
  - **Admission lines and suite warnings become `notice`s after `session`,** as 55b's resume notes are (`chat_session.cpp:868–879`, emitted at `:1504–1508`: "never stderr alone").
  - **A launch refusal stays on stderr with exit 1.** An `error` before `session` would break "session comes first", and an `error` with no turn would change what `error` means ("a turn failed"). That is not additive, and `machine-mode.md` writes the launch contract down.
- **One session opening** (ADR 0011): every door, `chat`, `execute`, the TUI's and `serve`'s, goes through it; the fix lives there once.
- **A media failure ends with `error`,** as `machine-mode.md` already says of every job stream. The refusal's words stay on stderr too.
- **Honest words:** each refusal names the named backend's own problem, never every backend's; compose's names `--voice-model`.

**Seam + files.**
- **`presentation/cli/chat_session.cpp`:**
  - a launch check after construction (around `:833`): a backend named by `-m` that didn't construct is refused with its own reason, as `complete.cpp:744–760` refuses;
  - the suite's admission line and warnings carried as notes, emitted as `notice`s after `session`;
  - the resolved suite handed to `begin_session`.
- **`presentation/cli/complete.cpp:716–723`:** the same ordering fix when nothing starts.
- **`machine/json_reporter.cpp:74`** (`.h:118`) and **`machine/protocol.cpp:30`** (`kSession`): the optional `suite`, and `orchestrating` for execute. `machine/protocol.h:47`: the schema date.
- **`presentation/cli/video_runtime.cpp:69–74`** and **`train.cpp:1937–1942`:** the refusal told to the job stream (`tell_job`/`fail`, as `models_pull.cpp:72–83` does), ending on `error`. The video and voice commands gain the `CLI::RuntimeError` backstop `train.cpp:86–101` has.
- **`presentation/cli/voice_say.cpp:79–82`:** `resolve_voice_model` takes the flag's name, default `--model`, which compose passes as `--voice-model`.
- **`lib/documentation/cli/reference/machine-mode.md`:** `suite` on `session`, the notices, and the launch contract.

**Reference.**
- 55b's resume notes as `notice`s after `session`: the shape the suite lines follow.
- M28's `chat_id` and 52d's `private` on `session` (`json_reporter.cpp:96–103`): the shape `suite` follows.
- `models pull`'s `tell_job`/`fail` (`models_pull.cpp:72–83`): a job told before it fails.
- The symphony documents' `suite` (`machine-mode.md:1117`): the naming precedent.
- Compose already tells its two model flags apart (`video_compose.cpp:85–96`).

**Decisions made** (dated):
- 2026-10-26 — **Opened from the macOS app's v0.2.0 run** (the user's call: "spec them into the backlog"; placed in CLI Maintenance, the user's call the same day). The app's 4f, 4i and M5 probes found findings 2–5. The research pass that grounded this document found finding 1 to be worse than recorded: it isn't the wrong refusal, it's the wrong model answering.
- 2026-10-26 — **Findings 1, 4 and 5 are one item** because they edit the same session opening. Split across sessions on a shared checkout, they would collide.

**Open calls:**
- [default: a backend named by `-m` (chat, execute) that didn't construct is refused at launch with its own problem, exit 1, as `complete -m` refuses. This partly revises M26's recorded "a request naming a skipped backend still falls to a default", for the launch alone: the router's fallback for later requests is untouched. The revision is recorded, dated.]
- [default: the suite's admission line and its warnings become `notice`s after `session` in machine mode, and stay on stderr in the terminal. This revises 27e's "one line, on stderr in every mode" for machine mode, recorded, dated. A launch refusal stays on stderr with exit 1, and `machine-mode.md` says so.]
- [default: `session` carries `suite` (the resolved suite's name, absent when none) and, for execute, `orchestrating`.]

**Guardrail(s).**
- **The bug becomes a fixture.** `chat -m <backend that can't start>` and `execute -m` refused with that backend's reason. A mock default never answers under another's name (`chat_test.cpp`/`chat_session_test.cpp`, `execute_test.cpp`, M26's skipped-backend fixture in `factory_harness_test.cpp`).
- **`suite` on `session`:**
  - a driven chat and execute under a suite, fresh and resumed, the field present and the goldens refreshed (`chat_test.cpp:2291`, `execute_test.cpp:339/534`, `machine_mode_test.cpp`, `suites_e2e.sh`);
  - `cli.machine_schema_conformance` green with the doc's new key.
- **Suite lines as notices,** a refusal on stderr with exit 1: `suite_residency_test.cpp`, `cli.suites_lifecycle`, `cli.reference_driver`.
- **Each media verb's runtime refusal** under `stream-json` ends with `error` (`video_runtime_test.cpp`, `video_cmd_test.cpp`, `video_compose_test.cpp`, `voice_say_test.cpp`, `video_e2e.sh`).
- **Compose's refusal names `--voice-model`;** the voice-say pins stay (`voice_say_test.cpp:179–182`, `models_voice_test.cpp:485–487`).

**Acceptance criteria:**
- [ ] A chat or execute naming a backend that can't run refuses with that backend's problem, and nothing else answers under its name.
- [ ] A runtime refusal under `stream-json` ends the stream with `error`, its words also on stderr.
- [ ] Compose's two-voice-models refusal names `--voice-model`.
- [ ] A suite's admission and warnings reach a driver as `notice`s, and its refusal is documented as the launch contract.
- [ ] `session` says the suite, fresh or resumed. The schema date and goldens move, and conformance passes.
- [ ] `lib/scripts/cicd.sh --test` passes with zero warnings, and the changed files pass the MinGW scan.

**Scope note.** CLI Maintenance, claimable by name, no gate. The macOS app reads all of this today from stderr and its fallbacks. Adopting the new fields and notices is a later GUI item, not gated either way; its sibling items are the CLI's M30–M33.
