# CLI-M47 · Config migrate carries every line the loader reads, or names the one it can't

| Field | Value |
|---|---|
| **Type** | 🐞 Bug |
| **Project** | CLI |
| **Status** | 🟢 Ready |
| **Fix version** | Maintenance (any time) |
| **Component** | Config › `config migrate` (`contracts/config_migrate.cpp`) and `check`'s Config row |
| **Labels** | `config` `yaml` `migrate` |
| **Epic** | — (maintenance has no track) |
| **Blocked by** | — |
| **Blocks** | — |
| **Reporter** | CLI-M44's probes (2026-11-22), then a sandboxed investigation, two skeptics and a critic the same day |
| **Created** | 2026-11-22 |

---

## Summary

`config migrate` promises to carry a YAML config to JSONC "exactly or not at all", naming the line when it can't. It doesn't keep either half.
- **It refuses files the loader reads, naming no line.** A comment on a `---` or `...` line, on a tag, or after a multi-line last value is enough: the words are "the conversion did not carry the file exactly". Apogee's own legacy template is refused once a hand-written backend ends in `system_prompt: |`.
- **It loses part of a file with exit 0.** A UTF-8 BOM drops the first comment, and a UTF-16 file loses every comment. A second YAML document is left out of the conversion.

`check`, the compat notice and every editor refusal name `migrate` as the way forward, and `check` promises "every comment kept". The item makes the migrator read one text and one document, carry every line once, and name the line it can't carry. `check` then says before the run when `migrate` would refuse.

## Description

### User story

**As** someone with an older `config.yaml` that Apogee tells me to migrate,
**I want** `apogee config migrate` either to carry my whole file, comments and all, or to name the line it can't,
**so that** the way out every refusal points me to actually works, and never loses part of my file while saying it succeeded.

### Steps to reproduce

The sandbox, 2026-11-22, with the built binary `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`: a throwaway `APOGEE_HOME` and `HOME`, `env -i`, stdin from `/dev/null`, a mock backend, `config.yaml` written by hand.

1. With `--- # config` as the first line of an otherwise ordinary config, run `apogee config get backends.m.type`, `apogee check`, then `apogee config migrate`.
2. Take Apogee's own legacy template (`tests/fixtures/config/legacy_template.yaml`, 570 comments) and add a mock backend whose last field is `system_prompt: |` with one line under it. Run `apogee config migrate`.
3. Save the legacy template as UTF-16LE with a BOM, and run `apogee check`, then `apogee config migrate`.
4. With `ui:\n  markdown: false` in a second YAML document (after `---` or `...`), run `apogee config migrate`, then look for `"ui"` in `config.json`.

### Actual result

1. `get` prints `mock` (exit 0). `check` says `ok   config.yaml  parses cleanly -- the older YAML format, still read; migrate converts it to config.json, every comment kept`. `migrate` says `apogee config: the conversion did not carry the file exactly -- migrate refuses rather than lose anything; convert it by hand` (exit 1), with no line named and nothing written.
2. The same refusal, exit 1. With a one-line `system_prompt: "…"`, the same file migrates with `5 sections, 570 comments carried`.
3. `check` says `ok … every comment kept`. `migrate` says `5 sections, 0 comments carried` (exit 0), and writes `"models": null` where the UTF-8 file gives an object.
4. `3 sections, 0 comments carried` (exit 0), no `"ui"` in `config.json`. The values survive only in `config.yaml.bak`.

### Expected result

1. Migrated, with the marker line's comment carried as `// config`.
2. Migrated, with all 570 comments carried, as for its one-line twin.
3. Refused with words naming the encoding (`the file is UTF-16, not UTF-8 -- save it as UTF-8 and run it again`), touching nothing. `check` warns with the same words.
4. Refused, naming the line where the second document starts and saying Apogee reads only the first. `check` warns with the same words.

### Background

`config migrate` arrived with 28i (2fbe6749, 2026-10-07, v0.1.5), and its core has not changed since. Its record (MILESTONES, the 28i entry) says it carries each comment "where it stood", verifies "the same values, exactly as many comments" before writing, and refuses "a file it cannot carry exactly … naming the line". `config_migrate.h:24-31` says the same, and so does `config migrate --help` ("every key, value and comment carried over").

The migrator reads the file twice, and the two readings disagree:
- **yaml-cpp gives the structure.** It decodes a BOM and UTF-16/32, reads only the first document, and marks only the lines where a node starts.
- **A byte-line classifier gives the comments** (`config_migrate.cpp:186-215`). It reads raw bytes, across every document and every line.
- **The writer** walks the marks and assumes every line that holds YAML has been written. It writes a line's trailing comment only through `trailing()`, called where a key, a value, an item or an owner starts (`:424`, `:453`, `:455`, `:467`, `:482`). `leading()` skips every non-comment line (`:334-344`), and `tail()` stops at the first line that is neither blank nor a comment (`:366-374`), a block scalar's body included.
- **The final check** counts comments from the same classifier that placed them (`:823-826` against `:341`, `:387`). So it catches only what the classifier saw and the writer couldn't place, and misses whatever the classifier never saw. It names no line: the words at `:841-843` are thrown inside `convert()`, called at `:853` outside the `try`.

`migrate` is the way out that the CLI names everywhere: the compat notice (`root.cpp:232-235`), `check`'s Config row (`check.cpp:434-439`), `config init` (`config_cmd.cpp:826`), `config upgrade` (`:879`), the README, 57e's refusals, and CLI-M43's and CLI-M44's planned refusals. CLI-M44 refuses every layout the editor doesn't write and points at `migrate`. So the shapes `migrate` refuses or half-carries are the dead ends of that path.

### Evidence

All measured 2026-11-22 by an investigator, two skeptics and a critic, each in its own sandbox. A matrix of about 40 shapes was run, each checked against what the loader reads (`config get`) and what `check` says.

1. **Refused with words that name no line, while the loader reads the file**:
   - a comment on a line where no node starts: `--- # c`, `---\t# c`, `--- #c`, `---  # c` (yamllint's two-space spelling), `... # c`, `%YAML 1.2 # c`, `--- !!map # c`, `!!map # c`, `&root # c`, `  - # c` over an item on the next line, and a plain scalar's continuation line;
   - with a BOM, with CRLF, after a header comment and a blank, and alone (`--- # config` with nothing else, which the loader reads as an empty config);
   - a trailing comment on a flow value written on the line after its key (`allowed_hosts:` / `    [docs.python.org] # c`);
   - an own-line comment after a multi-line last value, a block scalar or a wrapped plain scalar, with nothing later to pick it up. Apogee's own template plus a backend ending in `system_prompt: |` is refused; the same file with a one-line prompt carries 570.

   Each exits 1 with the generic words, stdout empty, the file's sha256 unchanged, no `.bak` and no `config.json`. One comment out of 571 blocks the whole template.
2. **Carried today**, the controls:
   - a bare `---`, `---` with trailing spaces, `# c` then `---`, `%YAML 1.2` then `---` (the directive dropped), `--- !!map` (the tag dropped), `--- {…}`, and `--- {…} # c` (a key starts there, so the comment is carried);
   - BOM + `---`, CRLF (kept in the output), `...` alone or followed by a blank, comments between `---` and the first key, and a trailing comment on the last line;
   - a compact list followed by a sibling key and a comment.
3. **A BOM drops a first-line comment with exit 0.**
   - BOM + the legacy template carries 569 of 570 comments, missing `// Apogee configuration.`.
   - BOM + `# config` carries 0.
   - BOM + an indented comment is refused with the generic words.

   The cause: `split_lines` (`:50-65`) keeps the BOM, so the first line's first byte is 0xEF and the classifier calls it content (`:192`, `:103`). The JSONC reader and yaml-cpp both skip a BOM.
4. **UTF-16 or UTF-32 loses every comment with exit 0.**
   - The legacy template as UTF-16LE or UTF-16BE with a BOM gives `5 sections, 0 comments carried`, and `"models": null` where UTF-8 gives an object.
   - UTF-16LE without a BOM carries `1 comment`, and writes NUL bytes inside it (`7b0a 2020 2f2f 0020 0041 …`). The file still loads.

   yaml-cpp decodes UTF-16/32 itself (`stream.cpp:15-35`), while the classifier reads raw bytes. `check` says `ok … every comment kept` for all of them.
5. **A later YAML document is silently left out.** Exit 0, `3 sections`, no `"ui"` in `config.json`, the `.bak` byte-identical to the original.
   - **A second document with content**: no setting in effect changes, since the loader reads only the first document (`config.cpp:1845`), but the file's visible content is not carried.
   - **An empty first document** (`---`/`---`/body, or `...`/body) is worse: the whole visible config goes to the `.bak`, and `{}` is written. Before migrating, `config get backends.m.type` says `no such config key`, while `check` says ok.
6. **`check` promises without trying.** Its Config row says "every comment kept" for every legacy file (`check.cpp:434-439`). `config_legacy` is set from `looks_like_jsonc` alone (`:3256`), and no trial migration runs. Even a shape `migrate` refuses by name today, a flow list spread over lines (`line 8: a flow list or map continues onto the next line -- …`), gets `ok … every comment kept`. The JSON row carries the same words, and the Mac app's Settings reads it.
7. **Who is stranded.** On a `--- # config` file in the 2-space layout, all 21 probed line editors land, the marker line kept byte for byte. But on a `--- # config` file with a compact host list (CLI-M43's bug), or in 4 spaces (CLI-M44's), the editors refuse and `migrate` refuses too, which leaves only a hand edit.
8. **Who writes these shapes.**
   - PyYAML writes a bare `---` only, with no comment path (`emitter.py:195-201`), and Apogee's template never wrote a marker. So `--- # comment` is a hand edit, or a yamllint style.
   - A UTF-16 file is what Windows PowerShell 5.1's `>` and `Out-File` write. That is by reading; Windows was not run.
   - How many installs still hold a `config.yaml` was not measured.
9. **No other surface migrates.** There is no HTTP twin, no machine-mode verb and no GUI caller, and the generic words are untested: `grep -c 'did not carry' tests/data/contracts/config_migrate_test.cpp` gives 0. CLI-59d (v0.2.3) plans `config migrate` from a paired device.

## Acceptance criteria

- [ ] **Given** a YAML config whose `---`, `...`, `%YAML` or properties-only line carries a comment, **when** `config migrate` runs, **then** it exits 0 and the comment is carried as `//` where the same comment on its own line goes today.
- [ ] **Given** Apogee's legacy template plus a backend whose last field is `system_prompt: |`, **when** it migrates, **then** all 570 comments are carried, and the text equals its one-line-prompt twin's.
- [ ] **Given** a flow value written on the line after its key with a trailing comment, **then** it migrates as its one-line twin does (`"allowed_hosts": ["docs.python.org"] // c`).
- [ ] **Given** a UTF-8 BOM, **then** every comment is carried (570 of 570 for the template), and `config.json` carries no BOM.
- [ ] **Given** a UTF-16 or UTF-32 config, or one holding a NUL byte, **then** `migrate` exits 1 naming the encoding, and nothing is written (the same bytes, no `.bak`, no `.json`).
- [ ] **Given** a later YAML document that holds content, after a full or an empty first document, **then** `migrate` exits 1 naming the line where it starts and saying Apogee reads only the first document. A later document that is null (a trailing `---`, or comments only) passes, and its comments are carried.
- [ ] **Given** any shape `migrate` still can't carry, **then** the refusal names a line (`line N: …`). The bare "did not carry the file exactly" words are gone.
- [ ] **Given** a legacy config that `migrate` would refuse, **when** `check` runs, **then** the Config row is `warn` with the refusal's words, its line included, in the text report and in `--output-format json`, and the remedy is the hand edit, then `apogee config migrate`. A file that migrates keeps today's ok row.
- [ ] **Given** every file that migrates today (the controls, `migrate_worst_case`, `legacy_template`), **then** the output is byte-identical, except a comment at a container's column right after a multi-line value, which now stays in its container (Decision log).

## Constraints

- **Exactly or not at all** (28i): nothing is written unless the same values and every comment are carried, and a refusal touches nothing.
- **A refusal names its line** (28i, `config_migrate.h:29-31`), or the encoding when the whole file is the problem.
- **One config mutation path** (CLAUDE.md → ⚠): `migrate` stays a file-lifecycle act through its own writer, and `check`'s trial is the pure `migrate_config_text` on the content `check` already read, never a write.
- **An older config loads** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)). Nothing here changes what the loader reads. A second document or a UTF-16 file keeps loading as today, and `check` now says what `migrate` would do with it.
- **Goldens are byte-stable and committed with plain bytes.** BOM and UTF-16 inputs are built in code, not committed as odd bytes.

## Technical approach

Paths under `lib/src/cli/`.

| File | Change |
|---|---|
| `source/data/contracts/config_migrate.cpp` | **One text** (`convert`, `:814-822`). Strip a leading UTF-8 BOM before `YAML::Load`, `split_lines` and the classifier. Refuse, touching nothing, a text holding a NUL byte or starting with a UTF-16/32 BOM, naming the encoding. |
| `source/data/contracts/config_migrate.cpp` | **One document.** After `YAML::Load`, `YAML::LoadAll` on the same text. A document after the first that is defined and non-null is refused through `refuse()`, naming its first line, whatever the first holds. |
| `source/data/contracts/config_migrate.cpp` | **Every line carried once.** `convert` collects the lines where a node's mark lands and hands the set to the `Migrator`. `leading()` (`:334-344`) writes the trailing comment of a content line it passes outside that set, on its own `//` line at the current indent. That carries markers, directives, properties lines, dash-only lines and continuations. `tail()` (`:364-377`) steps over continuation lines and content lines outside the set, and still stops at a node's line or anything shallower. `members()` (`:454-456`) flushes the trailing comment of a flow or empty collection written inline, as it does for a scalar. |
| `source/data/contracts/config_migrate.cpp` | **A refusal names its line** (`:840-844`). The `Migrator` records the lines it carried. A comment-count mismatch names the first counted line not carried, and a value mismatch names the first top-level key that differs. |
| `source/data/contracts/config_migrate.h` | `:24-32`: what is carried, and the refusals for an encoding and for a later document. |
| `source/presentation/cli/check.cpp`, `check.h` | For a legacy file, `check` runs `migrate_config_text` on the content it already read (`:3253-3257`). A refusal makes the Config row (`:434-439`) `warn` with the refusal's words and the hand-edit remedy. Otherwise the row is unchanged. |
| `tests/fixtures/config/migrate_markers.yaml`, `.json` (new) | `--- # config` on line 1, `  - # first host` over `    docs.python.org`, `  - example.com`, and `... # end` last. The `.json` is today's output for its one-line twin. |
| `tests/fixtures/config/migrate_written_lines.yaml`, `.json` (new) | `allowed_hosts:` / `    [docs.python.org] # the docs`, a backend whose `system_prompt: a prompt that` wraps onto `      wraps`, a backend whose last field is `system_prompt: \|` / `      You are terse.`, then `  # spare:` / `  #   type: mock` and `# end of file`. The `.json` is today's output for its one-line twin. |
| `tests/data/contracts/config_migrate_test.cpp`, `tests/presentation/cli/config_lifecycle_test.cpp` | The table and cases under Testing. |

**Reference.** 28i's record and `refuse()`, the refusal that already names a line (`line 8: a flow list or map continues onto the next line -- …`). The investigator's and critic's twins: for each shape, today's output for the same file with the comment on its own line, or with the multi-line value on one line. Those are the oracles for the goldens.

**Docs when it ships:**
- DEVELOPER.md's `config_migrate` row (`:364`) and test row (`:661`).
- `config migrate --help` (`config_cmd.cpp:843-844`), and the README's config paragraph, both true once it ships.
- MILESTONES: a dated entry, with a dated revision of 28i's record for the encoding and later-document refusals.

## Testing

Each case fails on today's binary unless marked as a control (measured 2026-11-22).

- **The bug as a fixture: two goldens.**
  - `migrate_markers.yaml` → `.json`, refused today.
  - `migrate_written_lines.yaml` → `.json`, refused today, while `config get ui.markdown` reads `false`. Four comments are carried.
- **The shape table**, in `config_migrate_test.cpp`. Each shape asserts that `migrate_config_text(shape).text` equals a pinned literal (today's output for its twin), and that the comment counts are equal: not only equality with a twin run through the same new code.
  - Shapes: `--- # c`, `---  # c`, `---\t# c`, `--- #c`, `%YAML 1.2 # c` + `---`, `--- !!map # c`, `# header` + blank + `--- # c`, `--- # c` alone, CRLF `--- # c` (CRLF kept), `... # end`, `...` then `# end`, `  - # c` over an item, a continuation line with `# c`, a flow value on the next line with `# c`, a block scalar last then comments, a wrapped plain scalar last then a comment, and the two `docstart_*` fixtures.
  - A comment after a plain scalar's continuation line lands on its own `//` line inside the container.
- **Built in code, not committed as odd bytes**:
  - `"\xEF\xBB\xBF"` + the legacy template carries 570 comments, with the same text as without the BOM; BOM + CRLF + `# c` carries 1;
  - UTF-16LE and UTF-16BE bytes, with and without a BOM, throw naming UTF-16, and `migrate_config_file` writes nothing;
  - the legacy template with a mock backend ending in `system_prompt: |` carries 570, its text equal to the quoted-prompt twin's.
- **Refusals**, each throwing with `line N` and `second YAML document`, and `migrate_config_file` writing nothing (the same bytes, no `.bak`, no `.json`): a second document with content; a document after `...`; `---`/`---`/body; `...`/body.
- **Controls, byte-identical to today**: the carried shapes in Evidence 2, and the existing `migrate_worst_case` and `legacy_template` goldens.
- **`check`**, in `config_lifecycle_test.cpp` beside its legacy cases: a file `migrate` refuses (a flow list over lines, or a second document) gives a `warn` row whose detail names the line, in the text report and in `--output-format json`. The ok case is unchanged.
- **Gate:** `lib/scripts/cicd.sh --app cli --test`, clang-format, and the MinGW scan of the changed files.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Where does a comment on a `---`, `%YAML` or properties-only first line go in the JSONC? | `[default]` | Wherever the same comment on its own line goes today: inside the brace before the first key, or above the brace when a blank line makes it part of the header (the one header rule, `:286-297`). For `--- # config` directly above `backends:`, that gives `{` / `  // config` / `  "backends"`. |
| Comments on or after a `...` end marker: inside the root brace, or after it? | `[default]` | Inside, as the root's closing comments (`  // end` / `}`), exactly as an end-of-file comment without `...` is carried today. |
| A later YAML document holding content: refuse naming the line, or carry the first document and leave the rest in the `.bak`? | `[default]` | Refuse, naming the line. That keeps "exactly or not at all", and matches CLI-M44's refusal of a second document. Files that migrate with exit 0 today then exit 1, though no setting in effect changes. |
| A UTF-16 or UTF-32 config: refuse naming the encoding, or convert it? | `[default]` | Refuse by name ("save it as UTF-8 and run it again"), touching nothing. Converting would need a new decoder in contracts, for a file the person can re-save in one step. |
| What does `check` say when `migrate` would refuse? | `[default]` | `warn`, with the refusal's words and line in place of "every comment kept", and the hand edit as the remedy, then `apogee config migrate`. |
| A comment at a container's column right after a multi-line value, which migrates today but lands at the next key's level: keep today's bytes, or keep it in its container? | `[default]` | Keep it in its container, by indentation, as 28i states for a container's closing comments. `migrate` runs once per file, and no committed golden has this shape. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-22 | Specced from CLI-M43's and CLI-M44's Out of scope, a sandboxed investigation, two skeptics and a critic (Evidence). Placed in **Maintenance** as M47. The bug grew from "a `--- # comment` line" to the writer's three rules, the encoding, a later document and `check`'s promise, all one seam: `config_migrate.cpp`, plus `check`'s row. | the spec |
| 2026-11-22 | **Not a gate on CLI-M44**, though M44's refusals name `migrate` as the way out. A gate names an earlier item, and this one is after M44 in the table. It is best built before or with M44. M44's own words for a second document now name the hand edit, since `migrate` refuses that shape too. | the spec |
| 2026-11-22 | **The loader reading only the first document is kept** (ADR 0006: an older config loads). `check`'s trial migration is what now says so, as a warning naming the line. | the spec |

## Out of scope

- **The line editor on a UTF-16 config**, which prints success and appends UTF-8 bytes it never reads back. That is the editor's, and is folded into [CLI-M44](yaml-editor-refuses-the-layouts-it-does-not-write.md) as one more layout refused by name. CLI-M43's read-back covers the three gate edits.
- **A list's column taken from its first item's value line** when the item sits on the line after its dash (`:469`), so a closing comment at the dash's column leaves the list. Nothing is lost, so it is a note only.
- **Counting the installs still on `config.yaml`.**

## Links

- **Blocked by:** — · **Blocks:** —
- **Relates to:**
  - [CLI-M44](yaml-editor-refuses-the-layouts-it-does-not-write.md) and [CLI-M43](gate-edits-land-or-say-they-did-not.md), whose refusals name `migrate` as the way out;
  - 28i (2fbe6749), `config migrate`'s record;
  - CLI-59d (v0.2.3, [reads and commands over the network](../v0.2.3/reads-and-commands-over-the-network.md)), which plans `migrate` from a paired device.
