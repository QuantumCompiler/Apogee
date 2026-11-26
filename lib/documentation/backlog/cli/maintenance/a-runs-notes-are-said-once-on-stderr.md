# CLI-M58 · A run's notes are said once, on stderr, as `apogee <verb>: …`

| Field | Value |
|---|---|
| **Type** | 🐞 Bug |
| **Project** | CLI |
| **Status** | 🔒 Blocked |
| **Fix version** | Maintenance (any time) |
| **Component** | Presentation › a finished run's notes in `graph build`/`update`/`communities`, `embed ingest`, `knowledge capture`/`reindex` and `agents delete`, and the one writer (`cli/notes.h`) |
| **Labels** | `house-voice` `stderr` `graph` `knowledge` |
| **Epic** | — (maintenance has no track) |
| **Blocked by** | [darwin M22](../../darwin/macos/maintenance/a-jobs-notes-reach-the-person.md) |
| **Blocks** | [M60](the-models-verbs-say-their-notes-on-stderr.md) |
| **Reporter** | The APP-M21 and APP-M22 probes' CLI asides (2026-11-26), then a sandboxed investigation, two skeptics and a critic the same day |
| **Created** | 2026-11-26 |

---

## Summary

A finished `graph build`, `graph update`, `graph communities`, `embed ingest`, `knowledge capture`, `knowledge reindex` or `agents delete` prints its notes on stdout among its result lines, with stderr empty. These are notes like a gone source tree, entity vectors skipped, a failed chunk, a collection's mixed vectors or a file it could not remove, and they come in five voices, `[graph] `, `warning:`, `Warning:`, `Note:` and `(could not …)`, or with no mark at all. Two of those runs also say the same fact on stderr in the house voice, so one fact appears twice. The Mac app keeps one stdout line and, once APP-M22 ships, exactly the `apogee <verb>: ` stderr lines, so these notes are lost or taken for the outcome. The item adds one writer and moves 14 statements onto it: each note is said once, on stderr, as `apogee <verb>: <today's words>`, and the exit code and the result stay as they are.

## Description

### User story

**As** someone running a graph build, an ingest, a capture, a reindex or an agent delete, at the prompt or through the Mac app,
**I want** each note about the finished run said once, on stderr, in Apogee's own voice,
**so that** stdout is the result a script or the app reads, the app's note card shows every note, and no fact reaches me twice in two voices.

### Steps to reproduce

The sandbox, 2026-11-26, with the built binary `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`: a throwaway home `<h>` nested three folders deep, `env -i`, `PATH=/usr/bin:/bin`, `HOME` and `APOGEE_HOME` both `<h>`, stdin from `/dev/null`, and stdout and stderr recorded as two pipes. Then `config init`, a mock `extractor` (a script answering with entities), a mock `flaky` (its second turn not JSON), and `config set-default-extraction extractor`. There is no embedder unless one is named.

1. Run `embed ingest notes <h>/docs -q`, then `graph build --source <h>/src --source <h>/src2 --graph code -q` (exit 0).
2. Run `mv <h>/src2 <h>/src2-gone`, then `graph update code -q` (`c-g1`).
3. Run `graph build --source <h>/src3 --graph code -q` (`c-g3`), then `graph build code -q` (`c-g2`).
4. Run `config add-graph ghosty --collections ghost`, then `graph build ghosty --source <h>/src3 -q` (`c-n1`).
5. Run `graph build notes -m flaky -q` (`c-f1`).
6. Add two mock embedders, `emba` (`mock-embed-a`) and `embb` (`mock-embed-b`). Run `embed ingest vecs <h>/docs -q` under `emba`, then `config set-default-embedding embb` and `embed ingest vecs <h>/more/added.md --retriever vector -q` (`c-e2`).
7. In a second home with a mock clerk and a mock embedder, capture one record with `--retriever vector` and one with `--retriever lexical`, then run `knowledge reindex` (`c-k3`).
8. In that home, run `agents create probe`, `chmod 555 <h>/prompts`, then `agents delete probe --purge` (`c-ag2`).

### Actual result

Every run exits 0.

2. Stdout is `Updating the code of "code" from [src, src2] -- tree-sitter, no model...`, then `[graph] source tree <h>/src2 is not a directory -- it contributes nothing, and what it contributed is forgotten.`, then `Code graph for "code" (no model):` and its lines. Stderr is empty.
3. `c-g3`: stderr is `apogee graph: warning: source tree '<h>/src2' is not a directory`, and stdout holds the same `[graph] source tree <h>/src2 …` line. One fact is said twice, in two voices. `c-g2`: the stdout line alone, and stderr empty.
4. Stderr is `apogee graph: warning: member collection 'ghost' is not configured yet (ingest registers it on first use)`. Stdout ends with `[graph] member collection "ghost" has no database -- it contributes nothing this build.`, then `[graph] no member collection of "ghosty" has any data yet -- only its code was built.`
5. Stderr is empty. Stdout's first line is `[graph] entity vectors skipped for "notes" -- backend 'extractor' cannot embed; entity search will be full-text only.`, then `Building the knowledge graph for "notes" with flaky...`, a blank line, `b.md [chunk 0] FAILED: the extractor did not return entities and relations; /: the answer is not a JSON object`, and the summary.
6. Stdout is `1 file(s), 1 chunk(s), 1 vector(s) [mock-embed-b]`, then `warning: 2 source(s) keep vectors from 'mock-embed-a' -- the collection searches lexically until every source is re-ingested with 'mock-embed-b'`. Stderr is empty.
7. Stdout is `Note: 1 of 2 record(s) in "knowledge" have no vectors (captured lexically).`, then `Reindexing embeds every record, including those -- pass an id to reindex just one.`, then `Reindexed 2 record(s) in "knowledge" [mock-embed-a]`. Stderr is empty.
8. Stdout is `removed agent 'probe' from <h>/config/config.json`, then `  (could not remove <h>/prompts/probe.txt: Permission denied)`, then `  removed <h>/schemas/probe-output.json`. Stderr is empty.

### Expected result

Every run exits 0 as today, and stdout carries the result alone. Each note is exactly one stderr line, `apogee <producer verb>: <today's words>`, with the `[graph] ` tag dropped and `Warning:`/`Note:` lower-cased. A fact the run already says is said once, and `-q` silences none of them.

2. Stderr is exactly `apogee graph: source tree <h>/src2 is not a directory -- it contributes nothing, and what it contributed is forgotten.`, and stdout holds no `source tree` line.
3. `c-g3`: that one stderr line, and no `apogee graph: warning: source tree …`. `c-g2`: as in step 2.
4. Stderr has one line about `ghost`: `apogee graph: member collection "ghost" has no database -- it contributes nothing this build.` Stdout's last line is `No member collection of "ghosty" has any data yet -- only its code was built.`, the outcome, kept on stdout with its tag dropped.
5. Stderr holds `apogee graph: entity vectors skipped for "notes" -- …` and `apogee graph: b.md [chunk 0] FAILED: …`. Stdout runs from `Building …` to the summary with no `FAILED`, and the summary is unchanged.
6. Stdout is `1 file(s), 1 chunk(s), 1 vector(s) [mock-embed-b]` alone. Stderr is `apogee embed: warning: 2 source(s) keep vectors from 'mock-embed-a' -- …`.
7. Stdout is `Reindexed 2 record(s) in "knowledge" [mock-embed-a]` alone. Stderr is `apogee knowledge: note: 1 of 2 record(s) in "knowledge" have no vectors (captured lexically) -- reindexing embeds every record, including those; pass an id to reindex just one.`
8. Stdout holds the two `removed` lines. Stderr is `apogee agents: could not remove <h>/prompts/probe.txt: Permission denied`.

### Background

**Two conventions, each kept on purpose.** A finished run's non-fatal notes have gone to one of two places, and nothing chooses between them.
- **"The summary says it", on stdout.** Each of these lines was put there deliberately:
  - the agents delete line came in f37cf4ad (2026-09-13);
  - the failed-chunk line came in 34ed0ce4 (2026-09-19), whose test checks the entity-vector skip on stdout and the progress on stderr in the same case;
  - reindex's `Note:` came in 1d57c282 (2026-09-19);
  - 27k (2026-10-05) recorded "The summary says … a tree that is gone or offers no file", and the "only its code was built" line came in c3127496 the same day;
  - 57s (2026-11-08) recorded that a partial re-ingest "warns", and pinned the warning on stdout.
- **The house voice, on stderr.** Registration and validation notes have always said `apogee <verb>: …` on stderr with exit 0.

The rule is written down for the structured modes and the full-screen shell, but never for text mode. There is no shared writer either: every file carries its own prefix literal. So each feature picked a stream when it was written, sometimes both in one run.

**How it came about.** APP-M22 (2026-11-26) found that the Mac app keeps one stdout line from a finished job and never reads its stderr. Its fix keeps the house-voice stderr lines as notes and shows them on a card. It left a remainder (its Evidence 11): the warnings this CLI prints on stdout, which no shape marks, so its rule can't catch them. Its Open questions deferred the choice to a follow-up: either the CLI says them in the house voice on stderr (then the app needs no change), or the app keeps every stdout line. This item is that follow-up, on the CLI side, and APP-M22's Open questions and Decision log now record that choice.

It was specced with the other CLI asides of the APP-M21 and APP-M22 probes. The investigation found 14 statements in 4 files, and the seam splits three ways:
- this item, the CLI's text-mode note stream;
- [CLI-M59](the-http-graph-jobs-say-what-the-cli-says.md), the HTTP graph jobs, which drop the same facts;
- [CLI-M60](the-models-verbs-say-their-notes-on-stderr.md), the models family, whose notes also ride a job stream.

**Why the CLI side.** The CLI's own written rules already pick stderr for narration and notes (`tui_child.cpp:106-109`, `machine-mode.md:224-226`, `job_stream.h:21-27`). The double voice can only be fixed here. Keeping every stdout line in the app would put summaries and listings on APP-M22's note card, against its rule that the toast is the outcome and the card holds the notes. It would help neither the terminal nor CLI-59d's remote relay.

**The design.**
- One writer in Presentation, `say_note(verb, text)`, writes one stderr line in the house voice.
- The 14 statements move onto it, keeping their words.
- `graph build` stops repeating the validation warnings that its build says again.
- The lines that are the result stay on stdout: a read's qualifier, a listing, a dry run's prediction, next-step advice and an outcome.
- A one-line test seam lets the paid-embedder note be driven in CLI tests.
- The rule is written into the contributor guide as an invariant, with dated revisions of 27k and 57s.
- CLI-59d relays these lines as they arrive: its document carries that constraint (written 2026-11-26, with this item).

### Evidence

All of this was measured on 2026-11-26 by an investigator, two skeptics and a critic, each in their own sandbox, with the built binary `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)` (`v0`). Each run's stdout, stderr and exit were recorded separately. The critic's runs are `c-…`, the investigator's are `g…` and `e…`, and the fix skeptic's long build is `b1`. Every `file:line` below was read at HEAD 5b8927c9. Paths: `cli/`, `operations/`, `httpserver/` and `views/` are under `lib/src/cli/source/presentation/`; `data/` and `business/` under `lib/src/cli/source/`; `tests/` under `lib/src/cli/`. `machine-mode.md` and `http-api.md` are in `reference/cli/`, and the `.swift` files are the Mac app's view-models.

1. **The binary stands for HEAD on this seam** (the critic, 2026-11-26, `v0`, `strings` and `git diff`). Its version line names 33ea685d, but its strings carry HEAD 5b8927c9's CLI changes. `git diff --stat 33ea685d HEAD -- lib/src/cli/source` lists 11 files, none of them graph, embed, knowledge, auth or agents.
2. **A gone source tree is said on stdout only** (recorded 2026-11-26).
   - `c-g1`, `graph update code -q`: exit 0, stdout `[graph] source tree <h>/src2 is not a directory -- it contributes nothing, and what it contributed is forgotten.` between the run's header and its code summary, stderr empty.
   - `c-g2`, `graph build code -q`: the same line, stderr empty.
   - The source is `print_code_summary`, `cli/graph.cpp:746-750`. This is the app's Rebuild (APP-M22's Evidence 11).
3. **One fact, twice, in two voices** (recorded 2026-11-26, then read at HEAD).
   - **A gone tree.** `c-g3`, `graph build --source <h>/src3 --graph code -q`: exit 0. Stderr is `apogee graph: warning: source tree '<h>/src2' is not a directory`, from `cli/graph.cpp:916-918` printing the warning that `validate_named_graph` pushes at `operations/graph_members.cpp:143-148`. Stdout holds the `[graph] source tree <h>/src2 …` line. `c-o2`, the same run on one pipe, shows the stderr warning first, then the `[graph]` line.
   - **A member without a database.** `c-n1`, `graph build ghosty --source <h>/src3 -q`: exit 0. Stderr is `apogee graph: warning: member collection 'ghost' is not configured yet (ingest registers it on first use)` (`graph_members.cpp:136-142`). Stdout ends with `[graph] member collection "ghost" has no database -- it contributes nothing this build.` (`cli/graph.cpp:1049-1054`), then `[graph] no member collection of "ghosty" has any data yet -- only its code was built.` (`:1056-1059`).
   - **Why the build always says it again.** `run_graph_build` runs `register_sources` and then always runs `build_named` (`cli/graph.cpp:1381-1405`). The validation pushes only two kinds of warning (`graph_members.cpp:136-148`), and the build re-says each one:
     - a tree that is not a directory lands in `GraphSourceBuild.missing` by the same `is_directory` test (`operations/graph_sources.cpp:99-100`), and `print_code_summary` says it, dry runs included (`cli/graph.cpp:855`);
     - a member with neither an entry nor a file has no file, so its store is null (`graph_members.cpp:52`), and the member loop says it.

     `graph update` never runs the validation, so the build's line is the one to keep.
4. **An empty tree** (recorded 2026-11-26, `g4`, `graph build --source <h>/empty --graph eg`): stdout `[graph] source tree <h>/empty offers no file to read -- inside a git repository, only what git tracks or does not ignore is read.`, stderr empty (`cli/graph.cpp:751-755`).
5. **"Entity vectors skipped" is said on stdout, first in a collection's build** (recorded 2026-11-26). Stderr is empty in each of these runs, and the source is `cli/graph.cpp:954-957` (a collection) and `:1069-1072` (a named graph):
   - `c-f1`, and `c-g5` (`graph build notes`, no embedder): stdout's first line, `[graph] entity vectors skipped for "notes" -- backend 'extractor' cannot embed; entity search will be full-text only.`;
   - `g6`, `graph build mix` (a named graph with a member `ghost` without a database): the member line, then `[graph] entity vectors skipped for "mix" -- …`;
   - `e3`, `embed ingest fresh <dir> --graph` with no embedder: the same line after the ingest's own lines, through `run_graph_build` (`cli/embed.cpp:496`).
6. **"Summary vectors skipped"** (recorded 2026-11-26, `g7`, `graph communities notes --min-size 2 -m summarizer`, no embedder): stdout line 1 `[graph] summary vectors skipped for "notes" -- backend 'extractor' cannot embed; summaries will be full-text searchable only.`, stderr only `[graph] summarising community 1/1` (`cli/graph.cpp:1663-1666`).
7. **A failed chunk** (recorded 2026-11-26, `c-f1`, `graph build notes -m flaky -q`).
   - Exit 0 and stderr empty. Stdout has a blank line, then `b.md [chunk 0] FAILED: the extractor did not return entities and relations; /: the answer is not a JSON object`, then the summary's `Chunks extracted:  1 (1 failed after retry -- re-run to retry them)`.
   - The source is `cli/graph.cpp:704-711`, inside `busy.above(...)`. A real build cuts the error to 200 characters. A dry run prints it whole, as part of its extraction preview.
8. **"Embedding stopped early"** (code read, 2026-11-26; not measured).
   - `cli/graph.cpp:305-308`: `Warning: entity embedding stopped early (<error>) -- affected entities stay full-text searchable; re-run to embed them.`
   - `:1728-1731`: `Warning: summary embedding stopped early (<error>) -- summaries stay full-text searchable; re-run to embed them.`
   - Neither can be driven: the mock embedder never fails, since its vectors are derived from the text (`data/backends/mock.h:207-264`).
   - On a collection rebuild with nothing to extract, the entity warning prints after `Nothing to extract -- …` (`cli/graph.cpp:327-336`). With nothing reconciled, it is then the last stdout line, which is the Graph place's toast.
9. **Ingest's two notes** (recorded and read, 2026-11-26).
   - **The binding warning.** `c-e2`: stdout `1 file(s), 1 chunk(s), 1 vector(s) [mock-embed-b]`, then `warning: 2 source(s) keep vectors from 'mock-embed-a' -- the collection searches lexically until every source is re-ingested with 'mock-embed-b'`, and stderr empty (`cli/embed.cpp:421-423`). The words, `warning: ` included, are Business's (`business/agentloop/retriever.cpp:271-275`).
   - **The paid-embedder note**, by code: `cli/embed.cpp:418-420` writes `decision.note` to stdout. Its words are `indexed for text search only: '<model>' is a paid embedder, and vectorising a whole collection through it takes --retriever vector (or a retriever: vector pin)` (`retriever.cpp:209-211`). It is said under the auto retriever with a metered embedder.
   - **Why the paid note can't be driven yet.** No mock embedder can be metered from config. `data/backends/factory.cpp:110-119` builds a `MockEmbeddingProvider` and ignores `model_path`, while a generator mock reads `"metered"` from its script (`:133`). `mock.h:245-246` states the purpose that is missing: "Free unless a test says otherwise -- the spend rules are testable against a paid embedder with nothing installed".
10. **Knowledge capture splits its notes across the streams** (code read, 2026-11-26). `cli/knowledge.cpp:548-550` writes `result.decision.note` to stdout, beside `:555-557`, which writes each of `result.notes` to stderr as `apogee knowledge: …`. The dry run prints its note (`:517-519`) and `Warning: a real capture would fail -- …` (`:520-523`) inside its `Would store in … (retriever: …)` prediction.
11. **Reindex's `Note:` is the Records toast** (recorded 2026-11-26).
    - `c-k3`, `knowledge reindex`: exit 0, stdout `Note: 1 of 2 record(s) in "knowledge" have no vectors (captured lexically).`, then `Reindexing embeds every record, including those -- pass an id to reindex just one.`, then `Reindexed 2 record(s) in "knowledge" [mock-embed-a]`. Stderr is empty (`cli/knowledge.cpp:897-902`).
    - The HTTP twin returns this fact as `note` (`reference/cli/http-api.md:785-787`; `httpserver/admin_knowledge.cpp:600-606`).
    - The Records place starts `knowledge reindex` (`RecordsViewModel.swift:333`) and toasts `notices.first` (`:354`), so today the toast is the Note.
12. **Agents delete** (recorded 2026-11-26, `c-ag2`, `agents delete probe --purge` with `prompts/` at 0555): exit 0, stdout `removed agent 'probe' from <h>/config/config.json`, then `  (could not remove <h>/prompts/probe.txt: Permission denied)`, then `  removed <h>/schemas/probe-output.json`, and stderr empty (`cli/agents_cmd.cpp:325-326`). The Library place runs it as a command and toasts its first stdout line (`LibraryViewModels.swift:302`, `:357-367`).
13. **The stdout convention is recorded** (code and docs read, 2026-11-26).
    - 27k's entry in MILESTONES (Milestone AE): "The summary says … a tree that is gone or offers no file".
    - `tests/presentation/cli/graph_test.cpp:166` checks `entity vectors skipped` on `out`, while `:168` checks `[graph] extracting` on `err`, in the same case.
    - 57s's entry (Milestone AT) records that the partial re-ingest "warns `1 source(s) keep vectors from 'mock-embed-a' -- ... with 'mock-embed-b'`", and `tests/scripts/cmake/config_e2e.cmake:506-507` pins it on stdout.
    - `cli/embed.cpp:398-399`: "The first line is the whole story in brief -- the app shows it as the ingest's outcome -- and every line after it names its part."
14. **The house voice beside it** (grep and read, 2026-11-26).
    - There are 79 statements of the form `std::cerr << "apogee <verb>: ` in 31 files of `presentation/cli`.
    - The registration and validation notes use it with exit 0: `cli/embed.cpp:454`, `:467`; `cli/graph.cpp:917`, `:988`, `:1000`; `cli/knowledge.cpp:181`, `:535`, `:556`.
    - `c-cag`, `config add-graph ghosty --collections ghost`: stderr `apogee config: warning: member collection 'ghost' is not configured yet (ingest registers it on first use)`.
    - The rule is stated for the shell and the structured modes:
      - `cli/tui_child.cpp:106-109`: "a command narrates on stderr as it works and says its outcome on stdout at the end";
      - `machine-mode.md:224-226`: "every diagnostic, warning, and progress note goes to stderr";
      - `cli/job_stream.h:21-27`: "stderr is untouched -- diagnostics, as ever".
    - It is never stated for text mode. CLAUDE.md's Code Style has no entry yet for shared-helper locations (`:355` says they will accrete there), and no CLI ADR mentions stderr.
    - There is no shared writer: 32 per-file `fail` helpers in 27 files, each with its own prefix literal.
15. **Root cause.** A finished run's notes follow two unwritten conventions, and no written rule or shared writer chooses between them. So each feature picked a stream when it was written, and sometimes picked both in one run:
    - `cli/graph.cpp:916-918` against `:746-750` (`c-g3`);
    - `:916-918` against `:1049-1054` (`c-n1`);
    - `cli/knowledge.cpp:548-550` against `:555-557`.

    The facts already exist as data: `GraphSourceBuild.missing` and `.empty`, `EntityEmbedder.note`, the null member stores, `IngestBinding.warning`, `IngestRetrieval.note`, and the store's `vectorless_count`. So the cause is the Presentation layer's choice of stream alone, and no Business change is needed.
16. **Order and quiet** (recorded and grep, 2026-11-26).
    - **Order on a merged pipe holds.** `c-o1`, `embed ingest 'my notes' …` with stdout and stderr on one pipe, prints `1 file(s), 1 chunk(s)`, then `apogee embed: could not register 'my notes' in config -- …`, in that order. `std::cerr` is tied to `std::cout`, so every stderr write flushes stdout first. The CLI never unties them: the source has no stream `tie(` call (its `tie(` hits are `std::tie` comparisons), no `sync_with_stdio` and no `unitbuf`, and its one `setvbuf` is on a file being hashed (`data/modelstore/sidecar.cpp:226`).
    - **`-q` means no progress, never no notes.** See `cli/graph.cpp:1457` ("No progress: no busy line, no per-chunk lines"), `:1487`, `:1623` and `cli/embed.cpp:258-259`. `c-g3` ran with `-q` and still printed its stderr note.
17. **Tests that pin today's placement** (read, 2026-11-26):
    - `graph_test.cpp:166`, `:295`, `:391` and `:514` check the notes on `out`;
    - `tests/presentation/cli/knowledge_test.cpp:787` checks reindex's `Note:` on `out`, and `:797`'s `out.find("Note:") == npos` would pass vacuously after the move;
    - `config_e2e.cmake:506-507` checks the binding warning on `APOGEE_OUT`, and the negative checks at `:530` and `:546` would pass vacuously unless they read `APOGEE_ERR` (the runner captures both apart, `:40-54`);
    - `tests/scripts/sh/code_graph_e2e.sh:63-64` merges `2>&1` in a dry run and greps the words, so it is unaffected.
18. **Blast radius.**
    - **Sites.** 14 statements in 4 files, across 7 verbs:
      - `cli/graph.cpp` ×9: `graph build`, `graph update` and `graph communities`, plus `embed ingest --graph` through `run_graph_build`;
      - `cli/embed.cpp` ×2: `embed ingest`;
      - `cli/knowledge.cpp` ×2: the real `knowledge capture`, and `knowledge reindex`;
      - `cli/agents_cmd.cpp` ×1: `agents delete`.

      10 are reproduced on the binary (`c-g1`, `c-g2`, `c-g3`, `c-n1`, `c-f1`, `c-g5`, `c-e2`, `c-k3`, `c-ag2`, `g4`, `g6`, `g7`, `e3`). 4 are by code only: `cli/graph.cpp:305-308` and `:1728-1731`, `cli/embed.cpp:418-420` and `cli/knowledge.cpp:548-550`.
    - **Kept on stdout:**
      - `knowledge capture --dry-run`'s note and `Warning:` (`cli/knowledge.cpp:517-523`, a prediction);
      - `auth list`'s warning (`cli/auth_cmd.cpp:100-102`, a read's qualifier, `c-a1`). Its JSON carries `warning` (`c-a2`), and `check` reports it as a row (`c-a3`);
      - `… only its code was built` (`cli/graph.cpp:1056-1059`, the outcome, its tag dropped);
      - next-step advice: dedupe's `Note: community memberships changed …` (`:1822`) and the build summary's ``Run `apogee graph stats …` `` (`:356`);
      - the notes of `embed query`, `knowledge query` and `graph_navigate` (read qualifiers, `config_e2e.cmake:517-526`);
      - every listing and count (`cli/embed.cpp:424-444`, `cli/graph.cpp:282`, `:553`, `:728-737`).
    - **JSON and stream faces are unchanged.**
      - `knowledge capture --json` keeps `note`, `warning` and `notes` (`cli/knowledge.cpp:151-172`).
      - `embed ingest`, `graph build`, `graph update` and `graph communities` refuse `--output-format json` and `stream-json` with exit 109 (the investigator, 8 runs).
      - None of these verbs opens a job stream.
    - **The Mac app.**
      - The Records toast for `knowledge reindex` over a mixed collection changes from the Note to `Reindexed N record(s) …` (`c-k3`).
      - A nothing-to-extract rebuild whose embedding fails loses its `Warning:` toast (by code).
      - The New Graph sheet's toast in the `c-n1` case loses only its `[graph] ` tag.
      - No other moved line sits where a toast is read. Collections toasts `notices.first`, which is always `N file(s)…` (`CollectionsViewModel.swift:233`), and agents delete toasts its first line.
      - Once APP-M22 ships, every moved line from Collections, Records and Graph reaches that place's card, which is why this item waits on it. Library runs `agents delete` as a command and toasts its first stdout line, and APP-M22 gives Library no card, so that note stays unseen in the app, as today's second stdout line is.
      - The design prototype and the mod have no parser for any moved line (grep, 2026-11-26).
    - **The full-screen shell** shows both streams (`cli/tui_child.h:50-55`). It drains stderr before it peeks at stdout (`cli/tui_child.cpp:106-130`), so a note said after the outcome may be drawn above it (by code).
    - **CLI-59d** (pending). Its text relay turns a job's stdout lines into `notice` events and ends the stream with a `stderr_tail` (its Open questions: which verbs run as a job). Its short-command limits give a 64 KiB stderr tail, and the job row names no other size, so this item reads a job's tail as 64 KiB too. After this item, a note at the head of a long build can fall out of that tail. `b1` measured 39,576 bytes of progress in 700 lines, about 56 B per chunk. A first-line note therefore leaves the tail after roughly 1,150 chunks: extrapolated, not measured, and sooner with longer file names.
    - **Not involved:** the HTTP plane (CLI-M59), machine-mode sessions and the MCP tools.
    - **Docs.** MILESTONES 27k and 57s name today's placement. No reference doc pins the stream (grep, 2026-11-26).
    - **Platforms.** No platform branch writes these lines. Only macOS was run.
19. **What already works**, and must stay:
    - The registration and validation notes already use the house voice on stderr with exit 0 (Evidence 14). `config add-graph` does the same (`c-cag`).
    - `knowledge capture --json` prints one document carrying `notes`, and says each note on stderr in the house voice (`cli/knowledge.cpp:532-537`).
    - Progress narration is already on stderr (`[graph] extracting`, `embedding`, `parsing`, `summarising`: `cli/graph.cpp:685`, `:692`, `:841`, `:1687`), and `-q` silences only progress.
    - Order on a merged pipe holds (Evidence 16).
    - The HTTP twins of the capture and reindex notes already carry them as `note` (`httpserver/admin_knowledge.cpp:69-84`, `:600-606`).
    - `BusyLine::above` already moves the busy line out of the way of a line meant to stay, and flushes stdout (`views/status_line.h:214-217`).

## Acceptance criteria

- [ ] **Given** a named code graph with one source tree removed, **when** `graph update <g> -q` or `graph build <g> -q` runs, **then** it exits 0, stderr is exactly `apogee graph: source tree <dir> is not a directory -- it contributes nothing, and what it contributed is forgotten.`, and stdout holds no `source tree` line (today: stdout `[graph] source tree …` and stderr empty, `c-g1` and `c-g2`).
- [ ] **Given** that graph, **when** `graph build --source <new> --graph <g> -q` runs, **then** the gone tree is said exactly once, on stderr, and not also as `apogee graph: warning: source tree '<dir>' is not a directory` (today twice, `c-g3`).
- [ ] **Given** a named graph whose member `ghost` has no database and no config entry, **when** `graph build <g> --source <dir> -q` runs, **then** stderr has exactly one line about `ghost`, `apogee graph: member collection "ghost" has no database -- it contributes nothing this build.`, and stdout's last line is `No member collection of "<g>" has any data yet -- only its code was built.` (today a stderr warning plus a stdout `[graph]` line, `c-n1`).
- [ ] **Given** an empty source tree, **when** `graph build --source <dir> --graph <g>` builds it, **then** stderr is `apogee graph: source tree <dir> offers no file to read -- inside a git repository, only what git tracks or does not ignore is read.` and stdout has no such line (today on stdout, `g4`).
- [ ] **Given** no embedder, **when** `graph build notes -q`, `graph build <named>` or `embed ingest <c> <dir> --graph -q` runs, **then** stderr holds `apogee graph: entity vectors skipped for "<name>" -- backend '<b>' cannot embed; entity search will be full-text only.`, and stdout holds no `entity vectors skipped` line (today on stdout: its first line in `c-g5` and `c-f1`, after the member line in `g6`, after the ingest's lines in `e3`).
- [ ] **Given** no embedder, **when** `graph communities notes --min-size 2 -m summarizer` runs, **then** `apogee graph: summary vectors skipped for "notes" -- …` is on stderr, not stdout (today on stdout, `g7`).
- [ ] **Given** an extractor that never conforms on one chunk, **when** a real `graph build` runs, **then** stderr holds `apogee graph: <source> [chunk N] FAILED: <error>`, stdout has no `FAILED` and no blank line for it, and the summary still says `(1 failed after retry -- re-run to retry them)`. With `--dry-run`, the FAILED line stays on stdout as today (today on stdout in both, `c-f1`).
- [ ] **Given** `cli/graph.cpp`'s two embedding-stopped-early warnings (`:305-308`, `:1728-1731`), **when** the source check runs, **then** neither is a `std::cout` write. Both go through `say_note("graph", …)` as `warning: entity embedding stopped early (…) -- …` and `warning: summary embedding stopped early (…) -- …`. No CLI test can make the mock embedder fail, so this one is held by the check, not by a run.
- [ ] **Given** a collection with vectors from `mock-embed-a` and the default switched to `mock-embed-b`, **when** `embed ingest <c> <one file> --retriever vector -q` runs, **then** stdout is `1 file(s), 1 chunk(s), 1 vector(s) [mock-embed-b]` alone, and stderr is `apogee embed: warning: 2 source(s) keep vectors from 'mock-embed-a' -- the collection searches lexically until every source is re-ingested with 'mock-embed-b'` (today both on stdout, `c-e2`).
- [ ] **Given** a mock embedder whose script carries `"metered": true`, **when** `embed ingest` or `knowledge capture` runs with no `--retriever` and no pin, **then** `apogee embed: indexed for text search only: …` or `apogee knowledge: indexed for text search only: …` is on stderr and not stdout. `knowledge capture --json` still prints one document carrying `note`, and `--dry-run` keeps the note in its stdout prediction (today on stdout, by code).
- [ ] **Given** a knowledge collection with one vector record and one lexical record, **when** `knowledge reindex` runs, **then** stdout is `Reindexed 2 record(s) in "knowledge" [mock-embed-a]` alone, and stderr is one line, `apogee knowledge: note: 1 of 2 record(s) in "knowledge" have no vectors (captured lexically) -- reindexing embeds every record, including those; pass an id to reindex just one.` (today stdout's lines 1 and 2, `c-k3`).
- [ ] **Given** an agent whose prompt file cannot be removed, **when** `agents delete <a> --purge` runs, **then** it exits 0, stdout has only the `removed …` lines, and stderr is `apogee agents: could not remove <file>: <reason>` (today on stdout, `c-ag2`).
- [ ] **Given** `-q`, **when** any run above says a note, **then** the note is still on stderr.
- [ ] **Given** stdout and stderr on one pipe, **when** a note is said after the outcome line, **then** it follows the outcome in the merged output (the `c-o1` precedent).
- [ ] **Given** a broken `credentials.json`, **when** `auth list` and `auth list --output-format json` run, **then** both outputs are byte-identical to today (`c-a1`, `c-a2`).
- [ ] **Given** a two-model store, a graph whose dedupe changes community memberships, and a graph with a chunk that fails, **when** `embed query`, `graph dedupe` and `graph build --dry-run` run, **then** their notes, advice and preview stay on stdout as today.
- [ ] **Given** a planted `std::cout << "[graph] x\n"` or `std::cout << "warning: x\n"` in `presentation/cli` outside the allow-list, **when** the source check runs, **then** it fails, naming the file and line.

## Constraints

- **Words kept; only the stream and prefix change.** A note is one stderr line beginning `apogee <verb>: `. That is the shape APP-M22's jobs core keeps as a note, and the shape CLI-M46's constraint promises for its own notes. The `[graph] ` tag is dropped, `Warning:` and `Note:` are lower-cased, and a two-line note is joined into one with ` -- `.
- **Exit codes unchanged.** Every note stays non-fatal, and every run that exits 0 today still does.
- **A refusal stays the last stderr line.** A refusal ends the run (each of the four files' `fail` helpers is `[[noreturn]]`, as at `cli/graph.cpp:48` and `:53`), so every moved note is said before it. A failing run's last stderr line is still its refusal, which APP-M22's failure path and CLI-M46's refusals rely on.
- **No JSON or stream face changes** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)): `knowledge capture --json`, `auth list`'s JSON and the HTTP envelopes keep their fields and meanings.
- **Stdout keeps the result.** A read's qualifier, a listing, a dry run's prediction, next-step advice and an outcome stay on stdout.
- **Never silenced by `--quiet`**, which means "no progress".
- **A note said while a busy line is live goes through `BusyLine::above`** (`views/status_line.h:214-217`), as the FAILED line already does.
- **Dated revisions for every record that names today's placement** (ADR 0006: "Behavior contracts named in records … hold until a dated revision says otherwise"): MILESTONES 27k and 57s, and the tests that pin them.
- **Presentation only** ([ADR 0001](../../../adrs/cli/layer-enforcement.md)). The facts are already data, so no Business change. The one Data change is a test seam that mirrors the generator mock.
- **Mode parity** ([ADR 0002](../../../adrs/cli/mode-parity.md)). The HTTP twins already carry the capture and reindex notes as fields. The HTTP graph jobs' gap is CLI-M59's. Machine mode is untouched.
- **One prefix literal.** Every moved site uses `say_note`, and no site writes its own `apogee <verb>: ` copy.
- **Hermetic tests only**: mock backends, no real model, no network. The removal-failure test is POSIX-only and skips as root.
- **Code style** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md#code-style)): a `.h`/`.cpp` pair, `std::string_view` for non-owning text, Homebrew LLVM's clang-format (v22), and every CI compiler builds without a warning of ours, with the MinGW scan of the changed files.
- **The dev channel** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel)). Probes run in a scratch `APOGEE_HOME`, never `~/.apogee` or `~/.apogee-dev`, and a live pass names `apogee-dev`.

## Technical approach

Paths under `lib/src/cli/`.

| File | Change |
|---|---|
| `source/presentation/cli/notes.h`, `.cpp` (new) | `void say_note(std::string_view verb, std::string_view text)` writes exactly one line, `apogee <verb>: <text>\n`, to `std::cerr`; the tie to `std::cout` keeps the order. It takes no quiet flag and has no BusyLine or JobStream branch. `std::string house_voice(std::string_view verb)` returns the prefix, the one place the literal lives. The doc comment names the rule and its precedents (`cli/embed.cpp:454`, `cli/graph.cpp:988`, `cli/knowledge.cpp:556`, `cli/tui_child.cpp:106-109`). This is not `business/tools/notes.h`, the notes tool. |
| `source/presentation/operations/graph_notes.h`, `.cpp` (new, unless CLI-M59 landed first) | The graph sentences shared with CLI-M59's HTTP jobs, in one place: the entity-vector skip, the summary-vector skip, a member without a database, and only the code built. If CLI-M59 landed first, this item calls its producer and adds nothing here. |
| `source/presentation/CMakeLists.txt` | The new files are listed: `cli/notes` beside `cli/job_stream.h`/`.cpp` (`:215-216`), and, when this item adds it, `operations/graph_notes` beside `operations/graph_members.h`/`.cpp` (`:88-89`). |
| `source/presentation/cli/graph.cpp` | Nine sites move onto `say_note("graph", …)`: `:305-308`, `:704-711` (inside `busy.above`, real build only), `:746-750`, `:751-755`, `:954-957`, `:1049-1054`, `:1069-1072`, `:1663-1666` and `:1728-1731`. `:1056-1059` stays on stdout, its `[graph] ` tag dropped and its first word capitalised. `:916-918`, the print of `validation.warnings`, is removed, while the error check at `:913-915` stays. |
| `source/presentation/cli/embed.cpp` | `decision.note` (`:418-420`) and `binding.warning` (`:421-423`) move onto `say_note("embed", …)`. The outcome line, the listings and the registration stay. The stale comment at `:478-479` ("Ingest's -m stays the embedding override") is corrected, since `embed ingest` registers no `-m`. |
| `source/presentation/cli/knowledge.cpp` | The real capture's `decision.note` (`:548-550`) and reindex's mixed-collection note (`:897-902`, joined into one line) move onto `say_note("knowledge", …)`. The dry run (`:517-523`) and the `--json` envelope are unchanged. |
| `source/presentation/cli/agents_cmd.cpp` | `(could not remove <file>: <reason>)` (`:325-326`) becomes `say_note("agents", "could not remove <file>: <reason>")`. The exit stays 0. |
| `source/data/backends/factory.cpp` | A mock embedder (`embedding_model` set) whose `model_path` names a script with a top-level `"metered": true` calls `set_metered(true)`, through `load_mock_script_metered` as the generator mock does at `:133`. A missing file reads as unmetered. This is a test seam only, the purpose `mock.h:245-246` states. |
| `tests/presentation/cli/graph_test.cpp` | `:166`, `:295`, `:391` and `:514` assert the house line on `err` and its absence on `out`. New cases are listed under Testing. |
| `tests/presentation/cli/knowledge_test.cpp` | `:787` asserts the joined `apogee knowledge: note: 2 of 3 record(s) …` line on `err`, with `out` holding `Reindexed 3 record(s) …` alone. `:797`'s negative check reads `err`, so the run at `:795` captures it. There is a new metered capture case. |
| `tests/presentation/cli/agents_test.cpp` | `agents delete --purge` with a 0555 `prompts/`. |
| `tests/data/backends/factory_test.cpp` | A mock embedder whose script says `"metered": true` is metered. Without the key, or with no file, it is not. |
| `tests/scripts/cmake/config_e2e.cmake` | `:506-507` matches `apogee embed: warning: 1 source\(s\) keep vectors from 'mock-embed-a' -- [^\n]* with 'mock-embed-b'` on `APOGEE_ERR`, and `APOGEE_OUT` does not match `warning:`. The negative halves of `:530` and `:546` read `APOGEE_ERR`. New runs: a metered mock embedder, and a merged-pipe run. |
| `tests/scripts/cmake/no_stdout_notes.cmake` (new), `tests/CMakeLists.txt` | The source check `cli.no_stdout_notes`, registered beside `cli.no_provider_probes` (`tests/CMakeLists.txt:707-711`). Under `presentation/cli` it allows no `std::cout << "[<tag>] ` write and no `"warning:` or `"Warning:` literal on `std::cout`, outside a named allow-list: `knowledge.cpp`'s dry-run `Warning:` (`:521`), and the models family's files (`models_pull.cpp`, `model_chain.cpp`, `models_migrate.cpp`) until CLI-M60 removes them. The tag is a lowercase word and `] ` (`[graph] `), so `graph_navigate.cpp:283`'s `[Knowledge graph: …]` header, a read's output, does not match. `auth list`'s kept `warning: ` is written to a string stream, not `std::cout` (`auth_cmd.cpp:101`), so a check keyed on `std::cout` passes it. A second registration runs the check over a fixture folder holding one planted violation, and passes only when the output names the planted file and line: a `PASS_REGULAR_EXPRESSION`, as `cli.help_lists_subcommands` has (`tests/CMakeLists.txt:517-519`). `WILL_FAIL` alone, as `cli.unknown_subcommand_fails` has (`:521-522`), would prove the failure but not the naming. |

**The moved lines, word for word.** The verb is the producer's: `graph` inside `embed ingest --graph`, as `cli/graph.cpp:988` already does.

| Site | Today, on stdout | After, one stderr line |
|---|---|---|
| `graph.cpp:746-750` | `[graph] source tree <dir> is not a directory -- it contributes nothing, and what it contributed is forgotten.` | `apogee graph: source tree <dir> is not a directory -- it contributes nothing, and what it contributed is forgotten.` |
| `graph.cpp:751-755` | `[graph] source tree <dir> offers no file to read -- …` | `apogee graph: source tree <dir> offers no file to read -- …` |
| `graph.cpp:954-957`, `:1069-1072` | `[graph] entity vectors skipped for "<g>" -- <note>; entity search will be full-text only.` | `apogee graph: entity vectors skipped for "<g>" -- <note>; entity search will be full-text only.` |
| `graph.cpp:1049-1054` | `[graph] member collection "<c>" has no database -- it contributes nothing this build.` | `apogee graph: member collection "<c>" has no database -- it contributes nothing this build.` |
| `graph.cpp:1663-1666` | `[graph] summary vectors skipped for "<g>" -- <note>; summaries will be full-text searchable only.` | `apogee graph: summary vectors skipped for "<g>" -- …` |
| `graph.cpp:704-711`, a real build | a blank line, then `<source> [chunk N] FAILED: <error, 200 characters>` | `apogee graph: <source> [chunk N] FAILED: <error, 200 characters>` |
| `graph.cpp:305-308` | `Warning: entity embedding stopped early (<error>) -- affected entities stay full-text searchable; re-run to embed them.` | `apogee graph: warning: entity embedding stopped early (<error>) -- …` |
| `graph.cpp:1728-1731` | `Warning: summary embedding stopped early (<error>) -- summaries stay full-text searchable; re-run to embed them.` | `apogee graph: warning: summary embedding stopped early (<error>) -- …` |
| `embed.cpp:418-420` | `indexed for text search only: '<model>' is a paid embedder, …` | `apogee embed: indexed for text search only: …` |
| `embed.cpp:421-423` | `warning: <n> source(s) keep vectors from <models> -- …` | `apogee embed: warning: <n> source(s) keep vectors from <models> -- …` |
| `knowledge.cpp:548-550` | `indexed for text search only: …` | `apogee knowledge: indexed for text search only: …` |
| `knowledge.cpp:897-902` | `Note: <n> of <m> record(s) in "<db>" have no vectors (captured lexically).` and `Reindexing embeds every record, including those -- pass an id to reindex just one.` | `apogee knowledge: note: <n> of <m> record(s) in "<db>" have no vectors (captured lexically) -- reindexing embeds every record, including those; pass an id to reindex just one.` |
| `agents_cmd.cpp:325-326` | `  (could not remove <file>: <reason>)` | `apogee agents: could not remove <file>: <reason>` |

**The writer's two branches it doesn't have.**
- **No BusyLine hook.** `BusyLine` has no accessor for the live line (`views/status_line.h:178-226`). The only note said while a busy line is live is the FAILED line, and it already runs inside `busy.above(...)` (`cli/graph.cpp:706`). The other notes print before the busy line is built (`:954-957` before `:962`) or after it is gone (the summaries, `cli/embed.cpp:418-423`).
- **No JobStream branch.** None of these verbs opens a job stream (`cli/job_stream.h:18-19` names the ones that do). CLI-M60 adds that branch: while a stream is open, the note goes out as a `notice` instead of on stderr, as machine mode already says warnings (`machine-mode.md:174-177`).

**Said once.** `graph build` keeps `validate_named_graph`'s error check and stops printing its warnings, since the build that always follows says both kinds again (Evidence 3). `config add-graph` and `POST /v1/admin/graphs` keep their warnings: nothing after them re-says the fact.

**Decisions taken from other items.**
- APP-M22 owns how the app shows a note: on the place's card, with the toast unchanged.
- CLI-M59 needs the same graph sentences for its HTTP job results: the entity-vector skip, the summary-vector skip, a member without a database, and only the code built. Whichever of the two lands first puts them in one producer, `presentation/operations/graph_notes.h/.cpp` (beside `graph_members`), and the second reuses it, with no second copy spelled. If this item lands first, it adds that producer, and `graph.cpp`'s sites pass its sentences to `say_note`.
- CLI-M46 adds a graph-build note before extraction and keeps its notes in the house voice. Whichever of the two lands second routes M46's notes through `say_note`.

**Reference.**
- The house-voice precedents: `cli/embed.cpp:454` and `:467`, `cli/graph.cpp:988` and `:1000`, `cli/knowledge.cpp:181` and `:556`.
- The shell runner's stated rule, `cli/tui_child.cpp:106-109`.
- `BusyLine::above`, the stay-line seam.
- The generator mock's `"metered"` script key (`data/backends/factory.cpp:133`, `mock.cpp:199-206`), which the embedder seam mirrors.
- `cli.no_provider_probes` (`tests/scripts/cmake/no_provider_probes.cmake`), the shape of a narrow source check.
- `reset_test.cpp:514-540`, the POSIX-only, skip-as-root precedent for a removal that fails.

**Docs when it ships:**
- **CLAUDE.md.** A `### ⚠ A finished run's notes are said on stderr in the house voice` invariant, in four parts:
  - **Rule:** one writer; stdout is the result; a read's qualifier, a listing, a prediction, next-step advice and an outcome stay; a JSON face carries the note as a field; an open job stream gets it as a `notice` instead (CLI-M60).
  - **Why:** APP-M22, `machine-mode.md`'s rule 3 and `tui_child.cpp:106-109`.
  - **Enforcement:** the per-site tests and `cli.no_stdout_notes`.
  - **Code Style:** the writer's location goes in the shared-helper line (`:355`).
- **DEVELOPER.md.** The `cli/notes.h/.cpp` row in the `cli/` table, and `cli.no_stdout_notes` in the tests table.
- **MILESTONES.** Dated revisions of:
  - 27k's "The summary says … a tree that is gone or offers no file" (Milestone AE);
  - 57s's "warns" and its acceptance line (Milestone AT).

  A dated entry records the full-screen shell's order: a note said after the outcome may be drawn above it.
- **CLI-59d's document** already carries the Constraint and its Decision-log row (written 2026-11-26, when this item was specced): a text job's `apogee <verb>: ` stderr lines are relayed as `notice` events as they arrive, not only in the 64 KiB tail. If CLI-59d has shipped before this item, check that its relay does so, and say it in this item's MILESTONES entry.
- **APP-M22's record.** Its document already records the CLI side as its stdout question's answer (its Open questions and Decision log, 2026-11-26). If its MILESTONES entry, written when it shipped (it gates this item), still calls that question open, a dated line there closes it.

## Testing

Hermetic: mock backends, a scratch home, no real model and no network. Each fixture fails on today's binary unless it is marked as a guard (the before-state recorded 2026-11-26, Evidence 2–12).

- **The bug as a fixture.** The reproduction is replayed in-tree. For each moved site there are three assertions: the note is exactly one stderr line in the house voice, stdout lacks it, and the exit is 0.
  - **`graph_test.cpp`**, over its mock extractor:
    - a gone tree under `graph update` and under a plain `graph build` (exactly one stderr line);
    - `--source` with a gone tree (one line, not two);
    - a member without a database via `--source` (one line about it, and stdout's last line `No member collection of "<g>" …`);
    - an empty tree;
    - entity vectors skipped for a collection, a named graph and `embed ingest --graph`;
    - summary vectors skipped;
    - the existing `-m prose` clerk that never conforms (`:285-300`): FAILED on `err` for a real build, and on `out` under `--dry-run`;
    - `-q` still says every note.
  - **`config_e2e.cmake`.** The binding warning on `APOGEE_ERR`, and `embed ingest` under a metered mock embedder: `apogee embed: indexed for text search only: …` on stderr, with stdout only the outcome line.
  - **`knowledge_test.cpp`.** Reindex over a mixed collection (`:755-801`'s case), and a capture under the metered mock embedder: the note on `err`, with `--json` still one document carrying `note`.
  - **`agents_test.cpp`.** The 0555 `prompts/`, POSIX-only and skipped as root.
- **Order on one pipe.** In `config_e2e.cmake`, one `execute_process` names the same variable for `OUTPUT_VARIABLE` and `ERROR_VARIABLE`, which CMake's documentation says merges them in the order produced (not tried in this investigation). Over the partial re-ingest, the `1 vector(s) [mock-embed-b]` line comes before `apogee embed: warning: …`. If the merge proves unordered, the run goes through a shell's `2>&1`, as `code_graph_e2e.sh:63` does.
- **The seam.** In `factory_test.cpp`, a mock embedder is metered only when its script says so.
- **The shared words**, when this item adds `operations/graph_notes`: `tests/presentation/operations/graph_notes_test.cpp` (new, registered in `tests/CMakeLists.txt`) pins each sentence, the tests mirroring the layer ([ADR 0004](../../../adrs/cli/tests-mirror-architecture.md)).
- **Guards** (pass today and must keep passing):
  - `auth list` with a broken `credentials.json`: text and JSON byte-identical to `c-a1` and `c-a2`, the warning still on stdout and stderr empty;
  - `embed query`'s notes still on stdout (`config_e2e.cmake:517-526`);
  - `knowledge capture --dry-run`'s note still in its stdout prediction;
  - `graph dedupe`'s `Note:` still on stdout;
  - `config add-graph`'s `member collection '…' is not configured yet` warning still on stderr (`graph_test.cpp:334`), since only `graph build` stops printing the validation's warnings;
  - `code_graph_e2e.sh:63-64` unchanged.
- **The source check.** `cli.no_stdout_notes` passes on the tree, and its planted-violation registration passes only when the check names the planted file and line.
- **Gate:** `lib/scripts/cicd.sh --app cli --test`, clang-format, and the MinGW scan of the changed files.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Does the CLI move these notes to stderr, or does the app keep every stdout line (APP-M22's deferred choice)? | `[default]` | The CLI side: the house voice on stderr. The CLI's own rules already pick it (`machine-mode.md:224-226`, `job_stream.h:21-27`, `tui_child.cpp:106-109`). The double voice (`c-g3`, `c-n1`) can only be fixed here. Keeping every stdout line would put summaries on APP-M22's note card, against its own rule that the toast is the outcome and the card holds the notes, and would help neither the terminal nor CLI-59d. Recorded as a dated decision; APP-M22's row already records the same answer. |
| Which lines move and which stay? | `[default]` | A write or build run's note moves: a skip, a degraded mode, a non-fatal failure, or a consequence the result alone doesn't show. That is the 14 sites. These stay on stdout: a read's qualifier (`embed query`'s and `knowledge query`'s notes, `graph_navigate`, `auth list`); a listing or count; a dry run's prediction (`knowledge.cpp:517-523`, a graph dry run's FAILED); next-step advice (dedupe's `Note:`, `Run apogee graph stats`); and an outcome (`… only its code was built`, with its tag dropped). |
| What exactly does a moved line say? | `[default]` | `apogee <producer verb>: ` followed by today's words. The `[graph] ` tag is dropped, `Warning:` and `Note:` are lower-cased, and a two-line note is joined with ` -- `. A `warning:` or `note:` word is kept after the prefix, as in `apogee train: note: …`. The FAILED line keeps its `<source> [chunk N] FAILED: <error, 200 characters>` words. |
| How is the paid-embedder note tested at CLI level? | `[default]` | The factory honours `"metered": true` in a mock embedder's `model_path` script, as it already does for a generator mock (`factory.cpp:133`). This is the purpose `mock.h:245-246` states. |
| How is the rule enforced? | `[default]` | The per-site stdout/stderr tests are the guard. A narrow source check covers the `[tag] ` voice and the `warning:` literal on `std::cout`, with a named allow-list. A literal check can't see notes written from variables (`embed.cpp:419`, `:422`; `knowledge.cpp:549`), so it is a backstop only. |
| How do a text job's notes reach a remote device under CLI-59d? | `[default]` | CLI-59d's text relay also relays `apogee <verb>: ` stderr lines as `notice` events as they arrive. This needs no protocol change, and the stdout lines are already relayed. It is recorded in CLI-59d's document as a constraint (2026-11-26), because a 64 KiB tail loses a head note after roughly 1,150 chunks (extrapolated from `b1`). |
| How does the Mac app show a moved note? | `(consumed decision)` | [APP-M22](../../darwin/macos/maintenance/a-jobs-notes-reach-the-person.md): its jobs core keeps a successful run's `apogee <verb>: ` stderr lines, and Collections, Records and Graph show them on a card. The toast stays the outcome line. |
| What do the HTTP graph jobs say for the same facts? | `(consumed decision)` | [CLI-M59](the-http-graph-jobs-say-what-the-cli-says.md): an additive `warnings` field on the job results, in the CLI's words from one producer. |
| What does the writer do while a job stream is open? | `(consumed decision)` | [CLI-M60](the-models-verbs-say-their-notes-on-stderr.md): it says the note as a `notice` instead of on stderr. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-26 | Specced from the APP-M21 and APP-M22 probes' CLI asides, then a sandboxed investigation, two skeptics and a critic (Evidence). The aside is APP-M22's remainder on stdout, which its Open questions left to a follow-up. **The diagnosis**: a finished run's notes follow two recorded conventions, stdout ("the summary says it") and the house voice on stderr, and no written text-mode rule or shared writer chooses between them. So this item changes a recorded convention rather than fixing a regression. | the spec |
| 2026-11-26 | Placed in **Maintenance** as M58, by the user's call to spec the APP-M21/M22 CLI asides as maintenance items from M48, grouped only where they share one seam. This item's seam is the CLI's text-mode note stream: the Presentation layer's choice of stream for 14 statements in 4 files, and one writer. The HTTP graph jobs have a root cause of their own in `admin_graph.cpp`, so they are CLI-M59. The models family needs the writer's job-stream branch, so it is CLI-M60. | the user (placement) · the spec (the split) |
| 2026-11-26 | **Blocked by darwin M22.** Two moved notes are the app's toast today: `knowledge reindex`'s Note (Records toasts `notices.first`, `c-k3`) and, by code, a nothing-to-extract rebuild's embedding warning (Graph toasts `notices.last`). Only APP-M22's card shows a stderr note, so landing this item first would hide them in the app. | the spec |
| 2026-11-26 | **Blocks CLI-M60**, which extends this item's writer with its job-stream branch. | the spec |
| 2026-11-26 | **No gate with CLI-M46, CLI-M59 or CLI-M45.** This item and CLI-M46 are built one after the other, since both edit `embed.cpp`'s ingest tail and `graph.cpp`'s `build_collection`. Whichever lands second routes M46's notes through `say_note`. CLI-M59 shares the graph notes' words: whichever lands first makes the producer (`operations/graph_notes`), and the second reuses it. CLI-M45 shares files with this item, not the seam. | the spec |
| 2026-11-26 | **A recorded placement reversed, by dated revision** (ADR 0006). This reverses 27k's "The summary says … a tree that is gone or offers no file" (Milestone AE) and 57s's "warns `1 source(s) keep vectors …`" (Milestone AT), each pinned by a test (`graph_test.cpp:166`, `config_e2e.cmake:506-507`). The revisions and the moved tests ship with the item. | the spec |
| 2026-11-26 | **"Said once" by a removal, not a new warning kind.** `graph build` stops printing `validate_named_graph`'s warnings, because the build that always follows says both kinds again. The reverse (keep the validation's line and quiet the build) can't work, because `graph update` never validates. | the spec |

## Out of scope

- **The HTTP graph jobs' missing facts and the mixed-graph 404**: [CLI-M59](the-http-graph-jobs-say-what-the-cli-says.md).
- **The models family** (`models pull`, `convert`, `quantize`, `migrate`, the chain summary) **and the writer's job-stream branch**: [CLI-M60](the-models-verbs-say-their-notes-on-stderr.md).
- **The app's card** for these notes, and every other done path in the app: [APP-M22](../../darwin/macos/maintenance/a-jobs-notes-reach-the-person.md) and its follow-up. Re-recording APP-M22's `graph-update-source-gone` guard fixture as a note is the app queue's chore; its rule needs no code change.
- **Notes already on stderr but not in the house voice**, which APP-M22's rule won't keep. Their stream is right; their voice is a separate seam, not in this item:
  - `analyze.cpp:763`, `[apogee] warning: could not save the report -- `;
  - `review_cmd.cpp:81`, `[review] skipped: `;
  - `task_cmd.cpp:454`, `[recipe] skipped: `;
  - `train.cpp:3444`, `[regime] skipped: `;
  - `complete.cpp:792`, `[apogee] skipping <server>: `;
  - `reset.cpp:225` and `uninstall.cpp:289`, a bare `could not remove `;
  - `complete.cpp:331`, the verb-less `apogee: <note>`.
- **Giving `embed ingest` or `graph build`/`update`/`communities` an `--output-format json` or `stream-json` face.** CLI-59d records that as their own work.
- **Chat's in-session capture dropping the paid-embedder note** (`chat_session.cpp:2261-2285` prints `result.notes` and never `result.decision.note`). It is an ADR 0002 parity gap of its own, and can't be driven with mocks until this item's seam lands.
- **A failing mock embedder.** The embedding-stopped-early warnings stay without an end-to-end test (Evidence 8); only the source check holds their stream.
- **The full-screen shell's order** for a note said after the outcome (`tui_child.cpp:106-130`) is accepted, because two pipes keep no order. MILESTONES records it.
- **The relay itself.** How CLI-59d relays these lines is CLI-59d's design; its document carries the constraint.
- **Collection names refused before the write**: [CLI-M46](a-collection-the-config-cannot-hold-is-refused-before-it-is-written.md).

## Links

- **Blocked by:** [darwin M22](../../darwin/macos/maintenance/a-jobs-notes-reach-the-person.md) · **Blocks:** [CLI-M60](the-models-verbs-say-their-notes-on-stderr.md)
- **Sibling:**
  - [CLI-M59](the-http-graph-jobs-say-what-the-cli-says.md), the same seam's HTTP half;
  - [CLI-M60](the-models-verbs-say-their-notes-on-stderr.md), the models family on this item's writer;
  - CLI-M48 to CLI-M57, specced in the same pass from the same probes' asides (the remedy-names and made-thing-names seams).
- **Relates to:**
  - [CLI-M46](a-collection-the-config-cannot-hold-is-refused-before-it-is-written.md), built one after the other with this item: its notes keep the house voice, and they are routed through `say_note` by whichever lands second;
  - [CLI-M45](a-collection-is-named-not-pathed-at-every-reader.md), which shares `embed.cpp` and `graph.cpp` with no gate;
  - [APP-M22](../../darwin/macos/maintenance/a-jobs-notes-reach-the-person.md), whose deferred stdout question this item answers, and [APP-M21](../../darwin/macos/maintenance/a-listed-name-reaches-the-verb-as-listed.md), whose probes ran beside it;
  - [APP-7c](../../darwin/v0.2.4/every-place-against-a-server.md), which reads a remote job's notes;
  - CLI-59d (v0.2.3, [reads and commands over the network](../v0.2.3/reads-and-commands-over-the-network.md)), whose Constraints carry the relay rule (2026-11-26);
  - CLI-58b (v0.2.2, [graphs and knowledge served](../v0.2.2/mcp-serve-graphs-and-knowledge.md)), which adds `graph list` to `graph.cpp` and shares the file, not the seam.
- **Revises:** MILESTONES [27k](../../../assistant/cli/MILESTONES.md#milestone-ae--the-code-graph) ("The summary says … a tree that is gone or offers no file") and [57s](../../../assistant/cli/MILESTONES.md#milestone-at--the-second-sweeps-cli-findings) ("warns"), by dated revision under [ADR 0006](../../../adrs/cli/backwards-compatibility.md). It follows [ADR 0002](../../../adrs/cli/mode-parity.md)'s "Reads have twins": the HTTP twins already carry these facts as fields.
