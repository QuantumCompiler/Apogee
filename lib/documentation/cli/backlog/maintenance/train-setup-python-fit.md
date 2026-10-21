# `train setup` picks a Python every requirement set supports

**What / why.** On the user's Mac, a reinstall's `apogee bootstrap` (2026-10-21) finished 7 of 9 steps, and the voice set could never install:

```
[7/9] the 'voice' set -- apogee train setup --with voice
  environment already exists at /Users/taylor/.apogee/training/venv
  installing the 'voice' set: torch>=2.6 kokoro>=0.9.4 misaki[en]>=0.9.4 ...
  apogee train: pip install for the 'voice' set failed (exit code 1):
  ERROR: Ignored the following versions that require a different python version: 0.8.1 Requires-Python >=3.10,<3.13; … 0.9.4 Requires-Python >=3.10,<3.13
  ERROR: No matching distribution found for kokoro>=0.9.4
```

The environment had been built at step 1 (`prepare`) from `/opt/homebrew/bin/python3`, which is Homebrew's 3.14.5. Homebrew's `python3.10` through `python3.13` sit beside it, and `python3.12` fits every set.

The root cause was measured, not guessed (2026-10-21). The sources were read, and the real binary was probed against scratch `APOGEE_HOME`s, with no packages installed:

- **The pick asks no version.** `training::locate_base_python` (`source/business/training/python_env.cpp:266`) takes `training.python` when the config sets it. Otherwise it takes the first `python3`, then `python`, on PATH (`:286`). Probed: with nothing configured, `train setup` created the environment "from /opt/homebrew/bin/python3", and its `pyvenv.cfg` says `version = 3.14.5`.
- **The sets state no Python range.** They declare their packages and their hosts and nothing else (`python_env.cpp:17-54`). So nothing can choose against them, and the first sign of a misfit is pip's refusal, after the environment already exists.
- **An environment, once made, is kept whatever its Python.** `run_train_setup` (`source/presentation/cli/train.cpp:1225`) creates one only when none exists (`:1251-1262`), and otherwise says `environment already exists` (`:1264`). `require_python_env` (`train.cpp:1192`), which runs when another command first needs the environment, creates it the same way. Probed: with `training.python` set to `python3.12` over an environment already on 3.14, setup said "environment already exists" and `pyvenv.cfg` still read `version = 3.14.5`. The key is silently ignored, and the only way out is deleting the environment by hand.
- **No command reaches the escape hatch.** `config set training.python python3.12` is refused (`'training.python' is not a key 'config set' moves`). The key is documented only in `reference/training.md:50`. Written by hand into `config.json` before the environment exists, it works: probed, `python3.12` gave `version = 3.12.13`.
- **The doctor hides it.** `check`'s `python env` row (`source/presentation/cli/check.cpp:1973-1990`) names the sets and never the Python. Its `voice runtime` row (`check.cpp:1913-1919`) says "kokoro is not installed" and names `apogee train setup --with voice`, a remedy that cannot succeed on this environment.

**Blast radius.** These are PyPI's published `requires_python` values for each set's packages at their current releases (read 2026-10-21):

- **Only the voice set has an upper bound:** `kokoro` 0.9.4 `<3.13,>=3.10` and `misaki` 0.9.4 `<3.13,>=3.8`.
- **The lower bounds:**
  - `mlx-lm` 0.32.2 `>=3.11`;
  - `>=3.10` for `torch` 2.14.1, `mlx-vlm` 0.7.9, `diffusers` 0.41.0, `transformers` 5.20.2, `datasets` 5.1.0, `bitsandbytes` 0.50.2, `gguf` 0.19.0, `peft` 0.21.2 and `accelerate` 1.15.0;
  - `sentencepiece` `>=3.9`.

So every set fits only on 3.11 or 3.12. A machine whose `python3` is 3.13 or newer (Homebrew's default today) can never get voice, by any command. One on 3.10 silently gets an older `mlx-lm`. The other five sets installed and run on the user's 3.14.5 environment: prepare, mlx, convert, mlx-vlm and video. `check` reads them ok.

**The fix.**
- `train setup` chooses, from the interpreters on the machine, the newest Python that every set this host can install supports, and says which and why.
- `--python <path>` is the one-run spelling of `training.python`, and `config set` learns the key.
- An existing environment on a Python outside a requested set's range is refused before pip, naming the way out: a rebuild, asked first.
- `check` names the environment's Python, and the mismatch with a remedy that works.

**Core constraint(s).**
- **Never the system's Python, and never an installed one.** The choice is among interpreters already on the machine. When none fits, setup refuses, naming the range, what it found, and where an interpreter comes from (`brew install python@3.12`). It never downloads one.
- **One declaration.** Each set's supported range lives in the sets' table beside its packages and its host (`python_env.cpp`), with its reviewed date. That is ADR 0009's discipline for carried facts that go stale. Setup, `require_python_env`, the bootstrap and the doctor all ask the table; none keeps a copy.
- **The person's word stands.** An explicit interpreter, `training.python` or `--python`, is used as given. One outside a requested set's range is refused before any pip call, naming both, and is never silently swapped for another.
- **The doctor reads files and never runs Python**, as its runtime rows already do. The environment's version comes from `pyvenv.cfg` (`version = …`, written by `python -m venv`), and the record (`apogee.json`) gains `base_version` at creation.
- **Nothing is destroyed unasked.** An environment is rebuilt only on the person's word. A terminal asks `[y/N]`. A pipe refuses, naming the command, and so do the bootstrap's children, whose stdin is closed. So an unattended install never replaces gigabytes of packages on its own. A rebuild reinstalls every set the record lists, in its order.
- **The refusal stays one last line naming its command.** The macOS app's consent sheet reads the refusal's own words (GUI 1m), so the shape it reads keeps working.
- **The suite stays hermetic**: no real interpreter and no pip, through the recording `CommandRunner` and a scripted PATH. A new flag ships with its completion (ADR 0007): `--python` completes paths, `--rebuild` is a flag.

**Seam + files.**
- `source/business/training/python_env.h/.cpp`:
  - each set's Python range (`python_range(RequirementSet)`) and the reviewed date;
  - `locate_base_python` grows into a choice over candidates, returning the interpreter, its version and the reason;
  - the candidates are `python3.<minor>` on PATH, newest first, then `python3` and `python`, each asked its version through the runner (`-c "import sys; print('%d.%d.%d' % sys.version_info[:3])"`);
  - `PythonEnvStatus::base_version`, from the record, else `pyvenv.cfg`;
  - `PythonEnv::rebuild`: remove, create, and reinstall the recorded sets.
- `source/presentation/cli/train.cpp`:
  - `run_train_setup` and `require_python_env` say the choice and why;
  - `--python <path>`;
  - `--rebuild`, asked;
  - an environment outside a requested set's range is refused before pip.
- `source/presentation/cli/check.cpp`:
  - the `python env` row names the version;
  - a set's runtime row out of range (today the voice one) names the range and the rebuild remedy.
- `source/presentation/cli/bootstrap_cmd.cpp`: expected to need nothing. When setup chooses against every set this host can install, the first step (`prepare`) already creates the environment on the fitting Python. Verify it.
- `source/data/contracts/config_edit.cpp`: `training.python` joins `config set`'s scalar table (52e).
- Tests: `tests/business/training/python_env_test.cpp`, `tests/presentation/cli/check_test.cpp`, `tests/presentation/cli/train_test.cpp`.
- Docs: `lib/documentation/cli/reference/training.md`, the setup section. Its "nothing runs `setup` for you" line predates 51's bootstrap and is corrected on the same pass.

**Reference.**
- **M15's `--model-type`**: a refusal that names its own way out. The rebuild door follows it.
- **ADR 0009**: carried facts declared once, with a reviewed date. The ranges are such facts.
- **51's bootstrap**: never-fatal steps, run as the binary's children with stdin closed. That is where the symptom surfaced, and why the rebuild must refuse rather than ask there.
- **External prior art:**
  - `uv`'s interpreter discovery picks the newest installed interpreter that satisfies a project's `requires-python`. That is the same rule as here, over a declared range.
  - pip's own `Requires-Python` check is what the carried ranges mirror. Carrying them lets the choice happen before an environment exists, rather than after pip refuses.

**Decisions made** (dated):
- 2026-10-21 — Specced from the user's reinstall, their call: a CLI Maintenance item in which `train setup` picks a Python every set supports (or takes `--python`) and says why. The evidence is the bootstrap's log (7 of 9 steps, voice refused by pip) and the probes above.
- 2026-10-21 — Diagnosis, recorded as measured above:
  - the pick is by name alone (`python_env.cpp:286`);
  - the sets carry no range (`:17-54`);
  - an existing environment is reused whatever its Python, and a later `training.python` is ignored (`train.cpp:1251-1264`);
  - `config set` cannot write the key;
  - the doctor's voice remedy cannot succeed on such an environment (`check.cpp:1913-1919`).
- 2026-10-21 — The choice is made against every set this host can install, the bootstrap's own table, not only the sets asked for now. The environment is one and shared, and the bootstrap creates it at its first step (`prepare`), so a choice that only fits `prepare` dooms `voice` six steps later.

**Open calls:**
- [default: the ranges are carried in the sets' table with a reviewed date, mirroring PyPI's published `requires_python` for each set's packages (today `kokoro`/`misaki` `<3.13` and `mlx-lm` `>=3.11`), not asked of pip or PyPI at setup time. That keeps setup deterministic and offline. A stale range fails as today, in pip's words, and the reviewed date says when to look again.]
- [default: the candidates are the interpreters on PATH by name: `python3.<minor>` for every minor from 3.10 up to the newest found, then `python3`, then `python`, each asked its version through the runner. No scan of install prefixes beyond PATH (Homebrew's are on it), and no `pyenv` or `uv` integration.]
- [default: when no interpreter fits every set the host can install but one fits the requested sets, use the newest that fits the requested sets, and say which host sets it leaves out and why. When none fits the requested sets, refuse.]
- [default: the rebuild is `train setup --rebuild`, asked `[y/N]` at a terminal and refused on a pipe naming `--yes`. A rebuild without `--python` re-chooses the same way a new environment would.]
- [default: `training.python` joins `config set`'s scalar table (52e's editor), so the lasting choice has a command. `--python` writes nothing.]

**Guardrail(s).** The bug becomes a fixture:
- **The scripted interpreters.** A temp directory first on PATH holds fake `python3` (answering 3.14.5), `python3.13` (3.13.13) and `python3.12` (3.12.13). The recording runner answers each version probe and records every command:
  - a new environment on an Apple-silicon host is created from `python3.12`, and the output names it, its version and the voice set's `<3.13`;
  - with `training.python` naming `python3`, `--with voice` is refused before any pip command is recorded, naming 3.14.5 and `<3.13`.
- **The existing environment.** One whose record and `pyvenv.cfg` say 3.14.5, with five sets recorded:
  - `train setup --with voice` is refused before pip, with the rebuild door;
  - `--rebuild` on a pipe is refused, naming `--yes`;
  - with `--yes`, it removes, re-creates on 3.12, and reinstalls the five recorded sets and then voice, in that order (the recorded commands).
- **The doctor.** An environment whose `pyvenv.cfg` says `version = 3.14.5`:
  - the `python env` row names 3.14.5;
  - the `voice runtime` row names `<3.13` and a remedy that can succeed (`train setup --rebuild`), held by `check_test`.
- **The table.** Each set's range and the reviewed date are held to the carried table, as the roster test holds the carried model lists.

**Acceptance criteria:**
- [ ] The inverted symptom. On a host whose `python3` is 3.14 with `python3.12` beside it (the user's Mac), a fresh `apogee bootstrap` installs the voice set: 8 of 9 steps on Apple silicon, `peft` skipped for its host alone. `apogee check`'s `voice runtime` row is ok.
- [ ] `train setup` says which interpreter it chose, its version, and why it passed over any newer one.
- [ ] `train setup --python <path>` uses that interpreter for a new environment. One outside a requested set's range is refused before any pip call, naming both.
- [ ] An existing environment on a Python outside a requested set's range is refused before pip with the rebuild door. `--rebuild` asks, and reinstalls every recorded set. On a pipe, and under the bootstrap, nothing is replaced.
- [ ] `check` names the environment's Python version, and a set's row out of range names a remedy that succeeds.
- [ ] `config set training.python <path>` writes the key through the one editor.
- [ ] No command downloads an interpreter, and the suite stays hermetic.

**Scope note.** Maintenance item: claimable at any time by name, ungated. Out of scope:
- installing interpreters, which is Homebrew's or `uv`'s job;
- per-set environments: one environment stays the design;
- resolving ranges through pip or PyPI at run time;
- the macOS app's Video & voice pane. It shows the CLI's words, and its consent sheet reads the refusal; a GUI follow-up is needed only if those words change shape.

Until this ships, the user's machine gets voice one way only: write `"training": {"python": "python3.12"}` into `config.json` by hand, delete `~/.apogee/training/venv`, and run `apogee bootstrap` again.
