# Submodules and the files a repository's config names stay in bounds

**What / why.** The git tools never ask before a read, so 57o confined them to repositories inside the allowed root and to what git opens there. 57y then had them judge the whole `.git` folder for links that lead out. Two routes outside the root still reach git, through something other than the repository's own git folder. 57y's record leaves both as found ([Milestone AT](../../../assistant/cli/MILESTONES.md#milestone-at--the-second-sweeps-cli-findings), `git-folder-links-stay-in-bounds`, Not done), and its second review reproduced both on every binary:
1. **A submodule's git folder, reached through its work tree.**
2. **A file the repository's own config names.**

Measured, not guessed (2026-11-15). The method:
- **The binary:** the build tree's `apogee` at 57y (`79bdbcc6`), git 2.53.0 on macOS.
- **The sandbox:** a scratch `APOGEE_HOME` and `HOME`, with `GIT_CONFIG_GLOBAL` and `GIT_CONFIG_SYSTEM` set to `/dev/null`.
- **The driver:** the mock backend driving `complete --tools` from the root `proj`.
- **The outside data:** an outside repository whose one commit is `1054fe6f…`, and an outside config setting `branch.main.merge = refs/heads/OUTSIDE-SECRET-BRANCH`.
- **The control:** every case paired with plain git.

**Route 1: a submodule's git folder reached through its work tree.** For a work-tree diff and for status, git reads each populated submodule's `HEAD` through `<path>/.git` (`--ignore-submodules=dirty` still does this, to see new commits). 57o's `confine` (`business/tools/git.cpp:672`) and 57y's walk judge only the repository the call opened. The absorbed form, a submodule's git folder under `.git/modules/`, is inside that repository and refused since 57y. These three are not judged:
- **`sub/.git` kept as a folder in the work tree**, as `git add` of an embedded repository or `git submodule add` of an existing one leaves it, with `refs/heads/main` linked to the outside repository's ref. `git_diff{repo:'super'}` printed `+Subproject commit 1054fe6f18ddb193cf23316f04b99a89f871697b`, and `git_status` printed `modified: sub (new commits)`.
- **`sub2/.git` a `gitdir:` file naming the outside repository's `.git`** (57o's recorded edge): the same `+Subproject commit 1054fe6f…`.
- **`sub3/.git` a link to the outside `.git`:** the same.
- **Launched inside `super` with no `repo` named:** the same lines.
- **Unaffected:** `git_log` and `git_show` on the superproject show no outside id, since they read commits, not a submodule's work tree. Every `git_diff` form that names two commits (`target`, a review, the default branch's merge base) is unaffected too.

What it gives is a commit id, with whether it moved. One id has been enough before: 57y's probe showed how the tools' own errors then name the tree and blob to fetch next.

**Route 2: a file the repository's own config names.** git reads the files a repository's `.git/config` names, at any path. Nothing judges them. The `Git` constructor's one config listing (`:437`) reads only the filter drivers' names, to switch them off. Six forms of include each made `git_status` print `Your branch is based on 'origin/OUTSIDE-SECRET-BRANCH', but the upstream is gone.`:
- `include.path` absolute;
- `include.path` relative (`../../../cfgdir/config`, read against the including file's folder);
- `includeIf.gitdir:<root>/.path`;
- `includeIf.onbranch:main.path`;
- an include of a file inside the root that is a link out;
- a nested include: `.git/config` including `../nested.cfg` inside the root, which includes the outside file.

The other settings that name a file git opens for these four tools:
- **`mailmap.file`** naming an outside mailmap had `git_show` print `Author: OUTSIDE NAME <t@example.com>`, a line of the outside file.
- **`core.excludesFile`** naming an outside file hid an untracked file from `git_status` that its pattern matched.
- **`core.attributesFile`** naming an outside `* -diff` turned `git_diff`'s text into `Binary files … differ`.

Those last two show no outside text, but each reads an outside file and answers by what it holds. `diff.orderFile` changed nothing visible through the tools (probed).

**What already works**, and bounds the item:
- The absorbed submodule's git folder, under `.git/modules/`, is walked and refused (57y).
- A link out anywhere in the git folder is refused, `config` itself included (57y).
- A config setting that would run a command is switched off or refused: filter drivers (57o), and the fetch's settings (57x).
- `discover` refuses a `core.worktree` an include sets outside the root (`its work tree at …`, 57o).
- The person's own global and system settings are theirs, by 57x's rule.

**Who can put them there:** a repository's `.git/config` and its work tree are inside the root, so anything that can write the root can. That includes the file tools themselves, once allowed (a prompt's yes, accept-edits, or a config `allow`): `write_file` can write `.git/config` or a `sub/.git` file, as 57o's own probe wrote a `.git` file. 57y's link check never needed that. Then, as 57y found, an allowed shell or the code server, and a folder the person extracts or copies in.

**Core constraint(s).**
- **What the gate shows is what it enforces** (57o): a read the person was never asked about stays inside the root. A submodule git reads through the work tree is judged as `repo` is, and so is every file the repository's own config names for git to open.
- **Judged from the filesystem at every call** (57o): no cached verdict.
- **The person's settings are theirs** (57x): only the repository's own config is judged, its local and worktree scope, which counts what an include pulls in. Global, system and command-line settings are not.
- **What already reads keeps reading.** A submodule's git folder vouched for from outside the root stays readable. That is a linked worktree's submodule, whose git folder sits in the main repository's `.git/worktrees/<id>/modules/`, vouched by its `core.worktree` back-pointer (57o's `vouched`). So are 57o's and 57y's still-read cases, and an include or a named file inside the root.
- **A bounded cost.** One listing per call, and one discovery per populated submodule. Both are measured below.

**Seam + files.**
- `source/business/tools/git.cpp`:
  - **Route 1.** A submodule check, run before `git_status` and before `git_diff`'s work-tree form (its `spec` `HEAD`, `:1983`): the forms that read a submodule's `HEAD` through its work tree.
    - It lists the index's gitlinks (`git ls-files -s -z`, mode `160000`), read whole, since the 256 KiB a run keeps by default is less than this checkout's own listing.
    - It confines each populated one, `<top>/<path>/.git` present, as `confine` (`:672`) confines `repo`: `discover` there, 57o's rules, 57y's walk. The refusal is 57o's words, naming the submodule: `the submodule at 'sub' of <shown> is outside the allowed root …`.
  - **Route 2.** The constructor's listing (`:437`) widens to read values with their scope and origin, in one process as now: `git config -z --show-scope --show-origin --get-regexp '^(filter\..*|include\.path|includeif\..*\.path|core\.excludesfile|core\.attributesfile|mailmap\.file)$'`. `filter_neutralising_options` (`:1194`) reads its names from the same listing. Each local or worktree-scope path is resolved as git resolves it, then judged by `lands_inside`:
    - **an include's path:** `~/` the person's home, a relative one against its origin file's folder;
    - **the other three:** `~/` the same, a relative one against the work tree's top (probed: git reads a relative `core.excludesFile` there, from any folder).
    - **The refusal names the setting and where it was set:** `<origin> sets <key> to <path>, a file outside the root`.
- `source/business/tools/process.h/.cpp`: none expected. `run_to_completion`'s `OutputLimit` already keeps a whole listing up to a bound, and the bound is an open call.
- `tests/business/tools/git_test.cpp`: the cases below, beside `[57o]`, `[57x]` and `[57y]`.
- Docs: [`reference/cli/tools.md`](../../../reference/cli/tools.md)'s "What the gate shows is what it enforces" (a submodule's git folder and the files a repository's config names), DEVELOPER.md's `tools/` and `git_test` rows, MILESTONES on ship.

**Reference.**
- **57o** (`confine`, `discover`, `vouched`, `entry_inside`) is the rule each submodule is confined by.
- **57y** (`link_out_under`, `object_store_leak`) is the walk it reuses.
- **57x's settings listing** (`ReviewFetch::listed`, `fetch_setting_refusal`) is the precedent for reading a repository's own settings by scope, refusing by name, honoring the person's, and reading a listing whole or refusing it (`kListedTooMuch`).
- **git's own include rules** are those of `git help config`'s "Includes": `~/` expands, and a relative path is read against the including file's folder. `includeIf` conditions are `gitdir:`, `gitdir/i:`, `onbranch:` and `hasconfig:remote.*.url:`.

**Decisions made** (dated):
- 2026-11-15 — Diagnosed by probe, as above. The scratch is `routes/`: `w1` holds the submodules, `w2` the includes, `w3` the three named files, `w4` the relative-path rule. The two routes were first reported by 57y's second review, which reproduced them on the binaries from before 57y, at 57y's first version and at its second.
- 2026-11-15 — **One item for both routes:** the same rule (57o's), the same seam (the `Git` constructor and `confine`), the same refusal words, and one session's work.
- 2026-11-15 — **v0.2.1, as 57z** (the user's call): track 57's last item, so the release ships 57o's claim whole for the git tools.
- 2026-11-15 — **`mailmap.file`, `core.excludesFile` and `core.attributesFile` are in**, though two of them only shape an answer: each reads an outside file whose content the answer then depends on, and the third shows a line of it. **`diff.orderFile` is out:** no tool's answer changed (probed).

**Open calls:**
- [default: a submodule whose git folder leads out refuses the whole call, in 57o's words naming the submodule, as `repo` is refused.] Showing the rest of the diff with that submodule hidden would need a per-submodule `--ignore-submodules`, and git honours that only by a submodule's name in `.gitmodules`. The gitlink in the probe had none.
- [default: the gitlinks come from `git ls-files -s -z`, read whole up to 64 MiB, about a million tracked files, and a listing past that refuses the call as a config listing past its bound does (57x).] The other ways of finding them fall short. `.gitmodules` misses an unregistered gitlink, which git still reads (probed: `sub` above had no entry). Parsing the index ourselves means its versions, the split index and the sparse index.
- [default: an `includeIf` path is judged whatever its condition, matched or not:] the condition is git's to evaluate. A repository's own config naming a file outside the root on purpose is refused, its key named. **The cost, named:** a project whose `.git/config` includes the person's own file (`include.path = ~/.gitconfig-work`) is refused. Their way out is the same include set in their global config, which stays theirs.
- [default: the refusal names the setting, its origin file and the path; a submodule's names its path in the superproject.]
- [default: no timing test; the costs are measured at build and recorded in MILESTONES.]

**Measured cost** (2026-11-15, medians of 15 runs):
- **The gitlink listing:** `git ls-files -s -z` takes 11.2 ms on this checkout (3,001 files, 325,647 bytes of output, against 10.0 ms for a bare `git rev-parse`), and 34.4 ms on a 100,000-file index (6,100,000 bytes), where `git status` itself takes 187.3 ms.
- **One submodule's discovery:** `git rev-parse --absolute-git-dir --git-common-dir --show-toplevel` takes 7.7 ms, plus 57y's walk of its git folder.
- **The config listing** is the one the constructor already runs, widened, so it costs nothing new.

**Guardrail(s).** The probe becomes the fixture. New cases in `git_test.cpp` are tagged `[57z]` beside `[tools][git][sandbox]`, POSIX only where they plant links, and each has a plain-git control showing git reading outside, so no case passes vacuously. Each refusal case is seen failing on 57y's code first.
- **Submodules:**
  - an embedded `sub/.git` folder with `refs/heads/main` linked out, a `sub/.git` `gitdir:` file naming an outside repository, and a `sub/.git` link to one. `git_diff` and `git_status` are refused naming the submodule, with no outside id shown; the control is plain `git diff --ignore-submodules=dirty --submodule=short` printing it.
  - the same with no `repo` named, launched in the superproject.
  - `git_log`, `git_show` and a two-commit `git_diff` still answer.
- **Includes and named files:**
  - each of the six include forms refused, `OUTSIDE-SECRET-BRANCH` in no answer;
  - `mailmap.file` refused, `OUTSIDE NAME` in no answer;
  - `core.excludesFile` and `core.attributesFile` naming outside files refused, each with its control.
- **Still reading:**
  - an include and each named file inside the root;
  - an include set in the person's global config (`GIT_CONFIG_GLOBAL` naming a file of the test's own, outside the root);
  - an absorbed submodule, and a linked worktree's submodule vouched from outside the root;
  - 57o's, 57x's and 57y's cases unchanged.

**Acceptance criteria:**
- [ ] The probe's cases, replayed through the binary in a sandbox, are refused in 57o's words: the three submodule forms (no `Subproject commit 1054fe6f…`), the six include forms (no `OUTSIDE-SECRET-BRANCH`), and `mailmap.file` (no `OUTSIDE NAME`), plus the excludes and attributes files.
- [ ] Everything that reads today reads: a repository without submodules or outside includes, absorbed and vouched submodules, the person's own global includes, and every `[57o]`, `[57x]` and `[57y]` case.
- [ ] The listing's and the submodule check's cost is measured on this checkout and on a 100,000-file index, and recorded.
- [ ] `tools.md` says a submodule's git folder and the files a repository's own config names are held inside the root, the person's own settings left theirs. The full suite is green.

**Scope note.** Earmarked for v0.2.1, track 57's last item (the user's call, 2026-11-15). Ungated: it builds on 57o, 57x's settings rules and 57y's walk, all shipped. Out of scope:
- **A submodule's own config:** the superproject never reads it for these tools.
- **The person's global and system settings**, which stay theirs.
- **Hard links and the race between judging and git's read**, as 57o and 57y left them.
- **Windows:** the git tools are not registered there (57k).
- **A launch inside a submodule's work tree with no `repo` named**, where the tools find the superproject. That is already the case, and worth its own look.
