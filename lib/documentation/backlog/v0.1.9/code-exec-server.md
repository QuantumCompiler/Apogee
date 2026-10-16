# The code-execution server: the interpreter in the box

**What / why.** The single biggest honest parity gap to ChatGPT is code execution (the built-in-MCP parity spike, 2026-10-16): Apogee's `shell` is a gated raw shell, not an interpreter with a working directory and artifacts, and a fresh install configures **zero MCP servers** (measured). This item ships the gap's answer as the spike's reframe demands — **one generic server, serving every provider identically**, because the host translates tools per vendor and a per-vendor server would have nothing vendor-specific to say (the same probe showed Apogee itself already answering the MCP handshake with 17 tools). The **code-execution server** is a bundled Python stdio MCP server: `run_python` (and a `write_input` companion for data files) executing in a **throwaway, session-scoped working directory with no network** — the user's standing default, 2026-10-16 — output bounded head-and-tail, result files returned as paths under that directory, errors as the interpreter's own traceback tail. It registers through the existing MCP client like any server (zero new protocol code), its tools reaching cloud and local models alike through the one registry; ChatGPT-interpreter parity for a Q4 local model is the headline.

**Core constraint(s).** **Python is the satellite language** (the stack rule) and the server is a **seeded script on the asset pipeline**: compiled-in copy via the generators, seeded skip-if-present, byte-identity-tested, its layout row (if a new directory) landing in the same commit — the drivers' exact precedent. **It is a stdio child**: the mandatory stderr sink, three pipes and nothing else, never a listening socket (the lsof check's tree sampling extends over a turn that uses it). **The sandbox is honest about what it is**: a fresh directory per session under the data directory's cache/tmp rows, deleted after; the no-network guard is in-process (the socket module removed from the exec environment) and **recorded as best-effort, not a boundary** — the real boundary is the permission gate: `run_python` is declared `writes` and asked like any gated tool, `--allow`-able, mode-governed (39b). Execution is bounded: a wall-clock limit, an output cap (the 8 KiB head/tail idiom), the caps named constants the tool result quotes when hit. **The interpreter**: `training/venv`'s when present, else `python3` on PATH, refused with a clear message when neither — said in the server's registration detail, never guessed.

**Seam + files.**
- `lib/src/cli/assets/mcp/code_exec_server.py` — the server: the MCP handshake over stdio (the same JSON-RPC the client speaks), `run_python`/`write_input`, the jail (all paths resolved under the session directory, escapes refused by name), the bounds, the socket-less exec environment.
- The generator (`scripts/generate_*_assets.py` family) — the compiled-in copy; `contracts/layout.h` — the seeded location's row if new (same commit).
- `cli/` — the composition: the session-scoped directory's creation/cleanup; the gate declaration (`writes`).
- Tests: the real script driven through the shipped MCP client in-process (hermetic: python3 required and skipped-with-reason where absent, the suite's existing python discipline): handshake, a run returning stdout, an artifact file returned as a path, the output cap, the time cap, a jail-escape attempt refused by name, the socket guard's refusal, the stderr sink holding a noisy run off the terminal (the pty idiom); byte-identity; the gate asked on a real turn (`cli.*` case).

**Reference.** The spike (2026-10-16) — W2's gap and the 0-servers/17-tools measurements; Milestone W — the MCP client this registers through (zero new protocol code); the mlx/training drivers — the seeded-Python-child precedent whole (stderr discipline, descriptor hygiene, byte-identity); 39b — the modes that govern the gate; [`mcp-presets.md`](mcp-presets.md) — the registration ergonomics, `(consumed decision)` there.

**Decisions made** (dated):
- 2026-10-16 — The set lands in v0.1.9 as track 47 (the user's placement call); the sandbox is **throwaway and network-less** (the user's standing default, confirmed by execution).
- 2026-10-16 — One generic server for all providers — never per-vendor variants (the spike's reframe, standing).
- 2026-10-16 — The no-network guard is best-effort in-process and said so; the permission gate is the real boundary, and `run_python` is `writes`-gated.

**Open calls:**
- [default: the bounds — 30 seconds wall clock, 8 KiB output head/tail, the session directory capped at a named size — exact numbers at build, quoted in refusals.]
- [default: registration in this item is by hand (`mcp create code --command <seeded path>`), documented; the one-command preset is [`mcp-presets.md`](mcp-presets.md)'s.]

**Guardrail(s).** The hermetic client-driven suite above (jail, bounds, socket guard, sink — each with a planted violation); byte-identity of the compiled-in copy; `cli.install_parity` green with any new row; the lsof check seeing the server's child and no socket; the gate's ask/allow/mode matrix on a real turn.

**Acceptance criteria:**
- [ ] The seeded server registers through `mcp create --command` and its tools reach a session: a local model and a cloud backend both run code and get artifacts back as paths.
- [ ] The jail, the bounds and the socket guard hold against planted violations; refusals name their cap or rule.
- [ ] `run_python` is asked as a `writes` tool, mode-governed; a noisy run never touches the terminal.
- [ ] Compiled-in and seeded copies are byte-identical; the full suite is green.

**Scope note.** Earmarked for v0.1.9; ungated — independently buildable. Out of scope: the preset mechanism (the sibling's), other languages, persistent sandboxes, any network allowance.
