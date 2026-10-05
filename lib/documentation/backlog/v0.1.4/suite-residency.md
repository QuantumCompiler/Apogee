# Suite residency: admission, warmth, and honest eviction

**What / why.** Co-residency already works mechanically — the spike (2026-10-03) confirmed it in the source: the harness holds a live provider per configured backend (`harness/harness.h`, the `providers_` map), and each llamacpp provider owns its model's residency with a per-backend `idle_unload` clock (`backends/llamacpp.h` — "a resident 16GB model is the single largest thing this policy touches"). The physics is comfortable for the target shape: a root 12B at Q4_K_M (~7–8 GB) + a 3B utility (~2 GB) + the 300M embedder (~0.6 GB) is ~10 GB of weights, and helper KV at small windows is megabytes (26m's measured datum: even a 31B's cache at a 32K window is 2.0 GiB — a 3B at 4K is noise). What nobody does is manage a **suite as a set** — the spike's **W4**: no admission answer ("will this suite fit this machine?", though every input exists — weights sizes in the store's records, KV cost from 26a's `kv_cache` machinery, the machine's budget from the same item), no suite-aware eviction (the embedder can idle-out mid-conversation and pay its load back on the next turn), no warmup, and loads happen silently. This item composes what is shipped into suite policy: **admission** (selecting a suite states the arithmetic and refuses honestly when it cannot fit, naming the number), **warmth** (members a session is using don't idle-unload out from under it; an explicit warmup option loads the set up front, narrated on the busy line — M1, shipped), and **honest accounting** (`models status` with a suite active shows each member resident-or-not and the set's footprint).

**Core constraint(s).**
- **Composition, not new math:** weights from the store's own records, KV from the 26a machinery, never a second estimator; where a number is unknown (a backend with no stored size) the admission says *unknown*, never guesses.
- **Admission informs; the user decides.** A suite that doesn't fit is refused at selection with the arithmetic shown and `--force` honored — the open-models principle's spirit: Apogee states consequences, it does not babysit.
- **Warmth is scoped to the session:** suite members gain an in-use hold on their idle-unload clocks while the session lives; the hold dies with the process (no daemon, nothing persisted, never-listens untouched).
- **Loads are narrated, never silent:** warmup and first-use loads ride the busy line exactly as the slow commands do (M1's one painter), silent on pipes, under `--quiet`, and with JSON output.
- **Eviction stays per-backend policy** underneath — this item adds the suite hold, not a second eviction engine.
- Code style carries: `.h`/`.cpp` pairs, smart pointers only.

**Seam + files.**
- `modelstore/kv_cache.h/.cpp` (shipped, 26a) + the store's size records: the admission function — `suite_footprint(config, suite, machine)` → per-member weights + KV at its pinned window, with unknowns named.
- `harness/harness.h/.cpp`: the in-use hold on providers (the idle-unload clock consults it); the warmup walk.
- `backends/llamacpp.h/.cpp`: the hold honored where `idle_unload` fires.
- `cli/chat.cpp` / `commands/models_cmd`: admission at `--suite`/`/suite` selection (stated, refusable, `--force`); `models status` footprint lines; `--warm` on suite selection.
- Tests: admission tables over scripted stores and fake machine budgets (fits / doesn't / unknown-size member); the hold's lifecycle (fires after session end, never during); busy-line goldens for warmup.
- Consumes: [model-suites.md](model-suites.md) (27d — the unit this manages); 26a/26m (shipped — window and cache math); M1 (shipped — the line).

**Decisions made** (dated):
- 2026-10-03 — Split from the suites spike as its own item: residency is policy over shipped machinery (the spike found every input already existing), and bundling it into 27d would make the config item balloon.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): admission reuses the 26a machine RAM share — one notion of what fits.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): load on first use; `--warm` opts into up-front loading on the busy line.
- 2026-10-03 — Confirmed (the default taken, the user's confirmation): the idle-unload hold covers members used this session; untouched members stay evictable.

**Guardrail(s).**
- Admission tables: fits / over-budget / unknown-size, each with the exact stated arithmetic golden-tested; `--force` honored and recorded in the session banner.
- The hold: a mock-clock test that an in-use member survives its idle window during a session and unloads after it ends.
- Warmup narration golden (and byte-silence on pipes/`--quiet`).
- No new estimator: the admission function's inputs are the shipped ones (asserted by construction — it takes the 26a types).

**Acceptance criteria:**
- [ ] Selecting a suite on a machine it fits states the footprint and proceeds; on one it doesn't, the refusal names the numbers and `--force` overrides.
- [ ] A suite session's utility member does not idle-unload between two turns separated by longer than its `idle_unload`; after the session ends, it does.
- [ ] `apogee chat --suite research --warm` loads the members up front on the busy line; `models status` then shows each resident with the set's total.
- [ ] A member with no recorded size yields an admission line that says unknown — and still runs.

**Scope note.** Item **27e**, earmarked for **v0.1.4**; **gated on [27d](model-suites.md)**. Out of scope: cross-process residency or any daemon; GPU-vs-CPU placement (llama.cpp's own affair); eviction-policy redesign; MLX members' footprints (27b's `config.json` windows slot into the same math when that track lands — interplay, not a gate). Symphony plays ([27q](symphonies.md)) hop members serially and are covered by this item's session hold as-is — interplay, not a gate.
