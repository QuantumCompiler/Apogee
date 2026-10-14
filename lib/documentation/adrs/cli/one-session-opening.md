# ADR 0011 — A mode is opened by its command's own path

**Status:** Accepted · **Date:** 2026-10-14

## Context

[ADR 0002](mode-parity.md) keeps behavior in one core that every mode renders, and [ADR 0010](the-shell-is-a-mode.md) made the full-screen shell a mode. Neither said where a *session's opening* lives. On 2026-10-13 the opening of `apogee execute` grew three behaviors ([Milestone AK](../../assistant/MILESTONES.md#milestone-ak--the-coding-mode)):
- **41a** — the bare door: with no suite named or the default, it opens on `-m` or the default backend;
- **41b** — the project opening: `AGENTS.md` (else `CLAUDE.md`) read into every request, the folder attached;
- **41c** — the coder persona, carried in a project unless `-s` replaces it.

The CLI↔TUI disparity audit (2026-10-14) drove the installed binary on a real terminal against sandboxed homes. Mid-session the shell measured strong: the one command table gives it some forty slash twins. Every wall it found was at a session's **opening**:
- **A fresh shell opens straight into a chat.** `TuiSessionDriver::start()` shows the picker only when a saved chat exists, so a new install has no execute entry anywhere.
- **The picker's execute rows are the shell's own decision.** It offers one row per configured suite, and a bare row only when no suite is configured at all (`suites.empty()`). `apogee execute` opens bare whenever no suite is named and none is the default.
- **The suite door is a function of its own.** `TuiSessionDriver::open_execute(const std::string& suite)` (`tui_session.cpp:333`) is suite-only. Every shell session is opened from an argv the shell spells itself (`shell_session_flags`: `--tools`, `--resume`, `--suite`, `--model` and nothing else).
- **The shell cannot be launched into a session.** `apogee tui --help` lists no option, against `apogee execute`'s 40 and `apogee chat`'s 39.
- **The ritual reaches the shell by where it happens to sit.** 41b and 41c live inside `run_session`, keyed on the mode and the process's working directory. `tui_session.cpp` names neither, and no test holds a shell-opened session to them.

These are one drift class: **a front end composing its own second opening of a mode.** Each new opening behavior then has to be remembered at every door, and nothing fails when one is forgotten. 41a's bare door reached the shell's picker only in its no-suite case.

## Decision

**A mode is opened by its command's own path: one open function and one flag table per mode. Every front end's door calls that function and speaks that table.**

- **A mode** is a session kind with a command of its own: today `chat` and `execute`, whose sessions the shell's Session view holds.
- **The open function** is what the command's callback runs: `run_session(context, flags, machine, mode, front)` today, with `bind_session_flags` its table. A door reaches it with a parsed `SessionFlags`. Everything that decides what opens is decided once, inside it:
  - which suite and which model (the rungs);
  - the project opening and the persona;
  - admission and warming;
  - `--orchestrate` staying a suite's.
- **A front end owns presentation only**: where the session writes (`SessionOutput`), how a picker looks, its keys. The doors a picker offers follow the command's own rules: a door is offered when the command would open, worded in the command's vocabulary.
- **A front end that launches into a session speaks the command's flag table by reference.** A flag the mode grows reaches every door with no door-side change, and a combination the mode refuses is refused in the command's words.
- **A second composition of an opening is the defect**: a door function choosing its own subset, an argv of its own that leaves a decision out, or a copy of a ritual.

## Consequences

- Opening behavior added to a mode reaches every front end by construction. A new door costs wiring, never a copy.
- A door that cannot express something the command can, such as a flag the shell has no way to pass, is a gap with a home: the item that wires it.
- The shell's picker renders the command's rungs, so its rows can be tested against them.

## Enforcement

- **Pending, 2026-10-14.** The door goldens hold a session opened by a front end's door byte-equal to the command's own opening on the same state: its saved record, its banner, and a scripted turn's request. They land with track 44 in the [backlog](../../backlog/README.md): **44b** for the picker's doors and **44c** for `apogee tui` speaking the table. Until then this record has no mechanical teeth, and the per-change checklist in [CLAUDE.md](../../assistant/CLAUDE.md) names it.
