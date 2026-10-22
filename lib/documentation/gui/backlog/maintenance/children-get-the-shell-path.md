# The app's children get the person's shell PATH

**What / why.** The user's run in the macOS app, 2026-10-22: a session refused with

`no backend serves 'claude' (configured backends: GPT-OSS-20B, Gemma-Embed, Gemma4-12B-Instruct, …, Qwen3.8-27B)`

`claude` is configured, and in the user's terminal it works: `check` says "credentials found". The app cannot reach it, and the cause is the app's environment.
- **The child inherits the app's environment unchanged.** `ProcessSpawner` gives every `apogee` child `ProcessInfo.processInfo.environment` with only the command's own variables laid over it (`lib/src/darwin/source/infrastructure/process/ProcessSpawner.swift:52`, `ChildCommand.environment(over:)`).
- **That environment carries launchd's `PATH`.** An app opened from the Dock or the Finder gets `/usr/bin:/bin:/usr/sbin:/sbin`. Every vendor CLI the user's cloud backends run lives elsewhere:
  - `claude` and `codex` in `~/.local/bin`;
  - `gemini` in `/opt/homebrew/bin`;
  - `ollama` in `/usr/local/bin`.

  The CLI looks each up on `PATH` and skips a backend it cannot find (`lib/src/cli/source/data/backends/claude_cli.cpp:149–156`).
- **So every cloud backend is out of the app's reach.** That is all four of the user's (`claude`, `codex`, `gemini`, `ollama`, with the roster variants on them), and every role that resolves to them. The user's `models.default`, `chat`, `embedding` and `extraction` roles all resolve to `claude` today, by `models status`.
- **Other tools go too:** Homebrew's `ffmpeg`, `gh` and `pdftotext`.

Measured, not guessed (2026-10-22):
- **Reproduced.** With the installed binary under that `PATH` and a sandbox config with `claude` as the default: `no backend serves 'claude' (configured backends: Local-Mock)`.
- **Under the shell's `PATH`,** `check` says "credentials found".
- **The vendor binaries' locations** were read with `which`.

This is a recorded trade-off coming due. 1b's MILESTONES entry says: "launched from the Finder, the app's `PATH` is launchd's, so a tool the CLI looks for on a shell's `PATH` … may not be found by a session the app starts … Widening the child's `PATH` is a later, deliberate call." 2g's entry repeats it for `gh`. The user read the refusal as their backend rename not mapping. It was not the rename: the CLI's [M26](../../../cli/backlog/maintenance/skipped-backend-says-why.md) makes the refusal say why.

**Core constraint(s).**
- **The app stays a machine-mode client.** It never finds `claude`, `gh` or any vendor tool itself, and never decides which backend runs. It only gives the CLI the `PATH` the person's own terminal would, and the CLI's lookups do the rest, unchanged.
- **The person's shell, not a guess.** The `PATH` comes from the person's login shell (`$SHELL`, the way their Terminal starts it), so tools installed by nvm, Homebrew, pipx, a version manager or a hand-edited profile are found exactly as in their terminal.
- **Bounded and safe.**
  - The shell is asked once per app launch, never once per child.
  - The ask has a deadline. Its standard input is closed. Its output is read between markers, so a profile that prints is harmless.
  - A shell that fails, hangs or isn't there falls back to launchd's `PATH` widened by the well-known directories: `~/.local/bin`, `/opt/homebrew/bin`, `/usr/local/bin`.
  - Only `PATH` is taken. No other variable of the shell's crosses, and no secret is read.
- **A child's own variables still win** (`environment(over:)`), and a test's spawner is unaffected.

**Seam + files.**
- **`lib/src/darwin/source/infrastructure/process/`:** a `ShellPath` resolver: the login shell asked once, with its deadline, markers and fallback. `ProcessSpawner` lays the resolved `PATH` under every child's own variables.
- **`lib/src/darwin/source/presentation/app/ViewModels/AppViewModel.swift`:** the resolution started at launch, off the main actor, before the first child. A child spawned before it finishes uses the fallback.
- **Tests (`lib/src/darwin/tests/infrastructure/process/`):**
  - the resolver over a scripted shell: a profile that prints around the markers, one that never answers (the deadline), one that fails (the fallback);
  - the spawner's child environment carrying the resolved `PATH` under its own variables.
- **A live check, optional:** the app's `apogee check` run reporting `claude` as found.

**Decisions made** (dated):
- 2026-10-22 — **Diagnosed from the user's run** (the user's bug report: "I recently renamed some of my backends and the new name is not mapping … This is happening in the GUI"). It is the launchd `PATH` that 1b recorded and deferred, not the rename: `models.default` is `claude`, untouched by any rename, and the app's sessions launch with no `-m` (`ChatLaunch.swift:78`).
- 2026-10-22 — **The app's half is the cause, the CLI's the message** (the CLI's M26). Each is useful alone, so neither is gated on the other.
- 2026-10-22 — **The road not taken: the CLI searching well-known directories itself.** It would reach every client the CLI has (the app, the mod, anything launched by launchd), but only by guessing. It would miss nvm's or a version manager's directories, and diverge from what the person's terminal finds. The login shell is the truth the person already has. Per-backend `binary` paths (`config set-backend <name> --binary <path>`) remain the CLI's manual way.

**Open calls:**
- [default: the login shell is asked as an interactive login shell (`$SHELL -ilc`, as editors such as VS Code resolve it), so both `.zprofile` and `.zshrc` are read, with a 5-second deadline and the well-known-directories fallback.]
- [default: Settings says which `PATH` the app's sessions use (resolved from the shell, or the fallback) in its General pane, read only. There is no editor for it.]

**Guardrail(s).** The user's case is the fixture: a scripted shell answering a `PATH` with `~/.local/bin`, and a child spawned through the real `ProcessSpawner` whose environment carries it. Besides that:
- **A profile that prints before and after the markers** still yields exactly the `PATH`.
- **A shell that never answers** is cut off at the deadline, and children get the fallback.
- **A child's own `PATH`,** when a command sets one, still wins.

**Acceptance criteria:**
- [ ] Launched from the Dock, the app starts a session on the user's `claude` default, and `check` run from the app finds `claude`, `codex`, `gemini` and `ollama` as the terminal does.
- [ ] A shell that hangs or fails delays nothing past its deadline, and the app still starts.
- [ ] Previews, `check-previews`, the layering check and `lib/scripts/cicd.sh --test` pass, with no Swift warning.

**Scope note.** GUI Maintenance, claimable by name, no gate. Until it ships, either of these works, both on the person's own word:
- **Name each binary's full path,** for example `apogee config set-backend claude --binary ~/.local/bin/claude`, and the same for `codex`, `gemini` and `ollama`.
- **Start the app from a terminal,** by running its executable inside the `.app`. A terminal's `open` does not pass the shell's environment.
