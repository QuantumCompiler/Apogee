# Provider surfacing

**What / why.** The doctor is blind and the listing is mute, both measured on the shipped binary (2026-10-03 spike, sandboxed): with `claude` stripped from PATH entirely, `apogee check` still prints `ok   backend: claude  claude-cli` — "ok" means only that the config entry parses — while the *use-time* error already knows better ("'claude' was not found on PATH. Install the Claude CLI and log in, or set 'binary' on this backend to its full path"). And `models list` shows `STATE -, VERIFIED -` for every provider row while llamacpp rows carry "size ok, header ok". This item makes the existing surfaces tell the truth the cache ([provider-detection.md](provider-detection.md)) already holds: **`check`** gains a **Providers** section — each configured provider backend with its tier in plain words, a missing binary **warning** with the same remediation the use-time error gives (promoted to the doctor, where it belongs), a detected-but-unregistered provider mentioned with the `providers scan` pointer; **`models list`** fills `STATE` for provider rows from the tiers (`installed`, `no binary`, `key resolves`, `no key`) and `VERIFIED` from the **last-successful-turn record** — which this item starts writing: when a provider backend completes a real turn, the cache's verified slot gets the provider and date, locally, nothing sent anywhere. The three tier words plus a date replace the dashes, and nothing anywhere says "authenticated".

**Core constraint(s).**
- **`check` stays offline and fast:** the section reads the cache and runs at most 28a's cheap re-checks (existence, `command -v`, key resolution); expensive probes only under the fingerprint rule. A missing cache degrades to "not scanned yet — run `apogee check --refresh-providers` or `providers scan`", never to silence or a probe storm.
- **Words, not claims:** the vocabulary is the tier model's — *installed / credentials found / verified <date> / not found / unknown* — with evidence named where it helps ("logged in per `codex login status`"). The word "authenticated" appears nowhere.
- **The verified record is passive and local:** written on a provider backend's successful turn (any surface), read by displays; never earned by a probe turn — no code path spends tokens to verify on Apogee's initiative. It is disposable cache state: absent means "not verified that we saw", never an error.
- **One set of facts:** the CLI rendering, any JSON output (the 28h conventions, adopted whether or not [28h](../v0.1.5/machine-readable-reads.md) has shipped first), and `check`'s section all read the same tier structure — no surface computes its own.
- **Layering holds:** the write hook lives at the backends' completion seam via the 28a cache module; the harness never includes backends; `check` and `models` consume through the same module. Code style carries: `.h`/`.cpp` pairs, smart pointers only.

**Seam + files.**
- `cli/check.cpp`: the Providers section — configured provider backends first (tier, warn + remediation on missing binary), then detected-unregistered mentions; the `--refresh-providers` spelling riding `check`'s existing flag conventions.
- `commands/models_cmd` (list/info): provider rows' `STATE`/`VERIFIED` cells from the tier structure; `models info` gains the evidence line for a provider backend.
- `backends/provider_cache.h/.cpp` (28a's): the verified-slot write, called from the shared provider-backend completion seam — one helper, each provider backend type calling it on turn success.
- Tests: `tests/presentation/commands/` — the check section against scripted cache states (including no cache); PATH-stripped sandbox check warns (the spike's probe, inverted into a test); `models list` golden rows per tier; the verified write exercised through the mock-shaped completion seam.
- Consumes: [provider-detection.md](provider-detection.md) (tiers, cache, cheap-check rules); the use-time error text (shipped) as the remediation source; [28h](../v0.1.5/machine-readable-reads.md) output conventions (adopted early, consumed decision).

**Reference (Ommi).** The doctor precedent is Ommi's `check` (its models doctor read GGUF headers so "a model that would die on its first turn is caught by the doctor rather than a user" — MODELS.md; the exact failure-shape this item ports to providers). No analog for provider tiers or a verified record.

**Decisions made** (dated):
- 2026-10-03 — Split from the provider-detection spike as the surfacing half; gated on 28a only, parallel to [provider-registration.md](provider-registration.md) — the doctor should tell the truth even for hand-registered backends on a machine that never runs a scan.
- 2026-10-03 — Verification is passive (the spike's recommendation, the user not objecting): the record is written when the user's own turn succeeds, and no `--verify` probe-turn exists in this item; if one is ever wanted it is a new, explicitly opt-in flag.

**Open calls:**
- [default: the verified record keeps one entry per provider — last success date and the backend name that earned it; no history] Record shape.
- [default: `check`'s Providers section appears only when at least one provider backend is configured or detected; a pure-local install sees no new section] Visibility.
- [default: llamacpp/mock rows are untouched — their STATE/VERIFIED semantics already exist and this item adds no second meaning to them] Scope of the cells.

**Guardrail(s).**
- The inverted spike probe as a pinned test: a configured `claude-cli` backend with no binary on PATH makes `check` **warn** with the remediation text — never `ok`.
- Golden `check` sections and `models list` rows across the cache-state matrix (no cache / absent / installed / credentialed / verified, per provider type).
- The passive-only pin: no code path issues a provider turn from `check`, `models` or the cache module (asserted structurally, the no-hot-path style).
- JSON and human outputs carry the same tier facts (golden-compared where 28h's shape exists).

**Acceptance criteria:**
- [ ] In the sandbox with a registered `claude-cli` backend and a stripped PATH, `apogee check` warns on that backend naming the install/binary remediation; with the binary present, it reports the tier honestly instead of bare `ok`.
- [ ] `models list` shows tier words in `STATE` for provider rows and a date in `VERIFIED` after a successful (mock-seamed) provider turn; dashes are gone for provider rows.
- [ ] With no cache file, `check` says providers were not scanned and points at the command — and probes nothing expensive.
- [ ] No token is ever spent by `check`/`models` paths (structurally pinned).

**Scope note.** Item **28c**, earmarked for **v0.1.5**; **gated on [provider-detection.md](provider-detection.md) (28a)**, parallel to [provider-registration.md](provider-registration.md). Out of scope: registration and offers (28b); proactive verification turns; changing llamacpp rows' semantics; serving provider status over HTTP (the admin plane follows its own parity rules if ever asked for).
