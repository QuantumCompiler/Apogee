# Prose reads gain JSON: chat details, recall's summaries, dedupe, raw records, the footprint

**What / why.** The macOS app's 4i and 4k (shipped 2026-10-26) reached five reads that answer only in prose. The app runs them as commands and draws, or cuts, their text, which is the fallback ADR 0006 tolerates and the parity law dislikes. Each was reproduced again on `apogee 0.2.0 (4b828e91)` over sandbox homes on 2026-10-26:
1. **`chats info <id> --output-format json` exits 109** (`The following arguments were not expected`). The "is taken by" list includes `chats list` and `chats show`, not `info`. `presentation/cli/chat_history.cpp:354–362` registers no `add_read_format`.
2. **No verb lists recall's summaries.**
   - **The read gives a count:** `chats recall` prints `recall holds 1 chat summary in …/memory/chats.db`, and with JSON it exits 109. The summaries exist only in the index.
   - **No enumerator:** `business/agentloop/recall.h:73–100` has none, only `chats()`, a count (`recall.cpp:232–243`).
   - **No summary stored apart:** `put` (`recall.cpp:183–185`) stores the summary under a date-and-title prefix.

   52e's record says the read shows "the summaries", but it shows a count. Settings' Forget asks with the count alone, and the inspector's Recall section shows the printed line.
3. **`graph dedupe [--dry-run] --output-format json` exits 109** (`presentation/cli/graph.cpp:1705–1771`). Its admin twin already builds the JSON (`httpserver/admin_graph.cpp:907–982`).
4. **`knowledge info <id> --raw` with JSON silently drops `--raw`.** With `--output-format json` (or `--json`) it exits 0 with the record alone, while text mode ends `-- Raw conversation --` and the conversation. `presentation/cli/knowledge.cpp:716–723` returns both JSON branches before the raw block (`:725–732`).
5. **A suite's footprint is text inside a field.** `models status --suite shared --output-format json` gives `{"field":"footprint","value":"suite shared\nchatlocal (chat): 4640 MiB …\ntotal: … -- fits"}`, its indentation stripped (`presentation/cli/models.cpp:2540–2547`). The typed source exists (`data/modelstore/footprint.h:76–172`, `SuiteFootprint`). M25 recorded "the footprint has no JSON view" as not done, and the design board's G10 is narrowed to it.

**Core constraint(s).**
- **The protocol grows additively** (ADR 0006).
  - **New readers** (`chats info`, `chats recall`, `graph dedupe`) are named in `machine-mode.md`, or `cli.machine_schema_conformance` fails (`schema_conformance.py:123–142`).
  - **New fields on existing documents** (`raw`, a typed `footprint`) sit beside what is there. The footprint's `fields[].value` text stays, since the app's 4i reads it.
- **Text mode is unchanged.** Every text answer keeps its words, including the `-- Raw conversation --` marker the app cuts at.
- **One serializer per document.** `graph dedupe`'s JSON is the admin twin's, from one `operations/` function both call, as 55h made `train_reads`.
- **Recall stays off `serve`.** `harness.layering` refuses any include from `httpserver/` or `operations/` into recall (26l). The listing is the CLI's alone.
- **An older recall index still reads.** A summary stored before this change is read from its prefix compatibly; nothing is migrated in place.

**Seam + files.**
- **`presentation/cli/chat_history.cpp`:**
  - `chats info` with `add_read_format`, as `chats list` (`:330–352`) and `chats show` (`:364–392`) use it (`cli/helpers.h:220`);
  - `chats recall`'s read branch (`:460–472`) with JSON listing each summary: chat id, name, date, text.
- **`business/agentloop/recall.{h,cpp}`:** an enumerator, and the summary stored in its own metadata field, older entries read from the prefix.
- **`operations/`:** a dedupe serializer shared by `presentation/cli/graph.cpp:1705–1771` and `httpserver/admin_graph.cpp:907–982`.
- **`presentation/cli/knowledge.cpp:716–732`:** a `raw` field under `--raw` in both JSON branches.
- **`presentation/cli/models.cpp:2540–2547`:** a typed `footprint` object beside `fields` (members with their bytes and whether known, the total, the budget and the verdict word), serialized beside `footprint_lines` in `cli/suite_residency`.
- **`lib/documentation/cli/reference/machine-mode.md`:** the new readers and fields.

**Reference.**
- 52e's typed `data` beside `models info`'s fields (`models.cpp:2224–2241`): fields kept, types beside them.
- 55h's `train_reads`: the CLI's JSON and the admin body from one serializer.
- The `memory` event's `sessions`, which read the same recall metadata (`chat_recall.cpp:193–206`).

**Decisions made** (dated):
- 2026-10-26 — **Opened from the macOS app's 4i and 4k records** (the user's call: "spec them into the backlog"; placed in CLI Maintenance the same day).

**Open calls:**
- [default: `chats recall --output-format json` lists `{data:[{chat, name, updated, summary}], object:"list"}`, and the text answer keeps its count line, then lists the summaries' names.]
- [default: the typed footprint is an object `footprint: {members:[{backend, role, bytes, known}], total_bytes, budget_bytes, verdict}` beside `fields`, with `verdict` one of the CLI's words (`fits`, `fits as far as known`, `does not fit`, `budget not known here`, `no member holds memory`).]

**Guardrail(s).**
- **Each new reader's JSON** pinned beside its text twin:
  - `read_documents_test.cpp` (its refusal case at `:241` inverted) and `chat_history_test.cpp`;
  - `graph_test.cpp:530` and `admin_graph_test.cpp` (the CLI equals the admin body);
  - `knowledge_test.cpp:601–640`;
  - `suite_residency_test.cpp`, `footprint_test.cpp` and `models_test.cpp:1280`.
- **Conformance's doc lines,** `cli.machine_schema_conformance`.
- **Recall:**
  - the listing over an index written before the change and one after (`chat_test.cpp`'s `[recall]` cases, `tests/business/agentloop/`);
  - `harness.layering` green.
- **Each verb's completion offers `--output-format`** where it is new (ADR 0007).

**Acceptance criteria:**
- [ ] `chats info`, `chats recall` and `graph dedupe` answer `--output-format json`, and recall's answer lists the summaries themselves.
- [ ] `knowledge info --raw` in JSON carries the raw conversation, and text mode is unchanged.
- [ ] `models status --suite`'s JSON carries a typed footprint beside the unchanged field.
- [ ] `lib/scripts/cicd.sh --test` passes with zero warnings, and the changed files pass the MinGW scan.

**Scope note.** CLI Maintenance, claimable by name, no gate. The macOS app reads these as prose today (4i's footprint verdict, 4k's details, recall, dedupe and raw view); adopting the JSON is a later GUI item, not gated.
