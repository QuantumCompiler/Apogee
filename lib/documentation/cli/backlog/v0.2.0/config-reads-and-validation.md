# Config reads and validation catch up with the writes: get, completion, typos, template, upgrade

**What / why.** ADR 0002's rule is that reads have twins: whatever the editor writes, a person or driver can read back through the same surface. The CLI/GUI audit (2026-10-22, three-lens verified) found the config's read and validation side lagging the fields v0.1.9/v0.2.0 taught the editor:

1. **`config get` cannot read eight backend fields `set-backend` writes** (high). `lookup`'s backend-field chain (`config_cmd.cpp:395-464`) covers `type`..`thinking_budget` and then fails `no such config key`, while `backend_editable_fields()` lists 26 (`config_edit.cpp:1824-1851`). The unreadable eight are `server_tools` (47a), `adapters` (53a), `draft`, `draft_block_size` (53i), `binary`, `mode`, `host`, `idle_unload_seconds`. Probe: each prints `apogee config: no such config key`, exit 1, while a known-but-unset field prints empty, exit 0. The admin view serializes the new four (`admin_config.cpp:168-185`). `config get` is the CLI's sole read door for them, so a person who ran `set-backend m --adapters a.gguf:0.8` can verify it only by opening the file.
2. **`config get`'s completion omits five 52e scalars the verb reads.** `config_keys()` hardcodes its list (`config_cmd.cpp:1520-1555`) without `memory.recall`, `knowledge.auto_capture` and `tools.search.provider|url|results`, which `lookup` reads (`:164-189`). Probe: `__complete config get ""` returns 44 keys without them, while `__complete config set ""` offers all eight, so the twin verbs disagree at the keystroke (ADR 0007). The existing parity test checks only the forward direction.
3. **A typo'd backend key is silently ignored, and `check` says nothing.** `parse_backend` (`config.cpp:239-333`) has no unknown-key loop. Probe: `"adaptors": [...]`, `"drafter": ...`, `"sever_tools": [...]` in an entry. `check` said `config.json parses cleanly` with no row, and a later `set-backend` preserved the dead keys. Every unknown key in an entry is equally dead (a misspelled `temprature:`). Adapter items, review targets and symphonies already refuse unknown keys. The house's no-silent-fallback norm applies.
4. **The starter template documents `adapters` and `server_tools` but not 53i's `draft`/`draft_block_size`.** The mlx example block (`assets/config.json:442-467`) documents adapters only, and the one `draft` hit is the unrelated `"drafts": false` in the review example (`:246`). 53i's not-done list doesn't record it.
5. **`config upgrade` can never deliver the release's new options.** `find_gaps` walks real keys only (`config_migrate.cpp:509-527`; `missing_template_options` the same, `:658-679`), and the template ships its options commented out on purpose (`assets/config.json:3-5`). The header's promise that upgrade adds new options with their comments (`:16-18`) therefore fires only for a real uncommented key, which the template's own design forbids. Probe: a config holding exactly the template's real keys got `is current -- it has every option this release's starter config has`.

**Core constraint(s).** **Reads twin writes** (ADR 0002): `config get` reads every field the editor writes, the list derived from one declaration (the editable-fields table) rather than a hand-kept chain, so the next field cannot lag. **Completion is derived** (ADR 0007): `config_keys()` builds from the same declarations (`settable_scalars()`, the editable fields), with a reverse-direction test. **No silent fallback**: an unknown key in a backend entry is a `check` row naming it, with a suggestion from the known set, and is refused at load if the house decides strictness. **The template documents every shipped field**, and **upgrade's promise is made true**: it offers commented-only options it can recognize as missing (a declared list of option comments per release), or the header stops promising it. Comments are preserved throughout (the ⚠ invariant).

**Seam + files.** `presentation/cli/config_cmd.cpp` (`lookup` from the editable-fields table; `config_keys` derived), `data/contracts/config.cpp` (unknown backend keys collected for `check`), `presentation/cli/check.cpp` (the unknown-key row), `assets/config.json` + `contracts/config_template.cpp` (the draft fields documented), `data/contracts/config_migrate.cpp` (upgrade's comment-only options, or the header's wording). Tests: every editable field readable; completion's reverse direction; the typo matrix in `check`; the template carrying every field (a test walking `backend_editable_fields()` against the template's text); upgrade over a config missing a commented option.

**Reference.** ADR 0002 (reads have twins), ADR 0007 (completion from the binary), the one-mutation-path invariant; 52e (`config set` and its scalar table); 47a, 53a, 53i (the fields); the CLI/GUI audit (2026-10-22).

**Decisions made** (dated):
- 2026-10-22: Specced from the CLI/GUI audit (the user's call).

**Open calls:**
- [default: unknown backend keys are a `check` warning with the nearest known key suggested (loading stays tolerant). Hard refusal at load would break configs written by newer releases read by older binaries.]
- [default: upgrade learns a declared list of commented options per release and inserts each missing one with its comment block, rather than the header's promise being withdrawn.]

**Guardrail(s).** The derived-list tests (read, completion, template) that fail the day a new field lands without its twin; the typo and upgrade probes pinned.

**Acceptance criteria:**
- [ ] `config get` reads every field `set-backend` writes, and its completion offers every key it reads.
- [ ] A typo'd backend key is a named `check` row; the template documents `draft`/`draft_block_size`.
- [ ] `config upgrade` adds a release's new commented options to a config missing them; the full suite is green.

**Scope note.** Earmarked for v0.2.0 (track 55); ungated. Sibling of [55f](../../assistant/MILESTONES.md#milestone-ar--the-audits-cli-findings) (the write side, shipped 2026-10-25: `field_extent`, the quote-aware `comment_start`, the add name rule and the edit lock).
