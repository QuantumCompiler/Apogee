# CLI-M61 · Every printed command goes through the renderer

| Field | Value |
|---|---|
| **Type** | 🐞 Bug |
| **Project** | CLI |
| **Status** | 🔒 Blocked |
| **Fix version** | Maintenance (any time) |
| **Component** | Remedies › every printed command CLI-M48 leaves: the backend, suite, agent and MCP holes only a hand edit reaches, model refs and handles, run and task ids, `train`, `models`, the video runtime, the TUI's agent and rollback confirms and the rest of the sweep; `cli.one_suggested_command`; CLAUDE.md's invariant |
| **Labels** | `remedies` `shell-quoting` `source-check` |
| **Epic** | — (maintenance has no track) |
| **Blocked by** | [M48](a-suggested-command-runs-as-printed-for-every-name-the-cli-makes.md) |
| **Blocks** | — |
| **Reporter** | The APP-M21 probes' CLI asides (2026-11-26), then a sandboxed investigation, two skeptics and a critic the same day; groomed on 2026-11-27 as Pass B of the split-first item, by the group run |
| **Created** | 2026-11-26 |

---

## Summary

[CLI-M48](a-suggested-command-runs-as-printed-for-every-name-the-cli-makes.md) adds one renderer for the commands Apogee prints, folds the five private quoters into it, and routes through it every site a name reaches with no hand edit, plus the backend doors. The rest of the sweep's about 226 name holes still join a name in raw: the kinds a hand edit or an older config reaches (backends, suites, agents, MCP servers), model refs and handles, run and task ids, and a few holes that are not names. This item, Pass B of the split, moves every remaining site onto the renderer with no other behaviour change. It adds `cli.one_suggested_command`, a source check that ships with an empty allowlist, so a raw join or a sixth quoter can't start again.

## Description

### User story

**As** someone who copies a command Apogee prints, whatever kind of thing it names (a backend, suite, agent or MCP server I wrote by hand, a model, a run),
**I want** every such command built by the one renderer, and kept that way,
**so that** following Apogee's advice never builds or deletes the wrong thing, never prints help and exits 0, and never fails to parse.

### Steps to reproduce

This item shares CLI-M48's reproduction: [its Steps to reproduce](a-suggested-command-runs-as-printed-for-every-name-the-cli-makes.md#steps-to-reproduce), with the same sandbox, binary and joins. Its own kinds were not reproduced one by one. A hand edit, or a config written before 55f's add rule (2026-10-25), is the only way to reach them (Evidence 15). Of their remedies, only graph (`c:e04`) and backend (`c:e05`) were measured to the editor's refusal; suites, agents and MCP servers stopped earlier (`c:e01`–`c:e03`). Run ids and task ids are never odd. The nearest steps there are 6 and 7, a hand-edited backend reaching a remedy.

### Actual result

At HEAD 5b8927c9, every site this item moves joins its name or value in raw, with no `--` and no quoting (Evidence 13): zero sites quote a name or put `--` before one. CLI-M48 measured what that join does at its own sites: a refusal (106, 109, 114), help with exit 0, a wrong target, `/bin/ls` read in zsh, and a shell syntax error (Evidence 15). Nothing holds the seam: a new printed command can still be joined in raw, and a sixth quoter can still start.

### Expected result

Every printed command at this item's sites, pasted whole into sh, bash or zsh, and read by the CLI's own splitter, runs the named verb on the named thing, as CLI-M48's Expected result says for its own sites. A plain name prints exactly the bytes it prints today. A planted raw join, `?:` operand or `<<` chain outside the renderer fails `cli.one_suggested_command`, naming the line.

### Background

A remedy is a command Apogee prints for a person to copy: a `run:` line after a refusal, a `check` row's doors, a hint after a success. The project promises they work:
- `reference/cli/training.md:135-136`: "each a command that runs as printed".
- MILESTONES' M14 (2026-10-19, `MILESTONES.md:3948`): "The doctor's doors run as printed."
- 53c's fix (2026-10-31, `MILESTONES.md:128`) treated a door that kept a Windows path's backslashes unquoted as a bug.
- `check.cpp:1033` calls its quoter "one word of a command a person copies".

Every site builds its command by joining strings: `"apogee graph build " + name`. Nothing on that path knows three things:
- CLI11 2.4.2 reads a dash-led positional as an option, eats a `--` that follows an option with no limit on its values, and reads anything after `--` as an operand.
- zsh, this host's shell, expands a leading `=` into a command's path.
- POSIX quoting.

Names reach that gap without any hand edit:
- `embed ingest` creates a collection under any non-empty name that holds no `..`, `/` or `\` (`embed.cpp:45-54`).
- `datasets create` admits a leading `-` (`business/training/datasets.cpp:42-49`).
- `symphonies create` admits one once a `symphonies:` section exists (`config.cpp:1453-1464`, `config_edit.cpp:1528-1545`).
- Every path under a home whose folder holds a space or a quote, such as Windows' `C:\Users\First Last`.

A hand-edited config, or one written before 55f's add rule (2026-10-25), reaches the rest.

The APP-M21 probes' CLI asides named the first symptom: remedies that print a dash name without `--` (`graph.cpp:1535`, `:1638`, and `models delete` suggesting `config delete-backend -x`). The investigation found the same seam under names with `;`, `$`, a backtick, a quote or a space, under zsh's `=`, and under home paths. It also found three changes beside it that are not rendering, now specced apart:
- the program word and root a remedy names: CLI-M49;
- the rename-backend door for a backend name the editors refuse: CLI-M50;
- the TUI's own argv: CLI-M51.

The design is one pure renderer in `source/data/contracts/`, the lowest layer every printing site may include. CLI-M48 builds it:
- **Order.** It keeps today's order when nothing needs it changed, so a plain name prints today's bytes. Only for a dash-led operand, or an option with no limit sitting directly before an operand, does it move the options first and add `--`.
- **Words.** A word stays bare when only safe characters make it up. Otherwise it gets 53c's double quotes where they read back whole, and single quotes everywhere else.
- **Folds.** The five quoters fold into it.

This item finishes the job:
- **Sites.** Every remaining site goes through the renderer, with no other behaviour change.
- **A source check.** It keeps a sixth quoter from starting, as `cli.one_key_resolver` does for secrets, and a new raw join with it.

The item was specced whole and marked split first: about 190 sites, six chmod rows, five quoters to fold, three test layers and a source check are more than one focused session. On 2026-11-27 it was groomed into two documents (Decision log):
- **[CLI-M48](a-suggested-command-runs-as-printed-for-every-name-the-cli-makes.md), Pass A**, keeps the id M48. It takes the renderer, the folds, the tests that pin it, and every site a name reaches with no hand edit: collections and graphs, symphonies, datasets with a leading `-`, and path values under the home. It also takes the backend doors, where a wrong target is measured (`m09`).
- **This one, Pass B**, takes the remaining sites, `cli.one_suggested_command` and the CLAUDE.md invariant. It is gated on CLI-M48.

### Evidence

All measured 2026-11-26 by an investigator, two skeptics and a critic, each in its own sandbox, with the built binary `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)` (Evidence 1): `env -i` with PATH, `HOME` and `APOGEE_HOME` three levels deep, stdin from `/dev/null`, mock backends only. Labels `c:…` are the critic's runs. Unprefixed labels are the investigator's, except `n01`–`n03` (the cause skeptic's) and `y05`–`y07` (the fix skeptic's). Source lines are at HEAD 5b8927c9.

The numbers are CLI-M48's, so a number names the same finding in both documents. The findings that bear only on CLI-M48's sites are cited there by number: [CLI-M48's Evidence](a-suggested-command-runs-as-printed-for-every-name-the-cli-makes.md#evidence).

1. **The binary.** `apogee version` (`c:v00`) prints `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)` and `channel: release`. The stamp names HEAD~1, but the binary holds HEAD 5b8927c9's source strings: `strings | grep -c "no pipeline run 'train pipeline resume' takes"` gives 1, and `git log -S` finds that string only in 5b8927c9. The stamp under-reports.
2. **A dash-led collection's remedy is refused** (`graph.cpp:1535-1536`, `:1637-1640`, and the stdout hints after a success): CLI-M48's Evidence 2 (`p02`–`p05`, `r2`, `r3`, `c:k03`–`c:k06`, `st3`, `n03`).
3. **zsh expands a leading `=`**: CLI-M48's Evidence 3 (`c:z01`–`c:z08`).
4. **A `;` name runs a second command, against another target**: CLI-M48's Evidence 4 (`r5`, and `m09`–`m12`, hand-written backends through `models delete`'s remedy, which is a CLI-M48 site). This item's sites join their names the same way (Evidence 13).
5. **A home path with a space or a quote**: CLI-M48's Evidence 5 (`c:c01`–`c:c03`, `y05`–`y07`, and the models and datasets path hints).
6. **Symphonies, once a section exists**: CLI-M48's Evidence 6 (`c:y01`–`c:y08`).
7. **A remedy with an option after the name can't simply gain a `--`** (`check.cpp:539-544`, since 00635d8a, 2026-10-19).
   - `ck1` and `j04`: the text row and the JSON `remedy` both read `apogee config set-backend -gone --model-path <an existing .gguf> to repoint it, or apogee config delete-backend -gone to remove it`.
   - `o1`, as printed: exit 106. `o2`, `set-backend -- -gone --model-path m.gguf`: exit 109, `The following arguments were not expected: …/m.gguf --model-path --`. `o3`, the options first, then `--`, then the name: reaches the verb.
   - The sweep counts 69 positional holes with an option after them.
8. **CLI11 2.4.2's rules, as measured** (`Version.hpp:16`; `App_inl.hpp:1975-1999`, where only required positionals hold back extra values, then `// Allow -- to end an unlimited list and "eat" it`).
   - `c:u01`: `graph build --source src g9` exits 1 with `which graph? …`, the operand eaten by `--source`. `c:u02`: `graph build g8 --source src` exits 0. `c:u03`: `graph build --source src -- g7` exits 0, `added graph 'g7'`.
   - `e1`: `graph build --source src -- -w` exits 109; `e8`: `-- -- -w` reaches the verb.
   - `c:k02`: on a verb with no such option, a second `--` is an operand. `graph show -- -- plain Fox` exits 109 with `The following arguments were not expected: Fox --`.
   - Option values parse whole, even `-h` and `--`. `c:o01`: `graph report --graph --` exits 0 with `# Graph report: --`. `v1`–`v3`: `--graph -x`, `--graph -h` and `--suite -x` reach the verb.
   - Today no remedy puts such an option directly before an operand: `graph.cpp:1013-1019` and `:1108-1112` print `--source <dir> --graph NAME`.
9. **Five private quoters disagree, and none is right for every word**: CLI-M48's Evidence 9. CLI-M48 folds all five, and the hand-typed `""`, into the renderer; this item's source check keeps a sixth from starting.
10. **The quoting table** (`c:rt`, `c:h`): CLI-M48's Evidence 10, the proof of the word rule.
11. **A name holding a newline** (`c:n01`, `c:n02`): CLI-M48's Evidence 11.
12. **The same text reaches other readers.**
    - **The HTTP admin graph bodies.** `http01` (`admin_graph_navigate.cpp:61-63`, from `navigate.cpp:564`'s `build_hint`, 9d8d3a64, 2026-10-05): `GET /v1/admin/graph/-y/neighbors?node=Fox` answers 404 `{"error":{"message":"no graph built for '-y' -- run: apogee graph build -y","type":"not_found_error"}}`.
    - **The graph tools.** `c:t03`: in machine mode, the `graph_query` call on unbuilt `-y` emits `{"id":"call-1","name":"graph_query","state":"failed","type":"tool_call"}` with no text, so the stream's event carries no remedy. `c:t04`, run as a task: the saved session's `.messages[2].content` holds `Error: no graph built for '-y' -- run: apogee graph build -y`. The remedy reaches the model as the tool result (`graph_nav.cpp:100-103`).
    - **The CLI's graph navigation verbs** (`graph_navigate.cpp:77-78`, `graph_artifacts.cpp:66-67`). `n01`, `n02`: `graph export html --graph -h -o out.html` and `graph report --graph -h` exit 1 with `no graph built for '-h' -- run: apogee graph build -h`.
    - **`knowledge query --graph` and its HTTP twin**, by reading: `query.cpp:163-164`'s record note, built at `knowledge.cpp:612` and `admin_knowledge.cpp:450-451`, and printed at `knowledge.cpp:629-630` and `:642-643`.
    - **check's JSON `remedy`** (`j04`). In JSON read mode, errors stay text on stderr (`j02`), so among the CLI's JSON read documents that field is the one that carries these commands.
    - **The TUI's confirm prompts**: `tui_agents.cpp:138-139`, `tui_training.cpp:188-191` and `:273-274`.
    - **The Mac app.** It shows check remedies as selectable text (`SettingsView.swift:279-280`). Its Console splits a pasted line with the CLI's quoting table and strips a leading `apogee` (`ConsoleViewModel.swift:141-155`, `:213`, `:273`).
    - **An agent's `run_command`**, which evals through `/bin/sh` (`business/tools/shell.cpp:32-34`), and through `cmd /C` on Windows (`:26`, `:161`). An agent relaying a remedy there needs a real model: not measured.
13. **Root cause: no function renders a printed command.**
    - **The sweep.** A walk of C++ string chains (`+` and `<<`) for `apogee <verb>` followed by a non-literal found 226 name holes at 186 sites in 42 files. It misses a literal inside a `?:` operand: `snapshot.cpp:510`, `models_pull.cpp:3400-3401` and `graph_report.cpp:220`, found by a second sweep. `mlx_local.cpp:474`, a drafter's path joined raw, is missing too (by reading, 2026-11-26). About five holes are not names: `models_pull.cpp:3277`'s suffixes, `check.cpp:2750`'s fragment, `mcp_cmd.cpp:125`'s `try_tool`. Six chmod rows in check sit outside it. The count is right to within about five, but it is not a lower bound.
    - **History.** `graph.cpp:1535` dates from 34ed0ce4 and `:1637` from a6e3fa54 (both 2026-09-19), `models_pull.cpp:869` from 2c50f86c (2026-10-03), `navigate.cpp:564` from 9d8d3a64 (2026-10-05), and `backend_doors` (`models_pull.cpp:416-419`) and `check.cpp:539-544` from 00635d8a (2026-10-19). Zero sites quote a name or put `--` before one.
14. **What the tests can and can't see today.**
    - `line_tokens.cpp:15-86` splits on blanks, removes quotes and backslash escapes, and does nothing else. A port maps `apogee graph build a;b` to `['apogee','graph','build','a;b']` and keeps `$HOME` literal. So a parse-back through `line_tokens` alone passes today for `;`, `$` and backtick names, and a replay must go through a real shell.
    - `tests/CMakeLists.txt:160` links `apogee_tests_data` to `apogee_data` and `apogee_infrastructure` only, and `line_tokens` lives in `presentation/cli`.
    - Pinned spellings:
      - `check_test.cpp:3089-3094` pins code-exec's single quotes: `--command '/opt/my python/bin/python3'` and `'/opt/it'\''s/python3'`.
      - Name-then-option order and `""` are pinned at `check_test.cpp:3426`, `train_test.cpp:313`, `adapters_test.cpp:161`, `llamacpp_adapters_test.cpp:249`, `mlx_local_test.cpp:1883` and `store_references_test.cpp:355`.
      - A regex finds at least 44 such literals in 14 files. That is a lower bound; a broader regex found 55 or more.
    - **By reading:** on Windows a temp path holds backslashes, and five pins join one raw where these sites print it bare today: `store_references_test.cpp:348`, `datasets_test.cpp:574`, `mlx_local_test.cpp:1828`, and `models_mlx_test.cpp:290` and `:465`. The pins 53c touched build the word with a helper, `check_test.cpp:73`'s `printed`.
15. **Blast radius.**
    - **Sites:**
      - about 226 name holes at about 189 sites in 42 files, plus six chmod rows in check;
      - per file, `check.cpp` 38, `models_pull.cpp` 31, `graph.cpp` 22, `train.cpp` 19, `models.cpp` 13, `business/video/runtime.cpp` 7, then 1 to 4 in each of 36 more;
      - by layer, Presentation about 160, Business about 18, Data about 10;
      - by kind (re-filed after a second reading, 3 of 11 spot-checked), model refs and handles about 60, backends about 60, collections and graphs about 43, run ids about 15, MCP servers 11, suites 10, symphonies 9, agents 9, task ids 5, datasets 4. The backends include 11 holes first filed as run ids (`train promote … --as`, `train rollback`).
    - **Reachable today with no hand edit:**
      - collections, under any non-empty name that holds no `..`, `/` or `\` (the holes for named graphs alone excepted, since those are refused at add);
      - symphonies, once a section exists;
      - datasets with a leading `-` (`c:t10`: `datasets create --from empty -- -x` exits 0);
      - every path value under a home that holds a space or a quote.
    - **Only through a hand edit or a config written before 2026-10-25:**
      - backends, graph entries, suites, agents and MCP servers;
      - of their remedies, only graph (`c:e04`) and backend (`c:e05`) were measured to the editor's refusal; suites, agents and MCP servers stopped earlier (`c:e01`–`c:e03`);
      - model handles with a dash-led owner need a hand-made store folder: not measured.
    - **Never odd:** run ids and task ids, generated as a prefix plus a UTC time.
    - **Surfaces:**
      - CLI stderr, and stdout hints on success;
      - check's text and its JSON `remedy`;
      - the HTTP admin graph and knowledge bodies;
      - the graph tools' results, to the model and in the saved session;
      - the TUI's confirm prompts;
      - the Mac app's Settings text and its Console.
    - **Harms:**
      - a refusal (106, 109, 114);
      - help printed with exit 0;
      - a wrong target (another collection built, another backend deleted);
      - `/bin/ls` read in zsh;
      - a shell syntax error.

    CLI-M46's create rule will stop new odd collection names, but stores already on disk keep this harm reachable (ADR 0006). So does every path under a home with a space, whatever names a person picks.
16. **What already works**, and must stay:
    - **`--` before a dash-led operand** reaches every verb probed (`q1`–`q7`, `o3`, `d1`–`d4`; `c:k06`, `c:y06`, `c:y08`, `c:t12`).
    - **Option values parse whole** (Evidence 8), so the 32 holes that are option values need only quoting.
    - **Names equal to a subcommand or to `help`** are read as names (`c:f01`–`c:f06`: `graph build stats` and `sh -c 'apogee graph build help'` both build).
    - **`line_tokens` and the Mac Console share one quoting table** (`line_tokens.h:7-14`; `ConsoleViewModel.swift:141-155`, GUI 6e). One rendering serves the POSIX shells, the TUI's exec line and the Console. The exec line keeps a leading `apogee` as a word (`c:w01`, exit 109), plain or not: an aside, not this item.
    - **A precedent.** `check_test.cpp`'s `doors_of` (`:3436-3459`) already replays a remedy's doors through `line_tokens` and `CliHome`, in "every door an adapter row names runs as printed, and the row then passes" (`:3507`).
    - **Display-only clients.** The Mac app, the design prototype and the mod only display remedies (`SettingsView.swift:279-280`; the mod's `render.ts:72-73`). Their recorded documents hold plain words only (the mod's `tests/support/recorded.ts`, the app's `PreviewDocuments.swift`), so a byte-identical plain rendering changes none of them.
    - **55f's add rule** keeps new odd names out of backends, graph entries, suites, agents and MCP servers (`kb1`–`kb5`, `gr1`).

## Acceptance criteria

- [ ] **Given** a planted `"apogee graph build " + name`, a `?:` operand or a `<<` chain outside the renderer, **then** `cli.one_suggested_command` fails naming the line. On the shipped tree it passes, with an empty allowlist.
- [ ] **Given** only plain names, and values made only of bare characters, **then** every command printed at the sites this item moves keeps its bytes: the existing pins and goldens for those sites pass unedited, `train_test.cpp:313`'s name-then-option order among them (Evidence 14).

## Constraints

- **Plain words keep their bytes.** Today's order and words are printed whenever nothing needs them changed. 53c's double quotes are kept for every word they quoted correctly. CLI-M48 records the dated revision of `MILESTONES.md:128` for the words they broke ([ADR 0006](../../../adrs/cli/backwards-compatibility.md): a record holds until a dated revision).
- **The `remedy` field stays, in its documented shape**: doors joined by `, or `, each a command followed by ` to <what it does>` (`machine-mode.md:1708-1712`, ADR 0006's additive rule). Only the command bytes of an odd name or path change.
- **The house voice is unchanged.** Every note stays one stderr line beginning `apogee <verb>: `, on the stream it uses today (CLI-M46's constraint; the Mac app keeps exactly those lines, [APP-M22](../../../assistant/darwin/MILESTONES.md#2026-11-26--jobs-notes-reach-the-person-maintenance-item-m22-a-jobs-notes-reach-the-person)). The program word inside a printed command is not the voice.
- **One renderer, in contracts.** CLI-M48's renderer is pure text in `source/data/contracts/` and includes nothing above the standard library: `contracts/` is the floor, and what two layers both need sinks there (the Data layer's card). Business and Data sites (`business/video/runtime.cpp`, `snapshot.cpp`) call it directly. No site keeps a quoter of its own.
- **CLI11 2.4.2, as measured.** Options come before `--`. A `--` directly after an option with no limit on its values is eaten. A second `--` anywhere else is an operand. Option values parse whole, so they never need `--`.
- **The words around each command don't change, and each site keeps its delimiter** (`run: `, `'…'`, backticks).
- **Mode parity** ([ADR 0002](../../../adrs/cli/mode-parity.md)): a surface that carries the CLI's text carries it from the same producer, with no code per surface, as the HTTP bodies and the graph tools do from CLI-M48's `build_hint`.
- **No client change.** The Mac app, the mod and the design prototype display the text, and their recorded documents hold plain words.
- **Code style** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md#code-style)): every member of `SuggestedCommand` and its parts carries a default (CLI-M48), so each site's designated initializer names only what it sets.
- **Tests and probes are hermetic**, with `APOGEE_HOME` in a temporary folder. Any live pass names `apogee-dev` ([Development runs on the dev channel](../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel)).

## Technical approach

Paths under `lib/src/cli/`.

| File | Change |
|---|---|
| `source/presentation/cli/check.cpp` | The name holes of its 38 that CLI-M48 leaves, each through the renderer with no other change: `promote_again_door` (`:1046-1067`) with its `&&` (`:1064`) through `render_all`, `:2729-2781` with `:2750`'s `&&` fragment, and the rest. Also CLI-M40's `delete-allowed-host '<entry>'` remedy, shipped 2026-11-27 after the sweep, whose hand-wrapped quotes go. CLI-M48 takes the collection, graph, set-backend and chmod rows. |
| `source/presentation/cli/train.cpp` | Its 19 sites, `train promote … --as` and `train rollback` among them. The take-off door at `:1054-1056` is the exception: CLI-M48 moves it with the hand-typed `""` it folds. |
| `source/presentation/cli/models_pull.cpp`, `models.cpp`, `source/data/modelstore/snapshot.cpp` | The rest of `models_pull.cpp`'s 31 sites, `:3277`'s suffixes and `:3400-3401`'s `?:` operand among them (CLI-M48 takes its backend doors and path hints). `models.cpp`'s 13. In `snapshot.cpp`, `:499`, `:510` and `:580`. |
| `source/business/video/runtime.cpp` | 7 sites. |
| `source/presentation/cli/tui_agents.cpp`, `tui_training.cpp` | The Agents view's confirm (`tui_agents.cpp:138-139`) and the Train view's rollback confirm (`tui_training.cpp:188-191`). If CLI-M51 has landed and the MCP view's `t` prefill (`tui_agents.cpp:212`) does not yet spell its key with `shell_word`, it is wired to it here. |
| `source/presentation/cli/mcp_cmd.cpp`, and the rest of the sweep | `mcp_cmd.cpp:125`'s `try_tool`, a non-name hole. Then 1 to 4 sites in each remaining file of the sweep's 42: the suites, agents and MCP servers, the model refs and handles, the run and task ids. |
| `tests/scripts/cmake/one_suggested_command.cmake` (new), `tests/CMakeLists.txt` | Fixture (3), registered as `cli.one_suggested_command` beside `cli.one_key_resolver` (`:698-703`). Its allowlist ships empty. |

**What it builds on.** CLI-M48's renderer, shipped before this item, in `source/data/contracts/suggested_command.h`: `shell_word`, `SuggestedCommand` (`program`, `verbs`, `parts`), `CommandPart`, `operand`, `option` (a greedy option is marked by its caller), `render` and `render_all`. Its shipped code and its fixtures (1) and (2) are the authority. This item adds no rule. The rules, as CLI-M48 specs them:

**Order.**
- When no operand starts with `-`, and no greedy option sits directly before an operand, the parts print in today's order, word for word.
- Otherwise it prints the program, the verbs, then the options in their order. A `--` follows the last option when it is greedy, and CLI11 eats it (`App_inl.hpp:1997-1999`). Then comes `--` if an operand starts with `-`, then the operands.
- Never an option after `--` (`o2`'s exit 109), and never a blind `-- --` (`c:k02`'s exit 109).
- An operand is a word, a composite word the site joined first (`models repair <model>/gguf/<id>`), or a placeholder.

**Words.** `shell_word(w)` is:
- bare when `w` is non-empty, every character is in `A-Za-z0-9_./:@%+=,-`, and it doesn't start with `=`;
- `""` for the empty word;
- double quotes when `w` holds no `$`, backtick, `"`, `!` or control character, no doubled backslash and no trailing backslash. This is 53c's spelling, kept for every word 53c quoted correctly;
- otherwise single quotes, with `'` written as `'\''`.

Placeholders print verbatim. A dash-led word is bare (`-x`), and the order rule gives it its `--`. A word with any other character, a non-ASCII letter included, is quoted where some sites print it bare today. It reads the same either way, and no pin holds one (by reading, 2026-11-26).

Worked examples:

| Command | Rendering |
|---|---|
| `graph build`, operand `-x` | `apogee graph build -- -x` |
| `graph build`, operand `--` | `apogee graph build -- --` |
| `graph build`, operand `a;b`, `=ls`, `my notes`, `it's` | `apogee graph build "a;b"` (and `"=ls"`, `"my notes"`, `"it's"`) |
| `graph build`, operand `$HOME`, or `it's!` | `apogee graph build '$HOME'`, `apogee graph build 'it'\''s!'` |
| `config set-backend`, operand `-gone`, option `--model-path <an existing .gguf>` | `apogee config set-backend --model-path <an existing .gguf> -- -gone` |
| the same with `flat` | `apogee config set-backend flat --model-path <an existing .gguf>`, today's bytes |
| `graph build`, greedy option `--source <dir>`, operand `-x` | `apogee graph build --source <dir> -- -- -x` |
| program `chmod`, verb `700`, operand `…/it's home/config` | `chmod 700 "…/it's home/config"` |

**Sites.** Each site builds a `SuggestedCommand` where it built a string, and keeps the words around it. For example, `graph.cpp:1535-1536` becomes `"no graph built for '" + target.name + "' -- run: " + render({.verbs = {"graph", "build"}, .parts = {operand(target.name)}})`. The display quotes around a name in prose (`'" + target.name + "'`) are not a command and stay. `render_all` joins `check.cpp:1064`'s and `:2750`'s `&&` pairs.

**The source check.** `cli.one_suggested_command` is the sibling of `one_key_resolver.cmake`.
- It fails on a string literal holding `apogee <verb> ` or `chmod ` that is followed by a non-literal through `+`, `<<` or a `?:` operand.
- It also fails on a command literal that wraps its operand in quotes by hand (`"chmod 700 '" + path`).
- It reads a statement across its lines, since most chains span two or three (`graph.cpp:1535-1536`). Comments are exempt. The house voice (`apogee <verb>: `) is not a command and doesn't match.
- Its allowlist gives a reason per line, and every entry must name a line that exists. It fails when it scanned no sources.
- It ships with the allowlist empty: every site, the non-name holes included, goes through the renderer. A non-name value is just a word that renders bare.

**What it hands on, and what lands around it.**
- CLI-M49 and CLI-M50 are gated on CLI-M48, not on this item. Built before this item, each moves onto the renderer, with no other change, any site of its own that this item has not moved yet. CLI-M49 also starts `one_suggested_command.cmake` with its rule for fully literal commands, which this item then extends (their Decision logs). Built after it, they find every site on the renderer. CLI-M49 sets the renderer's `program` field, and CLI-M50 chooses which door a backend site renders.
- CLI-M51's TUI argv takes no rendering, but the Train and Agents views' confirm texts are this item's (the Datasets view's is CLI-M48's). The MCP view's `t` prefill spells its key with `shell_word`: if CLI-M51 has landed first and the prefill is not on `shell_word` yet, this item wires it.
- CLI-M40's remedy (shipped 2026-11-27) is built through the renderer here, and the source check finds its hand-wrapped quotes until it is.
- The creation rules (CLI-M46, CLI-M53, CLI-M56) stop new odd names and change nothing here.

**Reference.** In-house precedents:
- `mcp_create_line` (`code_exec_server.cpp:47-53`) already renders a whole argv as one copyable line.
- `check_test.cpp`'s `doors_of` (`:3436-3459`) and its "runs as printed" case (`:3507`) replay doors through `line_tokens` and `CliHome`. CLI-M48 extends them, rather than writing a second splitter.
- `one_key_resolver.cmake`: an allowlist with a reason per line, every entry checked to exist, a refusal to pass vacuously, registered at `tests/CMakeLists.txt:698-703`.
- 53c's backslash fix (`MILESTONES.md:128`): a door that doesn't run as printed is a bug, and the pins move to the rule.
- CLI-M37's `pwsh` case: gated on `pwsh` and on child processes.
- The Mac app's `Argv.shown`, specced in APP-M21: options, then names, with `--` printed only when a name after it begins with `-`. Its `--` rule is the renderer's; its order is options first always, where the renderer keeps today's order for a plain name.

The one deliberate divergence from 53c is the single-quote branch, for the words its double quotes break.

**Docs when it ships:**
- CLAUDE.md: a `⚠` invariant, "A printed command runs as printed": rendered by `suggested_command`, enforced by `cli.one_suggested_command` and CLI-M48's two fixtures.
- DEVELOPER.md: the test row for the source check, beside `cli.one_key_resolver`'s.
- MILESTONES: the entry.

## Testing

Every fixture is hermetic: a `CliHome` or a temporary `APOGEE_HOME`, mock backends, no model and no network. Each fails on today's binary unless marked as a guard.

- **The source check** (fixture (3)). It passes on the tree after the item, with an empty allowlist. It fails on a planted `"apogee graph build " + name`, a `?:` operand and a `<<` chain, each named by file and line.
- **Pinned spellings** (guard). The existing pins and goldens for the sites this item moves pass unedited, `train_test.cpp:313` among them (Evidence 14).
- **The house voice** (guard). Every refusal and note at the moved sites keeps its `apogee <verb>: ` prefix and its stream; only the command inside it changes.
- **The renderings.** The renderer's words, its order and its shell replay are CLI-M48's fixtures (1) and (2), which this item reuses.
- **Gate:** `lib/scripts/cicd.sh --app cli --test`, clang-format, and the MinGW scan of the changed files.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Should a site that sets a command off in single quotes keep them when the command holds a quoted word? (First raised as the user's call; it changes no behaviour, so it is a default.) | `[default]` | Keep each site's delimiter. Most inner quoting under this rule is double quotes (`'apogee config delete-backend "my model"'`), so single quotes nest only for words with `$`, a backtick, `"` or `!`. Tests assert that the message contains the rendering, rather than parsing the prose. No house rule covers delimiters, and the choice changes no behaviour. |
| How does a site know that an option is greedy? | `[default]` | The caller marks it. Today no remedy puts a greedy option before an operand: `graph.cpp:1013-1019` and `:1108-1112` print `--source <dir> --graph NAME`. CLI-M48's Data test pins the rendering, and its parse-back covers the sites that name `--source`. |
| How a word is quoted, where `--` goes, a name holding a newline or a tab, the Windows and PowerShell rule, and what the HTTP bodies and tool results carry | `(consumed decision)` | [CLI-M48](a-suggested-command-runs-as-printed-for-every-name-the-cli-makes.md) owns them, in its Open questions and Decision log. This item's sites take its renderer as it ships, and add no rule. |
| Should a printed command name `apogee-dev` on the dev channel, or the root flag a run was given? | `(consumed decision)` | [CLI-M49](a-printed-command-names-the-binary-that-printed-it.md) owns it. CLI-M48 gives the renderer a `program` field, `apogee` by default, and changes nothing else about it. |
| Should a backend door name `rename-backend` first, for a name the config editors refuse? | `(consumed decision)` | [CLI-M50](a-refused-backend-name-gets-the-rename-that-reaches-it.md) owns it. Once the renderer quotes it, a pasted `delete-backend` reaches 57e's refusal, which names `rename-backend`, so no wrong target remains. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-26 | Specced from the APP-M21 probes' CLI asides, then a sandboxed investigation, two skeptics and a critic (Evidence). The seam is the missing renderer of a printed command, not dashes alone: `;`, `$`, a backtick, a quote, a space, zsh's `=` and home paths fail at the same joins. | the spec |
| 2026-11-26 | **Placed in Maintenance as M48**, by the user's call to spec the APP-M21/M22 CLI asides as maintenance items from M48, grouped only where they share one seam. This is its own item: no other one renders a printed command. The program word (CLI-M49), the rename door (CLI-M50) and the TUI's argv (CLI-M51) were found beside it and split off, each a different change. | the user |
| 2026-11-26 | **Split first, into Pass A and Pass B.** About 190 sites, six chmod rows, five quoters to fold, three test layers and a source check are more than one focused session. Pass A takes everything a name reaches with no hand edit, and the backend doors, where a wrong target is measured. | the spec |
| 2026-11-26 | **Today's order and bytes are kept** unless an operand starts with `-`, or a greedy option would sit directly before an operand. "Options first, always" would change every plain-name remedy with an option after its name (at least 44 pinned test literals), which contradicts "a plain name gives today's bytes". | the spec |
| 2026-11-26 | **The word rule**: bare in `A-Za-z0-9_./:@%+=,-` but never a leading `=` (zsh), double quotes without `$`, a backtick, `"`, `!`, a control character or a doubled or trailing backslash, otherwise single quotes. A bare `=`, and double quotes that admit a `!`, were each tried, and each failed a reader in the table (`c:rt`, `c:h`). PowerShell's `'it''s'` reads back as `its` in `line_tokens` and every POSIX shell, so it is kept only for the Windows remainder. | the spec |
| 2026-11-26 | **53c's quoting is revised for the words it broke** (`MILESTONES.md:128`): `;`, `$`, backticks, `a"b`, a UNC path, a path ending in a backslash, and the empty word `check.cpp` printed as nothing. Code-exec's single quotes become double quotes for the two pinned words. A dated MILESTONES line records it (ADR 0006). | the spec |
| 2026-11-26 | **Blocks CLI-M49 and CLI-M50**, both built on the renderer. **No gate with CLI-M46**: its own decision row says whichever lands first, and its two hand-quoted check remedies end up built through the renderer either way. **Independent of CLI-M45 and of APP-M21/M23**, the app's argv. | the spec |
| 2026-11-26 | **The rename-backend door is CLI-M50's, not this item's.** Choosing a door is a different change from rendering one, and the door sites don't know `yaml_plain_key`. 55f's record says "existing odd names reached by `rename-backend`" (`MILESTONES.md:11232`). | the spec |
| 2026-11-27 | **Split into Pass A, [CLI-M48](a-suggested-command-runs-as-printed-for-every-name-the-cli-makes.md), and Pass B, this item (CLI-M61)**, taking the item's own recorded default (its Open question "Should every site land in one item, or the reachable kinds first?": one item, built in two passes), which the grooming consumes. Pass A keeps the id M48: the maintenance sequence's numbers ascend down the rows, and CLI-M49 and CLI-M50 already gate on M48 and build on Pass A's renderer, so keeping M48 for Pass A keeps both the ascent and their gate. Pass B takes the next id, M61 (M60 was the highest ever assigned), and its row goes at the end of the Maintenance table, gated on M48. Pass B takes the source check's acceptance criterion, and the plain-bytes guard over the sites it moves. The run could not confirm the split with the user, who had been told of the plan and invited to object, so the split is flagged for their veto in the run's report. | default (the group run's grooming) |

## Out of scope

- **Pass A's work, already done in CLI-M48.** That covers the renderer and its order and word rules, the five quoters and the hand-typed `""` folded in, fixtures (1) and (2), and the `pwsh` case. It also covers every site a name reaches with no hand edit (collections and graphs, symphonies, datasets, path values under the home, check's six chmod rows) and the backend doors: [CLI-M48](a-suggested-command-runs-as-printed-for-every-name-the-cli-makes.md).
- **The program word and root a printed command names** (`apogee-dev`, `--dev`): [CLI-M49](a-printed-command-names-the-binary-that-printed-it.md).
- **Offering `rename-backend` to a backend the editors refuse**, and the dead ends for graph entries, suites, agents and MCP servers: [CLI-M50](a-refused-backend-name-gets-the-rename-that-reaches-it.md).
- **The TUI's argv without `--`** (its row actions on a dash-led row): [CLI-M51](the-tuis-row-actions-reach-a-dash-led-row.md). This item renders the Train and Agents views' confirm texts.
- **Creation rules**: [CLI-M46](../../../assistant/cli/MILESTONES.md#2026-11-27--a-collection-the-config-cannot-hold-is-refused-before-it-is-written-maintenance-item-m46-a-collection-the-config-cannot-hold-is-refused-before-it-is-written) (shipped 2026-11-27) for collections, [CLI-M53](a-symphony-the-cli-writes-is-one-it-can-delete.md) for a symphony with a leading `-`, [CLI-M56](a-new-dataset-name-is-one-every-verb-reads.md) for a dataset with a leading `-`.
- **The TUI's exec line not stripping a leading `apogee`** (`tui_runner.cpp:73-88`, `c:w01`): an aside, not in this item.
- **`cmd /C` on Windows expanding `%VAR%`** in a relayed remedy: a known gap, named by a test, not fixed here. An agent relaying a remedy through `run_command` was not measured.
- **Any Mac app, mod or design change**, the Mac dev app's Console prefix strip for `apogee-dev` included (CLI-M49's GUI follow-up).

## Links

- **Blocked by:** [CLI-M48](a-suggested-command-runs-as-printed-for-every-name-the-cli-makes.md), Pass A of the split, whose renderer and folds this item builds on · **Blocks:** —
- **Relates to:**
  - [CLI-M49](a-printed-command-names-the-binary-that-printed-it.md) and [CLI-M50](a-refused-backend-name-gets-the-rename-that-reaches-it.md), gated on CLI-M48, not on this item: each, built first, moves its own remaining sites onto the renderer, and CLI-M49 starts the source check this item extends;
  - [CLI-M51](the-tuis-row-actions-reach-a-dash-led-row.md), the TUI's row actions, whose Train and Agents confirm texts this item renders, and whose `t` prefill it wires if CLI-M51 lands first;
  - [CLI-M40](../../../assistant/cli/MILESTONES.md#2026-11-27--delete-allowed-host-removes-what-the-list-holds-maintenance-item-m40-the-verb-removes-an-entry-as-written-and-every-spelling-of-a-host) (shipped 2026-11-27), whose `delete-allowed-host '<entry>'` remedy this item builds through the renderer, and whose hand-wrapped quotes its source check finds;
  - [CLI-M46](../../../assistant/cli/MILESTONES.md#2026-11-27--a-collection-the-config-cannot-hold-is-refused-before-it-is-written-maintenance-item-m46-a-collection-the-config-cannot-hold-is-refused-before-it-is-written) (shipped 2026-11-27), whose two `embed ingest` remedies (`check.cpp:1764`, `:2123`) CLI-M48 builds; [CLI-M45](../../../assistant/cli/MILESTONES.md#2026-11-27--a-collection-is-named-not-pathed-at-every-reader-maintenance-item-m45-a-collection-is-named-not-pathed-at-every-reader) (shipped 2026-11-27), independent: its read rule refuses only a name that escapes the folder, so every odd name here still reads;
  - [CLI-M53](a-symphony-the-cli-writes-is-one-it-can-delete.md) and [CLI-M56](a-new-dataset-name-is-one-every-verb-reads.md), the creation rules for a dash-led symphony and dataset;
  - [CLI-M42](../../../assistant/cli/MILESTONES.md#2026-11-27--stubs-read-the-words-as-the-shell-does-maintenance-item-m42-a-quoted-word-completes-and-a-name-lands-as-one-word-in-every-shell) and [CLI-M37](../../../assistant/cli/MILESTONES.md#2026-11-27--powershell-completes-the-word-at-the-cursor-maintenance-item-m37-a-tab-completes-the-word-at-the-cursor-on-every-powershell), the shell stubs: a different seam, the same POSIX and PowerShell facts;
  - [APP-M21](../../../assistant/darwin/MILESTONES.md#2026-11-26--listed-name-reaches-the-verb-maintenance-item-m21-a-listed-name-reaches-the-verb-as-listed), whose CLI aside this item answers, and whose `Argv.shown` prints `--` by the same rule on the app's side; [APP-M22](../../../assistant/darwin/MILESTONES.md#2026-11-26--jobs-notes-reach-the-person-maintenance-item-m22-a-jobs-notes-reach-the-person), the house voice this item keeps; [APP-M23](../../../assistant/darwin/MILESTONES.md#2026-11-27--every-name-the-app-passes-is-read-as-a-name-maintenance-item-m23-every-name-the-app-passes-is-read-as-a-name), the app's own argv, independent;
  - [ADR 0002](../../../adrs/cli/mode-parity.md) and [ADR 0006](../../../adrs/cli/backwards-compatibility.md).
- **Follows from:** CLI-M48's renderer; M14's "The doctor's doors run as printed" (`MILESTONES.md:3948`) and `reference/cli/training.md:136`'s "a command that runs as printed".
