# Tool ergonomics for smaller context windows

**What / why.** The native tools were built for cloud models with large contexts and fast prompt reading. A local model has neither. Each byte a tool returns is decoded at roughly 100 tokens/s on a 27B model, before the model can act on it. And a model that must rewrite a whole file to change one line spends its slowest resource, generation at 10–15 tokens/s, on text it is not changing. Six changes make the same tools practical locally. Every backend benefits, since the tools are shared:

1. **`run_command` output is capped.** It has no cap today: a `find /` or a verbose build pours megabytes into the context. The cap keeps the head and the tail, joined by a line saying how many bytes were omitted and how to see more (redirect to a file and read it in ranges).
2. **`read_file` reads a range.** It takes `offset` and `limit` in lines, prefixes each line with its number, and says when a file continues past what was returned. The 64 KiB cap stays, and a range lets a model read past it deliberately.
3. **`edit_file`** replaces an exact string in a file: `old_string`, `new_string`, and `replace_all`. It refuses when `old_string` is absent, or when it matches more than once without `replace_all`; each refusal is a tool result the model can act on. It is gated like `write_file`, default `ask`.
4. **`grep_files`** searches file **contents** with a regular expression under the file root. It returns `path:line: text` matches, capped, skipping hidden directories and binary files. `search_files` matches names only, so today a model looking for a function reads files one by one.
5. **An environment note when tools are on.** The date and time zone, the operating system, the working directory and file root, and the shell. It is spliced as transient context, like RAG's, so it is current every turn and never saved. A model asked "what is the date?" said it could not know (2026-09-24); with tools it also needs to know where it is.
6. **Tool descriptions state their limits,** such as the caps and the root, so a model plans around them instead of discovering them by truncation.

**Core constraint(s).**
- **One gate.** `edit_file` declares `writes`, and joins `destructive_tool_names()` and the shipped template's `permissions:` block at `ask`. `check`'s `Tools` section, the admin view and the machine-mode permission question all read that one list.
- **The sandbox is component-wise.** `grep_files` and `edit_file` resolve paths through the same resolver as the other fs tools, and are added to the escape table in `tests/tools/fs_test.cpp` (`..`, absolute paths, symlinks, the `rootX` prefix).
- **Transient context never reaches history.** The environment note rides `transient_prefix`, as the review note and RAG do (`chat.cpp`, `agentloop/content.h`).
- **Every surface.** The note is built once, in the shared tool setup, for `chat`, `complete`, `serve --tools` and agents alike. A surface that builds its own is a parity bug.

**Seam + files.**
- `tools/shell.cpp`: the head-and-tail cap in `run_shell`'s rendering, with the byte count omitted.
- `tools/fs.h/.cpp`: `read_file`'s range arguments and numbered lines; `edit_file`; `grep_files`, a regex over files under the root, capped by match count and total bytes.
- `tools/toolsets.cpp`: `destructive_tool_names()` gains `edit_file`. `harness/config_template.cpp` lists it at `ask`, and `cli.config_lifecycle` holds the template byte-exact.
- `commands/helpers.cpp` (`make_built_in_tools`) and `agentloop/`: the environment note, built where the registry is and handed to the loop as a transient system message whenever a registry is present.

**Reference (Ommi).** Ommi's `ommi-mcp-fs` and `ommi-mcp-shell` (Python) had no edit, content-search or range tools, and returned command output unbounded; they were ported as they were in Milestone V. The edit semantics (exact match, unique unless `replace_all`) follow the convention coding agents have converged on, and are named here so the builder does not reinvent them.

**Decisions made:**
- 2026-09-25 — From the local-tools spike: the costs that dominate locally are prompt reading and generation, so the tools should return less and ask for less generation.
- 2026-09-25 — After [local tool calling](local-tool-calling.md), so that each change is verified on the local models it exists for. None of it depends on that item's code.

**Open calls:**
- [default: `run_command` keeps the first and last 8 KiB] That is enough for a compiler's first error and a test runner's summary, and it is stated in the tool's description.
- [default: `grep_files` returns at most 100 matches and 16 KiB] Past that it says how many were omitted and to narrow the pattern.
- [default: `read_file` numbers lines only when a range is asked for] A plain read stays byte-faithful for a model that is going to quote or edit it.
- [default: the environment note is on whenever tools are] A few dozen tokens a turn, and a date a model cannot otherwise know.

**Guardrail(s).**
- The command cap keeps the head and the tail and names the bytes omitted.
- `read_file`'s range is exact at the file's edges.
- `edit_file`:
  - refuses an absent or ambiguous `old_string`, leaving the file untouched;
  - honours `replace_all`;
  - asks through the gate on a terminal and is refused on a pipe.
- `grep_files` stays in the root, respects its caps and skips binary files.
- The environment note appears in the request and never in the saved transcript.
- Every one mutation-tested.

**Acceptance criteria:**
- [ ] A command printing 5 MB returns under 17 KiB with its first and last lines, and a note of what was cut.
- [ ] A local model asked to change one line in a 2,000-line file calls `edit_file` and leaves the rest of the file byte-identical.
- [ ] A local model asked where a function is defined finds it with `grep_files` in one call.
- [ ] A local model with tools on answers today's date correctly.
- [ ] `apogee check` lists `edit_file` among the gated tools; the shipped template lists it at `ask`.

**Scope note.** Phase 4, item **25d**; build after 25b. Out of scope:
- a persistent shell session (a `cd` that survives between calls; `run_command` already takes a `cwd`);
- long-running background commands;
- a sandbox for the shell itself (macOS seatbelt, Linux namespaces), which is its own larger item if `allow` for the shell is ever to be safe.
