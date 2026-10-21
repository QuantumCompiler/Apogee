# MLX engines and drafters in the app

**What / why.** Three of the CLI's MLX-for-every-kind items change how something runs, rather than what is stored. Each needs a place in the app:
- **Drafters (53i):** a `drafter` pairs with its MLX base through `config set-backend <base> --draft <it>`.
- **Video (53l):** `video generate` gains an MLX engine, chosen by the model's directory, with `--engine` there to name or refuse it, and the record gains `engine`.
- **Voice (53m):** `voice say` does the same.

Today the app has no draft field on a backend, and its clip and say sheets (`lib/src/darwin/source/presentation/video/Views/VideoScreen.swift:246`, `voice/Views/VoiceScreen.swift:206`) build their commands with no engine. The records they read back have none either. ADR 0003's parity: a flag the CLI grows has its place in the app.

**Core constraint(s).**
- **The CLI decides, the app builds its commands:** the engine is what the model's directory says. The sheets show it and offer `--engine` only to choose what the CLI allows.
- **The drafter's pairing is the CLI's:** the base is found by the CLI (53i, by family and width), and the app offers only that pairing.
- **An older binary keeps today's behavior:** no engine in a record means PyTorch, as now, and no drafter field.
- **ADR 0004:** each new state has a preview over recorded documents.

**Seam + files.**
- **`lib/src/darwin/source/presentation/models/`:**
  - Stored weights' **Pair with its base…** on a `drafter` set (the GUI's [3a](mlx-kinds-in-stored-weights.md) offers it), as `config set-backend <base> --draft <drafter>`, read back;
  - the backend's details say its drafter, with **Unpair**.
- **`lib/src/darwin/source/presentation/video/`:** the clip sheet's engine line (from the chosen model's kind), and the record's engine in a clip's page.
- **`lib/src/darwin/source/presentation/voice/`:** the say sheet's engine line, and the record's engine on a speech's page.
- **`lib/src/darwin/source/business/media/`:** the records' `engine`, read additively.
- **Fixtures:** recorded from the binaries that ship 53i, 53l and 53m.

**Decisions made** (dated):
- 2026-10-21 — **Opened with 3a from the CLI's spike**, one item for the three runtime changes, so the app's sheets change once.

**Open calls:**
- [default: the engine is shown, not chosen, unless a model's directory offers both (a pipeline beside its MLX conversion). Then the sheet offers the choice, PyTorch first, as the CLI's `auto`.]

**Guardrail(s).**
- **The pairing's command:** pinned to the CLI's recorded verdict, as M2's store commands are.
- **The sheets' commands with an engine:** held the same way.
- **A record with `engine: mlx`** drawn on a clip's and a speech's page.

**Acceptance criteria:**
- [ ] A drafter pairs with its base from Stored weights, and the base's details say so.
- [ ] A clip or speech made on MLX says its engine. The sheets say which engine a model runs on.
- [ ] Against older binaries, nothing changes.
- [ ] Previews, `check-previews`, the layering check and `lib/scripts/cicd.sh --test` pass, with no Swift warning.

**Scope note.** GUI v0.2.0, track 3, gated on [3a](mlx-kinds-in-stored-weights.md) and the CLI's [53i](../../../cli/backlog/v0.2.0/mlx-draft-heads.md), [53l](../../../cli/backlog/v0.2.0/mlx-video-generate.md) and [53m](../../../cli/backlog/v0.2.0/mlx-voice.md).
