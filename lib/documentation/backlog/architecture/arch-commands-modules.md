# Architecture A3: `commands/` into three presentation modules

**What / why.** `commands/` is the biggest package in the tree — 83 files at the spike's count — and it mixes three presentation concerns that change for different reasons: the **CLI commands** themselves (one file per subcommand plus the root/registry), the **terminal views** (status line, thinking view, answer painting, line reader, download progress, the ask prompt, terminal and interrupt handling), and the **machine-mode adapter** (the JSON reporter and its protocol glue). This item is the "modularize each layer" half of the restructure applied where it pays most (the user's call, 2026-10-03): `presentation/cli/`, `presentation/views/`, `presentation/machine/` — three modules whose boundaries are the ones the code already observes informally (views never parse argv; the adapter never paints; commands compose both). Deliberately **its own item after [A2](arch-layer-move.md)**: these are real judgment diffs (which unit is a view, which is command logic), and mixing them into A2's rename storm would make both unreviewable — the review-hygiene decision that set the track at four documents.

**Core constraint(s).**
- **Composition root stays one place:** `presentation/cli/` remains the only module that assembles providers, loops, stores and views (`commands/` is deliberately unguarded today *because* it is the composition root — that privilege follows the `cli` module, not the other two). `views/` and `machine/` gain allow-lists: views include `ansi/`, `contracts/`, `platform/` and themselves; `machine/` includes the Reporter seam's needs and never a painter.
- **The Reporter seam is the sorting rule, not a casualty:** `cli_reporter` composes views (→ `cli/` or `views/`? it *is* the terminal adapter → `views/`), `json_reporter` → `machine/`, and the seam's header stays in Business where it lives — adapters up, interface down, unchanged.
- **No behavior, no bytes:** PTY checks, machine-mode conformance, the piped byte-identity contracts and every golden pass unchanged — this is files finding their module, with include updates inside the presentation layer only.
- **The one command table and the slash ecosystem move whole** into `cli/` — splitting dispatch from completion would reopen the drift item 24 closed.
- **Short include paths hold** (consumed decision — A2): module dirs join the presentation include path; `#include "commands/..."` spellings update to their new module names in this item, the one place include text legitimately churns.

**Seam + files.**
- `presentation/cli/`: the per-command files, `root`, `registry`, `helpers`, `permissions`, the reporters' composition points, `main.cpp`'s registration surface.
- `presentation/views/`: `status_line`, `thinking_view`, `answer_view`/Markdown painting glue, `line_reader`, `download_progress`, `ask_prompt`, `terminal`, `input_gate`, `interrupt`, `cli_reporter`.
- `presentation/machine/`: `json_reporter`, machine-input parsing, the schema-conformance anchor files.
- `tests/presentation/{cli,views,machine}/` mirrored; `tests/schema_conformance.py` and the PTY checks re-pointed; layering/link rules for the two new allow-listed modules.
- Judgment calls that need deciding in-build get decided by the sorting rule above and recorded in the milestone (the handful of genuinely shared helpers land in `cli/` unless two modules need them, in which case they are the first candidates for `views/` or a tiny `presentation/common/` **only if forced**).

**Reference (Ommi).** No analog — Ommi's `cmd/` stayed monolithic to the end. The boundary being formalized is Apogee's own: the view/reporter split the terminal-UX milestone built (Milestone G) and machine mode's adapter discipline (Milestone M).

**Decisions made** (dated):
- 2026-10-03 — The user's call at specing: split, as its own item, after the move — three modules, review-isolated from A2's renames.

**Open calls:**
- [default: `cli_reporter` lands in `views/` (it is the terminal adapter: it owns paint order), `json_reporter` in `machine/`] The adapters' homes.
- [default: no `presentation/common/` unless two modules genuinely share a unit that belongs to neither — created reluctantly, named in the milestone if so] The escape hatch.
- [default: `httpserver/`, `markdown/`, `render/` stay single modules — nothing in the spike's counts says they need splitting] The rest of the layer.

**Guardrail(s).**
- The PTY suite, machine-mode e2e and conformance, and the piped byte-identity checks — green, unmodified in logic.
- The new modules' allow-lists in the layering test from the commit they exist; `machine/` including a painter or `views/` parsing argv fails by name.
- `git diff -M` again mostly renames; the include churn confined to `presentation/`.
- The one-table completion test still covers every verb (nothing dropped in the sort).

**Acceptance criteria:**
- [ ] `presentation/` holds four modules (`cli`, `views`, `machine`, plus the untouched `httpserver`/`markdown`/`render` siblings) with the allow-lists enforced; no file remains directly under a bare `commands/`.
- [ ] Every surface behaves byte-identically: a PTY chat, a piped run, a machine-mode session, `--help` output.
- [ ] `views/` builds without the CLI target or argv parsing anywhere in its includes; `machine/` without any painter.
- [ ] The milestone records where each ambiguous unit landed and why, in one table.

**Scope note.** **Architecture item A3**; build after [A2](arch-layer-move.md). Out of scope: splitting `httpserver/`/`markdown/`/`render/`; any view behavior change; the CMake target-per-module work ([A4](arch-build-enforcement.md) — these modules become three of its libraries).
