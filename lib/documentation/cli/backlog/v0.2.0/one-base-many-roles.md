# One base, many roles: a suite shares the model and differs by adapter

**What / why.** The RAM prize the adapter path unlocks (the LoRA spike, 2026-10-21): `llama_set_adapters_lora` is **per-context** — the pin's own header says it sets adapters "on the context" and "will only modify if the adapters currently in context are different" (`llama.h:718-726`) — so one loaded `llama_model` can serve several roles, each through its own context with its own adapter set. Today that is structurally impossible: `SessionHold` holds one provider per suite member and **each llamacpp provider owns its own `llama_model`**, so two members on the same base file load the multi-GB weights twice. This item builds the sharing: providers on the same base (keyed by the base's fingerprint — path, size, mtime, the prompt cache's own identity) share one loaded model, each member's contexts carrying its entry's adapter set at its scales; the footprint arithmetic (`modelstore/footprint`, 27e) counts the shared base **once** plus each member's cache and adapters, so suite admission stops double-pricing what memory holds singly; and residency (`SessionHold`, the idle-unload clocks) treats the shared base honestly — held while any sharer holds it, unloaded when the last lets go.

**Core constraint(s).** **Correctness before sharing**: contexts never bleed adapters — each context's set is exactly its entry's, re-asserted on use (the API's only-if-different makes that cheap); an entry with no adapters shares the bare base. **The capability probes stay typed** (no `dynamic_cast` leaks): whether two entries can share is a fact of their resolved base fingerprints, computed where providers are built. **Thread-safety is llama.cpp's contract honored, not assumed**: one model, multiple contexts is the library's supported shape; anything serialized today (the runtime seam's discipline) stays serialized. **Footprint honesty**: an unknown size is said, never guessed (the store's law); the shared-base saving is reported in the suite's priced admission.

**Seam + files.**
- `data/backends/llamacpp.cpp/.h`, `llama_real.cpp`, `llama_runtime.h` (the shared-model registry keyed by base fingerprint; per-context adapter assertion; last-sharer unload).
- `business/harness/` (`SessionHold`'s residency over shared bases), `data/modelstore/footprint.*` (the base counted once, adapters and caches per member).
- Tests over the runtime seam: two fake members on one base load it once and hold distinct adapter sets per context; the no-bleed matrix (interleaved turns across members, each context's set pinned); last-sharer unload; footprint fixtures (shared vs distinct bases priced correctly); `/suite` moves re-keying the hold.

**Reference.** The LoRA spike (2026-10-21) — the per-context evidence and the double-load measurement of today's shape; 27e — `SessionHold` and the footprint arithmetic this grows; 26j — the fingerprint identity reused as the sharing key.

**Decisions made** (dated):
- 2026-10-21 — Sharing is keyed by base fingerprint and scoped to llamacpp (the mlx backend is one child per provider by design — its sharing, if ever, is its own later question, recorded).

**Open calls:**
- [default: the registry's shape (a weak-held map in the factory's composition, the last sharer releasing) at build, within the smart-pointer law.]

**Guardrail(s).** The seam matrices above, hermetic and mutation-worthy (the no-bleed matrix especially); the real RAM measurement (one base serving two roles vs two loads) is the user's run, recorded per the standing rules.

**Acceptance criteria:**
- [ ] Two suite members on one base share a single loaded model, each serving through its own adapter set, with no bleed across interleaved turns.
- [ ] The footprint prices the shared base once and the admission report says so; the last sharer's release unloads it.
- [ ] Entries on distinct bases behave byte-identically to today; the full suite is green.

**Scope note.** Earmarked for v0.2.0 (track 53); gated on [`adapters-at-the-runtimes.md`](adapters-at-the-runtimes.md) (53a). Out of scope: mlx sharing (recorded later), scheduler or loop changes, any llama.cpp modification.
