# The GGUF header cache: fast sweeps, fresh truth

**What / why.** `apogee models list` is slow because it earns its facts every time: `models::inspect_gguf` reads each stored model's header on every run — and `models status` and `check` run the same sweep again for role and window reporting. The headers barely change (a store entry lives in a directory named by its weights' own hash — immutable by construction), so re-reading them is pure waste. This item caches the parsed result: a small on-disk map from a file's identity — path, size, mtime — to its `GgufInfo`, consulted by the sweeps and refreshed only when a fingerprint misses. A warm `models list` performs **zero** header reads; a cold or changed file is read once and remembered. [Item 26n](cli-busy-line.md) makes the wait visible; this item removes most of it — independent items, better together.

**The design line that matters: the cache serves sweeps and never lies to loads.** `inspect_gguf`'s callers split two ways. The listing sweeps (`models list/info/status`, `check`, `models migrate`'s scan) want speed and tolerate a fingerprint's staleness window. The correctness paths — the llamacpp backend loading a model, `convert`, `quantize`, acquisition's verify ladder, training's promotion — must read the truth of the bytes in front of them. So the cache is **opt-in at the call site**: a `CachedInspector` the sweeps construct, while `inspect_gguf` itself stays pure and uncached, and the correctness paths keep calling it directly. A load can never be wrong because a listing was fast — by construction, not by review.

**Core constraint(s).**
- **Advisory by definition:** deleting the cache file is always safe and changes nothing but speed. A corrupt, unreadable or version-mismatched cache is discarded wholesale and rebuilt — logged to the operational log, never a user-facing error, never a crash (the history-file precedent: recall must not cost a session).
- **One layout declaration:** the cache lives under a `cache/` row declared in `harness/layout.h` in the same commit (the parity rule; the row joins `seed_data_directory()` and `check` like every row). Nothing else writes there without its own declaration.
- **Fingerprint honesty:** a hit requires path, size and mtime to all match; any miss re-reads and rewrites. No TTL, no heuristics — the file's own identity is the whole invalidation story.
- **Atomic and concurrent-safe:** writes go through `harness::write_file_atomically`; two processes racing lose nothing but an entry, and the loser's next run re-reads. It is a cache — last writer wins is correct.
- **Byte-identical output:** a warm run's `models list`/`status`/`check` output equals the cold run's, asserted — the cache may change *when* facts are read, never *what* is printed.
- **Not a secret, still private-adjacent:** the cache holds header metadata and paths, no content and no keys; it ships no special mode, and the leak sweep's conventions still apply to what lands in it.

**Seam + files.**
- `models/gguf_cache.h/.cpp` (new): the fingerprint (`path`, `size`, `mtime`), the serialized `GgufInfo` map, `lookup`/`record`, load-once/save-once per process, the discard-on-corruption rule; pure over an injected clock/filesystem view for tests.
- `models/gguf_inspect.h/.cpp`: untouched semantics — a `CachedInspector` adaptor beside it (constructed with the cache, falls through to `inspect_gguf` on miss), so call sites choose explicitly.
- `commands/models.cpp`, `commands/check.cpp`, `commands/models_migrate.cpp`: the sweeps construct the adaptor; the single-target `models info <one>` reads fresh (one file is cheap; when the user asks about *one* model, freshness beats a stale hit).
- `backends/llamacpp.cpp`, `models/convert.cpp`, `models/quantize.cpp`, `models/acquire.cpp`, `commands/models_pull.cpp`, `commands/train.cpp`: deliberately not converted — named here so the builder knows the omission is the design.
- `harness/layout.h`: the `cache/` row (added here if the layout does not already carry one).
- Tests: `tests/models/gguf_cache_test.cpp` — the fingerprint table (hit; size change; mtime change; missing file; corrupt cache; version bump), the read-counting seam proving a warm sweep reads zero headers, atomic-write behavior; the byte-identity assertion cold-vs-warm over a fixture store.

**Reference (Ommi).** No direct analog — Ommi's model set was a pinned allowlist, so its listings had little to discover. The **fingerprint discipline is Ommi's**, though: its prompt cache keyed on the model file's size/mtime (and binary identity), the same "the file's identity is the invalidation" rule adopted here. Divergence: this cache is advisory metadata, never correctness-bearing, which is why the loading paths stay uncached where Ommi's prompt cache *was* the load path.

**Decisions made** (dated):
- 2026-09-30 — Asked for by the user, the follow-up named on [item 26n](cli-busy-line.md): cache the header reads; **v0.1.3, at the end** — the user's call.
- 2026-09-30 — Opt-in at the call site (`CachedInspector`), never inside `inspect_gguf`: the caller map splits cleanly into sweeps and correctness paths, and hiding the cache in the inspector would hand every future load path a staleness bug by default.

**Open calls:**
- [default: one JSON file, `cache/gguf-headers.json`, with a `schema_version` whose mismatch discards the file] Format and location.
- [default: entries for files that no longer exist are dropped at save time, so the cache cannot grow unboundedly on a store that churns] Pruning.
- [default: `models info <single target>` reads fresh; every multi-file sweep uses the cache] The freshness line.
- [default: no `--no-cache` flag — deleting the file is the escape hatch, and `check` mentions the path in its cache row] Escape hatch.

**Guardrail(s).**
- The fingerprint table, exhaustively, including the corrupt-and-rebuild path (a truncated file, a wrong version, a non-JSON file — each discarded with the log line, command succeeds).
- The counting seam: warm `models list` over the fixture store performs zero header reads; touching one file re-reads exactly one.
- Cold-vs-warm byte identity for `models list`, `models status`, and `check`'s model rows.
- The deliberate-omission list: a grep-style test (the `one_key_resolver` pattern) that no file outside the named sweeps constructs `CachedInspector`, so a correctness path cannot quietly adopt it.
- Concurrent writers: two processes populating simultaneously both exit 0 and leave a loadable cache.

**Acceptance criteria:**
- [ ] On a populated store, a warm `apogee models list` performs zero GGUF header reads (counted through the seam) and prints bytes identical to the cold run — and is perceptibly fast where it was slow.
- [ ] Touching one model file causes exactly one re-read on the next sweep; replacing a file's contents (same size unlikely — mtime still moves) refreshes its entry.
- [ ] Deleting or corrupting `cache/gguf-headers.json` never fails a command: the next sweep rebuilds it.
- [ ] `apogee models pull`, `convert`, `quantize` and backend loading read headers fresh, asserted by the omission test.
- [ ] A fresh install seeds the `cache/` row on every install path (`cli.install_parity` extended by the row).

**Scope note.** Item **26o**, earmarked for **v0.1.3** (the end — the user's call; lettered into track 26 per the release-prefix rule, 2026-09-30); gated on nothing pending. Interplay, not a gate: [item 26n](cli-busy-line.md)'s busy line still covers the cold first sweep. Out of scope: caching anything but GGUF header results (SafeTensors indexes, Ollama registry answers — their own items if wanted); a TTL or background refresh; caching for the correctness paths, permanently.
