# A backend that could not start says why

**What / why.** The user's run in the macOS app, 2026-10-22:

`no backend serves 'claude' (configured backends: GPT-OSS-20B, Gemma-Embed, Gemma4-12B-Instruct, Gemma4-26B-Instruct, Gemma4-31B-Instruct, Gemma4-E2B-Instruct, Gemma4-E4B-Instruct, Llama3.1-8B-Instruct, Llama3.2-1B-Instruct, Llama3.2-3B-Instruct, Qwen3.8-27B)`

`claude` **is** configured. It is a `claude-cli` backend, and in the user's terminal `check` says "credentials found". It could not be **started** in that process: the app's child ran with launchd's `PATH`, so `claude` (in `~/.local/bin`) was not found. The GUI's [M6](../../../gui/backlog/maintenance/children-get-the-shell-path.md) owns that cause. The message hides it twice.
- **The list is wrong.** It calls the backends that were built "configured backends". The router collects names only from providers the process constructed (`source/business/harness/harness.cpp:137–150`), and words them as the configuration (`:204–213`). So `claude`, `codex`, `gemini` and `ollama`, every cloud backend the user has, vanish from a list that claims to be the config.
- **The reason is dropped.** `build_providers` records why each skipped backend was skipped, "so a caller can explain the gap when the model the user actually asked for is the one that was skipped" (`source/data/backends/factory.h:68–72`). Every caller adds that reason only when *nothing* was built:
  - `presentation/cli/complete.cpp:716–724`
  - `chat_session.cpp:790–797`
  - `analyze.cpp:576`
  - `knowledge.cpp:465`

  When the requested backend is the skipped one and others were built, its reason is never said. The reason is `'claude' was not found on PATH`, with its remedy (`source/data/backends/claude_cli.cpp:149–156`).

Measured, not guessed (2026-10-22). The installed binary (`0.1.9 4ed12ce5`) was run against a sandbox config with a `claude-cli` backend as the default beside a mock, under the `PATH` a Dock-launched app has:

```
$ env -i HOME=$HOME PATH=/usr/bin:/bin:/usr/sbin:/sbin apogee --custom <sandbox> complete "hi"
apogee complete: no backend serves 'claude' (configured backends: Local-Mock)
```

Under the same `PATH`, `check` says `warn backend: claude claude-cli -- 'claude' was not found on PATH`, with its remedy. So the fact is known to the binary, just not where the person meets it. The user read the message as a rename not mapping, which it was not.

**The fix.** When the requested backend, or the default it fell back to, is a configured entry that was skipped, the router's refusal says so: the backend is configured but could not be started, the factory's own reason, and its remedy. The list it prints is named for what it is: the backends that *started*.

**Core constraint(s).**
- **One place decides the message.** The router's refusal is raised once (`SimpleRouter::route`) for every surface: the command line, machine mode, `serve` and the TUI (ADR 0002). The skip reasons reach it from `build_providers`, which already installs the router. The callers do not each patch the message.
- **A skipped backend stays non-fatal** (`factory.h:68–72`'s rule). A config with one unstartable entry still runs the others.
- **The words are the doctor's** (28c): the same `not_on_path` sentence and remedy `check` prints, never a second wording.
- **Machine mode's error event grows additively** if it gains a field (ADR 0006).

**Seam + files.**
- **`source/data/backends/factory.h/.cpp`:** the skipped entries and their reasons handed to the router it installs.
- **`source/business/harness/harness.h/.cpp`:** `SimpleRouter` keeps the configured-but-skipped names with their reasons. `route` says the reason when the requested name or the default is one of them, and names its list "started backends". `NoAvailableBackendError` carries the reason for a caller that wants it.
- **The callers** (`complete.cpp`, `chat_session.cpp`, `analyze.cpp`, `knowledge.cpp`): their `constructed_count() == 0` path unchanged.
- **Tests:**
  - `tests/business/harness/`: the router's refusal for a skipped requested backend, for a skipped default, and for a name configured nowhere, which reads as today;
  - `tests/data/backends/`: the reasons handed through, over a `claude-cli` entry whose `binary` names a path that does not exist, beside a mock.

**Reference.** The factory's skip rule and its stated purpose (`factory.h:68–72`). 28c, the doctor's remedy table (`provider_for_type(...)->remedy`). The GUI's 1b, whose MILESTONES entry recorded the launchd `PATH` cost and deferred widening it.

**Decisions made** (dated):
- 2026-10-22 — **Diagnosed from the user's run** (the user's call: the bug report, "I recently renamed some of my backends and the new name is not mapping … This is happening in the GUI"). The rename was not the cause: `models.default` is `claude`, which no rename touched. The app's sessions launch with no `-m` and so take that default (`lib/src/darwin/source/business/session/ChatLaunch.swift:78`), and the default's backend could not start.
- 2026-10-22 — **The CLI's half is the message, the GUI's the cause** (GUI M6). Each is useful alone, so neither is gated on the other.

**Open calls:**
- [default: the refusal names only the skipped backend that was asked for (or the default it fell to), not every skipped entry. `check` remains the place that lists them all.]

**Guardrail(s).** The user's run is the fixture: a `claude-cli` default not found on `PATH`, beside a started local backend.
- **The refusal** names `claude`, says it is configured but could not be started, gives the doctor's reason and remedy, and calls its list the started backends.
- **A name configured nowhere** still reads "no backend serves '<name>'" as today.
- **Machine mode's error** carries the same words.

**Acceptance criteria:**
- [ ] Under a `PATH` without `claude`, `apogee complete "hi"` with `claude` as the default says `claude` is configured but was not found on `PATH`, and how to fix it.
- [ ] A backend that is not configured at all is refused exactly as today.
- [ ] `lib/scripts/cicd.sh --test` is green.

**Scope note.** Maintenance, claimable by name, no gate. Making the vendor CLI findable from a GUI-launched process is the GUI's [M6](../../../gui/backlog/maintenance/children-get-the-shell-path.md). Searching well-known directories from the CLI itself is recorded there as the road not taken.
