# Model lineage: consumed snapshots leave the listing, derived GGUFs say their source

**What / why.** The user's `models list` (2026-10-03) opens with eleven `(not configured)` SafeTensors rows — but most of those snapshots already did their job: they were converted into the GGUFs registered right below them. Showing a consumed input as an unconfigured model misreads the store twice — it implies action where none is wanted, and it buries the one snapshot that genuinely *is* waiting (a pull never converted). The user's rule, adopted verbatim: **a snapshot used to create a GGUF no longer shows as an unregistered model; one never converted still does** — and the backends built from a snapshot **reference it in what they print**. The catch the recon found: lineage is not recorded anywhere today — `convert.cpp` and `quantize.cpp` write no source into the provenance sidecar. So the item has two halves: **record lineage at creation** (convert stamps the source snapshot into the produced GGUF's sidecar record; quantize stamps its parent GGUF, chaining back to the snapshot), and **display it** — consumed snapshots fold out of the default listing (an `--all` flag still shows everything), derived rows show where they came from, and `models info` prints the full chain: quant → F16 → snapshot → the upstream Hugging Face ref the snapshot's own sidecar already holds.

**Core constraint(s).**
- **Recorded truth over inference, inference labeled.** New conversions and quantizations stamp exact lineage into the sidecar (the provenance/integrity split that file already observes: what was *done* is integrity-side fact, not source-side claim). For GGUFs made before this item, consumption is **inferred** from the store's own shape — a GGUF directory under the same `<model>/` root — and anything shown from inference says so (`from this model's snapshot (inferred)`), never dressed as a record. No backfill rewrites old sidecars.
- **Hiding is a view, never a state change:** a consumed snapshot stays in the store, stays trainable, stays deletable, and still appears under `--all` and in `models info <snapshot>`; `check` and the store rules treat it exactly as today. Deleting all of a snapshot's derived GGUFs makes it reappear in the default listing — the display follows the store, live.
- **Honest when the chain breaks:** a derived GGUF whose source snapshot was deleted says `source snapshot no longer on disk` rather than hiding the lineage or inventing presence; a quant whose F16 parent is gone still names it.
- **One layout authority** (consumed decision — the model store): all lineage resolution asks `models/store.h`'s declarations; no display-side path arithmetic.
- **The listing stays the sweep it is:** lineage reads sidecars the sweep already visits; this item must not add a second pass over the store ([M2](../../assistant/MILESTONES.md#milestone-n--model-operations), shipped 2026-10-03, made the sweep fast by fixing the header reader rather than caching it).

**Seam + files.**
- `models/sidecar.h/.cpp`: the integrity-side lineage fields — `derived_from` (a store-relative path: `…/safetensors/<hash>` or `…/gguf/<hash>`) and the operation that made it (`convert`, `quantize`, training promotion already knows its origin); absent fields mean pre-item artifacts, read as unknown.
- `models/convert.cpp`, `models/quantize.cpp`: stamp the record at commit time (training promotion's convert path inherits it through the same functions).
- `models/store.h/.cpp`: `consumed(snapshot)` — recorded lineage first, same-root inference second, each answer tagged with which.
- `commands/models.cpp`: the default listing folds consumed snapshots; `--all` restores them; derived rows' SOURCE cell reads `converted` (instead of `local`); `models info` prints the chain with the inferred/recorded tag and the upstream ref.
- Tests: `tests/models/` — sidecar round-trip with the new fields; `consumed()` table (recorded, inferred, no-gguf, deleted-gguf-reappears); listing goldens before/after convert, with `--all`, with a broken chain; the single-pass property asserted via the sweep's read counts.

**Reference (Ommi).** No analog — Ommi had neither open acquisition nor a conversion ladder, so nothing ever had lineage. In-house precedents consumed: the sidecar's claimed-versus-verified split (its header war story), the store's one-declaration rule, and [M2](../../assistant/MILESTONES.md#milestone-n--model-operations)'s sweep-cost discipline.

**Decisions made** (dated):
- 2026-10-03 — Asked for by the user from the live listing, placed in **Maintenance** at their direction; the display rule is theirs verbatim (consumed snapshots out of the default view, unconverted ones stay, derived backends say their source).
- 2026-10-03 — **Record going forward, infer for the past:** stamping lineage at creation is one line in the right place; reconstructing it for existing artifacts honestly is impossible (two snapshots of one model, either could be the parent) — so inference is same-root, coarse, and labeled.

**Open calls:**
- [default: the flag is `models list --all`; the folded state prints one tail line — `N snapshot(s) consumed by conversions — --all shows them` — so the fold is visible, not silent] Discoverability of the fold.
- [default: SOURCE reads `converted` for GGUFs with recorded or inferred lineage, `local` otherwise; the full chain lives in `models info`, not new columns] Column treatment — taste, veto freely.
- [default: a snapshot consumed *and also registered as a backend itself* (possible once the MLX track lands) is not folded — folding applies only to unregistered, consumed snapshots] Interaction with future direct-snapshot backends.
- [default: [M3](pull-register-chain.md)'s chain stamps the same lineage through the same functions — nothing extra; noted so the two items compose] M3 interplay.

**Guardrail(s).**
- The `consumed()` table, exhaustively, including both-snapshots-one-converted (only the recorded one folds; the other stays).
- Listing goldens: the motivating listing's shape — consumed snapshots folded with the tail line, the unconverted `gemma-4-E4B`-style row *staying* when its GGUFs are deleted; `--all` byte-superset.
- The broken-chain messages (`source snapshot no longer on disk`), golden-tested in `models info`.
- Sidecar fields round-trip; pre-item sidecars (fields absent) read as unknown and never crash a sweep; mutation-tested where convention applies.
- The sweep does not grow a second store pass (read counts asserted, with and without M2's cache).

**Acceptance criteria:**
- [ ] On the motivating store, `models list` shows zero `(not configured)` rows for snapshots whose GGUFs exist, one tail line counting them, and still shows any snapshot never converted; `--all` shows everything.
- [ ] A fresh `models convert` stamps lineage; its GGUF's `models info` prints `converted from <model>/safetensors/<hash>` *(recorded)* plus the upstream Hugging Face ref; a quant chains through its F16.
- [ ] Pre-existing GGUFs show the same facts tagged *(inferred)*.
- [ ] Deleting a snapshot's derived GGUFs returns it to the default listing; deleting the snapshot leaves its GGUFs listing with the honest broken-chain note.

**Scope note.** **Maintenance item M5**; gated on nothing pending. Interplay, not gates: [M2](../../assistant/MILESTONES.md#milestone-n--model-operations) (sweep cost, shipped), [M3](pull-register-chain.md) (the chain stamps the same records). Out of scope: backfilling old sidecars; lineage for Ollama-pulled or hand-copied GGUFs (unknown stays unknown); surfacing lineage in machine-readable output (rides [27e](../v0.1.4/machine-readable-reads.md)'s documents when that lands).
