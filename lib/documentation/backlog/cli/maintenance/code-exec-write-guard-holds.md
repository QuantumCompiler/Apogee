# CLI-M35 · The code server's write guard holds for deletes, folders and permissions

| Field | Value |
|---|---|
| **Type** | 🐞 Bug |
| **Project** | CLI |
| **Status** | 🟢 Ready |
| **Fix version** | Maintenance (any time) |
| **Component** | Code-execution MCP server (47b) › the write guard |
| **Labels** | `code-exec` `sandbox` `mcp` `python` |
| **Epic** | — (maintenance has no track) |
| **Blocked by** | — |
| **Blocks** | — |
| **Reporter** | A spike: found 2026-11-15 while probing 57o's link gap (57y) |
| **Created** | 2026-11-15 |

---

## Summary

The code-execution server's write guard has never refused `os.remove`/`os.unlink`, `os.rmdir`, `os.mkdir` (and so `os.makedirs` and `tempfile.mkdtemp`), `os.chmod`, `os.chown` or `os.utime` outside the working directory, on any Python, because its branch for those calls waits for a directory descriptor of `None` and CPython reports a call without one as `-1`. The item makes the guard refuse them as tools.md promises, fixes the same seam's false refusal of a link inside that points outside, and makes tools.md say plainly what the guard refuses, what it costs and what it does not judge.

## Description

### User story

**As** a person using Apogee who allows `run_python`,
**I want** the code server to refuse a delete, a new folder or a permission, owner or date change outside the session's working directory, naming the path, as the reference says it does,
**so that** an honest mistake (a script that tidies up the wrong folder, or makes its cache under an absolute path) is stopped and told why, and I don't lose files to a call the table says can't happen.

### Steps to reproduce

1. Seed the real server script (`lib/src/cli/assets/mcp/code_exec_server.py`, v0.2.1 at `a002d24a`) into a scratch data directory under a scratch `HOME` and `APOGEE_HOME`.
2. Start it with `--python` on any Python 3.9.6–3.14.5 and drive it over MCP stdio.
3. Plant a file and a folder outside the working directory.
4. Call `run_python` with `os.remove('<outside file>')`, `os.rmdir('<outside folder>')`, `os.mkdir('<outside>/new')` or `os.chmod('<outside file>', 0o777)`.
5. For the second defect: make a link inside the working directory that points to an outside folder, and call `os.rename('link', 'other')`.

### Actual result

- Each call in step 4 runs: the file is deleted, the folder removed or made, the mode left `-rwxrwxrwx`. No refusal.
- The rename in step 5 is refused, though it only renames an entry inside.

### Expected result

- Each call in step 4 is refused with `PermissionError: Apogee's code-exec sandbox writes only inside its working directory (<work>): '<path>' is outside it`, and the outside file or folder is as it was.
- The rename in step 5 works.

### Background

The code-execution server (47b) runs the model's Python behind a write guard: an audit hook in the run's bootstrap that refuses a write outside the session's working directory, naming the path. [tools.md](../../../reference/cli/tools.md#the-sandbox-said-plainly) promises it ("Code that writes outside the working directory is refused, naming the path"), calling it best effort, not a boundary, and 47b's record says the same ([Milestone W](../../../assistant/cli/MILESTONES.md#milestone-w--the-mcp-client), "an audit hook refuses the code's writes outside the working directory"). Yet for `os.remove` and `os.unlink`, `os.rmdir`, `os.mkdir` (and so `os.makedirs` and `tempfile.mkdtemp`), `os.chmod`, `os.chown` and `os.utime`, the guard has never refused anything.

The bug was found while probing 57o's link gap (57y), whose "who can plant a link" probe reached the code server. It was specced from a probe of the real server on Python 3.9–3.14, and placed in Maintenance by the user's call, since the permission gate stays the boundary.

### Evidence

Paths are under `lib/src/cli/` unless written otherwise; `:N` lines are in `assets/mcp/code_exec_server.py`.

1. **Root cause.** The guard's branch for those events (`lib/src/cli/assets/mcp/code_exec_server.py:137-141`) judges a path only when the event's last argument, the call's directory descriptor, `is None`. CPython reports a call made without one as **-1**, never `None`: each of these events passes `dir_fd == DEFAULT_DIR_FD ? -1 : dir_fd` (`Modules/posixmodule.c`, verbatim at v3.8.20 and v3.14.0). So the branch takes every call for a descriptor-relative one, which its comment (`:138-139`) says the guard leaves alone, and lets it through.

2. **How it was measured** (not guessed, 2026-11-15). The real server script (v0.2.1 at `a002d24a`), copied into a scratch data directory under a scratch `HOME` and `APOGEE_HOME`, was driven over MCP stdio by a probe client: one `run_python` call per interpreter, run with `--python` on Python **3.9.6** (Xcode's, through `/usr/bin/python3`), **3.10.20, 3.11.15, 3.12.13, 3.13.13 and 3.14.5** (Homebrew), each against a folder of planted files outside the working directory. It was re-run as the item was reviewed. The record is the last two runs' transcripts, and they cover all six versions, today's guard against the prototype fix. Every version gave the same table (items 3–5).

3. **Refused, as promised** (the controls): `open(<outside>, 'w')`, `os.open(<outside>, O_CREAT | O_WRONLY)`, `os.truncate`, `os.chflags`, `os.rename`, `os.symlink` to an outside name, and `shutil.rmtree` of an outside folder.

4. **Not refused, and done:**
   - `os.remove`, `os.unlink` and `pathlib.Path.unlink` each deleted an outside file.
   - `os.rmdir` removed an outside folder.
   - `os.mkdir`, `os.makedirs` (three levels), `tempfile.mkdtemp(dir=<outside>)` and `os.mkdir('<outside>/../<sibling>')` each made a folder outside.
   - `os.chmod(<file>, 0o777)` left it `-rwxrwxrwx`.
   - `os.utime(<file>, (0, 0))` set its date to the epoch.
   - `os.chown(<file>, -1, -1)` ran.
   - **Through a link inside the working directory that points to an outside folder**, written with a trailing slash: `os.rmdir('link/')` removed the outside folder (macOS follows a final link written that way).
   - `os.remove(<absolute outside path>, dir_fd=<a descriptor on the working directory>)` deleted the file. A system call ignores `dir_fd` for an absolute path, but the guard still skips it.

5. **Not refused, and outside what any branch judges:**
   - `os.remove('victim', dir_fd=<a descriptor opened on the outside folder>)`. This is the comment's own rule.
   - `os.chmod(<a descriptor opened read-only on an outside file>, 0o777)`. A descriptor is taken for inside (`_inside`, `:109-110`).
   - `os.mkfifo` made a FIFO outside. CPython raises no audit event for `mkfifo` or `mknod` (both probed).
   - `sqlite3.connect(<outside>/made.db)` made a database outside: sqlite writes through its own C code. Its one event, `sqlite3.connect`, fires for reading too.
   - `/dev/null` is let through for every event, not only for writing: deleting it or changing its mode is stopped today only by the system's own permissions.

6. **The raw events.** A raw hook printing these events' arguments on the same six interpreters showed `('os.remove', (<path>, -1))`, `('os.mkdir', (<path>, 511, -1))`, `('os.chmod', (<path>, 384, -1))`, `('os.utime', (<path>, None, None, -1))` and `('os.chown', (<path>, 4294967295, 4294967295, -1))`. It also showed that `os.lchflags` raises `os.chflags`, `os.replace` raises `os.rename`, `os.lchmod` raises `os.chmod`, and that `shutil.rmtree`'s arguments are `(path,)` on 3.9 and 3.10 but `(path, dir_fd)` from 3.11.

7. **The branch has fired on no Python at all.** CPython's `v3.8.0` `posixmodule.c` audits only `open`, `os.truncate`, `os.listdir` and `system`: there is no `PySys_Audit` in `os_unlink_impl`, `os_mkdir_impl`, `os_rmdir_impl`, `os_chmod_impl`, `os_chown_impl` or `os_link_impl`. They are present, with -1, at v3.8.2 and v3.8.20. So on 3.8.0 and 3.8.1, which the server accepts (`probe`, `:305-324`, "3.8 or later"), the guard judges little beyond `open` and `os.truncate`.

8. **Platforms.** Linux was not run here: the -1 comes from code every platform shares, and Linux CI runs the new cases. Windows never runs the server (see Testing).

9. **Since when.** The branch shipped with 47b (`58f9e6b7`, 2026-10-18, v0.1.9), the only commit on the file. Every release since has seeded the same bytes (sha256 `c23c9734926f4e8315c18a437632002a2488df01a69b7cb82bae4d8741d0631a` at v0.1.9, v0.2.0 and `a002d24a`). 47b's test pins the guard only for `open` for writing (`tests/business/mcp/code_exec_server_test.cpp:298-303`), and no other case or script touches these calls. So nothing ever ran the broken branch: it is a guarantee that was recorded but never tested, not a regression.

10. **A second defect in the same seam.** The guard judges every path by `os.path.realpath` of the whole path (`_inside`, `:108-116`), which follows a final link. For a call that acts on the link itself, that judges the wrong file:
    - `os.rename('link', 'other')`, where `link` sits in the working directory but points outside, is refused on all six versions, though it only renames an entry inside.
    - `shutil.rmtree('link')` is refused by the guard as well. Python would refuse it anyway, with its own error, so only the reason given is wrong.

    Fixing the dead branch alone would spread the false refusal to `os.remove` of such a link, which works today only because the branch is dead. (`os.rmdir` of a link fails with `NotADirectoryError` either way.)

11. **What already works**, on Python 3.8.2 or later, and bounds the item:
    - `open` for writing (including `os.open` with `O_CREAT`, `O_WRONLY`, `O_RDWR`, `O_APPEND` or `O_TRUNC`), `os.truncate`, `os.chflags` and `os.lchflags`, `os.rename` and `os.replace`, `os.link`, `os.symlink` and `shutil.rmtree`: each refused outside with the path named, trailing slashes included.
    - `write_input`'s own path check (`:566-569`).
    - `/dev/null` stays writable, and the environment is an allow-list.
    - The boundary itself: both tools are gated as writers, so no code runs without the user's yes, a `--allow`, a config permission or a session launched in `bypass` mode ([the sandbox](../../../reference/cli/tools.md#the-sandbox-said-plainly), [the modes](../../../reference/cli/machine-mode.md)).

12. **Severity: moderate.** The guard is not the boundary: a subprocess or a C extension gets past any audit hook, as tools.md says. Its job is to stop honest mistakes, such as a script that tidies up the wrong folder or makes its cache under an absolute path, and to make the refusal say why. For these calls it has never done that job, while the reference says it does. Someone who allows `run_python` because "writes outside are refused" can lose files to an `os.remove` or a hand-written tree delete that the table says can't happen.

## Acceptance criteria

- [ ] **Given** the probe's table, **when** each "not refused, and done" case is replayed, **then** it is refused, naming the path, and the outside file or folder is as it was. The calls outside what any branch judges are said in tools.md.
- [ ] **Given** a run inside the working directory, **when** any call is made there, **then** nothing new is refused but the recorded cost (a no-follow `chmod`, `chown` or `utime` of a link that points outside), and `os.rename` of such a link now works.
- [ ] The `[M35]` cases pass on the host's `python3`. The probe matrix is also run through the server's `--python` on every `python3.N` the build machine has (3.9.6 and 3.10–3.14 on the dev Mac), with any missing version skipped by name. The per-version results are recorded in the MILESTONES entry.
- [ ] **Given** an install seeded by an earlier Apogee, **when** it next seeds, **then** it gets the fixed script, unless the user edited it.
- [ ] tools.md's Writes row says what the guard refuses, what it costs and what it does not judge, and still calls it best effort. The full suite is green.

## Constraints

- **Best effort, said so.** The guard stays a guard, and the permission gate stays the boundary: no gate, tool or protocol change. Nothing in the docs may start calling it a boundary.
- **Nothing inside the working directory starts failing**, apart from one recorded cost (see Open questions). These keep working:
  - nested `os.makedirs` and `shutil.rmtree`;
  - `tempfile` and its cleanup;
  - `chmod`, `utime` and `remove` of the run's own files;
  - a relative `./name`, and a folder written with a trailing slash (`mkdir('n/')`, `rmtree('p/')`, `rename('r/', 's/')`);
  - a cache under `HOME`, which is the working directory;
  - `subprocess` with `DEVNULL`, and writing to `/dev/null`;
  - removing a link inside that points outside.

  Renaming such a link **starts** working (the second defect).
- **The refusal's wording is unchanged**: `PermissionError: Apogee's code-exec sandbox writes only inside its working directory (<work>): '<path>' is outside it`. 47b's test and the user's transcripts read it.
- **Standard library only, Python 3.8 or later.** The bootstrap stays one string run with `-c` (`BOOTSTRAP`, `:81-183`), and an audit hook cannot be removed once added.
- **The fix reaches existing installs.** Seeding is skip-if-present, so the edited script goes through `scripts/generate_training_assets.py`:
  - **What it writes.** It rewrites `source/data/contracts/assets_mcp.cpp` and `assets_training.cpp` (`bundled_scripts_retired()`), and appends the committed copy's digest to `assets/retired-scripts.txt`.
  - **Why it must run first.** It takes that digest from `git show HEAD:` (`record_retired_scripts`, `:134-160`). So **run it before the first commit that touches the script, on any branch, the item's worktree branch included.** Otherwise it records an intermediate commit's digest instead of the shipped one.
  - **The line it must record** is `code_exec_server.py c23c9734926f4e8315c18a437632002a2488df01a69b7cb82bae4d8741d0631a`, the bytes every release from v0.1.9 through v0.2.0 seeded. The assets case (Testing) fails without it.
  - **What follows.** `refresh_seeded_scripts` replaces an install's unedited seeded copy at its next seeding. A user's edited copy stays theirs, as ever.

## Technical approach

Paths under `lib/src/cli/` (the reference under `lib/documentation/`). Ungated: it touches only 47b's script, its generated units, two tests and the reference.

| File | Change |
|---|---|
| `assets/mcp/code_exec_server.py` (inside `BOOTSTRAP`) | `_inside` gains the entry-itself judgment; `/dev/null` counts as inside only for `open`; a new `_judged(path, dir_fd)` gates the six events that carry a descriptor last; the guard's own `os.lchflags` name goes; the comment at `:138-139` is rewritten |
| `source/data/contracts/assets_mcp.cpp` | generated by `scripts/generate_training_assets.py`, never by hand |
| `source/data/contracts/assets_training.cpp` | generated (`bundled_scripts_retired()`), never by hand |
| `assets/retired-scripts.txt` | gains the shipped digest, written by the generator, never by hand |
| `tests/business/mcp/code_exec_server_test.cpp` | the `[M35]` cases (Testing) |
| `tests/data/contracts/assets_test.cpp` | the digest joins the list of earlier versions (Testing) |
| [`reference/cli/tools.md`](../../../reference/cli/tools.md#the-sandbox-said-plainly) | the Writes row and the best-effort paragraph (below) |

**The script, inside `BOOTSTRAP`:**

- **`_inside` gains the entry-itself judgment:** the path's folder resolved, its own last name kept. A path that ends in a separator, or whose last name is `.` or `..`, is judged whole, because the system may follow a final link written that way. Never strip the separator: a first prototype did, and `shutil.rmtree('link/')` then deleted an outside tree.
- **`/dev/null` counts as inside only for `open`**, so writing to it stays allowed while deleting it, changing its mode or owner, or linking or renaming over it is refused.
- **A new `_judged(path, dir_fd)` gates only the six events that carry a descriptor last.** A call is judged when its descriptor is -1 or `None`, or when its path is absolute, since a system call ignores `dir_fd` then. Every other event keeps today's rule: its paths are judged as written, whatever descriptor it carries. The arguments each event carries, from CPython's audit table and the probe:

  | Event | Arguments | Judged |
  |---|---|---|
  | `os.remove`, `os.rmdir` | `(path, dir_fd)` | the entry itself, through `_judged` |
  | `os.mkdir` | `(path, mode, dir_fd)` | the entry itself, through `_judged` |
  | `os.chmod` | `(path, mode, dir_fd)` | where the path leads, through `_judged` |
  | `os.chown` | `(path, uid, gid, dir_fd)` | where the path leads, through `_judged` |
  | `os.utime` | `(path, times, ns, dir_fd)` | where the path leads, through `_judged` |
  | `os.rename` (also `os.replace`) | `(src, dst, src_dir_fd, dst_dir_fd)` | both ends as entries |
  | `os.link` | `(src, dst, src_dir_fd, dst_dir_fd)` | `src` where it leads, `dst` as an entry |
  | `os.symlink` | `(src, dst, dir_fd)` | `dst` as an entry |
  | `shutil.rmtree` | `(path,)` before 3.11 (probed on 3.9 and 3.10), `(path, dir_fd)` from 3.11 | `args[0]` as an entry, never `args[-1]` |
  | `os.truncate`, `os.chflags` (also `os.lchflags`) | `(path, length)`, `(path, flags)` | where the path leads |
  | `os.setxattr`, `os.removexattr` (Linux) | `(path, attribute, …)` | where the path leads (new) |
  | `open` | `(path, mode, flags)` | where the path leads, `/dev/null` allowed |

- **The guard's own `os.lchflags` name goes**: CPython never raises it.
- **The comment at `:138-139` is rewritten** to say what is still not judged (see Open questions).

**The generated units:** `source/data/contracts/assets_mcp.cpp`, `source/data/contracts/assets_training.cpp` and `assets/retired-scripts.txt`, written by `scripts/generate_training_assets.py` and never by hand, run as Constraints says (before the first commit that touches the script).

**The reference:** [`reference/cli/tools.md`](../../../reference/cli/tools.md#the-sandbox-said-plainly). The Writes row and the best-effort paragraph say:
- what is refused: writing, deleting, making folders, changing permissions, owners, dates or extended attributes, renaming and linking;
- the cost: a link inside that points outside cannot have its mode, owner or dates changed, even without following it, so copying one with its metadata reports an error, though the copy is made;
- what is not judged: a call relative to a directory descriptor or made on an open one, `mkfifo` and `mknod`, what standard-library modules write through their own C code (`sqlite3`, `dbm`), and on Python 3.8.0 or 3.8.1 nearly everything but `open`.

**Reference.** 47b's own guard is the precedent: its `open` branch and its `write_input` check (`realpath`, then a prefix test against the working directory) are the shape kept. CPython's [audit events table](https://docs.python.org/3/library/audit_events.html) lists each event's arguments, the source of the table above. The file tools' `resolve_in_root` (`business/tools/fs.h:29-34`) judges a path not there yet "by where it will land", through its longest existing prefix. The entry-itself judgment shares only that idea. `resolve_in_root` follows every link, the last one included, which is the whole-path judgment kept here for the calls that act on what a path leads to.

**Docs when it ships:**
- a dated entry in Milestone W beside 47b, recording what the guard now judges, the cost and the per-version results;
- the CLI index's Maintenance preamble gains "M35 shipped <date>";
- the M35 row leaves the index and the [overview](../../README.md);
- ROADMAP's M35 line is ticked.

## Testing

The probe becomes the fixture, run on the host's `python3` through the shipped client like 47b's cases (skipped by name without one).

**POSIX only, in practice.** Apogee starts no child process on Windows yet (`supports_child_processes`, `child_process.cpp:280-286`; `start_child`, `:474-482`). So the server never runs there and the suite skips by name. No Windows handling is needed, and the separator rule is revisited when Windows gains child processes.

On Linux, `os.rmdir('link/')` may fail natively (`ENOTDIR`), so each case asserts the guard's refusal text first and what is on disk second: the hook runs before the system call.

New cases in `tests/business/mcp/code_exec_server_test.cpp` are tagged `[M35]` beside `[mcp][code-exec][python]`. Each case marked † is seen failing on today's guard before the fix.

- **Outside, refused**, with the refusal text naming the path and the target as it was on disk:
  - † `os.remove`, `os.unlink`, `pathlib.Path.unlink`: the file still there.
  - † `os.rmdir`: the folder still there.
  - † `os.mkdir('<outside>/../<sibling>')`: nothing made.
  - † `os.makedirs('<outside>/a/b/c')` and `tempfile.mkdtemp(dir=<outside>)`: the named path starts with `<outside>/` (`<outside>/a` and a random child), and nothing is made.
  - † `os.chmod`: the mode unchanged.
  - † `os.utime`: the date unchanged.
  - † `os.chown(…, -1, -1)`: the refusal text is the whole check, since the call changes nothing.
  - † `os.rmdir('link/')` through a link inside that points to an outside folder: the folder still there.
  - † `os.remove(<absolute outside path>, dir_fd=<a descriptor on the working directory>)`: the file still there.
  - † `os.remove(os.devnull)`.
  - `os.setxattr` and `os.removexattr` on an outside file, where `os` has them (Linux), skipped by name elsewhere.
  - `os.rename('link/', …)`, `os.rename(…, 'link/')` and `shutil.rmtree('link/')` through such links: refused today and still refused. These pin the separator rule against the first prototype's mistake.
- **Inside, still working:**
  - nested `makedirs` and `rmtree`;
  - `mkdir('n/')` and `rmdir('n/')`, `makedirs('p/q/')` and `rmtree('p/')`, `rename('r/', 's/')`;
  - `chmod`, `utime` and `remove` of the run's own file, and `./name`;
  - `tempfile.TemporaryDirectory()`;
  - writing to `/dev/null`, and `subprocess` with `DEVNULL`;
  - a link pointing outside, removed, and † renamed (the false refusal today);
  - † `shutil.rmtree` on such a link raises Python's own `OSError` (the guard's `PermissionError` today), and the folder it points at is untouched.
- **The cost, pinned on purpose** so it cannot drift either way:
  - `os.utime('<link to an outside file>', follow_symlinks=False)` is refused, naming the link.
  - `shutil.copytree(<folder holding such a link>, <dst>, symlinks=True)` makes the copy, link included, and raises `shutil.Error`.
- **The existing cases unchanged:** `open` refused, `/dev/null` and `DEVNULL` working, the refusal's text.
- **The assets** (`tests/data/contracts/assets_test.cpp`): `assets_test`'s byte match, and the shipped digest above joins the "every version an earlier Apogee seeded" list in "the compiled-in driver list names earlier versions, never this build's" (`:816`, the list at `:858-868`).

## Open questions

| Question | Status | Answer |
|---|---|---|
| Is a call on a directory descriptor judged? | `[default]` | A call on a directory descriptor stays unjudged, and the comment and tools.md say so. That covers a relative path with a real `dir_fd`, and a call made on an open descriptor (`os.chmod(fd, …)`, `os.utime(fd, …)`). Resolving a descriptor's folder (`fcntl`'s `F_GETPATH` on macOS, `/proc/self/fd` on Linux) could close those, but not `os.open(name, O_CREAT, dir_fd=…)`: CPython's `open` event carries no descriptor, so the class stays open whatever is done here. `shutil.rmtree`'s own descriptor walk issues these calls inside a top the guard already judged. |
| Are `os.mkfifo` and `os.mknod` wrapped, or only named? | `[default]` | `os.mkfifo` and `os.mknod` are named in tools.md as not judged, with no wrapper. Neither raises an audit event on any version probed. Wrapping the `os` functions would leave `posix.mkfifo` open, so a wrapper is effort without a promise. |
| Are writes `sqlite3` and `dbm` make through their own C code judged? | `[default]` | What `sqlite3` and `dbm` write through their own C code is named in tools.md as not judged. Judging `sqlite3.connect` was rejected: the event fires for reading too, and reading outside is allowed. |
| How are `os.chmod`, `os.chown` and `os.utime` judged when they may not follow a link? | `[default]` | `os.chmod`, `os.chown` and `os.utime` are judged by the file they lead to, even with `follow_symlinks=False`, or as `os.lchmod` or `os.lchown`. The event does not say which, so the guard takes the reading that refuses. Reading the caller's frame to tell the cases apart was rejected as fragile. **The cost, named, probed on all six versions on macOS:** **on every platform**, a no-follow `chmod`, `chown` or `utime` of a link inside that points outside is newly refused; **on macOS**, `shutil.copymode` and `shutil.copy` with `follow_symlinks=False` on such a link are newly refused (they reach `os.lchmod`), while `shutil.copy2(follow_symlinks=False)`, `copystat` and `copytree(symlinks=True)` already fail today, through `os.chflags`, and now fail earlier, at `os.utime`; **on Linux** (from CPython's source, not run), `copy2(follow_symlinks=False)`, `copystat`, `copytree(symlinks=True)` and 3.14's `pathlib.Path.copy(…, follow_symlinks=False, preserve_metadata=True)` newly end in an error, though the copy is made, and `copymode` stays a no-op there. |
| Does the Python 3.8 floor stay? | `[default]` | The 3.8 floor stays. On 3.8.0 and 3.8.1 the guard judges little beyond `open` and `os.truncate`, and tools.md says so in one clause. The floor is 47b's, and 3.8 has been end-of-life since 2024-10. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-10-18 | The branch shipped with 47b (`58f9e6b7`, v0.1.9), the only commit on the file | — |
| 2026-11-15 | Diagnosed by probe. The real server was run over MCP on Python 3.9.6–3.14.5, with a raw event dump on each. CPython's source was read at v3.8.0 (no events), v3.8.2 and v3.8.20 (-1) for the versions not on this Mac, and at v3.14.0 to confirm the line is unchanged. The bug was found while probing 57o's link gap (57y), whose "who can plant a link" probe reached the code server. | — |
| 2026-11-15 | The rename branch's false refusal is fixed here, not queued apart. It is the same function, and fixing the dead branch without it would extend the false refusal to `os.remove`. | the spec |
| 2026-11-15 | An absolute path given with a `dir_fd` is judged. The system ignores the descriptor then, so judging it needs no descriptor lookup, and leaving it would keep a hole with no reason behind it. | the spec |
| 2026-11-15 | `/dev/null` is allowed for `open` alone. Writing to it is what subprocess's `DEVNULL` needs. Deleting it or changing it never is, and as root (a Linux container) the system would not stop it. | the spec |
| 2026-11-15 | The set grows by `os.setxattr` and `os.removexattr` (Linux): they change a file outside as `chmod` does, and the hook fires before the filesystem is asked. The dead `os.lchflags` name goes. | the spec |
| 2026-11-15 | Two rounds of independent review shaped this document. **A fact-check of the first draft** confirmed the table and found the first prototype's trailing-separator mistake, the unjudged absolute path with a `dir_fd`, and the copy cost. **A three-lens review** of the second draft found: the cost reached beyond Linux copies (every no-follow `chmod`, `chown` or `utime` of an outward link); `/dev/null`'s exemption; writes through `sqlite3` and `dbm`; `shutil.rmtree`'s per-version arguments; the digest that must be recorded. All were folded in, and the prototype that does all of this (scratch, not the repository) was re-probed on all six versions. | the spec |
| 2026-11-15 | No gate change. The guard's severity is moderate because the gate is the boundary. That is the reason for a guard fix and an honest table, not for a new refusal path in the CLI. | the spec |
| 2026-11-15 | CLI Maintenance, as M35, claimable any time by name. It is release-agnostic, and since the gate is the boundary it need not hold v0.2.1's release. | the user |

## Out of scope

- Anything a subprocess or a C extension does (the guard is not a boundary, said so).
- The network guard, and the gate.
- Descriptor calls, `mkfifo`, `mknod`, and what `sqlite3` and `dbm` write (named, not closed; see Open questions).

## Links

- **Blocked by:** — · **Blocks:** —
- **Follows from:** 57o's link gap (57y), whose "who can plant a link" probe reached the code server
- **Relates to:** 47b, the code-execution server, and its record in [Milestone W](../../../assistant/cli/MILESTONES.md#milestone-w--the-mcp-client); [tools.md › the sandbox, said plainly](../../../reference/cli/tools.md#the-sandbox-said-plainly); [the modes](../../../reference/cli/machine-mode.md); CPython's [audit events table](https://docs.python.org/3/library/audit_events.html); the [backlog overview](../../README.md)
