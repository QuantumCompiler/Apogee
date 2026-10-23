# The config editor writes only what it means: block lists, fragments, names and concurrent edits

**What / why.** The one config mutation path (`contracts/config_edit.h`, CLAUDE.md's ⚠ invariant) promises that an edit touches its key and nothing else, and that a bad transform cannot corrupt a config. The CLI/GUI audit (2026-10-22, three-lens verified) found four ways an accepted edit corrupts or strands the file:

1. **Clearing or replacing a block-list `server_tools` on a YAML config corrupts the neighboring field** (high). `set_backend_field` routes `server_tools` to `set_backend_path_field` (`config_edit.cpp:1897-1908`), which replaces only the key line's value token (`:1612-1641`), and `remove_backend_field` erases a single line (`:1666-1673`). On a YAML config whose `server_tools:` is a block list (a shape the loader accepts), `config set-backend claude --server-tools ""` printed `cleared server_tools`. The orphaned item lines then folded into the previous field: the file read `api_key: ${ANTHROPIC_API_KEY}\n      - web_search`, and `config get backends.claude.api_key --reveal` printed ` - web_search`. The re-parse passes, so the corruption lands. Replacing the list fails with a raw yaml-cpp error. 53a's `set_backend_adapters` already removes a block value whole (`:1782-1804`). JSONC configs are unaffected.
2. **`config set` corrupts a YAML line whose value contains `#`.** `with_value`'s hash scan (`config_edit.cpp:2121`) and `set_section_rendered`'s (`:2064`) are not quote-aware. `comment_start` (`:1718`) is, but only `set_backend_adapters` uses it. Probe: `url: "http://127.0.0.1:8888/#frag"  # my instance` became `url: "http://10.0.0.2:8080"#frag"  # my instance` after `config set tools.search.url http://10.0.0.2:8080`. The damage is durable (later sets carry it forward), and `config get` keeps showing the clean value, so nothing surfaces it. An unquoted `#` after a non-space is mangled too.
3. **`add-backend` accepts names the JSONC editor then cannot find.** `append_entry` (`config_edit.cpp:404-410`) allows any name without space, tab, colon or `#`. But set-backend and delete-backend run through the `yaml_of_json` bridge (`:2886`), where keys outside `plain_key`'s `[A-Za-z0-9_./-]` (`config_yaml.cpp:63-74`) are double-quoted, and `entry_name` (`:134-150`) returns the quoted text, so `find_entry_line` never matches. Probe: `add-backend 'qwen@home'` succeeded, then set-backend and delete-backend both said `backend 'qwen@home' not found`, while `config get backends` listed it. Only `rename-backend` (JSONC-native, `:2947`) reaches it. The same name edits fine on YAML, a format-parity break.
4. **Two config edits at the same time lose writes** (completeness pass, with probe; reproduce first). `harness::edit_config_file` (`config_edit.h:466-477`) reads, edits and renames into place with no lock. Probe: 16 parallel `config add-backend bN --type openai` each printed `added backend 'bN'`, and 6 entries remained. The app triggers this: each Settings picker change runs its own task (`SettingsViewModel.swift:251-253`), and job-end registrations and `serve`'s admin routes share the function.

**Core constraint(s).** **The one mutation path stays one** (the ⚠ invariant), and gains what it was missing. Block-aware replace and remove for every list-valued field, through the module's existing block machinery. Quote-aware comment detection on every scan, through `comment_start`. A name rule that is the editable set in both formats, so add refuses what set/delete could not reach. **An exclusive lock around read-edit-rename**, so concurrent editors serialize rather than overwrite (a lock file beside the config, held for the edit only). **Byte-identical golden round trips** keep holding for every untouched line; JSONC and YAML behave alike.

**Seam + files.** `data/contracts/config_edit.cpp/.h` (`server_tools` through the block path; `comment_start` in `with_value`/`set_section_rendered`; `append_entry`'s name rule aligned to `plain_key`, or the bridge's `entry_name` matching quoted keys; the lock in `edit_config_file`), `data/contracts/config_yaml.cpp` (if the bridge carries quoted names). Tests in `tests/data/contracts/config_edit_test.cpp` and `config_jsonc_test.cpp`: the block-list clear and replace on YAML (neighbor bytes unchanged); `#`-bearing values (quoted, unquoted, fragment); an `@`-named entry's full lifecycle in both formats, or its refusal at add; N concurrent edits all landing (a multi-process test over the real function).

**Reference.** The ⚠ one-config-mutation-path invariant and its golden suite; 47a (`server_tools`) and 53a (`set_backend_adapters`, the block-aware precedent); the CLI/GUI audit (2026-10-22).

**Decisions made** (dated):
- 2026-10-22: Specced from the CLI/GUI audit (the user's call). The concurrent-edit finding came from the completeness pass, reproduced first.

**Open calls:**
- [default: names are restricted at add to the set both formats edit (`plain_key`'s), refused naming the rule. That is simpler and safer than teaching the bridge quoted keys. Existing odd names stay reachable through `rename-backend`, said in the refusal.]
- [default: the lock is an advisory file lock around the whole read-edit-rename, waiting a bounded time and failing said, never silently.]

**Guardrail(s).** The golden round-trip suite extended per finding; the concurrent-edit test; the existing invariant tests kept green.

**Acceptance criteria:**
- [ ] Clearing or replacing a block-list field never touches a neighbor; `#` inside a value survives any edit.
- [ ] Every name add-backend accepts is editable and deletable in both formats.
- [ ] Concurrent edits all land; the full suite is green.

**Scope note.** Earmarked for v0.2.0 (track 55); ungated. Its sibling for the read and validation side is [`config-reads-and-validation.md`](config-reads-and-validation.md) (55g).
