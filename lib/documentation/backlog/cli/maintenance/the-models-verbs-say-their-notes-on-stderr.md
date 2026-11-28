# CLI-M60 · The models verbs say their notes in the house voice on stderr, and as notices on an open stream

| Field | Value |
|---|---|
| **Type** | 🐞 Bug |
| **Project** | CLI |
| **Status** | 🟢 Ready |
| **Fix version** | Maintenance (any time) |
| **Component** | Models › `models pull`, `convert`, `quantize` and `migrate`, the register chain's summary (`cli/model_chain`), and the note writer's job-stream branch (`cli/notes`) |
| **Labels** | `models` `house-voice` `job-stream` |
| **Epic** | — (maintenance has no track) |
| **Blocked by** | — |
| **Blocks** | — |
| **Reporter** | The investigation of the APP-M21 and APP-M22 probes' CLI asides (2026-11-26): found by its sandboxed investigator, then two skeptics and a critic the same day |
| **Created** | 2026-11-26 |

---

## Summary

`apogee models migrate --yes` over an unreadable collection file prints `note: could not open <db>: database error: file is not a database` on stdout, between its result lines, with stderr empty. Every `note:` and `warning:` the models verbs say is written the same way: ten print sites across `models pull`, `convert`, `quantize`, `migrate` and the register chain's summary. They were left on stdout because 52b's relay turns every stdout line into a `notice` on `models pull --output-format stream-json`. The item moves the family onto CLI-M58's note writer and gives the writer the branch M58 left out. In text, a note is one stderr line, `apogee models: …`. On an open job stream, it is a `notice` instead, carrying the words the stream carries today.

## Description

### User story

**As** someone pulling, converting, quantizing or migrating models, at the prompt, in a script, through the Mac app or from a driver,
**I want** each note said on stderr in the house voice, and as a notice when I asked for a stream,
**so that** stdout holds only the run's result, the note reads like every other verb's note, and a driver streaming a pull still gets every note, once.

### Steps to reproduce

The sandbox, 2026-11-26, with the built binary `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`: a throwaway `APOGEE_HOME` and `HOME` (one directory, three levels deep), `env -i`, stdin from `/dev/null`, stdout and stderr recorded as two pipes, and `config init`.

1. Write a fake 24-byte GGUF header (no tensors) to `<home>/models/flat.gguf`, and junk bytes to `<home>/embeddings/bad.db`.
2. Run `apogee config add-backend local --type llamacpp --model-path <home>/models/flat.gguf` (`c-m0`).
3. Run `apogee models migrate --yes` (`c-m2`).

### Actual result

3. Exit 0, stderr empty. After the plan it prints first, stdout ends:

```
moved    <home>/models/flat/gguf/a4e5e156ddec
updated  <home>/config/config.json
note: could not open <home>/embeddings/bad.db: database error: file is not a database

migrated.
```

### Expected result

3. Exit 0. stdout keeps the plan and the result lines (`moved`, `updated`, `migrated.`) and holds no note. stderr is one line: `apogee models: note: could not open <home>/embeddings/bad.db: database error: file is not a database`.

For the one models verb that streams, `models pull --output-format stream-json`, each note is one `notice` event carrying its words, as a one-line note is today, and nothing reaches stderr.

### Background

The models family put its notes on stdout through two recorded choices:
- **The chain's summary says them.** M3's register chain (b5f55a78, 2026-10-03) collects what each stage would warn about on its own and, in the words of its record, "says them once in its summary" ([Milestone N](../../../assistant/cli/MILESTONES.md#milestone-n--model-operations), the M3 record). `model_chain.cpp:17` still says "A note as the summary says it". 55d (3c0bebdb, 2026-10-25) fixed only the doubled `note: note:` prefix and did not choose the stream.
- **52b relays stdout.** `models pull --output-format stream-json` (ff17031a, 2026-10-20) redirects `std::cout` into a line relay while the stream is open, so "every line written to `std::cout` becomes a `notice` event … stderr is untouched -- diagnostics, as ever" (`job_stream.h:24-27`). A note on stdout reached a driver. A note on stderr would have reached no one on the stream.

The standalone verbs followed the same habit. Each site writes `note:` or `warning:` on `std::cout`, next to its result lines.

The investigation behind APP-M22 found that a successful run's stderr notes never reach the Mac app. Its follow-up chose the CLI side: [CLI-M58](../../../assistant/cli/MILESTONES.md#2026-11-27--a-runs-notes-are-said-once-on-stderr-maintenance-item-m58-a-runs-notes-are-said-once-on-stderr-in-the-house-voice) (shipped 2026-11-27) writes the text-mode rule down and adds one writer, `say_note(verb, text)`, which writes one stderr line `apogee <verb>: <text>`. No verb M58 moves opens a job stream, so M58's writer has no stream branch. This item brings the models family onto the writer and adds that branch. APP-M22's Open questions row "Should this also cover warnings printed on stdout?" names this item for the models verbs.

**The design.**
- **One writer, two faces.** With no stream open, a note is one stderr line in the house voice. With one open (`JobStream::current()`), it goes straight to the stream as a `notice`, with the words alone and nothing on stderr. This is the rule machine-mode sessions already follow for a suite's warnings (Evidence 6).
- **Ten print sites move**, with their `note:`/`warning:` words kept and each two-line note joined into one line, as CLI-M58 joins its own.
- **The chain says its notes where its summary said them**, after `ready:` and before the next step, now through the writer.
- **Everything that is the run's result stays on stdout**: the plan and result lines, the chain's stage, summary and resume lines, the hints, `models delete`'s preview, and `models info`'s rows.

### Evidence

All measured or read on 2026-11-26 by an investigator, two skeptics and a critic, each in its own sandbox. The probes ran the built binary `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)` (`v0`). Its version line names 33ea685d, but its strings carry HEAD 5b8927c9's CLI changes, and `git diff --stat 33ea685d HEAD -- lib/src/cli/source` lists 11 files, none of them a `models_*` file. Every `file:line` below was re-read at HEAD.

1. **The case, reproduced** (`c-m0`, `c-m1`, `c-m2`).
   - `c-m0` added the backend (`added backend 'local' …`, `models.default set to 'local' (none was set)`), exit 0.
   - `c-m1`, `models migrate` without `--yes`, printed the plan and `re-run with --yes to apply.`, exit 0. It opens no collection, so it says no note.
   - `c-m2`, `models migrate --yes`: exit 0, stderr empty, and stdout as under Actual result.
   - The source is `models_migrate.cpp:353-356`: `std::cout << "note: could not open " << entry.path().string() << ": " << e.what()`, in the catch around each `embeddings/*.db`.
   - The collection pass runs only when a moved model was some backend's model (`renamed`, `:331-338`). That is why the reproduction names `flat.gguf` in a backend.
2. **The other nine sites, by code.** None was run: under the probes' rules each needs a download, the Python converter or a real GGUF.

   | Site | Verb | What it says today, on stdout |
   |---|---|---|
   | `models_pull.cpp:742-744` | `models pull --safetensors` (`pull_snapshot`, chained or not) | `note: the snapshot landed but its record could not be written` |
   | `models_pull.cpp:1797-1800` | `models pull` of an MLX repo (`pull_mlx`) | `note: its files carry no mlx-lm marks (…) -- mlx-lm reads such weights all the same` |
   | `models_pull.cpp:2294-2296` | `models convert --mlx`, standalone | the base-model note (`base_model_note`, `:253-258`, two lines) |
   | `models_pull.cpp:2461-2467` | `models convert`, standalone (`finish`) | each of `out.warnings`: the projector that could not be made (`:2633-2637`, two lines), the base-model note (`:2494-2496`, `:2638-2640`), an architecture not known to run (`:2641-2644`) |
   | `models_pull.cpp:2779-2782` | `models quantize`, standalone | each of `out.warnings`: the projector that did not come along (`:2772-2775`, two lines) |
   | `models_pull.cpp:3788-3795` | `models pull` from the Ollama store, without `--yes` | `warning: architecture '<a>' is not known to run in Apogee's llama.cpp.` / `Pulling anyway is fine -- pass --yes to skip this notice.` |
   | `models_pull.cpp:3831-3835` | `models pull` from the Ollama store, its projector | `warning: the projector could not be copied -- <error>` / `this model will work for text but not for images` |
   | `models_pull.cpp:3865-3867` | `models pull` of a GGUF | `note: <the acquire's error>`, such as `the model landed but its provenance record could not be written` (`business/models/acquire.cpp:200`) |
   | `model_chain.cpp:95-97` | the register chain: `models pull --safetensors --register[-with]`, `models convert --register[-with]` and `models convert --mlx --register[-with]` (`run_mlx_register_chain`'s resume, `:3276-3282`) | each collected note, `out << said_as_note(note)` |

3. **What the chain collects.**
   - When chained, convert and quantize hand their warnings to the chain instead of printing them (`models_pull.cpp:3136-3138`, `:3146-3148`).
   - The registration adds an embedding entry's note (`:2945-2949`, `embedding_role_note` at `:1491-1495`), and the MLX chain adds the embedding or base-model note (`:3294-3299`).
   - `ChainLog::note` keeps each note once (`model_chain.cpp:29-33`), and `said_as_note` gives it one prefix (`:17-25`).
   - The summary prints the notes after `ready:` and before `next` (`:89-100`), only once every stage has finished. A failed stage prints `stopped at …` and `resume with:` instead (`:72-76`).
4. **The relay.**
   - `job_stream.h:21-27`: "What the verb prints on stdout for people is relayed too: while a stream is open, every line written to `std::cout` becomes a `notice` event, so no stray byte of prose reaches the stream. stderr is untouched -- diagnostics, as ever."
   - [machine-mode.md](../../../reference/cli/machine-mode.md#a-long-jobs-run) `:397-399`: "Every line a verb prints for people on stdout reaches the stream as a `notice` (blank lines dropped); stderr keeps the diagnostics, as ever."
   - Among the models verbs, only `models pull` opens a stream (`models_pull.cpp:3910-3917`). 52b's record says "`models convert --register` is not streamed".
   - 52b's record also says "A test holds a text pull's lines equal to the same pull's stream notices". `job_stream_test.cpp:231-237` holds it over an Ollama fake whose llama GGUF says no note.
5. **A two-line note is two notices today.**
   - The relay says one `notice` per newline and keeps a line's leading spaces (`job_stream.cpp:57-62`). So each of the five two-line notes (Evidence 2) reaches a stream as two notices, the second beginning with spaces.
   - `model_chain_test.cpp:249-254` shows an indented line kept as its own notice (`  apogee models convert org--model/safetensors/a --register-with Q4_K_M`).
   - So "one note, one notice" holds today only for a one-line note.
6. **The precedent for "instead".**
   - Machine-mode sessions say a suite's warnings as notices, and "stderr carries none of them. The terminal prints them on stderr as before" (machine-mode.md `:164-177`).
   - A resume's notes moved the same way in 55b (`:126-133`).
   - The pull's producers already ask `JobStream::current()` and frame instead of painting: the chain's stages (`model_chain.cpp:39`, `:48-53`) and a failure (`tell_job_failure`, `job_stream.cpp:104-108`).
7. **The tests today pin no stream for these notes.**
   - Several tests check the notes' words in output that merges stdout and stderr: `models_convert_test.cpp`'s run helper (`:164`, `*out = captured.str() + errors.str()`), its `chain()` helper (`:306-307`), and `models_mlx_test.cpp:3120-3121`. The checks at `models_convert_test.cpp:215-217`, `:256`, `:641-643` and `:675-677` keep passing after a move, but none would fail if a note went back to stdout.
   - `model_chain_test.cpp:89-104` asserts the notes on `run_chain`'s own stream, so it fails after the move. Its check at `:83` that the note is said once (`find == rfind`) would pass with no note at all.
   - `models_mlx_test.cpp:3153` looks for `"\nnote: an embedding model -- …"`, which the house prefix breaks.
   - `models_migrate_test.cpp`'s run helper keeps stdout and drops stderr (`:107-129`), and no case covers the could-not-open note.
   - `model_chain_test.cpp:217-255` pins the stream's notices in order, with `note: a base model` between `  model-Q4_K_M` and `chat with one:  apogee chat -m model-Q4_K_M`. It is this item's guard.
   - No test drives the Ollama pull's architecture advisory. `bert` is known to be unrunnable (`acquire_test.cpp:346-348`), so the Ollama fake (`job_stream_test.cpp:55-72`) built on `minimal_gguf("bert")` would drive it, by reading.
   - The convert tests' shell-script converter (`models_convert_test.cpp:71-116`), their `FakeQuantizer` (`:266-300`) and the MLX fakes play these verbs with no model. Among the MLX fakes, `FixtureSource` (`models_mlx_test.cpp:77`), a Hugging Face repository served from a directory, drives `pull_mlx` (`:222-237`), so a set written with no mlx-lm marks would reach `models_pull.cpp:1797-1800`, by reading.
   - No test drives the two record-write notes (`models_pull.cpp:742-744`, `:3865-3867`): each needs a record write that fails after the bytes landed.
8. **The Mac app's Downloads card.**
   - Pulls run with stream-json (`DownloadsViewModel.swift:239-253`), so their notes stay notices.
   - Store jobs run in text: convert, quantize and repair (`:357-362`). The card's log shows the stdout lines, then the stderr tail (`DownloadsView.swift:152-153`, `progress.notices + ["", progress.stderr]`), so a moved warning stays visible, after the stdout lines.
   - Convert and quantize say their notes at the run's end, which the stderr tail keeps. `models repair` prints none of these notes (`repair_snapshot_in_place`, `models_pull.cpp:1401-1428`). The app never runs `models migrate`.
   - The parsers key on lines the item does not touch:
     - `registeredName` on `registered <name>` lines (`:286-295`);
     - `madePath` and `storedPath` on a line that is an absolute path alone (`:423-442`). A two-line warning's continuation (`point mmproj_path at /…`) never matches, since it starts with words.
   - A repair's outcome is its last stdout line (`:383-386`). The resume command is read from notices only on a stopped or failed run (`JobProgress.swift:209-223`), and the chain says its notes only after every stage finished.
9. **No other surface carries these notes.**
   - Every models verb is an HTTP backfill with no twin (`parity_test.cpp:132-137`).
   - MCP tools and machine-mode sessions don't run them.
   - CLI-59d (v0.2.3) plans its door to run `models pull` with its stream-json relayed verbatim, and `models convert` and `quantize` as text jobs (59d's Open questions, which verbs run as a job). CLI-M58's recorded default writes into 59d that `apogee <verb>: ` stderr lines are relayed as notices as they arrive.
10. **Root cause.** For this family, stdout became the notes' channel through recorded choices:
    - M3's summary says the chain's notes (`model_chain.cpp:17-25`, `:95-97`).
    - 52b made every stdout line a `notice` on an open stream, leaving stderr for "diagnostics, as ever" (`job_stream.h:21-27`, machine-mode.md `:397-399`). A line on stdout therefore became an event, which made stdout look like the right place.

    It is the missing text-mode rule and missing writer that CLI-M58 names, plus a documented relay this item must keep. Because the placement is recorded, this is a convention change: ADR 0006 asks for dated revisions of the records that name it (Decision log).
11. **Blast radius.** Ten print sites in three files: `models_pull.cpp` ×8, `model_chain.cpp` ×1 and `models_migrate.cpp` ×1, fed by the `out.warnings` pushes, `base_model_note` and `embedding_role_note` (Evidence 2 and 3).
    - **Text mode**: each note moves to stderr as one line, `apogee models: …`.
    - **stream-json** (`models pull` only): a one-line note is byte-identical. A two-line note becomes one `notice` where it was two.
    - **The Mac app**: no functional change (Evidence 8). A moved convert or quantize note shows in the log's stderr part.
    - **HTTP, MCP and machine-mode sessions**: none.
    - **Tests**: those in Evidence 7.
    - **Docs**: dated revisions of the M3, 52b and 55d records, and DEVELOPER.md's `cli/model_chain` and `cli/job_stream_test` rows. machine-mode.md's "A long job's run" stays as it is, since a note on a stream is still a notice.
12. **What already works**, and must stay:
    - 52b's relay frames the stdout lines as notices and keeps stderr for diagnostics.
    - The models verbs' fatal refusals already use the house voice on stderr: `fail` at `models_migrate.cpp:32-35`, the repair's error at `models_pull.cpp:1423`, and the chain's cancel at `model_chain.cpp:58`. A failure also tells the stream, so it ends on its `error` (52b).
    - The chain says each note once (`ChainLog::note`), with one prefix (`said_as_note`, 55d).
    - The Downloads card shows the stderr tail in its log.
    - `std::cerr` is tied to `std::cout`, so on a merged pipe a note keeps its place among the result lines (`c-o1`, a one-pipe probe from the same investigation).
13. **Not measured.**
    - The eight `models_pull.cpp` sites and `model_chain.cpp:95-97` are by code, as are the Ollama advisory and MLX pull fixtures (Evidence 7).
    - Only the macOS binary was probed. The writes have no platform branch.
    - The full-screen shell drains stderr before stdout (`tui_child.cpp:106-128`), so a note said after an outcome line may be drawn above it. That is CLI-M58's accepted aside, and this item's notes inherit it.

## Acceptance criteria

- [ ] **Given** a backend on a flat-layout GGUF and an unreadable `embeddings/bad.db`, **when** `models migrate --yes` runs, **then** it exits 0, stdout has `moved`, `updated` and `migrated.` and no `could not open` line, and stderr is exactly `apogee models: note: could not open <home>/embeddings/bad.db: database error: file is not a database`. In one merged capture the note sits between `updated` and `migrated.`. (Today it is on stdout with stderr empty, `c-m2`.)
- [ ] **Given** `models migrate` without `--yes` on the same home, **then** its output is byte-identical to today's (`c-m1`).
- [ ] **Given** a register chain on an open job stream (`model_chain_test`'s table), **when** it finishes, **then** its notices are byte for byte today's list, with `note: a base model` in its place, and stderr holds none of it.
- [ ] **Given** an Ollama store whose model is a `bert` GGUF, **when** `models pull <it> --output-format stream-json` runs without `--yes`, **then** exactly one `notice` carries `warning: architecture 'bert' is not known to run in Apogee's llama.cpp -- pulling anyway is fine; pass --yes to skip this notice.`, after `copying from your Ollama store (…)` and before the first `bytes` event, and stderr has no `architecture` line.
- [ ] **Given** the same pull in text mode, **then** stderr holds that note as one line beginning `apogee models: warning: architecture 'bert'`, stdout has no `architecture` line, and the stream run's notices equal the text run's stdout lines with the note's words in its place.
- [ ] **Given** `models convert` of a snapshot with no chat template (the fake converter), **then** stderr holds exactly one line `apogee models: note: this model has no chat template -- it is a base model, which continues text rather than answering -- to chat, convert its instruction-tuned release (often named '-it' or '-Instruct'); a base model is what fine-tuning starts from.`, and stdout has no `chat template`.
- [ ] **Given** a vision snapshot whose projector the converter refuses, **then** stderr holds one line `apogee models: warning: its projector could not be made -- <reason> -- the model works for text; it cannot read images without one`, and stdout has no `projector could not`.
- [ ] **Given** a register chain over a base model (`models convert <snapshot> --register-with Q4_K_M`, the fake converter and quantizer), **then** the base-model note is said once, on stderr, in the house voice. stdout keeps `ready:`, the entries with their `(a base model …)` asides, and `chat with one:  apogee chat -m <name>` last. In a merged capture the note sits after the entries and before that last line.
- [ ] **Given** an embedding chain (GGUF or MLX), **then** each `apogee models: note: an embedding model -- the backend embeds and answers no chat; point the embedding role at it: apogee config set-default-embedding <entry>` line is on stderr, and stdout's last line is still `point the embedding role at it:  apogee config set-default-embedding <name>`.
- [ ] **Given** any moved note, **then** it has exactly one `note: ` or `warning: ` after `apogee models: `: never `note: note:`, never `note: warning:`, never two house prefixes.
- [ ] **Given** `models delete` with a backend pointing into what goes, `models info` on a projector or a combined blob, and a chain that stops at a stage, **then** the delete's preview, the info's `note:` rows, and `stopped at …` / `resume with:` / `from here on, if this stops, resume with:` stay on stdout, byte-identical.
- [ ] **Given** the Mac app's convert or quantize log, **then** a moved warning is still shown in the log's stderr part, and `madePath`, `storedPath` and `registeredName` parse as before, with the app's `StoreJobsTests` unchanged.
- [ ] **Given** CLI-M58's source check `cli.no_stdout_notes`, **when** it runs, **then** its allow-list names none of `models_pull.cpp`, `model_chain.cpp` and `models_migrate.cpp`, and it passes. A `std::cout << "warning: x\n"` added to any of them fails it, naming the file and line.

## Constraints

- **No protocol change** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md); machine-mode.md `:397-399`). On an open stream a note is a `notice`: no new event type, no new field. Its text is the note's words without the `apogee models: ` prefix, as the stream carries it today.
- **Instead, never also.** On an open stream the note is not written to stderr too. A driver reading both channels would get it twice, and APP-M22's core keeps every `apogee <verb>: ` stderr line as a note. Machine-mode sessions' suite warnings are the precedent.
- **One note, one line.** A two-line note is joined into one line as CLI-M58 joins its own, with its words kept and the continuation's indentation dropped. A note is one line beginning `apogee <verb>: ` (APP-M22's shape rule; CLI-M46's and CLI-M58's constraint).
- **The words are kept.** Only the stream, the prefix and the join change. `note:`/`warning:` stay, and so does `said_as_note`'s one-prefix rule (55d).
- **The result stays on stdout**: the migration's plan and result lines; the chain's `[i/n]` lines, `ready:`, its next step, `stopped at`, `resume with:` and `from here on…`; every hint; `models delete`'s preview, a prediction said before its confirmation; and `models info`'s rows, a read's fields. The Mac app reads the resume lines (`JobProgress.swift:209-223`) and the path and `registered` lines.
- **Exit codes are unchanged**, and every note stays non-fatal. A fatal refusal keeps its stderr line and the stream's `error`.
- **52b's relay stays whole.** stdout prose is still relayed line by line as notices. The writer only stops a note from passing through stdout.
- **Mode parity** ([ADR 0002](../../../adrs/cli/mode-parity.md)): the stream face carries every note the text face says. The models verbs have no HTTP twin to change (backfills).
- **Dated revisions** (ADR 0006): the M3, 52b and 55d records, and every test that pins the old placement, move in the same change.
- **Hermetic, with fakes only.** No model is downloaded, converted or quantized in a test. The in-process fakes stand in: the shell-script converter, `FakeQuantizer`, `minimal_gguf`, the Ollama store fake and the MLX fakes (CLAUDE.md's Implementing a Feature).
- **The Downloads card's parsers are untouched**, and need no change.
- **A live check runs on the dev channel**: `apogee-dev` over a scratch `APOGEE_HOME`, never `~/.apogee` or `~/.apogee-dev` ([CLAUDE.md → Development runs on the dev channel](../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel)).
- **Every CI compiler builds without a warning of ours**: formatting through `make -C lib/src/cli format` (Homebrew LLVM's clang-format 22), and the MinGW scan of the changed files.

## Technical approach

Paths under `lib/src/cli/`.

| File | Change |
|---|---|
| `source/presentation/cli/notes.h`, `.cpp` (CLI-M58's) | `say_note` gains the stream branch. When `JobStream::current()` is open, the text goes to `events().on_notice(text)`, the words alone and nothing on stderr. Otherwise it is the house line on stderr, as M58 built it. The doc comment names the rule and its precedent. |
| `source/presentation/cli/models_pull.cpp` | The eight print sites go onto `say_note("models", …)`: `:742-744`, `:1797-1800`, `:2294-2296`, `:2461-2467`, `:2779-2782`, `:3788-3795`, `:3831-3835` and `:3865-3867`. The two-line notes are joined (below), `base_model_note` (`:253-258`) among them. The hints and `models delete`'s preview (`:4065-4078`) stay on stdout. |
| `source/presentation/cli/model_chain.cpp`, `.h` | `run_chain` says each collected note as `say_note("models", said_as_note(note))` where `:95-97` wrote it on `out`, still after `ready:` and before `next`. `said_as_note` stays. `ChainLog::notes`' comment (`model_chain.h:33-38`) and `run_chain`'s (`:50-60`, "narrating on `out`") say where the notes go. |
| `source/presentation/cli/models_migrate.cpp` | `:353-356` becomes `say_note("models", "note: could not open " + path + ": " + e.what())`. |
| `tests/scripts/cmake/no_stdout_notes.cmake` (CLI-M58's `cli.no_stdout_notes`) | `models_pull.cpp` leaves the allow-list, which names it whole (CLI-M58 listed it alone: the check flags no line in `model_chain.cpp` or `models_migrate.cpp`, and fails on an entry that flags nothing, so the entry must go once its two `warning:` lines move). The warnings these files still build as strings (`out.warnings.push_back("warning: …")`) are no `std::cout` write. `models info`'s `note:` rows (`models.cpp:1310`, `:1312`), a read's fields written to its own stream, are not what the check matches either. |
| `tests/presentation/cli/models_migrate_test.cpp`, `model_chain_test.cpp`, `job_stream_test.cpp`, `models_convert_test.cpp`, `models_mlx_test.cpp` | The fixtures under Testing. |

**The stream branch.** The pull's producers already work this way: each asks `JobStream::current()` and frames what it has instead of painting it.

```cpp
void say_note(std::string_view verb, std::string_view text) {
    if (JobStream* const job = JobStream::current(); job != nullptr) {
        job->events().on_notice(text);  // the words alone; stderr carries none
        return;
    }
    std::cerr << house_voice(verb) << text << '\n';
}
```

Every stdout line before the note ended in a newline, so the relay has already said it, and the notice lands in its place. The branch serves every caller of the writer. Today that is this item's sites and CLI-M58's, and no M58 verb opens a stream, so no other verb's stream changes.

**The joined notes.** Each is joined by CLI-M58's rule, its recorded default for what a moved line says (its Open questions, "What exactly does a moved line say?"): the continuation follows ` -- `. As M58's own reindex note shows, the first line's closing period goes, the continuation's first word is lower-cased, and a ` -- ` inside the continuation becomes `; `. The words are unchanged.

| Note | One line |
|---|---|
| the base-model note (`:253-258`) | `note: this model has no chat template -- it is a base model, which continues text rather than answering -- to chat, convert its instruction-tuned release (often named '-it' or '-Instruct'); a base model is what fine-tuning starts from.` |
| convert's projector (`:2634-2636`) | `warning: its projector could not be made -- <why> -- the model works for text; it cannot read <what> without one` |
| quantize's projector (`:2773-2774`) | `warning: its projector did not come along -- <why> -- point mmproj_path at <path>` |
| the pull's advisory (`:3791-3793`) | `warning: architecture '<a>' is not known to run in Apogee's llama.cpp -- pulling anyway is fine; pass --yes to skip this notice.` |
| the pull's projector copy (`:3834-3835`) | `warning: the projector could not be copied -- <error> -- this model will work for text but not for images` |

The item consumes CLI-M58's writer, its `house_voice` prefix, its join and its rule for which lines move and which stay. It extends the writer by the one branch M58 left to it.

**Reference.**
- Machine-mode sessions' suite warnings (machine-mode.md `:164-177`): a notice on the stream, stderr carrying none, the terminal printing them on stderr. That is exactly this branch.
- `run_chain`'s stage events (`model_chain.cpp:39-53`) and `tell_job_failure` (`job_stream.cpp:104-108`): producers that ask `JobStream::current()` and frame instead of paint.
- `said_as_note` (55d), the one-prefix rule kept inside the writer's words.
- CLI-M58's `say_note`, the writer extended.

**Docs when it ships:**
- MILESTONES: dated revisions of three records:
  - M3's "says them once in its summary" ([Milestone N](../../../assistant/cli/MILESTONES.md#milestone-n--model-operations)): once, where the summary said them, on stderr in the house voice;
  - 52b's "A test holds a text pull's lines equal to the same pull's stream notices" ([Milestone AO](../../../assistant/cli/MILESTONES.md#milestone-ao--the-desktop-clients-cli-growths)): the text run's stdout lines plus the notes its stderr says;
  - 55d's one note prefix ([Milestone AR](../../../assistant/cli/MILESTONES.md#milestone-ar--the-audits-cli-findings)), now after `apogee models: `.

  Then a dated entry.
- DEVELOPER.md: the `cli/model_chain.h/.cpp` row ("Warnings are said once, in the summary"), the `cli/job_stream_test.cpp` row ("the text run's lines exactly the stream's notices"), and CLI-M58's `cli/notes` row, which gains the stream branch.
- CLAUDE.md: CLI-M58's ⚠ invariant names the stream branch as built, and its source check's allow-list loses the models files.
- `reference/cli/machine-mode.md`'s "A long job's run": no change, since a note is still a notice.

## Testing

Hermetic: a temporary `APOGEE_HOME`, mock backends, and the in-process fakes the models tests already use. No model is downloaded, converted or quantized.

- **The bug as a fixture** (`models_migrate_test.cpp`): `c-m2` replayed. Use the existing home (a flat GGUF that a backend names) plus junk bytes at `embeddings/bad.db`. The run helper keeps stderr, which it drops today (`:107-129`). `models migrate --yes` exits 0. stdout holds `moved`, `updated` and `migrated.` and no `could not open`. stderr is exactly `apogee models: note: could not open <home>/embeddings/bad.db: database error: file is not a database`. In a merged capture the note sits between `updated` and `migrated.`. Fails today.
- **The stream guard** (`model_chain_test.cpp:217-255`): unchanged, and passing. The notices are byte for byte today's list, `note: a base model` in its place, and a captured stderr is empty.
- **The chain's notes** (`model_chain_test.cpp:71-104`). The summary case and the one-prefix case read a captured stderr:
  - `apogee models: note: a base model` exactly once;
  - `apogee models: warning: no projector`;
  - `apogee models: note: an embedding model`;
  - `out` holds no `note:`, and there is no `note: note:`.

  The said-once check reads stderr, so it can fail.
- **A pull that says a note** (`job_stream_test.cpp`), with the `OllamaStore` fake built on `minimal_gguf("bert")` and no `--yes`:
  - Streamed: exactly one notice carries the joined advisory, between `copying from your Ollama store (…)` and the first `bytes` event, and `err` holds no `architecture`.
  - As text: `err` holds the house line and stdout none. The streamed notices equal the text run's stdout lines with the note's words in its place.
  - The existing llama case keeps its equality as it is.
- **Convert and quantize** (`models_convert_test.cpp`, with the shell-script converter and `FakeQuantizer`). The run helper and `chain()` keep stdout and stderr apart, keeping a merged copy for the order checks.
  - A base model: one stderr line with the joined note, and no `chat template` on stdout.
  - A refused projector: one stderr line with the joined warning.
  - A chain over a base model: the note once on stderr, the entries and `chat with one:` on stdout.
  - An embedding chain: the house-voice embedding notes on stderr, the door last on stdout.
- **MLX** (`models_mlx_test.cpp:3109-3160`): the embedding note is on stderr in the house voice, and the door is stdout's last line. A `pull_mlx` through `FixtureSource` of a set with no mlx-lm marks says `apogee models: note: its files carry no mlx-lm marks …` on stderr and holds no `mlx-lm marks` on stdout.
- **Kept, as guards**: `models delete`'s preview (`models_pull_test.cpp:232`, `:567`, `:859`) and `models info`'s rows pass unchanged.
- **The source check**: CLI-M58's `cli.no_stdout_notes` passes with the models files out of its allow-list. Checked once by hand: a `std::cout << "warning: x\n"` added to `models_pull.cpp` fails it, then is removed. The check matches only `warning:`/`Warning:` and `[<tag>] ` literals, so the two record-write `note:` sites (`:742-744`, `:3865-3867`), which no test drives, are held by review.
- **The app**: `StoreJobsTests` and `DownloadsViewModelTests` run unchanged.
- **Gate:** `lib/scripts/cicd.sh --app cli --test`, `make -C lib/src/cli format-check`, and the MinGW scan of the changed files.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Do the chain summary's notes (55d) leave the summary for stderr? | `[default]` | Yes, as house-voice notes, said once each where the summary said them: after `ready:` and its entries, before the next step. On an open stream they are notices, as today. This covers every note the chain collects: the convert and quantize warnings, the base-model note and the embedding-role note. The next-step line, which already names the embedding role's command, stays on stdout. |
| Does a note on an open stream need its own event type, so a driver can tell a warning from narration? | `[default]` | No. It stays a `notice`, with no protocol change. An additive event type waits for a driver that needs it. |
| A two-line note on an open stream: one notice, or today's two? | `[default]` | One. It carries the joined line, the same words the terminal prints on stderr after `apogee models: `. A one-line note's notice is byte-identical to today's. For the five two-line notes, the only change is that the indented continuation joins its first line, with no event type, field or order changed. |
| The writer: its name, its prefix, how a two-line note is joined, and which lines move and which stay | `(consumed decision)` | [CLI-M58](../../../assistant/cli/MILESTONES.md#2026-11-27--a-runs-notes-are-said-once-on-stderr-maintenance-item-m58-a-runs-notes-are-said-once-on-stderr-in-the-house-voice) (shipped 2026-11-27): `say_note(verb, text)` and `house_voice(verb)`. A two-line note is joined with ` -- `. A run's note moves; a read's qualifier, a listing, a prediction and next-step advice stay. |
| How does a text job's note reach a paired device? | `(consumed decision)` | CLI-M58's recorded default, written into [CLI-59d](../v0.2.3/reads-and-commands-over-the-network.md): the door relays `apogee <verb>: ` stderr lines as notices as they arrive. `models pull`'s stream is relayed verbatim. |
| Where does the Downloads card show a finished job's notes? | `(consumed decision)` | [APP-M22](../../../assistant/darwin/MILESTONES.md#2026-11-26--jobs-notes-reach-the-person-maintenance-item-m22-a-jobs-notes-reach-the-person)'s follow-up, "every done path shows its notes". Until then the card's log shows them in its stderr part. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-26 | **Diagnosed.** For the models family, stdout became the notes' channel through two recorded choices: M3's summary says the chain's notes, and 52b's relay makes every stdout line a `notice` on an open stream. The text-mode rule and the shared writer CLI-M58 adds were missing here too (Evidence 10). | the spec |
| 2026-11-26 | **Placed in Maintenance as M60**, by the user's call to spec the APP-M21 and APP-M22 probes' CLI asides as maintenance items from M48, grouped only where they share one seam. It shares CLI-M58's seam, the stream a finished run's note is said on, but it is its own item for three reasons: it changes a documented job-stream relay (52b); its notes travel on stream-json; and the Mac app's Downloads card reads those notices. The writer's stream branch is its alone. | the user |
| 2026-11-26 | **Blocked by CLI-M58**, whose writer this item extends. M58's writer has no stream branch, since no verb it moves opens a stream. | the spec |
| 2026-11-26 | **Not gated on APP-M22.** No moved models note is an app toast today: pulls stream, so their notes stay notices; the convert and quantize logs show the stderr tail; repair prints none of these notes; and the app never runs migrate. So landing this item hides nothing the app shows. It still lands after APP-M22, through its gate on CLI-M58, which is gated on APP-M22. | the spec |
| 2026-11-26 | **"Instead", not "also"**, on an open stream: the note is a `notice` and is not written to stderr, as machine-mode sessions say a suite's warnings. Saying it on both would duplicate it for a driver, and for APP-M22's core. | the spec |
| 2026-11-26 | **Recorded placements change, each with a dated revision** (ADR 0006): M3's "says them once in its summary", 52b's "a text pull's lines equal to the same pull's stream notices", and 55d's one prefix, now after `apogee models: `. | the spec |
| 2026-11-26 | **Every two-line note is joined into one line**, and so into one notice on a stream. The investigation named only the quantize and convert continuations, and said the stream would be byte-identical. That holds for every one-line note. The five two-line notes are two notices today (Evidence 5), and APP-M22's shape rule needs one line. Each is joined by CLI-M58's recorded join (` -- `, as its reindex note is joined), so the house voice reads the same in both items. | the spec |
| 2026-11-26 | **Ten print sites, not eleven.** The investigation counted 11. Re-read at HEAD, there are eight in `models_pull.cpp` and one each in `model_chain.cpp` and `models_migrate.cpp`, fed by the warning pushes and the two note builders. | the spec |
| 2026-11-27 | **Released: CLI-M58 shipped** ([MILESTONES](../../../assistant/cli/MILESTONES.md#2026-11-27--a-runs-notes-are-said-once-on-stderr-maintenance-item-m58-a-runs-notes-are-said-once-on-stderr-in-the-house-voice)), with `say_note` and `house_voice` in `presentation/cli/notes.h/.cpp` as this item assumes, and `cli.no_stdout_notes`'s allow-list holding `models_pull.cpp` whole and no other models file -- the check flags nothing in `model_chain.cpp` or `models_migrate.cpp` (the migrate note starts `note:`, not one of its voices), so only the one entry leaves, and the check's stale-entry rule fails the build until it does. | CLI-M58's build |

## Out of scope

- **The other verbs that open a job stream**: `video generate`, `video compose`, `voice say`, `datasets pull`, and the training verbs under stream-json (55h, `train.cpp:87-95`). Sweep them with the same writer only if a note is found on their stdout; none is listed today.
- **`models delete`'s stranded preview** (`models_pull.cpp:4065-4078`), a prediction before the confirmation, and **`models info`'s `note:` rows**, a read's fields. Both are kept.
- **The other families' notes** (graph, embed, knowledge, agents), and the writer itself: [CLI-M58](../../../assistant/cli/MILESTONES.md#2026-11-27--a-runs-notes-are-said-once-on-stderr-maintenance-item-m58-a-runs-notes-are-said-once-on-stderr-in-the-house-voice) (shipped 2026-11-27).
- **The HTTP graph jobs' missing notes**: [CLI-M59](../../../assistant/cli/MILESTONES.md#2026-11-27--the-http-graph-jobs-say-what-the-cli-says-maintenance-item-m59-the-http-graph-jobs-say-what-the-cli-says-and-build-a-mixed-graphs-code-as-the-cli-does) (shipped 2026-11-27).
- **A card for a Downloads job's notes in the Mac app**: APP-M22's follow-up.
- **An event type for warnings** (Open questions).
- **59d's relay of house-voice stderr lines**: CLI-M58 writes that constraint into 59d.
- **The full-screen shell's draw order** for a note said after an outcome line: CLI-M58's accepted aside.
- **HTTP routes for the models verbs**: backfills owned by the models plane (`parity_test.cpp:132-137`).

## Links

- **Blocked by:** — (CLI-M58 [shipped 2026-11-27](../../../assistant/cli/MILESTONES.md#2026-11-27--a-runs-notes-are-said-once-on-stderr-maintenance-item-m58-a-runs-notes-are-said-once-on-stderr-in-the-house-voice), the writer this item extends) · **Blocks:** —
- **Sibling** (the same investigation's seams): [CLI-M58](../../../assistant/cli/MILESTONES.md#2026-11-27--a-runs-notes-are-said-once-on-stderr-maintenance-item-m58-a-runs-notes-are-said-once-on-stderr-in-the-house-voice) (shipped 2026-11-27), the CLI's other notes on stderr; [CLI-M59](../../../assistant/cli/MILESTONES.md#2026-11-27--the-http-graph-jobs-say-what-the-cli-says-maintenance-item-m59-the-http-graph-jobs-say-what-the-cli-says-and-build-a-mixed-graphs-code-as-the-cli-does) (shipped 2026-11-27), the HTTP graph jobs' notes.
- **Relates to:**
  - [APP-M22](../../../assistant/darwin/MILESTONES.md#2026-11-26--jobs-notes-reach-the-person-maintenance-item-m22-a-jobs-notes-reach-the-person), whose stdout-warnings question this answers for the models verbs, and [APP-M21](../../../assistant/darwin/MILESTONES.md#2026-11-26--listed-name-reaches-the-verb-maintenance-item-m21-a-listed-name-reaches-the-verb-as-listed), whose probes ran beside it;
  - [CLI-M46](../../../assistant/cli/MILESTONES.md#2026-11-27--a-collection-the-config-cannot-hold-is-refused-before-it-is-written-maintenance-item-m46-a-collection-the-config-cannot-hold-is-refused-before-it-is-written) (shipped 2026-11-27), whose constraint keeps its own notes in the house voice on stderr;
  - [CLI-59d](../v0.2.3/reads-and-commands-over-the-network.md), whose door relays `models pull`'s stream and runs `models convert` and `quantize` as text jobs;
  - [APP-7c](../../darwin/v0.2.4/every-place-against-a-server.md), whose Downloads card runs these jobs against a server.
- **ADRs:** [ADR 0006](../../../adrs/cli/backwards-compatibility.md) (the protocol grows additively; a recorded contract holds until a dated revision) and [ADR 0002](../../../adrs/cli/mode-parity.md) (every mode says the note).
- **Revises:** the M3 record ([Milestone N](../../../assistant/cli/MILESTONES.md#milestone-n--model-operations)), the 52b record ([Milestone AO](../../../assistant/cli/MILESTONES.md#milestone-ao--the-desktop-clients-cli-growths)) and the 55d record ([Milestone AR](../../../assistant/cli/MILESTONES.md#milestone-ar--the-audits-cli-findings)).
- **Follows from:** [machine-mode.md's "A long job's run"](../../../reference/cli/machine-mode.md#a-long-jobs-run) (52b), whose relay it keeps.
