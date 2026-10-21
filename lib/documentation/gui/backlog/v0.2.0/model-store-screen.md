# The model store: backends and stored weights, apart

**What / why.** The user ran the built app again on 2026-10-20, after M1, and said: "when I try to go to the Models tab … these are only pulling up the configured backends. There's no actual way to look at like the safe tensors and then convert them and everything else like you can in the CLI. I want that as part of this GUI application."

Measured the same day, by reading the source and probing the installed `apogee 0.1.9 (73495a5)`:
- **Backends and stored weights share one table.** Since M1, each of the store's sets draws as its own row, named by its handle. The user's store holds 18 SafeTensors sets and 2 MLX models; `config scan` says "18 SafeTensors snapshot(s) not listed". But the place still mixes backends and stored weights under the CLI's groups, behind the tabs All, On this Mac, Cloud & subscriptions, Downloads and Roles (`ModelsViewModel.Tab`, `ModelsViewModel.swift:15`).
- **Only three store verbs are reachable:** `models delete`, `rename` and `repair` (`ModelsViewModel.swift:281–303`). Nothing reaches `models convert`, `models quantize`, `config add-backend`, `config scan --register` or `config delete-backend`.
- **2a's audit couldn't see this.** It classifies top-level command words and counted `models` as covered.

The design grew the same evening: the Apogee Desktop canvas, version 33; `lib/design/spec/html/main.html`, whose health check passes all 32 start screens; the boards `ScreenModelsStore`, `ScreenModelsConvert` and `ScreenModelsRegister`. Its tabs are **Backends · Stored weights · Jobs · Roles**.

This item builds that screen and its quick verbs. The long runs (convert, quantize, check its files) are 2i's.
- **Backends** lists only what sessions, suites and roles can use: local backends, then cloud and subscription ones. Each local backend has:
  - **Show its weights**, which opens its set under Stored weights;
  - **Remove the backend…** (`config delete-backend <name>`), which keeps the weights;
  - **Delete with its weights…** (`models delete <name> --yes --and-backends`).
- **Pipelines and voice models move to Stored weights**, with their components and voices in a set's details as Backends shows them today.
- **Stored weights** lists every set as `models list --all --output-format json` reads it, grouped by its model folder. Each row has format, precision, size on disk and a state:
  - "weights only · convert to run" for SafeTensors;
  - "used by a conversion" for a set `--all` alone lists;
  - "not registered" for a GGUF or MLX set no backend points at;
  - "backend `<name>`" when one does;
  - the CLI's own state wherever it has one.
- **The fold** keeps M1's sentence and its Show them.
- **"Register the N not registered…"** runs `config scan --register`.
- **A set's details** come from `models info <handle> --output-format json`:
  - the typed `data`: bytes, window, cache, and the structured `lineage` steps (`kind`, `from`, `detail`, `inferred`, `missing`);
  - the record;
  - **made from**, linking to the parent set, and **made from it**, the CLI's own field;
  - the command that reads it.
- **A set's actions depend on its format:**

  | Format | Actions |
  |---|---|
  | SafeTensors | Convert to GGUF… and Convert to MLX… (2i's sheets) |
  | An unquantized GGUF | Quantize… (2i) |
  | GGUF or MLX, not registered | Register… |
  | GGUF or MLX, registered | Use in a new session; Remove the backend… |
  | A pipeline | New clip; Make it the default |
  | A voice model | Open Voice |
  | Every set | Check the files (2i), Show in Finder, Delete… |

- **The model itself** has Rename… (`models rename <model> <new>`) and Delete all of it… (`models delete <model> --yes [--and-backends]`).
- **Register…** opens a sheet. It builds `config add-backend <name> --type llamacpp|mlx --model-path <file> [--mmproj-path <projector>] [--context-size N] [--cache-type f16|q8_0|q4_0]` and shows it as typed:
  - the name, defaulting to the CLI's `<base>-<level>`;
  - what it runs on (llama.cpp or MLX, by format);
  - the window, defaulting to the model's own, with the flag left out;
  - a GGUF's attention cache: the CLI's default `q8_0` leaves the flag out (its `--help`: "q8_0 (the default)");
  - the projector carried, when the set has one.

**Core constraint(s).**
- **The machine-mode rule.** Every change is the binary's own verb, run with the exact argv the screen shows. The GUI stores nothing of its own about the store.
- **Read back, don't mirror.** After each change, `models list --all` and the open set's `models info` are read again.
- **Deletes and removals confirm.** `--and-backends` goes in only when the person chooses it; the CLI's `--yes` never implies it (1h's decision, kept).
- **Names offered are the CLI's defaults.** The base is `model_base_name` (`lib/src/cli/source/presentation/operations/renames.cpp:126`): the repository part of an `owner--repo` folder, or the folder as it is. The real store holds both kinds side by side (`Wan-AI--Wan2.2-TI2V-5B` beside the renamed `Gemma4-12B-Instruct`). The level goes in without its underscores (`-Q4KM`), and MLX keeps its precision (`-4bit`). A refusal, such as a taken name, is said in the CLI's words.
- **Honest by default.** No size, window or memory appears unless a document states it.
- **The design boards and the screen agree.** Any drift found while building is settled in both, per `lib/design/spec/CLAUDE.md`.
- ADR 0001 and ADR 0002 hold.

**Seam + files.**
- `source/presentation/models/` (`PresentationModels`):
  - `ViewModels/ModelsViewModel`: the tabs re-cut; the store's groups, states and fold; selection by handle; the register sheet's form; `config scan --register`; `config delete-backend`; delete on a set and on a whole model; rename.
  - `Views/ModelsScreen`: the tabs.
  - New views: `StoreView` (the stored-weights table) and `StoreDetails` (the 380-point pane), `RegisterSheet`.
  - `ModelTable` and `ModelDetails`, narrowed to backends.
  - The Downloads tab becomes Jobs; 2i grows it.
- `source/business/reads/Models.swift`: `ModelInfo` gains the `made from it` field; `ModelRow` gains the consumed mark, from the difference between the plain list and `--all`.
- `lib/design/spec/`: the boards already exist; fix drift on both sides.

**Reference.**
- **M1:** `ModelRow` by handle, the fold and Show them, and the recorded fixtures `tests/business/reads/Fixtures/models-stored-{list,list-all,info-set,info-voice}.json`.
- **1h:** delete's two answers, rename by `renameName`, read-back through `run`, and roles' targets.
- **The CLI's items:** M12 (`config scan --register`, byte for byte the hand-typed add-backends), M14 (`--and-backends`; entries kept with their two doors, repoint or remove), and M3 and M16 (`<base>-<level>` naming).
- **The design:** the Models rows on the contract board (`lib/design/spec/html/BuildContract.dc.html`), which mark this item's parts ready apart from G19.

**Decisions made** (dated):
- 2026-10-20 — From the user's ask (quoted above): the Models place shows the store, not only the backends, and every store verb the CLI has gets a place in the GUI (the SPEC's Full parity).
- 2026-10-20 — The design's cut (canvas version 33): Backends · Stored weights · Jobs · Roles. Backends lists only what can be used. Pipelines and voice models live under Stored weights, where Video's and Voice's links now land.
- 2026-10-20 — Two items, not one. The screen and its quick verbs are here. The long runs (convert, quantize, check its files) are 2i's, because they need the jobs core's supervision and the G18/G20 fallbacks.
- 2026-10-20 — Remove the backend… joins the Models place for local backends (`config delete-backend`). This supersedes part of 1h's "Rename and Delete are offered for local rows only". Cloud backends are still removed in Settings.
- 2026-10-20 — No CLI sibling. Every verb this item runs exists today. G19 (`config scan` reports only as text) is recorded on the contract board as later CLI growth, not this item's work (2c's precedent).

**Open calls:**
- [default: the register-all confirmation shows `config scan`'s own report, word for word: each stored model with the name it is "registerable under", in its words. After `--register`, the names are read back from `models list`. This is G19's fallback; the app never predicts a name as fact. The contract board's G19 line says the same.]
- [default: a row's "made from" line comes from each set's `models info`, read in the background after the list. The reads are bounded and never hold up the table. If a measurement at build shows this is slow, the line moves to the details only, and the design is reconciled.]
- [default: a backend whose weights are gone shows `models list`'s own state for it and offers Remove the backend…. Repointing it (`config set-backend`) is the console's (2b) until a screen wants it.]
- [default: the register sheet leaves out `--context-size` and `--cache-type` unless the person changes them, so the CLI's defaults stand.]

**Guardrail(s).**
- **View-model matrices over recorded documents:** M1's fixtures, grown with a registered GGUF, an unregistered GGUF and MLX set, a consumed set and its children, and `models info` with `made from it`. Each is recorded from the installed binary over a scratch store, as M1 did.
- **Argv pinned exactly:**
  - `config add-backend`, with and without a projector, a window and a cache type;
  - `config scan --register`;
  - `config delete-backend`;
  - `models delete` on a set and on a whole model, with and without `--and-backends`;
  - `models rename`.
- **Read-back** after each.
- **The default names** pinned to the CLI's rule, `owner--repo` → the repository part.
- **The layering check** holds the new views. Everything stays hermetic.

**Acceptance criteria:**
- [ ] Models shows Backends · Stored weights · Jobs · Roles.
- [ ] Backends lists only backends; each local one has Show its weights and Remove the backend….
- [ ] Stored weights lists every set by handle, grouped by model, with format, precision, size and state, and the fold said.
- [ ] A set's details show its typed facts, with made from and made from it as links, its actions by format, and its command.
- [ ] Register… and Register the N not registered… add backends that appear under Backends once read back.
- [ ] Deleting a set or a whole model, with or without its backends, and renaming a model with its backends following are each confirmed, run as typed and read back.
- [ ] The full suite is green, and the boards match the screen.

**Scope note.** Earmarked for v0.2.0 (track 2). No gate: M1 has shipped, and the screen is useful on its own. [`convert-quantize-jobs.md`](convert-quantize-jobs.md) (2i) builds on it.

Out of scope:
- the CLI's G18–G20 machine faces (later CLI growth);
- repointing a backend at other weights;
- removing cloud backends (Settings' job);
- pulls, which are 1i's and unchanged apart from the tab's name.
