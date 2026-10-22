# `check` says every MLX runtime and its version

**What / why.** The MLX-for-every-kind track (53e–53m) added three Python sets and moved a floor:

| Set | Item | Requirement | Installed |
|---|---|---|---|
| `mlx-embeddings` | 53h | `>=0.1.0` | by the install's bootstrap on Apple silicon |
| `mlx-video` | 53k | GitHub, pinned at commit `87db56a5` (2026-05-13) | opt-in only |
| `mlx-audio` | 53m | `>=0.4.0` | opt-in only |
| `mlx-vlm` | 53f | floor raised from `>=0.3` to `>=0.6.1` | — |

`apogee check` does not follow, by the source (2026-10-22):
- **Missing rows.** Its MLX section reports the runtime, vision and embeddings rows (`source/presentation/cli/check.cpp`, around `:1996–2080`) and the seeded drivers (`mlx driver`, `mlx voice driver`, `embedding driver`). It has no row for mlx-video or mlx-audio. Its MILESTONES records say so (53k, 53l, 53m "Not done").
- **No version compared.** Nothing compares an installed version with its floor, or the GitHub install's commit with its pin (53f, 53h, 53k, 53m "Not done"). An environment set up before 53f keeps an mlx-vlm below 0.6.1 until `train setup --with mlx-vlm` runs, and nothing says so. mlx-audio 0.6.1 pins `mlx==0.32.4` exactly (53m's finding), so installing it can move mlx under the other sets, and nothing says that either.
- **Hearing not said.** A backend's hearing (53g) is said nowhere the way its vision is. `check` and `models info` say whether an `mlx` backend reads images, but not whether it hears (53g "Not done").
- **Stale help.** `train setup --with`'s help text lists the sets as they were before this track (53h "Not done").

**Core constraint(s).**
- **`check` reads files, never imports.**
  - An installed version comes from its `*.dist-info/METADATA` in the environment's site-packages.
  - A GitHub install's commit comes from pip's `direct_url.json` (`vcs_info.commit_id`, PEP 610).
  - Hearing comes from 53g's `probe_mlx_audio`, a files-only read.
- **A row says the way, as every `check` row does:** a set below its floor or off its pin names the `train setup --with <set>` that brings it up. An opt-in set that is absent is said as absent and not wanted, never as a warning, since nobody asked for it.
- **`check --fix` installs nothing unasked.** It never installs an opt-in set (mlx-video, mlx-audio): the SPEC's install amendment, 53k's "never at install unasked".
- **The machine-mode JSON grows additively** (ADR 0006).

**Seam + files.**
- **`source/business/training/python_env.h/.cpp`:** one reader of a set's installed version, or of its commit for a pinned source, from the environment's files, beside the floors and `PinnedSource` it already declares.
- **`source/presentation/cli/check.cpp`:**
  - MLX rows for mlx-video and mlx-audio;
  - each MLX set's installed version or commit against its floor or pin;
  - an `mlx` backend's hearing beside its vision.
- **`source/presentation/cli/models.cpp`:** `models info`'s line for an `mlx` backend says whether it hears, as it says whether it sees.
- **`source/presentation/cli/train.cpp`:** `train setup --with`'s help built from the one set list, so it cannot go stale again.
- **Tests:**
  - `tests/presentation/cli/check_test.cpp`: each set's row present, current, below its floor, off its pin and absent, over environments written by the test (`dist-info` and `direct_url.json` files only);
  - `tests/business/training/python_env_test.cpp`: the version reader;
  - the help's set list held to the declared sets.

**Reference.** 53f (mlx-vlm's floor and its reviewed date), 53h, 53k (`PinnedSource`, `set_opt_in`), 53m (mlx-audio and its mlx pin), 53g (`probe_mlx_audio`), M19 (the Python range rule), M11 (installs end green). [PEP 610](https://peps.python.org/pep-0610/), the `direct_url.json` record pip writes for a VCS install.

**Decisions made** (dated):
- 2026-10-22 — **Opened from the MLX-for-every-kind run's record** (the user's call: "Queue the MLX track's follow-ups as backlog items").

**Open calls:**
- [default: a set below its floor or off its pin is a warning, never a failure. The run still works where the older library does, and the row names the upgrade.]
- [default: an opt-in set that is not installed reads as "not installed (opt-in)", skipped and not warned, so a fresh install still ends `check` green (M11).]

**Guardrail(s).**
- **A fresh install's `check` ends with zero warnings** (M11's rule), on an environment with no opt-in sets.
- **Each row, from environments the test writes:** present and current; below the floor (mlx-vlm 0.3.x); the GitHub install off its pin; absent. Each says its way.
- **`check` spawns no Python for these rows.** Its existing no-spawn checks are extended.

**Acceptance criteria:**
- [ ] `check` says mlx-vlm, mlx-embeddings, mlx-video and mlx-audio each present or absent, at what version or commit, and against what floor or pin, naming the setup that brings one up.
- [ ] `check` and `models info` say whether an `mlx` backend hears.
- [ ] `train setup --with`'s help lists every set it takes.
- [ ] `lib/scripts/cicd.sh --test` is green.

**Scope note.** Maintenance, claimable by name, no gate. Upgrading an environment is out of scope; it stays `train setup`'s, on the person's word (M19).
