# Apogee GUI — Developer Reference

The package-by-package architecture reference for the GUI applications: every directory, file, interface, and build/test command. The contributor entry point is [CLAUDE.md](CLAUDE.md); the product spec is [SPEC.md](SPEC.md). The CLI's own reference — including the machine-mode protocol the GUI is built against — is [`cli/assistant/DEVELOPER.md`](../../cli/assistant/DEVELOPER.md) and [`cli/reference/machine-mode.md`](../../cli/reference/machine-mode.md).

---

## Layout

Nothing is built yet. The planned shape, per the repo's recorded convention for sibling applications (each self-contained, owning its own build, `source/` and `tests/` at its first level):

```
lib/src/darwin/     the macOS application
lib/src/linux/      the Linux application
lib/src/windows/    the Windows application
```

_TODO:_ the real package breakdown lands with the first application — including the first spike's answer on what is shared between the three apps (a machine-mode client core: spawning the `apogee` child, the JSONL session, tolerant event decoding, the cancel path).

## The contract the GUI builds against

- **The protocol:** [`machine-mode.md`](../../cli/reference/machine-mode.md) — the JSONL events, the driver's stdin lines (including `cancel`), the reads' `--output-format json` documents, task and play narration.
- **The schema:** `machine-schema.json` — printed by the binary (`apogee __machine-schema`), shipped in every CLI archive, and pinned to the doc and the implementation by `cli.machine_schema_conformance`.
- **Getting a CLI to drive:** a release install (`install.sh` / `install.ps1`), a source build (`make -C lib/src/cli install`, channels per the CLI docs), or CI's `apogee-<target>` artifacts.

## Build & test

_TODO:_ lands with the first application. When it does: the app joins `cicd.sh`'s `APPS` list, gains its own `lib/scripts/changed.sh` entry (what it is built from), and a CI pipeline of its own beside the CLI's — the repo-wide release law is in the [CLI docs](../../cli/assistant/CLAUDE.md#release-and-install-infrastructure).
