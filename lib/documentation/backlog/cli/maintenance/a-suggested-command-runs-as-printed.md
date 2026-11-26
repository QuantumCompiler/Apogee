# CLI-M48 · A suggested command runs as printed, whatever the name or path in it

| Field | Value |
|---|---|
| **Type** | 🐞 Bug |
| **Project** | CLI |
| **Status** | 🟢 Ready |
| **Fix version** | Maintenance (any time) |
| **Component** | Remedies › every command Apogee prints for a person to run: a renderer in `contracts/`, `check`'s doors and chmod rows, the graph, models, symphonies, datasets and training hints, the HTTP graph bodies, the graph tools and the TUI's confirms |
| **Labels** | `split-first` `remedies` `shell-quoting` `source-check` |
| **Epic** | — (maintenance has no track) |
| **Blocked by** | — |
| **Blocks** | [M49](a-printed-command-names-the-binary-that-printed-it.md), [M50](a-refused-backend-name-gets-the-rename-that-reaches-it.md) |
| **Reporter** | The APP-M21 probes' CLI asides (2026-11-26), then a sandboxed investigation, two skeptics and a critic the same day |
| **Created** | 2026-11-26 |

---

## Summary

`apogee graph show -- -x Fox` says `run: apogee graph build -x`, and that line, pasted, exits 109. For a collection named `a;b` the pasted line builds collection `a` and then runs `b`. For a hand-written backend `a;b`, `models delete`'s remedy deletes backend `a`. No function renders a printed command: about 226 name holes at about 189 sites join a name or path in raw, and five private quoters each solve part of the job, differently. The item adds one renderer in contracts, folds the quoters into it, routes every site through it, and guards it with a source check. It is split first, into two passes.

## Description

### User story

**As** someone who copies a command Apogee prints (a `run:` line, a `check` remedy, a hint after a success),
**I want** it to run exactly as printed in my shell, whatever the name or path in it,
**so that** following Apogee's advice never builds or deletes the wrong thing, never prints help and exits 0, and never fails to parse.

### Steps to reproduce

The sandbox, 2026-11-26, with the built binary `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)`, which carries HEAD 5b8927c9's CLI strings: `env -i` with PATH, `HOME` and `APOGEE_HOME` set to a throwaway folder three levels deep, stdin from `/dev/null`, mock backends only.

1. Run `apogee config init`, `apogee config add-backend extractor --type mock --model-path extractor.json` and `apogee config set-default extractor`.
2. Run `apogee embed ingest -q --retriever lexical -- <n> a.txt` for `-x`, `--`, `=ls`, `a;b`, `my notes`, `it's` and a plain `a`.
3. Run `apogee graph show -- <n> Fox`, take its `run:` line, and paste it into `sh -c`, `bash -c` and `zsh -f -c`.
4. Run `apogee symphonies create s1`, then `apogee symphonies create -- -q`, and paste the printed `Play it:` and `Edit it:` lines.
5. With `APOGEE_HOME` at a folder named `it's home`, run `chmod 755 config`, then `apogee check -q --no-color`, and paste the Filesystem row's remedy.
6. Hand-edit `config.json` to add mock backends `a;b`, `-x` and `a`. Run `apogee models delete --yes -- <n>` and paste the remedy.
7. Hand-edit a llamacpp backend `-gone` whose model is gone, and run `apogee check --output-format json`. Run its set-backend door as printed, then with `--` before the name, then with the options first.

### Actual result

2. Each exits 0 and writes `embeddings/<n>.db`.
3. The pasted lines fail:
   - `-x`: `apogee graph: no graph built for '-x' -- run: apogee graph build -x`. Pasted, it exits 109 with `The following argument was not expected: -x`.
   - `-h` prints graph build's help and exits 0, building nothing. `--graph` exits 114 with `--graph: 1 required NAMED_GRAPH missing`. `--` prints `run: apogee graph build --`, and pasted it exits 1 with `apogee graph: which graph? -- give NAME, or --graph <name> with --source <dir>`.
   - `a;b`: `sh -c 'apogee graph build a;b'` exits 127. Its stdout is `Building the knowledge graph for "a" … Graph build complete for "a"`, and its stderr `/bin/sh: b: command not found`.
   - `=ls`: `zsh -f -c 'apogee graph build =ls'` exits 1 with `apogee graph: '/bin/ls' is not a plain graph or collection name`.
   - `my notes` exits 109, and `it's` is a shell syntax error.
4. `create -- -q` prints `Play it:  apogee symphonies play -q --input "…"` and `Edit it:  apogee symphonies edit -q`. Pasted, each exits 106 with `name is required`.
5. The row prints `chmod 700 '…/it's home/config'   (or: apogee check --fix)`. Pasted into sh, it exits 2 with `unexpected EOF while looking for matching`, and the mode stays `drwxr-xr-x`.
6. For `-x`: `apogee models: backend '-x' (mock) has no model file to delete -- remove the backend itself with 'apogee config delete-backend -x'`, and pasted it exits 106. For `a;b`, pasted it exits 127 after `removed backend 'a' from …/config.json`: backend `a` is gone, and `a;b` remains.
7. The text row and the JSON `remedy` both read `apogee config set-backend -gone --model-path <an existing .gguf> to repoint it, or apogee config delete-backend -gone to remove it`. As printed (with a real file for the placeholder) it exits 106. With `--` before the name it exits 109 with `The following arguments were not expected: …/m.gguf --model-path --`. Only the options first, then `--`, then the name reaches the verb.

### Expected result

Every printed command, pasted whole into sh, bash or zsh, and read by the CLI's own splitter, runs the named verb on the named thing. A plain name prints exactly the bytes it prints today.

3. `run: apogee graph build -- -x` (and `-- -h`, `-- --graph`, `-- --`), `run: apogee graph build "a;b"`, `run: apogee graph build "=ls"`, `run: apogee graph build "my notes"` and `run: apogee graph build "it's"`. Each builds its own graph.
4. `Play it:  apogee symphonies play --input "…" -- -q` and `Edit it:  apogee symphonies edit -- -q`.
5. `chmod 700 "…/it's home/config"   (or: apogee check --fix)`, which changes the mode.
6. `'apogee config delete-backend -- -x'` and `'apogee config delete-backend "a;b"'`: backend `a` is never touched.
7. `apogee config set-backend --model-path <an existing .gguf> -- -gone to repoint it, or apogee config delete-backend -- -gone to remove it`, in the text and in the JSON `remedy`.

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

The design is one pure renderer in `source/data/contracts/`, the lowest layer every printing site may include:
- **Order.** It keeps today's order when nothing needs it changed, so a plain name prints today's bytes. Only for a dash-led operand, or an option with no limit sitting directly before an operand, does it move the options first and add `--`.
- **Words.** A word stays bare when only safe characters make it up. Otherwise it gets 53c's double quotes where they read back whole, and single quotes everywhere else.
- **Folds.** The five quoters fold into it.
- **A source check.** It keeps a sixth from starting, as `cli.one_key_resolver` does for secrets.

### Evidence

All measured 2026-11-26 by an investigator, two skeptics and a critic, each in its own sandbox, with the built binary named above: `env -i` with PATH, `HOME` and `APOGEE_HOME` three levels deep, stdin from `/dev/null`, mock backends only. Labels `c:…` are the critic's runs. Unprefixed labels are the investigator's, except `n01`–`n03` (the cause skeptic's) and `y05`–`y07` (the fix skeptic's). Source lines are at HEAD 5b8927c9.

1. **The binary.** `apogee version` (`c:v00`) prints `apogee 0.2.0 (33ea685d, 2026-11-22, macos-arm64)` and `channel: release`. The stamp names HEAD~1, but the binary holds HEAD 5b8927c9's source strings: `strings | grep -c "no pipeline run 'train pipeline resume' takes"` gives 1, and `git log -S` finds that string only in 5b8927c9. The stamp under-reports.
2. **A dash-led collection's remedy is refused** (`graph.cpp:1535-1536`, since 34ed0ce4, 2026-09-19, v0.1.0).
   - `p02`: `graph show -- -x Fox` exits 1 with `apogee graph: no graph built for '-x' -- run: apogee graph build -x`.
   - `p03`: `sh -c 'apogee graph build -x'` exits 109, `The following argument was not expected: -x`.
   - `r2`: with `-h`, it prints graph build's help and exits 0. `r3`: with `--graph`, it exits 114.
   - The CLI creates `--` too (`c:k03`: exit 0, `embeddings/--.db`). `c:k04` prints `run: apogee graph build --`. Pasted (`c:k05`), it exits 1 with `which graph?`, and `graph build -- --` (`c:k06`) builds it.
   - `graph communities` does the same (`graph.cpp:1637-1640`, a6e3fa54): `p04` prints `build it first: apogee graph build -x`, and pasted (`p05`) it exits 109.
   - So do the stdout hints after a success:
     - `st3` ends `No communities stored for "-x". Run: apogee graph communities -x` (`graph.cpp:579-580`).
     - `n03`: `graph stats -- -h` exits 0 with `No graph built for "-h". Run: apogee graph build -h` (`graph.cpp:1196`), which pasted prints help and exits 0.
     - `c:k06`'s own success line ends ``Run `apogee graph stats --` for the graph's shape.`` (`graph.cpp:356`).
3. **zsh expands a leading `=`.** `c:z01`: a collection `=ls` is created. `c:z02` prints `run: apogee graph build =ls`. `c:z03`: `zsh -f -c 'apogee graph build =ls'` exits 1 with `apogee graph: '/bin/ls' is not a plain graph or collection name`. `c:z05`: `'=ls'` exits 0. `c:z07`: `zsh -f -c 'apogee graph build =x'` exits 1 with `zsh:1: x not found`, and `c:z08` (the same line under bash) exits 0.
4. **A `;` name runs a second command, against another target.**
   - `r5`: `sh -c 'apogee graph build a;b'` exits 127. Stdout: `Building the knowledge graph for "a" … Graph build complete for "a"`. Stderr: `/bin/sh: b: command not found`. Graph `a;b` stays unbuilt.
   - `m09` (`models_pull.cpp:869-874`, since 2c50f86c, 2026-10-03): `sh -c 'apogee config delete-backend a;b'` exits 127 after `removed backend 'a' from …/config.json`. Afterwards backend `a` is gone and `a;b` remains.
   - `m10`: a backend `my model` exits 109, `The following argument was not expected: model`.
   - `m11`: a backend `it's` exits 2, `unexpected EOF while looking for matching`.
   - `m12`: a backend `$HOME` is expanded by the shell, and the CLI looks for a backend named after the sandbox's home folder (`backend '…/home' not found in config`).
5. **A home path with a space or a quote.**
   - `c:c01`–`c:c03` (`check.cpp:863`): with `APOGEE_HOME` at `…/it's home` and `chmod 755 config`, `check -q --no-color` exits 1 with `chmod 700 '…/it's home/config'   (or: apogee check --fix)`. Pasted into sh, that exits 2 with `unexpected EOF while looking for matching`, and the mode stays `drwxr-xr-x`.
   - check's other chmod rows wrap the path in quotes the same way (`:1323`, `:1354`, `:1582`, `:1841`), or print it bare (`:2001`).
   - `y05`–`y07`, read 2026-11-26 (`symphonies_cmd.cpp:484-487`): under a home `my home`, `symphonies edit` refuses a rename with `make 'solo2' with 'apogee symphonies create solo2 --from …/my home/cache/symphony-solo.yaml'`. Pasted, that exits 109 with `The following argument was not expected: home/cache/symphony-solo.yaml`; single-quoted, it exits 0.
   - The models and datasets path hints join the path raw the same way (`models_pull.cpp:267-268`, `:1450-1452`, `:1479-1481`, `:1510-1512`; `datasets.cpp:128-131`; the drafter's door, `mlx_local.cpp:474`).
6. **Symphonies, once a section exists** (`symphonies_cmd.cpp:392-396`).
   - `c:y01`: `symphonies create -- -q` exits 1 while no symphony exists. After `c:y02` (`create s1`), `c:y03` exits 0, printing `Play it:  apogee symphonies play -q --input "…"` and `Edit it:  apogee symphonies edit -q`.
   - `c:y04`: `zsh -f -c 'apogee symphonies edit -q'` exits 106 with `name is required`. `c:y05`: `sh -c 'apogee symphonies play -q --input "hi"'` exits 106.
   - `c:y06`: `symphonies play --input hi -- -q` exits 0, as does `symphonies show -- -q` (`c:y08`).
   - The creation rule is CLI-M53's.
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
9. **Five private quoters disagree, and none is right for every word.**
   - `code_exec_server.cpp:14-26` (58f9e6b7, 2026-10-18): single quotes, but its bare set (`:15-17`, `…0123456789_./~:@%+=,-`) leaves a leading `~` or `=` bare.
   - `check.cpp:1036-1038` (fb5dca5d; last changed by f3bd8e08 on 2026-10-31 for 53c): double quotes only for a space, `'`, `"` or `\`. So `;`, `$` and backticks stay bare, `a"b` breaks, and the empty word prints as nothing.
   - `backend_fields.cpp:75-78` (1e83b970): the same rule, but the empty word prints as `""`.
   - `spell_adapters` (`data/backends/adapters.cpp:259-274`): the same rule over the joined set.
   - `adapter_argument` (`store_references.cpp:131-140`): quotes nothing, and is joined raw at `:421` and `:425-426`.
   - Hand-typed `""` sits at `mlx_local.cpp:569-570`, `store_references.cpp:409` and `:426`, `backend_fields.cpp:319` and `train.cpp:1056`.
10. **The quoting table** (`c:rt`: 31 words read back through `/bin/sh`, bash 3.2, zsh 5.9 `-f`, dash and a line-for-line port of `line_tokens`).
    - Under check's rule, sh reads `a;b` as `['a']`, expands `$HOME`, reads `a"b` as `[]` and runs the command inside a backtick word. It also drops one backslash of a UNC path, and reads `[]` for a path ending in a backslash.
    - A single-quote rule that leaves `=` bare, as `code_exec_server.cpp`'s does, fails only `=ls` (`['/bin/ls']`) and `=x` (`[]`), both in zsh.
    - The rule under Technical approach reads back all 31 words in every reader.
    - In interactive shells (`c:h`), `printf '[%s]\n' "it's!x"` gives bash `event not found` and zsh `event not found: x`, while `'a!x'` prints `[a!x]` in both. So double quotes can't hold a `!`.
    - The double-quote rule mangles a UNC path and a path ending in a backslash in every reader. No test path has either shape, so CI never sees it.
11. **A name holding a newline** (`c:n01`, `c:n02`): the ingest exits 0. The remedy, read with `od -c`, is `… run: apogee graph build n\nl\n`: two lines, the first naming collection `n`.
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

### Split first

This document is the item's whole design. It is built as two documents, each one focused session. Final ids are assigned when it is split.

1. **Pass A: one renderer, and every name the CLI makes** (`a-suggested-command-runs-as-printed-for-every-name-the-cli-makes.md`, no gate). This part includes:
   - `suggested_command.h/.cpp` with the order and word rules, and the five quoters and the hand-typed `""` folded into it;
   - fixtures (1) and (2) under Testing, and the `pwsh` case;
   - every site a name reaches with no hand edit:
     - collections and graphs: every `graph.cpp` site, `embed.cpp`'s three, `config_cmd.cpp`'s two graph lines, `navigate.cpp`'s `build_hint`, `query.cpp`'s note, `graph_report.cpp`, and check's collection and graph rows, CLI-M46's `:1764` and `:2123` among them;
     - symphonies: `symphonies_cmd.cpp`, `business/symphony/runner.cpp`, `tools.cpp` and `chat_play.cpp`;
     - datasets: `datasets.cpp`'s hints and the TUI's dataset confirm;
     - path values under the home: the models and datasets path hints, the drafter's door (`mlx_local.cpp:474`), and `symphonies edit`'s rename door;
     - check's six chmod rows;
   - the backend doors (`backend_doors`, `models_pull.cpp`'s delete-backend doors, check's set-backend row, `backend_fields.cpp`), since `m09`'s wrong target is measured.

   Its acceptance criteria are every one below but the source check's.
2. **Pass B: every printed command goes through the renderer** (`every-printed-command-goes-through-the-renderer.md`, gated on Pass A). This part includes:
   - the remaining sites, each with no other behaviour change: the kinds reached only by a hand edit or an older config (the other backend holes, suites, agents, MCP servers), model refs and handles, and run and task ids. That covers `train.cpp`, `models.cpp`, `business/video/runtime.cpp`, the TUI's agent and rollback confirms, the rest of the sweep, and its non-name holes;
   - `cli.one_suggested_command`, whose allowlist must reach empty before it ships;
   - the CLAUDE.md invariant and its enforcement.

   Its acceptance criteria are the source check's, and the plain-bytes guard over the sites it moves.

When the item is split, CLI-M49 and CLI-M50 are re-gated on Pass A, which holds the renderer they build on.

## Acceptance criteria

- [ ] **Given** a collection `-x` (and `-h`, `--graph`, `--`), **when** `graph show -- <n> Fox` fails, **then** it prints `run: apogee graph build -- <n>`, and that line pasted into sh, bash and zsh exits 0 and builds `<n>`. It never exits 106, 109 or 114, never prints help, and never says `which graph?` (today `p03`, `r2`, `r3`, `c:k05`).
- [ ] **Given** collections `a;b` and `a`, **when** graph show's remedy for `a;b` is pasted into sh, **then** graph `a;b` is built, `a` is not, and no other command runs (today `r5` builds `a`).
- [ ] **Given** hand-written backends `a;b` and `a`, **when** `models delete`'s remedy for `a;b` is pasted, **then** backend `a` is still in the config (today `m09` deletes it).
- [ ] **Given** a collection `=ls`, **when** its remedy is pasted into `zsh -f`, **then** graph `=ls` is built (today `c:z03` reads `/bin/ls`).
- [ ] **Given** collections `my notes`, `it's`, `$HOME` and a backtick name, **when** each remedy is pasted into sh, bash and zsh, **then** each builds its own graph, and nothing is expanded or executed.
- [ ] **Given** an `APOGEE_HOME` whose path holds a space and a `'`, **when** check's Filesystem row and `symphonies edit`'s rename door are pasted, **then** chmod changes the mode and the create runs (today `c:c03` is a syntax error, and `y06` exits 109).
- [ ] **Given** check's row for a hand-written `-gone`, **then** its text and its JSON `remedy` read `apogee config set-backend --model-path <an existing .gguf> -- -gone to repoint it, or apogee config delete-backend -- -gone to remove it`, and each door, its placeholder filled, reaches the verb (today `o1` exits 106).
- [ ] **Given** a symphony `-q` created after `s1`, **then** create prints `apogee symphonies play --input "…" -- -q` and `apogee symphonies edit -- -q`, and both parse (today `c:y04` and `c:y05` exit 106).
- [ ] **Given** a dataset `-x`, **then** the TUI's delete confirm reads `apogee datasets delete -- -x`, and the `--dataset -x` hints keep the bare value, which parses whole.
- [ ] **Given** a task whose graph tool reads unbuilt `-y`, **then** the saved tool result reads `run: apogee graph build -- -y`, and `GET /v1/admin/graph/-y/neighbors` returns the same text (ADR 0002).
- [ ] **Given** a collection whose name holds a newline, **when** graph show fails, **then** the remedy holds the name as one single-quoted word, so no line of it names another collection (today `c:n02` prints `run: apogee graph build n`, then `l`).
- [ ] **Given** only plain names, and paths made only of bare characters, **then** every remedy's bytes are unchanged: the existing pins and goldens pass unedited. The one exception is `check_test.cpp:3089-3094`, whose two code-exec lines now read `"/opt/my python/bin/python3"` and `"/opt/it's/python3"`. On Windows, the five pins that join a temp path raw (Evidence 14) build the word by the rule, as 53c's did.
- [ ] **Given** the table's words, **then** the Data test's shell replay and the Presentation test's `line_tokens` round trip each return the word exactly, a word with a newline renders single-quoted, and a word with `!` renders single-quoted.
- [ ] **Given** the tree after the item, **then** no file but `suggested_command.cpp` quotes a word of a printed command. The `shell_word` copies in `code_exec_server.cpp`, `check.cpp` and `backend_fields.cpp` are gone, and `spell_adapters`, `adapter_argument` and the hand-typed `""` go through the renderer.
- [ ] **Given** a planted `"apogee graph build " + name`, a `?:` operand or a `<<` chain outside the renderer, **then** `cli.one_suggested_command` fails naming the line. On the shipped tree it passes, with an empty allowlist.

## Constraints

- **Plain words keep their bytes.** Today's order and words are printed whenever nothing needs them changed. 53c's double quotes are kept for every word they quoted correctly. A dated MILESTONES line revises `MILESTONES.md:128` only for the words it broke ([ADR 0006](../../../adrs/cli/backwards-compatibility.md): a record holds until a dated revision).
- **The `remedy` field stays, in its documented shape**: doors joined by `, or `, each a command followed by ` to <what it does>` (`machine-mode.md:1708-1712`, ADR 0006's additive rule). Only the command bytes of an odd name or path change.
- **The house voice is unchanged.** Every note stays one stderr line beginning `apogee <verb>: `, on the stream it uses today (CLI-M46's constraint; the Mac app keeps exactly those lines, [APP-M22](../../darwin/macos/maintenance/a-jobs-notes-reach-the-person.md)). The program word inside a printed command is not the voice.
- **One renderer, in contracts.** It is pure text in `source/data/contracts/` and includes nothing above the standard library: `contracts/` is the floor, and what two layers both need sinks there (the Data layer's card). Business and Data sites call it directly. No site keeps a quoter of its own.
- **CLI11 2.4.2, as measured.** Options come before `--`. A `--` directly after an option with no limit on its values is eaten. A second `--` anywhere else is an operand. Option values parse whole, so they never need `--`.
- **The words around each command don't change, and each site keeps its delimiter** (`run: `, `'…'`, backticks).
- **Mode parity** ([ADR 0002](../../../adrs/cli/mode-parity.md)): the HTTP bodies and the graph tools carry the CLI's text, from the same `build_hint`, with no code per surface.
- **No client change.** The Mac app, the mod and the design prototype display the text, and their recorded documents hold plain words.
- **Code style** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md#code-style)): a `.h`/`.cpp` pair, and every member of `SuggestedCommand` and its parts carries a default, so a designated initializer names only what it sets.
- **Tests and probes are hermetic**, with `APOGEE_HOME` in a temporary folder. Any live pass names `apogee-dev` ([Development runs on the dev channel](../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel)).

## Technical approach

Paths under `lib/src/cli/`.

| File | Change |
|---|---|
| `source/data/contracts/suggested_command.h`, `.cpp` (new), `source/data/CMakeLists.txt` | `shell_word`, `SuggestedCommand`, `render` and `render_all`, with the order and word rules below. |
| `source/presentation/operations/code_exec_server.cpp` | Drop its `shell_word` (`:12-26`). `mcp_create_line` (`:47-53`) renders through the renderer. |
| `source/presentation/cli/check.cpp` | Drop its `shell_word` (`:1033-1038`). Every name hole goes through the renderer (38 sites), among them `:539-544` (the options first only for a dash-led name), `promote_again_door` (`:1046-1067`) and its `&&` (`:1064`), `:1764` and `:2123` (CLI-M46's), and `:2729-2781` with `:2750`'s `&&`. The chmod rows at `:863`, `:1323`, `:1354`, `:1582`, `:1841` and `:2001` take the program word `chmod`. |
| `source/presentation/operations/backend_fields.cpp` | Drop its `shell_word` (`:72-78`). `set_backend_command` and `adapters_take_off` (`:308-320`), and the two role doors (`:372-375`, `:377-380`), are built on the renderer. |
| `source/data/backends/adapters.cpp`, `mlx_local.cpp`, `source/presentation/operations/store_references.cpp` | `spell_adapters` (`:259-274`) quotes its joined set with `shell_word`. `adapter_argument`'s set (`:131-140`, joined at `:421` and `:425-426`), the door at `:409`, and the doors at `mlx_local.cpp:474` and `:569-570` go through the renderer. |
| `source/presentation/cli/graph.cpp`, `source/presentation/render/graph_report.cpp` | All 22 `graph.cpp` sites, among them `:163-164`, `:356`, `:579-580`, `:610-611`, `:614-616`, `:1013-1019`, `:1102-1127`, `:1196`, `:1535-1536` and `:1637-1640`. `graph_report.cpp:212` and `:220`. |
| `source/business/graph/navigate.cpp`, `source/business/knowledge/query.cpp` | `build_hint` (`:564`) and the record note (`query.cpp:163-164`). The HTTP bodies, the graph tools and the CLI's graph navigation verbs follow with no code of their own. |
| `source/presentation/cli/models_pull.cpp`, `models.cpp`, `source/data/modelstore/snapshot.cpp` | 31 and 13 sites: `backend_doors` (`:416-419`), `:869-874`, `:2867-2870`, `:2911-2914`, `:3195-3198`, `:3400-3401`, and the path hints `:267-268`, `:1450-1452`, `:1479-1481` and `:1510-1512`. In `snapshot.cpp`, `:499`, `:510` and `:580`. |
| `source/presentation/cli/symphonies_cmd.cpp`, `datasets.cpp`, `embed.cpp`, `config_cmd.cpp`, `train.cpp`, `chat_play.cpp`, `tui_agents.cpp`, `tui_training.cpp`, `source/business/symphony/runner.cpp`, `tools.cpp`, `source/business/video/runtime.cpp`, and the rest of the sweep | Symphonies: `:392-396`, `:484-487`, `:500-505`, `:759-761`. Datasets: `:128-131`, `:367-368`, `:520`. `train.cpp`: 19 sites. `runtime.cpp`: 7. The TUI confirms: `tui_agents.cpp:138-139`, `tui_training.cpp:188-191`, `:273-274`. Then 1 to 4 sites in each remaining file. |
| `tests/data/contracts/suggested_command_test.cpp` (new) | Fixture (1). |
| `tests/presentation/cli/suggested_command_test.cpp` (new), `tests/presentation/cli/check_test.cpp`, `tests/support/cli_home.h`, `.cpp` | Fixture (2). `doors_of` (`:3436-3459`) splits outside quotes. The two code-exec pins (`:3089-3094`) read double quotes. `CliHome` gains a folder name, so a home's path can hold a space and a `'`. |
| `tests/presentation/operations/store_references_test.cpp`, `tests/presentation/cli/datasets_test.cpp`, `tests/data/backends/mlx_local_test.cpp`, `tests/presentation/cli/models_mlx_test.cpp` | The pins at `:348`, `:574`, `:1828`, `:290` and `:465` build a joined path with the rule, as `check_test.cpp:73`'s `printed` does, so they hold on Windows. |
| `tests/scripts/cmake/one_suggested_command.cmake` (new), `tests/CMakeLists.txt` | Fixture (3), registered as `cli.one_suggested_command` beside `cli.one_key_resolver` (`:698-703`). |

**The interface**, a sketch the build may shape:

```cpp
namespace apogee::harness {

/// One word of a command a person copies, as sh, bash, zsh, dash and
/// line_tokens all read it back.
[[nodiscard]] std::string shell_word(std::string_view word);

struct CommandPart {
    bool is_option = false;                 // an option, else an operand
    std::string flag{};                     // `--model-path`, for an option
    std::optional<std::string> value{};     // the word; none for a flag alone
    bool placeholder = false;               // printed verbatim: `<an existing .gguf>`, `"…"`
    bool greedy = false;                    // an option with no limit on its values
};

struct SuggestedCommand {
    std::string program = "apogee";         // `chmod` for check's rows
    std::vector<std::string> verbs{};       // literal words: `graph`, `build`; `700`
    std::vector<CommandPart> parts{};       // in today's order
};

[[nodiscard]] CommandPart operand(std::string_view word);
[[nodiscard]] CommandPart option(std::string_view flag, std::optional<std::string> value = {},
                                 bool greedy = false);

[[nodiscard]] std::string render(const SuggestedCommand& command);
[[nodiscard]] std::string render_all(const std::vector<SuggestedCommand>& commands,
                                     std::string_view joiner);  // " && "

}  // namespace apogee::harness
```

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

**The quoters fold.**
- `code_exec_server.cpp`'s line renders `{mcp, create, <name>, --command <interpreter>, --args <script>}`.
- `check.cpp`'s and `backend_fields.cpp`'s copies become the word rule.
- `spell_adapters` quotes its joined set with `shell_word`, which `data/backends` may include.
- `adapter_argument`'s joined set and the hand-typed `""` become words.

**The source check.** `cli.one_suggested_command` is the sibling of `one_key_resolver.cmake`.
- It fails on a string literal holding `apogee <verb> ` or `chmod ` that is followed by a non-literal through `+`, `<<` or a `?:` operand.
- It also fails on a command literal that wraps its operand in quotes by hand (`"chmod 700 '" + path`).
- It reads a statement across its lines, since most chains span two or three (`graph.cpp:1535-1536`). Comments are exempt. The house voice (`apogee <verb>: `) is not a command and doesn't match.
- Its allowlist gives a reason per line, and every entry must name a line that exists. It fails when it scanned no sources.
- It ships (Pass B) with the allowlist empty: every site, the non-name holes included, goes through the renderer. A non-name value is just a word that renders bare.

**What it builds on and hands on.**
- CLI-M46's decision row (2026-11-26) records the order between the two: whichever lands first, check's two `embed ingest` remedies (`:1764`, `:2123`) end up built through the renderer.
- CLI-M49 sets the renderer's `program` field.
- CLI-M50 chooses which door a backend site renders.
- CLI-M51's TUI argv takes no rendering, but its rows' confirm texts are this item's. The MCP view's `t` prefill (`tui_agents.cpp:212`) spells its key with `shell_word`: if CLI-M51 has landed first, this item wires that prefill to it.
- The creation rules (CLI-M46, CLI-M53, CLI-M56) stop new odd names and change nothing here.

**Reference.** In-house precedents:
- `mcp_create_line` (`code_exec_server.cpp:47-53`) already renders a whole argv as one copyable line.
- `check_test.cpp`'s `doors_of` (`:3436-3459`) and its "runs as printed" case (`:3507`) replay doors through `line_tokens` and `CliHome`. This item extends them, rather than writing a second splitter.
- `one_key_resolver.cmake`: an allowlist with a reason per line, every entry checked to exist, a refusal to pass vacuously, registered at `tests/CMakeLists.txt:698-703`.
- 53c's backslash fix (`MILESTONES.md:128`): a door that doesn't run as printed is a bug, and the pins move to the rule.
- CLI-M37's `pwsh` case: gated on `pwsh` and on child processes.
- The Mac app's `Argv.shown`, specced in APP-M21: options, then names, with `--` printed only when a name after it begins with `-`. Its `--` rule is this one; its order is options first always, where this item keeps today's order for a plain name.

The one deliberate divergence from 53c is the single-quote branch, for the words its double quotes break.

**Docs when it ships:**
- CLAUDE.md: a `⚠` invariant, "A printed command runs as printed": rendered by `suggested_command`, enforced by `cli.one_suggested_command` and the two fixtures (Pass B). Also the Codebase Map line for the new file.
- DEVELOPER.md: the contracts row for `suggested_command.h/.cpp`, and the test rows for the two tests and the source check, beside `cli.one_key_resolver`'s.
- MILESTONES: the entry, and a dated revision of 53c's quoting (`MILESTONES.md:128`) for the words it broke.
- `reference/cli/machine-mode.md:1708-1712`: a door's words may be quoted, and ` to ` and `, or ` are read outside quotes.

## Testing

Every fixture is hermetic: a `CliHome` or a temporary `APOGEE_HOME`, mock backends, no model and no network. Each fails on today's binary unless marked as a guard.

- **The bug as a fixture** (fixture (2), `tests/presentation/cli/suggested_command_test.cpp`).
  - **The home.** A `CliHome` under a folder whose name holds a space and a `'`, with:
    - lexical collections `-x`, `-h`, `--`, `=ls`, `a;b`, `my notes`, `it's` and a plain `a`;
    - a symphony `-q`, created after `s1`;
    - a dataset `-x`;
    - hand-written backends `-gone` (llamacpp, model gone), `a;b` and `a`;
    - a mock extractor as the default backend.
  - **The runs.** `graph show`, `stats` and `communities`, `symphonies create`, `models delete`, and `check` in text and JSON.
  - **The replay.** Each printed command is replayed through `/bin/sh` (and zsh where present) as `printf '%s\0' <command>`, to get its argv. The program word is dropped, and the rest runs with `CliHome::run`. A chmod row runs whole through `/bin/sh`. Placeholders are filled from fixture values first (`gguf_builder.h`).
  - **The asserts.** Never exit 106, 109 or 114, and never help. Each graph is built for its own name. Collection `a` and backend `a` are untouched. Check's chmod row changes the mode.
  - **The splitter.** The table's words go through `line_tokens`, which is also the Mac Console's table, and each comes back exactly.
  - Shells and child processes are skipped where absent, and the skip names which.
- **The words** (fixture (1), `tests/data/contracts/suggested_command_test.cpp`).
  - On POSIX, each of the 31 words of the measured table: `plain`, `-x`, `-h`, `--graph`, `--`, `my notes`, `a;b`, `it's`, `$HOME`, `a"b`, `` x`id` ``, `~x`, `=ls`, `=x`, the empty word, `C:\Users\A B\m.gguf`, a UNC path, `C:\dir\`, `it's!`, `a!b`, a word with a newline, a word with a tab, `a*b`, `#x`, `{a,b}`, `a&b`, `a|b`, `a>b`, `x(y)`, `O'Brien$x` and `a b'c"d`.
  - Each `shell_word(w)` is replayed as `printf '%s\0' <rendered>` through `/bin/sh`, bash, `zsh -f` and dash, the environment cleared and the working folder a temporary one, and must return exactly `w`. Each shell is skipped where absent: whether CI's Linux container has zsh and dash is not measured. Words with `!` must come back single-quoted.
  - The command table: the worked examples above, placeholders verbatim, `""` kept, and a plain name's exact bytes.
- **Pinned spellings** (guard). The existing pins and goldens pass unedited, but for the two code-exec lines (`check_test.cpp:3089-3094`). On Windows, the five pins that join a temp path raw build the word by the rule, as 53c's four did.
- **PowerShell.** A case gated on `pwsh` and on child processes, as CLI-M37's: it runs on the Linux unit-test rows, whose image carries PowerShell 7.6.10, and skips on the Windows rows, which start no child. It replays the PowerShell renderings through `pwsh -NoProfile -NonInteractive` with a fake `apogee` that prints its argv, and settles whether `--` and `""` reach a native command. If either is dropped, the test names it, and so does this item's record. The rule takes its platform as a parameter, so the case can run off Windows.
- **The source check** (fixture (3), Pass B). It passes on the tree after the item. It fails on a planted `"apogee graph build " + name`, a `?:` operand and a `<<` chain, each named by file and line.
- **The house voice** (guard). Every refusal and note these runs print keeps its `apogee <verb>: ` prefix and its stream; only the command inside it changes.
- **Gate:** `lib/scripts/cicd.sh --app cli --test`, clang-format, and the MinGW scan of the changed files.

## Open questions

| Question | Status | Answer |
|---|---|---|
| What should the renderer print on Windows for a word that holds `'` together with `$`, a backtick, `"`, `!` or a doubled or trailing backslash? And do `--`, `""`, a bare `,` and a leading `@` reach `apogee.exe` from PowerShell? | `[default]` | One rule on every platform: its double quotes read the same in PowerShell, cmd and the POSIX shells. On `_WIN32`, a bare `,` or a leading `@` moves the word to double quotes, and the remaining cases use PowerShell single quotes with `''`. `line_tokens`, the TUI's exec line, reads `''` as nothing, so on Windows that rare spelling round-trips through PowerShell and not the exec line, and a test says so. `install.ps1` and `apogee.ps1` make PowerShell the person's shell. `run_command`'s `cmd /C` (`shell.cpp:26`) expands `%VAR%` inside and outside quotes (by reading): a known gap, named by a test. The `pwsh` case under Testing settles `--` and `""`. PowerShell was not run on this host. |
| Should a site that sets a command off in single quotes keep them when the command holds a quoted word? (First raised as the user's call; it changes no behaviour, so it is a default.) | `[default]` | Keep each site's delimiter. Most inner quoting under this rule is double quotes (`'apogee config delete-backend "my model"'`), so single quotes nest only for words with `$`, a backtick, `"` or `!`. Tests assert that the message contains the rendering, rather than parsing the prose. No house rule covers delimiters, and the choice changes no behaviour. |
| What should a remedy print for a name that holds a newline or a tab? | `[default]` | Single quotes. They round-trip whole in every POSIX shell and in `line_tokens` (`c:rt`). Not `$'…'`, which `line_tokens` and PowerShell can't read. CLI-M46's create rule stops new collection names like this. |
| Should every site land in one item, or the reachable kinds first? | `[default]` | One item, built in two passes (Split first). Pass A: the renderer, the quoters, the tests, and every site a name reaches with no hand edit, plus the backend doors. Pass B: the remaining sites and the source check. |
| How does a site know that an option is greedy? | `[default]` | The caller marks it. Today no remedy puts a greedy option before an operand: `graph.cpp:1013-1019` and `:1108-1112` print `--source <dir> --graph NAME`. The Data test pins the rendering, and the parse-back covers the sites that name `--source`. |
| Should HTTP error bodies and tool results carry the rendered command? | `[default]` | Yes, the same text the CLI prints (ADR 0002). It comes from `navigate.cpp`'s `build_hint`, and needs no code per surface. |
| Should a printed command name `apogee-dev` on the dev channel, or the root flag a run was given? | `(consumed decision)` | [CLI-M49](a-printed-command-names-the-binary-that-printed-it.md) owns it. This item gives the renderer a `program` field, `apogee` by default, and changes nothing else about it. |
| Should a backend door name `rename-backend` first, for a name the config editors refuse? | `(consumed decision)` | [CLI-M50](a-refused-backend-name-gets-the-rename-that-reaches-it.md) owns it. Once this item quotes it, a pasted `delete-backend` reaches 57e's refusal, which names `rename-backend`, so no wrong target remains. |
| Should a new name that needs quoting be refused at create? | `(consumed decision)` | [CLI-M46](a-collection-the-config-cannot-hold-is-refused-before-it-is-written.md) for collections, [CLI-M53](a-symphony-the-cli-writes-is-one-it-can-delete.md) for a dash-led symphony, [CLI-M56](a-new-dataset-name-is-one-every-verb-reads.md) for a dash-led dataset. Names already on disk, and every path under a home, still need this item. |

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

## Out of scope

- **The program word and root a printed command names** (`apogee-dev`, `--dev`): [CLI-M49](a-printed-command-names-the-binary-that-printed-it.md).
- **Offering `rename-backend` to a backend the editors refuse**, and the dead ends for graph entries, suites, agents and MCP servers: [CLI-M50](a-refused-backend-name-gets-the-rename-that-reaches-it.md).
- **The TUI's argv without `--`** (its row actions on a dash-led row): [CLI-M51](the-tuis-row-actions-reach-a-dash-led-row.md). This item renders those rows' confirm texts.
- **Creation rules**: [CLI-M46](a-collection-the-config-cannot-hold-is-refused-before-it-is-written.md) for collections, [CLI-M53](a-symphony-the-cli-writes-is-one-it-can-delete.md) for a symphony with a leading `-`, [CLI-M56](a-new-dataset-name-is-one-every-verb-reads.md) for a dataset with a leading `-`.
- **`graph build -- <odd collection>`'s post-build `could not register` and `could not set graph.enabled` notes** (`c:z05`, `c:k06`): CLI-M46.
- **The TUI's exec line not stripping a leading `apogee`** (`tui_runner.cpp:73-88`, `c:w01`): an aside, not in this item.
- **`cmd /C` on Windows expanding `%VAR%`** in a relayed remedy: a known gap, named by a test, not fixed here. An agent relaying a remedy through `run_command` was not measured.
- **Any Mac app, mod or design change**, the Mac dev app's Console prefix strip for `apogee-dev` included (CLI-M49's GUI follow-up).

## Links

- **Blocked by:** — · **Blocks:** [CLI-M49](a-printed-command-names-the-binary-that-printed-it.md) and [CLI-M50](a-refused-backend-name-gets-the-rename-that-reaches-it.md), both built on the renderer (re-gated on Pass A when the item is split)
- **Relates to:**
  - [CLI-M51](the-tuis-row-actions-reach-a-dash-led-row.md), the TUI's row actions, whose confirm texts this item renders;
  - [CLI-M46](a-collection-the-config-cannot-hold-is-refused-before-it-is-written.md), whose two `embed ingest` remedies (`check.cpp:1764`, `:2123`) this item builds, whichever lands first; [CLI-M45](a-collection-is-named-not-pathed-at-every-reader.md), independent: its read rule refuses only a name that escapes the folder, so every odd name here still reads;
  - [CLI-M53](a-symphony-the-cli-writes-is-one-it-can-delete.md) and [CLI-M56](a-new-dataset-name-is-one-every-verb-reads.md), the creation rules for a dash-led symphony and dataset;
  - [CLI-M40](delete-allowed-host-removes-what-the-list-holds.md), whose `delete-allowed-host '<entry>'` remedy is built through this renderer once it ships. If M40 lands first, Pass B's source check finds its hand-wrapped quotes;
  - [CLI-M42](stubs-read-the-words-as-the-shell-does.md) and [CLI-M37](powershell-completes-the-word-at-the-cursor.md), the shell stubs: a different seam, the same POSIX and PowerShell facts, and M37's `pwsh` case as this item's pattern;
  - [APP-M21](../../darwin/macos/maintenance/a-listed-name-reaches-the-verb-as-listed.md), whose CLI aside this item answers, and whose `Argv.shown` prints `--` by the same rule on the app's side; [APP-M22](../../darwin/macos/maintenance/a-jobs-notes-reach-the-person.md), the house voice this item keeps; [APP-M23](../../darwin/macos/maintenance/every-name-the-app-passes-is-read-as-a-name.md), the app's own argv, independent;
  - [ADR 0002](../../../adrs/cli/mode-parity.md) and [ADR 0006](../../../adrs/cli/backwards-compatibility.md).
- **Revises:** 53c's quoting ([MILESTONES](../../../assistant/cli/MILESTONES.md), `:128`), for the words it broke. **Follows from:** M14's "The doctor's doors run as printed" (`MILESTONES.md:3948`) and `reference/cli/training.md:136`'s "a command that runs as printed".
