# Registration offers

**What / why.** A fresh install knows how to talk to seven cloud providers and registers none of them: `apogee check` says "backends: none configured — run: apogee config add-backend", and every entry is typed by hand. With [provider-detection.md](provider-detection.md) able to see what this machine has, registration becomes an *offer*: **`apogee providers scan`** prints the detection table (each provider with its tier and evidence, in plain words), and **`providers scan --register`** writes one default entry per detected-but-unregistered provider through the one config editor — plus a **first-launch offer**: the first time an interactive terminal session starts with providers detected and none registered, one question ("Found claude, codex and gemini — register them as backends? [y/N]"), asked exactly once ever, declining remembered. Never on a pipe, never in machine mode or under `serve`, never silently: config is the user's file, and nothing is written to it on Apogee's initiative — the user says yes first, every time. The SPEC's fresh-install acceptance is preserved exactly: with nothing detected or the offer declined, Apogee behaves byte-for-byte as today.

**Core constraint(s).**
- **Consent-shaped, always.** No code path writes a backend entry without an explicit yes in this session (`--register`, or the offer answered `y`). The offer appears on an interactive terminal only — a pipe, machine mode, `serve`, and `--quiet` never see it — and only once ever: asked-and-declined is recorded (in the provider cache, not in config) and never re-asked; `providers scan` remains the explicit way back.
- **One mutation path:** entries are written by the comment-preserving config editor through the same code `config add-backend` uses — same validation, same `--force` semantics (never applied here: an existing entry is never touched; a name collision is skipped and said).
- **Idempotent and honest:** a second `--register` is a no-op that says so per provider; every write is reported with the entry name and type; nothing sets `models.default` unless no default exists, and then it says which it chose.
- **Detection is consumed, never re-run on the hot path** — the scan refreshes per 27a's staleness rules; the first-launch offer reads the cache and probes nothing expensive itself.
- **No network, no spend:** registering is a config write; nothing validates a key or a login against a provider API.
- Code style carries: `.h`/`.cpp` pairs, smart pointers only.

**Seam + files.**
- `commands/providers_cmd.h/.cpp` (new): `providers scan [--register] [--refresh]` — the table rendering (tier words, evidence strings), the registration pass over the knowledge table's defaults.
- `commands/chat.cpp` (and the shared interactive-entry seam it uses): the first-launch offer hook — fires only when a terminal is interactive, providers are detected, zero provider backends are configured, and the asked-once marker is unset.
- `harness/config_edit.cpp`: reused as-is (the add-backend writer); no new writer.
- The per-provider default entry (name, type, whether a `model:` is written) lives beside 27a's knowledge table — one table, one home.
- Tests: `tests/commands/` — register-pass tables (none detected / some / all registered already / name collision), the once-ever offer lifecycle, the pipe/machine/serve suppression, config byte-comparison for the declined path.
- Consumes: [provider-detection.md](provider-detection.md) (tiers, cache, knowledge table); the shipped `config add-backend` and editor; 26o's precedent that session-scoped consent never loosens config.

**Reference (Ommi).** No analog — Ommi never offered registration; its backends were always hand-configured. The consent shape follows Apogee's own recorded spirit (nothing spent or changed at scale on Apogee's initiative — the 26e rule, applied to config).

**Decisions made** (dated):
- 2026-10-03 — Split from the provider-detection spike; gated on 27a because offers without detection are guesses.
- 2026-10-03 — Both surfaces, explicit command and one-time offer (the spike's recommendation, the user not objecting): the command is the contract, the offer is the first-run courtesy — and the offer is interactive-only because a pipe cannot consent.
- 2026-10-03 — One entry per provider, not a model catalog: no CLI can enumerate its models offline, and a catalog would be a guess with a shelf life. The entry omits `model:` where the provider CLI supplies its own default; richer model setup stays the user's `config` work.

**Open calls:**
- [default: entry names are the provider's short name (`claude`, `codex`, `gemini`, `ollama`, `anthropic`, `openai`, `google`); a taken name is skipped and said, never suffixed] Naming.
- [default: API-type providers are offered only when their key already resolves (tier ≥ credentials-present); CLI types when installed — a CLI present but without credential evidence is offered with its tier said, since its own first run handles login] What qualifies.
- [default: the asked-once marker lives in `cache/providers.json`; deleting the cache forgets the decline, which is acceptable for disposable state] The marker.

**Guardrail(s).**
- Config byte-identity on every no path: scan without `--register`, offer declined, pipe/machine/serve/quiet, nothing detected.
- The once-ever lifecycle: offer → decline → never again across sessions; offer → accept → entries present and marker set.
- Registration tables: each tier/registered-state combination writes exactly the expected entries, second pass no-op, collision skipped-and-said.
- The entries written round-trip the config editor with comments preserved (the editor's own tests extended with these shapes).

**Acceptance criteria:**
- [ ] In a sandbox with fake `claude`/`codex` on PATH and no backends, `providers scan` prints both with tier words and registers nothing; `providers scan --register` writes two entries through the editor and reports each; running it again changes nothing and says so.
- [ ] The first interactive session in that sandbox asks once; `n` leaves config byte-identical and no later session asks again; `y` registers and names what it wrote.
- [ ] `apogee complete "hi" < /dev/null` and machine mode never print an offer, with or without providers detected.
- [ ] With a backend already named `claude`, `--register` skips it, says so, and touches nothing.

**Scope note.** Item **27b**, earmarked for **v0.1.4**; **gated on [provider-detection.md](provider-detection.md) (27a)**. Out of scope: `check`/`models list` rendering ([provider-surfacing.md](provider-surfacing.md)); network key validation; model catalogs per provider; changing `config add-backend` itself.
