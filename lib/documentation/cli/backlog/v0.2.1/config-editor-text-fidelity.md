# The config editor writes what it says it did, and keeps what it does not edit

**What / why.** The one config mutation path (`contracts/config_edit`) promises that an edit touches its key and nothing else, and that a printed success means the file now says so. The second bug sweep (2026-10-31) found seven ways that promise still breaks after 55f ([Milestone AR](../../assistant/MILESTONES.md#milestone-ar--the-audits-cli-findings)). Two print a success the file does not hold (1, 3), three change what the user never asked to change (2, 4, 5: a value's text, a comment, a symlink), one refusal says a backend that exists is absent (6), and one leaves a secret under a readable mode (7). Each was reproduced on the built binary against sandbox configs (a temporary `APOGEE_HOME`, `--config` on scratch files) and survived a three-lens review. Line numbers below are re-read at HEAD on 2026-10-31.

1. **A YAML section header with a comment or a flow mapping gets a shadow section, and the edit reports success.** `is_section_header` (`data/contracts/config_edit.cpp:134-140`) matches only a bare `<section>:` line, so `find_section` (`:177-198`) misses `backends:   # x` and `memory: {recall: true}` and reports no section. The editors then append a second top-level block at EOF, which the loader never reads, and exit 0. The editors affected are add-backend and delete-backend (`append_entry` / `delete_entry`, `:525`, `:638`), `config set <section>.<field>` (`set_section_rendered`, `:2009`) and add-allowed-host (`:2408`). Only `set_attachments_graph` (`:2173-2190`) refuses this shape today. Probes on legacy YAML files (`scratchpad/y/bc.yaml`, `fm.yaml`, `fm2.yaml`, `fm3.yaml`) each ended with a second `backends:`/`models:`/`memory:`/`tools:` block appended at EOF, and the reads returned the old values:
   - `backends:   # my models` with backend `a`: `config add-backend c --type mock --model m` prints `added backend 'c'` and `models.default set to 'c' (none was set)`, but `config get backends` lists only `a`. The default now names a backend that does not exist, and `config delete-backend a` says `backend 'a' not found in config`.
   - `models: {default: a, default_utility: a}`: `config delete-backend a` prints `models.default cleared` and `models.default_utility cleared`, but `config get models.default` still says `a` and check FAILs `default_backend`. These are M33's clearing messages, said for a change that never landed.
   - `memory: {recall: true}`: `config set memory.recall false` prints `memory.recall = false`, but get says `true`.
   - `tools: {allowed_hosts: [a.com]}`: `add-allowed-host b.com` says the list includes b.com, but the list stays `a.com`.

   A header with only trailing whitespace (`models: `) is handled correctly. JSONC configs are probably safe, because the bridge (`yaml_of_json`) writes bare block headers, but that was not probed. Only the legacy YAML format is affected; the CLI still loads it, flags it as the older format and points to `config migrate`.
2. **A multi-line text value loses its newlines on every write, and on YAML a later edit wedges the file.** `yaml_scalar` (`config_edit.cpp:317-336`) quotes a value that holds `\n`, `\r` or `\t`, but its loop escapes only `"` and `\\`. The raw newline therefore lands inside the double quotes, and YAML folds it: single newlines become spaces and a blank line becomes one newline, with no warning. This hits the default `config.json` (JSONC) through the bridge:
   - `config set-backend c --system-prompt $'You are terse.\nAnswer in French.\n\nBe kind.'` writes `"system_prompt": "You are terse. Answer in French.\nBe kind."`.
   - `config add-backend d --type mock --system-prompt $'a\nb'` writes `"a b"`.
   - On a YAML config the value is written as `system_prompt: "line1` followed by `line2"` at column 0, and `config get backends.c.system_prompt | od -c` shows `line1 line2` with no newline (`scratchpad/y/e.yaml`).
   - Setting the prompt again to `short` leaves `line2"` in the file: `d.yaml` ends with `    system_prompt: short\nline2"`. From then on `config add-backend zz ...` fails with `<edited config>: not valid YAML: ... illegal map value`, and the stray line survives `delete-backend c`.

   55f made a block scalar (`system_prompt: |`) no longer fold into its replacement, but the writer itself still folds. This extends that fix.
3. **On JSONC, the bridge retypes number-looking names and values.** `yaml_scalar` never quotes text that looks like a number. `json_of_yaml_scalar` (`data/contracts/config_yaml.cpp:227-244`) then parses that plain scalar as a JSON number (`:240-242`), and `in_format` (`config_edit.cpp:2797-2819`) splices the number into the file. Probe on `scratchpad/y/n.json`:
   - `config add-backend 1.50 --type mock --model m` prints `models.default set to '1.50'`, but the file holds `"default": 1.5`.
   - `config set-default 1e3` prints `models.default = 1e3` and writes `"default": 1000.0`, after which check says `FAIL default_backend names a backend that is not configured: '1000.0'`.
   - `config set-backend x --model 1.50` writes `"model": 1.5`, and get reads `1.5`.

   The backend key itself stays a string. `yaml_plain_key` (`config_yaml.cpp:206-217`) accepts these names, which contradicts 55f's "one name rule ... names both config formats can edit". `rename-backend x 2.0` and a `3.0` default round-trip only because nlohmann happens to keep the `.0`. The same names are fine on a YAML config.
4. **The quote-aware comment scan misreads YAML's `''` escape.** `comment_start` (`config_edit.cpp:203-220`) closes a single-quoted scalar at the first `'` (`:211`), so in `'it''s #1'` the scan treats `s #1'` as plain text and the ` #` as a comment's start. With `model: 'it''s #1'  # c1`, `config get` correctly reads `it's #1`, but `config set-backend a --model NEWa` writes `model: NEWa #1'  # c1` (probe diff on `scratchpad/y/d.yaml`: `<     model: 'it''s #1'  # c1` → `>     model: NEWa #1'  # c1`). The stale `#1'` stays fused into the user's comment, though the new value reads back as `NEWa`. This is an incomplete 55f fix ("A quote-aware comment scan everywhere", [MILESTONES.md](../../assistant/MILESTONES.md#milestone-ar--the-audits-cli-findings), line 10914). Double-quoted values with an escaped quote, and plain `x#y`, were handled correctly. YAML format only.
5. **An edit replaces a symlinked config with a regular file.** `write_file_atomically` (`config_edit.cpp:3445-3494`) renames its sibling temp file over `path` (`:3488`) without resolving a link. When `config.json` is a symlink (into a dotfiles repo, say), the first `apogee config set` replaces the link with a regular file, exits 0, says nothing, and leaves the link's target with the old content. Probe on `scratchpad/h3`: before the edit, `config.json` was a symlink to `scratchpad/dot/config.json`. After `config set`, `ls -la` shows `h3/config/config.json` as a regular file of 41989 bytes, and `dot/config.json` is still 41951 bytes, the pristine content. Correction at HEAD: the finding also named the fs tool's write/edit (`business/tools/fs.cpp:635`) as affected. That path is already resolved with `weakly_canonical` before it reaches the write (`fs.cpp:201-218`), so a live link there is written through. The fs tool's link handling belongs to [57o](tool-gate-enforces-what-it-shows.md) and [57k](windows-filesystem-safety.md).
6. **A hand-written JSONC backend with an odd name is said not to exist.** On a JSONC config, set-backend, delete-backend and the other `set_backend_*` edits on a backend named, for example, `"my model"` or `"qwen@home"` fail with `backend '<name>' not found in config`. The text comes from `delete_entry` (`config_edit.cpp:646-649`) and `set_backend_path_field` (`:1676-1677`), reached through `in_format` (`:2973-3070`): the bridge writes such a key quoted, and the line editor finds names by their plain text. Yet `config get backends` lists the backend, `set-default 'my model'` accepts it, and `rename-backend 'my model' mymodel` reaches it (probe on `scratchpad/y/o.json`). 55f decided the restriction itself. The defect is only that the refusal claims the backend is absent and names no way out, where the add refusal (`:530-541`) names `apogee config rename-backend`. `config_jsonc_test.cpp:587-588` currently pins the misleading text.
7. **Secret files sit on disk at the umask's mode while they are written, and a private config loses its mode on any edit.** `write_file_atomically` writes the content to the temp file (`config_edit.cpp:3460-3471`) and only then, under `private_mode`, chmods it to `0600` (`:3474-3485`). That contradicts the code's own comment and the header's promise (`config_edit.h:456-458`, "never on disk under a permissive mode, not even between two syscalls"). The private-mode callers are the admin token (`presentation/httpserver/admin_auth.cpp:66`), the credential store (`data/secrets/store.cpp:95`) and the knowledge store (`business/knowledge/store.cpp:44`). Separately, `edit_config_file` (`config_edit.h:492-506`) writes without private mode and carries no existing mode over, so a config the operator chmodded `600` because it holds `api_key: sk-...` is `0644` again after `apogee config set-default m` (or one `[a]lways` answer). Probes:
   - 150 runs of `auth add openai --stdin` in a `0755` config dir, with a Python watcher polling the directory: 149 temp files were caught at mode `0o644`, 140 bytes, containing the `sk-` key. The final `credentials.json` is `0600`.
   - A `config.yaml` at `0600` with a literal `api_key` became `-rw-r--r--` after `apogee config set-default m`.

   An attacker is another local user polling `APOGEE_CONFIG=/srv/apogee/config.yaml`'s `0755` directory for `credentials.json.tmp.*` or `admin-token.tmp.*`. This is a **regression of** [Milestone U](../../assistant/MILESTONES.md#milestone-u--the-control-plane)'s `admin_auth` entry (MILESTONES.md line 4617: "written `0600` on the temporary file before the rename … never on disk under the umask's mode").

What already works: a header with only trailing whitespace, double-quoted values with escaped quotes and plain `x#y` values, and block scalars (55f). The same number-looking names on a YAML config work. A rejected edit leaves the file untouched (the re-parse). The final `credentials.json` and admin token are `0600`. `attachments.graph` refuses the flow form instead of shadowing it.

**Core constraint(s).**
- **⚠ One config mutation path**: every fix lands in `contracts/config_edit` and the bridge. The transforms stay pure (text in, text out), and only `edit_config_file()` touches the filesystem. A JSONC config is still edited through the bridge and spliced in place, never by a second set of transforms. Every edit still re-parses its own output before it lands.
- **⚠ Secrets are unleakable by construction**: "on disk it is `0600` before it is renamed into place" is made true: the temp file is private before its first byte, not after its last.
- **An edit touches its key and nothing else**: a comment, a neighbour, a value's text and a file's mode and identity (the link it is reached through) stay as they were.
- **A printed success is a landed change**: an editor that cannot edit a shape refuses it by name, as `set_attachments_graph` does. It never appends beside it.
- **55f's one name rule** stands (names both formats can edit). This item makes the bridge honour it for values and makes the refusal for an existing odd name say so; it does not widen or narrow the rule.
- **Development runs on the dev channel**: any live pass names `apogee-dev`; probes and tests use a temporary `APOGEE_HOME`, never `~/.apogee` or `~/.apogee-dev`.

**Seam + files.**
- `data/contracts/config_edit.cpp`:
  - `is_section_header` / `find_section`: a header followed by a comment is the section's header. A header carrying a flow value is a shape the line editor refuses: one shared check, generalised from `set_attachments_graph`'s, called by every editor that would otherwise append a section (`append_entry`, `delete_entry`, `set_section_rendered`, `set_subsection_rendered`, `add_allowed_host` and its siblings, `set_backend_path_field`).
  - `yaml_scalar`: `\n`, `\r` and `\t` are written as escapes inside the double quotes.
  - A string renderer for string-typed fields that also quotes text the bridge would read as a number (finding 3).
  - `comment_start`: inside a single-quoted scalar, `''` is an escaped quote, not a close.
  - `unquote` (`:2242`): decodes the escapes the writer now emits, as yaml-cpp does.
  - The not-found refusals in `delete_entry` and `set_backend_path_field`: an odd name gets the rename pointer.
  - `write_file_atomically`: the temp file is created private (POSIX `0600` at creation, before any write) under `private_mode`, and an existing target's mode is carried onto the temp before the rename.
- `data/contracts/config_edit.h`:
  - `edit_config_file` writes through a symlinked config to its target.
  - `write_file_atomically`'s comment is corrected to what the code does.
- `data/contracts/config_yaml.cpp`: unchanged in behaviour unless the string-field fix lands on the bridge side (open call).
- Tests:
  - `tests/data/contracts/config_edit_test.cpp`: the YAML header shapes, the multi-line round trip, `''`, the symlink and the modes.
  - `tests/data/contracts/config_jsonc_test.cpp`: number-looking values, multi-line through the bridge, and the odd-name refusal. Lines 587-588 change with the fix.
  - `tests/data/secrets/store_test.cpp` and `tests/presentation/httpserver/admin_auth_test.cpp`: no temp file is ever readable beyond the owner.
  - `tests/scripts/cmake/config_e2e.cmake`: the real binary over a symlinked config and a flow-mapping header.

**Reference.**
- [Milestone AR](../../assistant/MILESTONES.md#milestone-ar--the-audits-cli-findings)'s `config-editor-correctness` (55f), which findings 2, 3 and 4 extend:
  - whole-value replace at every edit;
  - the quote-aware `comment_start`;
  - the one name rule, with its add refusal pointing to `rename-backend`.
- [Milestone U](../../assistant/MILESTONES.md#milestone-u--the-control-plane)'s `private_mode`, which finding 7 regresses.
- `set_attachments_graph` (`config_edit.cpp:2173-2190`): the refusal of a shape the line editor does not see, which finding 1 generalises.
- The fs tool's edit carries the file's mode over (`business/tools/fs.cpp:629-638`), the precedent for keeping a mode across the rename. It does so after the rename; this item does it before.
- M33 (`config-verbs-finish-their-job`, [Milestone AO](../../assistant/MILESTONES.md#milestone-ao--the-desktop-clients-cli-growths)): the role clearing whose messages finding 1 makes false.
- Siblings:
  - [57f](config-upgrade-and-check-remedies.md) (config upgrade under the lock, check's unknown-key remedy) is the other config-editing item of this sweep.
  - [57o](tool-gate-enforces-what-it-shows.md) and [57k](windows-filesystem-safety.md) own the fs tool's link handling.

**Decisions made** (dated):
- 2026-10-31: Specced from the second CLI/GUI bug sweep (the user's call: every finding becomes a v0.2.1 item in its queue, CLI track 57, GUI track 6).
- 2026-10-31: One item, because all seven are the config editor's text and file fidelity: the same two files (`config_edit.cpp/.h`), the same invariant (one config mutation path), and the same test files. The secret-mode finding came in from the serve-and-tools-security dimension, but its fix is the same `write_file_atomically` the config edit writes through.
- 2026-10-31: Finding 5's fs-tool clause is dropped as checked at HEAD: the fs tool writes to an already resolved path (`fs.cpp:201-218`). Its link questions are 57o's and 57k's.
- 2026-10-31: The JSONC format's own header shapes (finding 1) are not in scope unless the in-item probe shows otherwise: the bridge always writes bare block headers.

**Open calls:**
- [default: a section header followed by a comment (`backends:   # my models`) is the section's header: edits land under it and the comment is kept. A header holding a flow value (`memory: {recall: true}`) is refused by every editor, generalising `set_attachments_graph`'s message ("the <section>: line holds more than its key -- the editor edits the block form, '<section>:' alone with its fields under it"). The editor never rewrites flow to block. `apogee config migrate` is named in the refusal as the way to the JSONC form, once the item has checked that migrate carries a flow mapping over.]
- [default: the same comment tolerance applies to an entry line (`  a:   # mine`) under a section, checked in-item, since `entry_name` (`:143-160`) has the same bare-line test.]
- [default: `yaml_scalar` writes `\n` as `\n`, `\r` as `\r` and `\t` as `\t` inside the double quotes, so the value stays on one line in YAML and reads back exactly through yaml-cpp and the bridge. A block scalar is not used: it would need indentation-aware writing at every caller.]
- [default: a string-typed field (role pointers, `model`, the other name and path fields) is rendered by a string renderer that quotes anything YAML or the bridge would read as a non-string: `json_number` text, besides what `yaml_scalar` already quotes. Numeric fields (`max_tokens`, …) keep their plain rendering. The string fields are named in one table beside `backend_editable_fields`. Number-looking names stay allowed (55f's rule unchanged). Refusing them at add instead would narrow the rule, and is the alternative if the field table proves unworkable.]
- [default: the odd-name refusal reads `backend '<name>' is not one both config formats can edit -- <rule>; rename it with 'apogee config rename-backend'`, for every set and delete on such a name, whether or not the config holds it. Both halves are true either way.]
- [default: only the config edit (`edit_config_file`) writes through a symlink: it resolves the link and writes the temp beside the target, so a dotfiles-managed config keeps its link. The other `write_file_atomically` callers keep replacing what is at their path: they write files Apogee owns under its root, and following links there is a confinement question 57o and 57k own. A dangling config link is refused with the link and its target named.]
- [default: `write_file_atomically` creates the temp private before its first byte under `private_mode` (POSIX open with `0600`), and for every caller carries an existing target's mode onto the temp before the rename. A `0600` config, ledger or script stays `0600`. Windows, where modes do not exist, is unchanged (`supports_private_modes`).]

**Guardrail(s).**
- Each finding's failure is pinned as a hermetic test (temporary `APOGEE_HOME`, mock backend, no network):
  - **Headers:** each of the four sandbox YAML shapes runs add-backend, delete-backend, `set memory.recall` and add-allowed-host. The comment-tailed header edits in place and keeps its comment. The flow form is refused, its bytes unchanged. No test file ever ends with a second top-level block of the same key.
  - **Multi-line:** `"You are terse.\nAnswer in French.\n\nBe kind."` round-trips byte for byte through set and add, in both formats. On YAML, replacing it with `short` leaves no stray line, and a following add-backend succeeds.
  - **Numbers:** on JSONC, `1.50`, `1e3` and `2.0` as a default, a model and a backend name each read back as the same string. Check passes `default_backend` after `set-default 1e3`. `max_tokens 9` is still the number `9`.
  - **`''`:** `model: 'it''s #1'  # c1` set to `NEWa` reads `model: NEWa  # c1`.
  - **Symlink:** a config reached through a symlink is edited at its target. The link is still a link, the target holds the edit, and a dangling link is refused.
  - **Odd names:** `"my model"` and `"qwen@home"` on JSONC: set and delete name the rename verb and never say `not found`. `config_jsonc_test.cpp:587-588` asserts the new text.
  - **Modes:** a test hooks the write (or polls the directory over many writes, as the probe did) and asserts no temp file is ever readable beyond the owner under `private_mode`. A `0600` config stays `0600` after `set-default`.
- `config_e2e.cmake` runs the symlinked config and the flow-header refusal on the real binary.
- Mutation: reverting each fix (header match, escape, string quoting, `''`, link resolution, mode order) fails its test.

**Acceptance criteria:**
- [ ] On a YAML config whose header carries a comment, `config add-backend c` adds `c` under that header, and `config get backends` lists it. On a flow-mapping header, every editor refuses by name and the file is unchanged. No editor prints a success it did not write.
- [ ] A multi-line `system_prompt` set or added in either format reads back with its newlines exactly. On YAML, a later edit leaves the file valid.
- [ ] On JSONC, `config set-default 1e3` writes `"default": "1e3"` and check names no unconfigured backend. `--model 1.50` reads back `1.50`.
- [ ] `model: 'it''s #1'  # c1`, edited, keeps `# c1` with no fragment of the old value.
- [ ] `apogee config set` on a symlinked `config.json` changes the link's target and leaves the link in place.
- [ ] `config set-backend 'my model' …` on JSONC says the name is not editable and names `apogee config rename-backend`, never `not found in config`.
- [ ] No secret's temp file is ever readable beyond its owner, and a `0600` config keeps `0600` across any edit.
- [ ] The full suite is green.

**Scope note.** Earmarked for v0.2.1 (track 57); ungated. Out of scope:
- the fs tool's and the other writers' symlink confinement (57o, 57k);
- `config upgrade`'s lock and check's remedies (57f);
- migrating YAML configs automatically;
- widening or narrowing 55f's name rule;
- symphony names (55f's recorded "Not fixed").
