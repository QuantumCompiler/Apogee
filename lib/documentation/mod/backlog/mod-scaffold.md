# The mod scaffold: a validating plugin with the law at birth

**What / why.** Nothing of the mod exists: no `lib/src/mod/`, no `changed.sh` entry, no docs beyond this home (the mod spike, 2026-10-21) — while the toolchain is proven on the dev machine (`claude plugin validate` answers) and the contracts the mod needs are measured live. This item makes the plugin real: the skeleton (`.claude-plugin/plugin.json` — plain `apogee` naming per the house rule — `hooks/hooks.json` → `register.tsx` as the composition root, `types/` as needed), **the four layers with the layering check at birth** ([ADR 0001](../adrs/layer-enforcement.md)'s enforcement clause: a TypeScript import-graph check holding each file to its layer's row, `$`-API confinement included — `$.process` Infrastructure's, `$.ui` Presentation's — failing upward edges by name, refusing an empty graph), **binary discovery with the not-installed guard** (the GUI 1b ladder pattern: known channel paths + an override atom; absence a typed, toasted condition with the install hint — never a crash), and the wiring: `tsc`/`claude plugin validate` as the local gates, `lib/scripts/changed.sh` gaining the `mod` entry (what the mod is built from, declared once).

**Core constraint(s).** The ⚠ client invariant from birth: nothing in the skeleton opens a port or parses prose where a document exists. The scaffold's one visible behavior is honest: loaded with no binary, it says so once and stays quiet. Hermetic tests: the discovery/guard matrix over scripted `$.process` results; the layering check verified against planted violations (the house bar). `plugin validate` and `tsc` clean.

**Seam + files.** `lib/src/mod/` whole (the layout DEVELOPER.md plans); `lib/scripts/changed.sh` (the `mod` entry); the repo `.gitignore` for the plugin's build outputs. Tests under `lib/src/mod/tests/` mirroring the layers.

**Reference.** The mod spike (2026-10-21) — the toolchain and contract measurements; the GUI's 1a — teeth-at-birth and the discovery ladder, mirrored; `plain-apogee-naming` — the product name is `apogee`, never a suffixed variant.

**Decisions made** (dated):
- 2026-10-21 — The user's calls, with the spike: `lib/src/mod/`, this docs home, the four-layer law regardless of language; the skills grown to three homes the same day.

**Open calls:**
- [default: the layering check's implementation (an import-graph script in the test suite) at build, the CLI's and GUI's checks the reference; the plugin's `name` field `apogee`.]

**Guardrail(s).** The layering check with planted violations; the discovery/guard matrix; `tsc` + `claude plugin validate` clean; `changed.sh mod` answering correctly for mod-only and docs-only diffs.

**Acceptance criteria:**
- [ ] `claude --plugin-dir lib/src/mod` loads the plugin; with no binary it states the condition once with the hint.
- [ ] The four layers exist with the check failing planted upward edges and misplaced `$`-API use by name; `changed.sh mod` exists; `tsc` and validate are clean.

**Scope note.** Earmarked for v0.2.1; first of track 1, ungated. Out of scope: tools/commands ([`mod-tools-and-commands.md`](mod-tools-and-commands.md)), the pane (1c), CI (1d), any CLI change (the CLI's track 54 is the sibling).
