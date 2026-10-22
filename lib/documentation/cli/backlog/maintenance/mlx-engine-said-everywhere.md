# The MLX engine said wherever a run is described

**What / why.** Since 53l and 53m, `video generate` and `voice say` each have two engines, chosen by the model's directory and recorded as `engine`. The record is right. Three places that describe a run still speak as if PyTorch were the only engine, read in the source and the run's records (2026-10-22).

- **A Wan set's description names one runtime.** `describe_diffusion_model` (`source/data/modelstore/snapshot.cpp`, around `:465–473`) ends:

  `what runs this model today is its Diffusers pipeline in 'apogee video generate', pulled whole with 'apogee models pull <ref>-Diffusers'`

  That was true before 53l. Now the set's MLX conversion runs on `video generate` too, and the sentence before it already says how to convert it. The same text is `models info`'s `diffusion:` line, the conversion's refusal and the pull's closing hint (53l "Not done").
- **The streamed events carry no engine.** Under `--output-format stream-json` (52b), the events of `video generate` and `voice say` say which device and dtype a run uses, but not its engine. Only the final document does (53l and 53m "Not done"). The macOS app's 3b reads the engine from the final document, so a run in progress can't be said as MLX until it ends.
- **`video generate` doesn't point a Wan original layout to its conversion.** Handed the layout itself, it refuses it as neither a pipeline nor an MLX video model. It does not say that `models convert <it> --mlx` makes the MLX video model it would run (53j "Not done").

**Core constraint(s).**
- **One wording, from the one place that decides.** The description reads the same facts M20, 53j and 53k already compute (`snapshot_wan_layout`, the mlx-video check). It is never a second copy of them.
- **The machine-mode protocol grows additively** (ADR 0006): the streamed event gains `engine`, and every field it has stays.
- **Say only what is true now.** The description names the MLX route as it stands for this set: converted already, convertible, or what it lacks.

**Seam + files.**
- **`source/data/modelstore/snapshot.cpp`:** `describe_diffusion_model`'s closing sentence names both runtimes. The Diffusers pipeline runs on PyTorch. The MLX conversion runs through mlx-video once made, and is said as made where an MLX video directory converted from this set stands in the store.
- **`source/business/video/generation.cpp`, `voice.cpp`, and the stream writer they report through:** the streamed `ready` event carries `engine`.
- **`source/presentation/cli/video_generate.cpp`:** the refusal of a Wan original layout names `models convert <it> --mlx` (and 53j's missing files when it is not whole).
- **`lib/documentation/cli/reference/machine-mode.md`:** the event's `engine`.
- **Tests:**
  - the description's wording each way: converted, convertible, not whole;
  - the streamed event's `engine` on both engines;
  - `video generate`'s refusal over 53j's fixture;
  - the M20, 53j and 53k wording cases updated to the new sentence.

**Reference.** 53j (`snapshot_wan_layout`, the description's first half), 53k (the conversion route and its wording), 53l and 53m (the engines, the record's `engine`), 52b (the streamed job events).

**Decisions made** (dated):
- 2026-10-22 — **Opened from the MLX-for-every-kind run's record** (the user's call: "Queue the MLX track's follow-ups as backlog items").

**Open calls:**
- [default: the description finds an existing MLX conversion of the set by the converted directory's record (its `ref` and `transform`), the way 53l finds the source set's tokenizer, and says it made. It never matches by name.]

**Guardrail(s).**
- **The description's three states, over 53j's fixture:** converted, convertible and not whole, each naming exactly what runs it.
- **The streamed `ready` event on each engine** carries `engine`. `cli.machine_schema_conformance` holds the documentation to the binary.

**Acceptance criteria:**
- [ ] `models info` on the user's Wan set names both the Diffusers pipeline and its MLX conversion as what runs it, true to what the store holds.
- [ ] `video generate` and `voice say` stream their engine before the run ends.
- [ ] `video generate -m <a Wan original layout>` names the conversion that makes it runnable.
- [ ] `lib/scripts/cicd.sh --test` is green.

**Scope note.** Maintenance, claimable by name, no gate. The GUI can show a run's engine live once the event carries it. That is a GUI item for when it is wanted, not this one.
