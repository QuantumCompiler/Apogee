# Config engine: typed loader, template, and comment-preserving mutation

**What / why.** One config file for the whole harness, built in two internal phases inside one item. Phase 1 (loader): typed loading of ~/.apogee/config/config.yaml — backends: map (types anthropic|openai|google|llamacpp|mock with api_key/model/model_path/context_size/system_prompt/temperature/max_tokens), models role pointers (default, default_embedding, default_extraction), paths:, and top-level UX keys (status_mode, color) — with ${ENV_VAR} expansion in string fields, case-insensitive backend-name handling with explicit collision rejection, and a SaveConfigTemplate that byte-matches the shipped starter sample. Phase 2 (mutation): the load-bearing comment-preserving edit helpers — section-scoped, line-oriented text surgery (locate an entry's block by indentation, splice/replace/delete lines, validate by re-parsing) — never marshal-and-rewrite, plus the `apogee config` command family (init, add-backend flags+interactive, delete-backend, set-default / set-default-embedding / set-default-extraction, get dotted keys, format), registered into the root-command scaffold and argument parser standardized in cpp-project-skeleton. Every future mutating surface (embed auto-registration, the entire admin plane's byte-identical-edit parity) routes through these helpers, so they must exist before any of them. The file-format decision made here is load-bearing for everything after — decide it before writing a line. The closed backend-type enum (anthropic|openai|google|llamacpp|mock) is a deliberate v0.1.0 boundary: later items widen it as recorded events, never silent edits — the CLI-backend types (`claude-cli`, then `codex-cli`/`gemini-cli`/`ollama-cloud` per their items) and eventually a SafeTensors-capable type for training (see training-distillation). Design the loader's type dispatch so a widening is one registration, not a scattered switch.

**Core constraint(s).**
- ALL config mutations, from any surface forever, go through these helpers — an HTTP-made edit must be byte-identical to a CLI-made one (Ommi's parity invariant, made cheap by building this first); marshaling the struct back to disk is forbidden
- Sample template and SaveConfigTemplate must always match (test-enforced)
- Case-insensitive name collisions rejected, never merged (Ommi's Viper-lowercasing lesson, made explicit)
- Local by default, cloud by choice: a fresh config with zero keys and zero models must load and pass check
- Helpers are section-scoped end-to-end so same-named entries in different sections can never cross-contaminate
- check-style tooling never auto-edits config; it reports the exact command to run
- ${ENV} values are stored literally and expanded at load
- Role pointers are consumed through ONE shared resolver once it exists (model-profiles-and-management) — CLI and HTTP must never grow independent resolution chains (a real Ommi bug class)

**Seam + files.** lib/src/cli/source/harness/config.h/.cpp (Config, BackendConfig structs, LoadConfig, expandEnv), lib/src/cli/source/harness/config_template.cpp (SaveConfigTemplate), lib/src/cli/source/harness/config_edit.h/.cpp (AppendBackend/DeleteBackend/SetDefault*/FormatConfig/Get section-scoped text-surgery helpers), lib/src/cli/source/commands/config_cmd.cpp, assets/config.yaml (checked-in starter sample with commented examples incl. a fill-in user-supplied embedder), lib/src/cli/tests/harness/config_test.cpp + config_edit_test.cpp (golden-file byte-diff suite).

**Reference (Ommi).** src/harness config.go (Viper load, ${ENV} expansion, role pointers, SaveConfigTemplate byte-match test) plus the comment-preserving Append*/Delete*/Set*/FormatConfig helpers and Milestone J. Divergences: backend types gain openai/google with api_key fields; all claude-CLI-only fields (native_tools, permission_mode, allowed_tools) are dropped; claude entries' deliberate context_size omission does NOT carry over — Apogee owns context tracking for cloud, so cloud entries need window sizes (the compiled model→window fallback table itself is owned by harness-core). Ommi's own answer to comment preservation was text surgery, since marshaling a struct back strips comments and reorders keys.

**Decisions made** (dated):
- 2026-08-24 — Planned from Ommi's documentation (parallel digest → two planning lenses → merge → adversarial verify). Position in the queue: Ommi's sequencing lesson #3 verbatim: config engine with comment-preserving edits early — Milestone J sits directly on A and beneath every CRUD surface, and five hard invariants lean on byte-identical config writes.

**Open calls:**
- [user] Config format: YAML vs TOML — THE decision; no mainstream C++ YAML lib round-trips comments (text surgery required, Ommi's own approach), while toml++ round-trips natively at the cost of ecosystem convention. Decide before any config-touching feature.
- [default: follows the format decision — yaml-cpp for YAML, toml++ for TOML] Parse library
- [user] Data-dir name ~/.apogee/ — confirm
- [default: flags-only for v0.1.0, interactive step-through later] Interactive add-backend prompting depth

**Guardrail(s).** The golden-file byte-diff suite is the contract — every new helper lands with before/after fixtures including comment-dense and edge-of-section cases; template byte-match test in CI so drift fails the build; loader fuzz/garbage-input tests so a malformed config yields a message, never a crash.

**Acceptance criteria:**
- [ ] LoadConfig parses a full sample config into typed structs; unknown backend type is a clear error; ${ENV_VAR} expansion in api_key/model_path is test-covered
- [ ] Checked-in sample config byte-matches SaveConfigTemplate output (Ommi's template-drift test, ported); config is valid and loadable with zero cloud keys and zero models present
- [ ] Two backend names differing only by case are rejected at load and at add with a named-collision error, never silently merged
- [ ] add-backend then delete-backend on a heavily commented config leaves every untouched line byte-identical (golden-file byte-diff tests); a failed edit leaves the file unmodified (write-temp-then-rename)
- [ ] Every helper's output re-parses cleanly through the loader; `apogee config get <dotted.key>` and `config init` work end to end
- [ ] set-default-extraction writes the models.default_extraction key and validates the target backend exists; the written form is the one the future shared role resolver (model-profiles-and-management) is specified to read

**Scope note.** earmarked for v0.1.0. Gate satisfied: the C++ project skeleton shipped 2026-08-25 ([MILESTONES.md](../assistant/MILESTONES.md) → Milestone A) — this is now the topmost claimable item.
