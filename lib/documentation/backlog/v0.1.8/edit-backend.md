# `config set-backend`: editing an entry in place

**What / why.** A backend's fields cannot be changed — the user's pasted verb table (2026-10-14) shows `config`'s twenty-four verbs are add/delete/set-default/scan shaped, so today's only "edit" is delete-and-re-add, which loses the entry's comments and its place, and briefly orphans every reference while the key is gone. The editor already half-knows how: **`set_backend_model_path` and `set_backend_mmproj_path` transforms exist** (`contracts/config_edit.h:144/149` — repair and migrate's tools), each touching exactly its field with comments kept. This item ships the verb over that precedent: **`apogee config set-backend <key> [--model …] [--model-path …] [--mmproj …] [--window …] [--type …] …`** — one flag per editable field, each flag one transform, only the named fields touched, everything else byte-stable — plus the admin twin, so an entry is finally editable in place from every mode.

**Core constraint(s).** **One config mutation path**: every field is its own pure transform in `contracts/config_edit.h` (the two existing ones reused as they are), composed into one atomic write per invocation — re-parsed before landing, comments and ordering kept, the untouched fields byte-identical (the golden idiom). **A key is never taken on the command line**: there is **no `--api-key` flag** — storing a key stays `auth add`'s (echo-off, `--stdin`, `--from-env`), and the refusal names it (the secrets invariant's rule, verbatim). `--type` changes what the entry *is*, so it revalidates as `add-backend` would (an mlx entry's ladder, a llamacpp path's existence stay those paths' business at use time, exactly as today — the editor validates shape, not reachability). Renaming is **not** this verb's (`(consumed decision)` — [`rename-propagation.md`](rename-propagation.md) owns names and their propagation); `set-backend` edits fields under a fixed key. Every mode (ADR 0002): the admin config slice gains the field-edit twin calling the same transforms, byte-identical; the exec line covers the verb in the shell (ADR 0010); completion offers the keys and flags from live state (ADR 0007, free).

**Seam + files.**
- `data/contracts/config_edit.h/.cpp` — the missing per-field transforms beside the two that exist (`set_backend_model`, `set_backend_type`, `set_backend_window`, … — the editable field list enumerated from the entry's own schema, one transform each).
- `presentation/cli/config_cmd.cpp` — the verb: flags to transforms, at least one field required, unknown key refused by name, the one atomic write, the result line naming the fields changed and the file.
- `presentation/httpserver/` — the field-edit twin in the config CRUD slice, parity-tested.
- Tests: per-field goldens over the comment-dense fixture (each flag touches exactly its span; combinations compose; zero flags refused; unknown key refused; `--api-key` absent and the ask refused with the secrets rule), twin byte-parity, `cli.config_lifecycle` grown by an in-place edit, the classification row.

**Reference.** `config_edit.h:144/149` — the existing field transforms this generalizes; the one-mutation-path invariant and its golden suite (the exact test idiom); the secrets invariant (the no-key-on-argv rule quoted); [`rename-propagation.md`](rename-propagation.md) — names consumed there; the user's finding (2026-10-14).

**Decisions made** (dated):
- 2026-10-14 — The set lands in v0.1.8 as track 46 (the user's placement call); the edit gap ships as its own item beside the renames, each useful alone.
- 2026-10-14 — No `--api-key` on `set-backend`, ever — keys stay `auth add`'s doors (the secrets invariant applied, not reopened).
- 2026-10-15 — The editable field list is enumerated from the entry schema's writable fields minus the key and the api-key, tied by a test — a field added later fails the enumeration rather than silently missing its flag (the default, confirmed 2026-10-15).
- 2026-10-15 — Clearing an optional field spells the empty string (`--mmproj ""` removes the line, said); one spelling, no `--unset-*` family (the default, confirmed 2026-10-15).


**Guardrail(s).** The per-field golden matrix (exact spans, byte-stable elsewhere, compositions, re-parse); the refusals (no fields, unknown key, api-key); twin parity; the enumeration test tying the flag set to the schema; `cli.config_lifecycle`'s in-place edit on the real binary.

**Acceptance criteria:**
- [ ] `config set-backend <key> --model <m>` (and every editable field) edits exactly that field in place, comments kept, references untouched, the result naming what changed.
- [ ] No key can be set through it; the refusal quotes the rule and names `auth add`.
- [ ] The admin twin is byte-identical; the exec line runs it; the full suite is green.

**Scope note.** Earmarked for v0.1.8; second of track 46, ungated (independent of the renames — no gate just to mirror the story). Out of scope: renames (the sibling's), suite/symphony field editors (their own verbs exist), any validation beyond what `add-backend` does.
