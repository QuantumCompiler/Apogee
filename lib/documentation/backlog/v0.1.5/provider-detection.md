# Provider detection and the cache

**What / why.** Apogee already speaks to every provider — nine backend types, including `claude-cli`, `codex-cli`, `gemini-cli` and `ollama-cli` — but it cannot *see* them: a fresh install registers nothing, and nothing anywhere knows whether `claude` is installed, logged in, or gone. This item builds the eyes: a **detector** that, for each known provider, establishes three honesty tiers — **installed** (the binary found, its version; or for API types, a key source resolving), **credentials present** (on-disk evidence, named), **verified** (a recorded successful real turn — written by [provider-surfacing.md](provider-surfacing.md), read here) — and a **cache** that makes the answer free at runtime. The costs are measured, not guessed (2026-10-03 spike, this machine's real CLIs): cold `claude --version` **2,596 ms**, `gemini --version` **700–900 ms on every run**, `codex --version` 58 ms, `command -v` instant — so probing at launch would multiply Apogee's startup several times over, and the cache is the design, not an optimization. Cache entries are fingerprinted by **binary path + mtime** (the pattern the GGUF sweep work set): the expensive version probe re-runs only when the binary actually changed; the cheap checks (`command -v`, file existence, env/key-store lookup) re-run on every explicit scan. Auth evidence is heterogeneous and the detector says so per provider: codex has a cheap truthful status command (`codex login status`, 65 ms, non-interactive); claude and gemini offer file existence only (`~/.claude.json`, `~/.gemini/google_accounts.json`) with the real credential possibly in the keychain — which Apogee **never reads**. "Authenticated" is a word the detector never uses; its tiers are what it can actually know.

**Core constraint(s).**
- **Never on the hot path.** No chat, `complete`, `serve` or machine-mode startup ever runs a probe; detection happens inside `check`, an explicit `providers scan` ([provider-registration.md](provider-registration.md)), or not at all. Pinned by test, not convention.
- **Existence and exit codes only — never contents.** The detector tests that an evidence file exists; it never opens one, and it never touches any keychain or another application's credential store (secrets hygiene). Status commands run only from a per-provider allowlist of known non-interactive, offline, no-spend invocations with a hard deadline each.
- **No network, no spend, ever.** Version and status probes are local process spawns; nothing calls a provider API. A probe that hangs is killed at its deadline and recorded as unknown, honestly.
- **One knowledge table.** Each provider's facts — binary name, version argument, evidence paths, status command (if any), key environment names, key-store id — live in one table beside the probe code, so adding a provider is a row, not a hunt.
- **The cache is disposable state**, under the `cache/` row of the one layout declaration (`contracts/layout.h`): versioned schema, deleted or corrupt rebuilds silently on the next scan, never required for correctness — every consumer must behave sanely with no cache at all.
- **Layering:** the harness never includes backends; the detector lives beside the backends and is called by command surfaces. Child process output is captured, never inherited. Code style carries: `.h`/`.cpp` pairs, smart pointers only.

**Seam + files.**
- `backends/provider_probe.h/.cpp` (new): the knowledge table; `probe(provider, runner)` pure over an injected process-runner and filesystem view; the tier model (`Installed`/`Credentialed`/`Verified` with per-tier evidence strings for honest display).
- `backends/provider_cache.h/.cpp` (new): load/store of the cache file, the path+mtime fingerprint check, the verified-turn record slot ([provider-surfacing.md](provider-surfacing.md) writes it).
- `contracts/layout.h/.cpp`: the `cache/providers.json` row (additive).
- Key-presence for API types through the existing resolver (`auth list`'s "which source each configured backend uses" machinery) — presence only, never the key's bytes in the result.
- Tests: `tests/data/backends/provider_probe_test.cpp` — fake PATH trees and scripted runners covering the state matrix (absent / installed-only / evidence-present / status-command variants / hung probe); fingerprint tests (touch the binary → one re-probe); the no-hot-path pin (the chat/complete startup path contains no probe call — asserted the way the layering greps assert).
- Consumes: the key resolver (shipped, Milestones T–U); the fingerprint pattern (shipped with the M2 work). The exclusion-list rule is untouched — detection never implies routing.

**Reference (Ommi).** No provider-detection analog. The adjacent precedents are Ommi's `check --fix` as the local-machine doctor (its CLAUDE.md records the rationale) and `autoDetectTrainer`'s hardware-based backend selection — detection feeding choice, which this track adopts for providers.

**Decisions made** (dated):
- 2026-10-03 — Asked for by the user, targeted **v0.1.4**, slotted ahead of the queued releases (the 27–30 → 28–31 renumber). The spike's measurements (probe costs, evidence heterogeneity, the blind doctor) are this document's What/why.
- 2026-10-03 — Cache over launch probing is forced by measurement: 2.6 s cold for one provider is not a startup cost, it is a startup multiple.
- 2026-10-03 — Three tiers instead of a boolean: the spike showed "authenticated" is not cheaply knowable for two of three CLIs, and claiming it from file existence would be a lie the UI repeats.

**Open calls:**
- [default: first-cut provider rows — the four CLI types (`claude`, `codex`, `gemini`, `ollama`) and the three API types (anthropic, openai, google via the key resolver); grow by row] Breadth.
- [default: cheap checks re-run on every scan; version/status probes only on fingerprint change or `--refresh`; nothing expires by clock alone] Staleness.
- [default: one JSON file `cache/providers.json` with a `schema` field; unknown fields ignored on read] Cache shape.

**Guardrail(s).**
- State-matrix table tests per provider row, including the hung-probe deadline and the corrupt-cache rebuild.
- The no-hot-path pin: startup paths of `chat`/`complete`/`serve`/machine mode call no probe (asserted structurally, grep-style, like the layering rules).
- The no-contents pin: the probe API accepts an existence-only filesystem view, so reading a credential file cannot compile.
- A probe sweep over the knowledge table completes in under ~100 ms when fingerprints match (cache hit path), measured in-test with the scripted runner.

**Acceptance criteria:**
- [ ] In a sandbox (temp `APOGEE_HOME`) with fake provider binaries on PATH, a scan writes the cache with correct tiers and evidence strings per provider; removing a binary flips it to absent on the next scan with no error.
- [ ] Touching a fake binary re-runs exactly its version probe (probe counts asserted); an unchanged one is answered from the cache.
- [ ] A deleted or truncated cache file rebuilds silently on the next scan.
- [ ] `apogee chat` startup performs zero provider probes, before and after this item (pinned).

**Scope note.** Item **28a**, earmarked for **v0.1.5** — the track's foundation; gated on nothing pending. Out of scope: writing config ([provider-registration.md](provider-registration.md)); `check`/`models list` rendering and the verified-record write ([provider-surfacing.md](provider-surfacing.md)); any network validation of keys; reading keychains, ever.
