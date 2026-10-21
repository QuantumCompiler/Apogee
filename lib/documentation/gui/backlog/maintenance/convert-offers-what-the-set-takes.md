# Convert offers what the set takes

**What / why.** On 2026-10-21 the user started an MLX conversion of `Wan-AI--Wan2.2-TI2V-5B/safetensors/6ef9b00b8b83` from Stored weights. It ran three seconds and failed in mlx-lm's words: `FileNotFoundError: No safetensors found in …`. The set is a diffusion model's transformer, which no converter Apogee runs takes (the CLI's [M20](../../../cli/assistant/MILESTONES.md#milestone-n--model-operations) has the measurements). The app offered the conversion because it offers **Convert to GGUF…** and **Convert to MLX…** on every SafeTensors set (`lib/src/darwin/source/presentation/models/Views/StoreView.swift:272–273`). It does the same on the user's four Assistant sets, which the CLI refuses as draft heads ("Draft Heads", 2026-10-21).

The app is right not to decide this itself: what a set's weights are is the CLI's to say (CLAUDE.md → ⚠ The GUI is a machine-mode client). Today the CLI says it only in prose. `models info`'s `format:` line reads "a draft head's weights, not runnable and not convertible", and its "made from it" line still offers both conversions. M20 gives the fact a typed home: `data.converts_to`, `[]` for a set that takes none. This item makes Stored weights offer exactly what that field names.

**Core constraint(s).**
- **The binary's word decides; prose decides nothing.** The buttons follow `data.converts_to`. The CLI's own framing (the `format:` line, the listing's note) is shown as said, as it is today.
- **An older binary keeps today's behavior.** Where `data` has no `converts_to` (a CLI before M20), both buttons stay, and the CLI's refusal is the answer, as now.
- **ADR 0004.** Each new state has a `#Preview` over a recorded document.

**Seam + files.**
- **`lib/src/darwin/source/business/reads/Models.swift`:** `ModelInfo`'s `Data` gains `convertsTo: [String]?`, `nil` when the field is absent.
- **`lib/src/darwin/source/presentation/models/Views/StoreView.swift`:** `StoreDetails`' actions by format. A SafeTensors set shows the conversions its `convertsTo` names. With none, it shows no convert action, and the set's `format:` line says why in the CLI's words.
- **`lib/src/darwin/source/presentation/models/ViewModels/StoreViewModel.swift`:** `askToConvert` refuses a format the set does not take, so no door opens around the view.
- **Fixtures:** `models info --output-format json` on a diffusion set, on a draft head and on a language model's snapshot, recorded from the binary that ships M20. Each is under `tests/business/reads/Fixtures/`, beside 2h's `store-info-*.json`, made the way those were.
- **Previews:** `models/Previews/` gains the documents and the states.

**Decisions made** (dated):
- 2026-10-21 — **Split from [M2](../../assistant/MILESTONES.md#milestone-c--cli-parity)** at the same diagnosis. M2's commands are buildable now. This waits on the CLI stating the fact, since a field the binary does not yet print is never invented (ADR 0004's law that documents are the binary's own).

**Open calls:**
- [default: a set that takes no conversion shows no convert buttons, rather than greyed-out ones. Its details already carry the CLI's `format:` line, which says what the set is and why it does not convert.]

**Guardrail(s).** The user's set becomes a fixture.
- **Wan:** over the recorded Wan document, Stored weights offers no convert action, and `askToConvert` on it opens no sheet.
- **A draft head:** over the recorded draft head, the same.
- **A language model's snapshot:** over the recorded snapshot, both actions are offered as today.
- **An older binary:** over 2h's existing `store-info-snapshot.json`, which has no field, both are offered.

**Acceptance criteria:**
- [ ] On the user's store, `Wan-AI--Wan2.2-TI2V-5B` and the four Assistant sets show no **Convert to…** action, and their details say why in the CLI's words. Their language-model snapshots offer both.
- [ ] Against a binary without M20, Stored weights behaves as today.
- [ ] Previews cover a set with no conversion. `check-previews`, the layering check and `lib/scripts/cicd.sh --test` pass, with no Swift warning.

**Scope note.** Maintenance, its gate open: the CLI's [M20](../../../cli/assistant/MILESTONES.md#milestone-n--model-operations), shipped 2026-10-21, states `converts_to`. Out of scope:
- **The convert sheet's commands:** [M2](../../assistant/MILESTONES.md#milestone-c--cli-parity).
- **Converting a diffusion model to MLX at all:** a CLI capability. It is a spike, on the user's word.
