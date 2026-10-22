# MLX kinds in Stored weights

**What / why.** The CLI's MLX-for-every-kind track (its 53e–53m, from the 2026-10-21 spike the user asked for) makes MLX directories of six kinds: `chat`, `vision`, `embedding`, `drafter`, `video` and `voice`. Each is recorded in its own record (53e) and stated in `models info`'s `data.kind` and `models list`'s rows.

Stored weights reads every MLX set as a chat model's weights today:
- its **Register…** opens the same `config add-backend --type mlx` form for any of them (`lib/src/darwin/source/presentation/models/ViewModels/StoreViewModel.swift:336`);
- **Use in a new session** is offered on any MLX backend;
- the convert sheet says nothing of which converter an MLX conversion will run.

M3 already offers only the conversions `data.converts_to` names (`StoreViewModel.swift:286`), so new kinds become convertible in the app as the CLI states them. What a set *is*, once made, the app does not show. This is the sibling the CLI's 53e names. Its GUI parity rule is ADR 0003's: what the CLI does, the app does.

**Core constraint(s).**
- **The binary's word decides:** a set's kind is `data.kind`, never read from a name. A kind the app does not know is shown as the CLI wrote it, with nothing offered beyond its details and its delete.
- **An older binary keeps today's behavior:** with no `kind`, every MLX set is a chat model's, as now.
- **ADR 0004:** every new state has a `#Preview` over documents recorded from the binary that ships 53e.

**Seam + files.**
- **`lib/src/darwin/source/business/reads/Models.swift`:** `ModelInfo.Facts` and `ModelRow` gain `kind`.
- **`lib/src/darwin/source/presentation/models/ViewModels/StoreViewModel.swift`:** each kind's actions:
  - `chat` and `vision` sets register as today, and a vision one is said to read images;
  - an `embedding` set registers as an entry the embedding role can name, and Roles offers it there;
  - a `drafter` offers **Pair with its base…**, the GUI's [3b](mlx-engines-and-drafters.md);
  - `video` and `voice` sets offer **New clip with it** and **Open Voice**, as pipelines and voice sets do today.
- **`lib/src/darwin/source/presentation/models/Views/StoreView.swift`:** the kind beside the format in the details' header and the table, and, in the convert sheet for a towered set, which converter runs: the towers kept (mlx-vlm), or **Text only** (the CLI's `--text-only`, its 53f).
- **Fixtures:** `models list` and `models info` of a directory of each kind, recorded from the binary that ships 53e, beside `tests/business/reads/Fixtures/store-*.json`.

**Decisions made** (dated):
- 2026-10-21 — **Opened from the CLI's MLX-for-every-kind spike** (the user's call: "create the documents for v0.2.0"), as the GUI sibling its 53e names.

**Open calls:**
- [default: the kind is shown as a word beside the format (`MLX · vision`), and a set's actions follow it. Nothing is hidden that the CLI lists.]

**Guardrail(s).** The recorded documents of each kind: each lists with its kind and offers only its kind's actions. An older binary's documents (2h's, with no `kind`) read as today.

**Acceptance criteria:**
- [ ] Each MLX kind is said in Stored weights, and only its own actions are offered. An embedding set registers for the embedding role.
- [ ] The convert sheet for a towered set says that its towers are kept, and offers text only.
- [ ] Against a binary before the CLI's 53e, Stored weights reads as today.
- [ ] Previews cover each kind. `check-previews`, the layering check and `lib/scripts/cicd.sh --test` pass, with no Swift warning.

**Scope note.** GUI v0.2.0, track 3, its gate, the CLI's [53e](../../../cli/assistant/MILESTONES.md#milestone-aq--mlx-for-every-kind), shipped 2026-10-21 (`data.kind` and the list rows' `kind`, in `machine-mode.md`). Each kind's actions land as its CLI item ships: the embedding role with [53h](../../../cli/backlog/v0.2.0/mlx-embeddings.md), and text-only conversion with [53f](../../../cli/backlog/v0.2.0/mlx-vlm-conversions.md). A kind whose CLI item has not shipped has no documents to show yet.
