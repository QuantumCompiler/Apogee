# Install channels: dev, test and release roots

**What / why.** The thorough fix behind [M9](reset-keep-models.md)'s ask (the user, 2026-10-04): stop dev and test state from ever sharing the real root. Three **install channels** for building from source, each with its own data directory — **release** → `~/.apogee` (the real thing, today's behavior and the default), **dev** → `~/.apogee-dev`, **test** → `~/.apogee-test` — chosen at install time and **baked into the binary**, so an installed executable immediately knows which root it owns with no environment set. The make targets keep their names and the channel is the argument fed after make (`make install MODE=dev`, `make uninstall MODE=dev` — `make` has no long options, so `MODE=<channel>` is the make-native spelling of the ask's `--dev`); `uninstall` then removes **the respective directory**, because the channel binary's own `apogee_home()` already points there — the existing flags and prompts carry unchanged. On top of the baked default, the user can point any binary elsewhere at runtime: **`--dev` / `--test` / `--release`** as global flags for the common roots, and **`--custom <config file>`** for a completely unique location — the root derived from the config file's position per the layout rule. Much of the runtime half is nearly shipped machinery, found before specing: a global `--config <path>` exists today (`root.cpp`), and `home_for_config()` (`paths.cpp`) already derives a root as the config's parent-of-parent — what's missing is the baked channel (today `~/.apogee` is hard-coded as the env fallback), the shorthand flags, and **one resolution chain** that re-roots the *entire* layout, not just the config read.

**Core constraint(s).**
- **One resolution chain, declared once:** `apogee_home()` stays the single seam every layout row flows through; its order becomes **`--custom`/channel flag → `APOGEE_HOME` env → the baked channel's root** (explicit beats ambient beats built-in). A flag re-roots *everything* — models, chats, secrets, cache, config — never just the config read; a root chosen two ways that disagree is refused, not guessed.
- **The channel is a build fact, visible everywhere:** a CMake-level channel (compile definition) sets the baked root; `apogee --version` and `check` name the channel and the **resolved** root with which rung chose it (the roles-resolver honesty, applied to paths), so "which Apogee am I talking to" is never archaeology.
- **Channels coexist:** a non-release channel installs its binary under a suffixed name (`apogee-dev`, `apogee-test`) beside the release `apogee` *(recorded as the default, vetoable)* — three channels that overwrite one binary would make the baked root a lie. Completion stubs stay release-only (stubs invoke `apogee` by name; suffixed stubs are litter until someone asks).
- **Flags are session-scoped and never persisted:** pointing a release binary at the dev root with `--dev` is that invocation's affair; nothing writes the choice anywhere. Uninstall ignores the runtime flags and removes its **baked** channel's root — a release uninstall must never delete `.apogee-dev` because a flag was in the air.
- **Install modes unaffected where they stand** ([ADR install-mode-stability](../../adrs/cli/install-mode-stability.md)): curl/PowerShell installs are release-channel by definition and byte-identical to today; CI builds release (`cicd.sh` default unchanged); the asset set is identical across channels (the no-silent-install-drift non-goal — a channel changes the root, never the contents).
- **Secrets hygiene per root:** each root carries its own 0600 store; nothing ever falls back to another root's secrets.
- Code style carries: `.h`/`.cpp` pairs, smart pointers only; tests in the layer of what they test ([ADR](../../adrs/cli/tests-mirror-architecture.md)).

**Seam + files.**
- `data/contracts/paths.h/.cpp`: the chain — the baked channel constant replacing the hard-coded `".apogee"`, the override injection point for the flags; `home_for_config()` promoted to the one `--custom` derivation, refusing a path that doesn't sit at a layout-shaped `<root>/config/<file>` position *(the default below)*.
- `presentation/cli/root.cpp`: the global `--dev`/`--test`/`--release`/`--custom <path>` flags beside the existing `--config` (whose relation is stated in help: `--config` names a file, `--custom` names a file **and** re-roots the layout to match); mutual exclusivity enforced; completion per [ADR tab-completion](../../adrs/cli/tab-completion.md).
- `lib/src/cli/CMakeLists.txt`: the channel option (default `release`), the output-name suffix for non-release channels.
- `lib/src/cli/Makefile`: `MODE=<channel>` plumbed through `install` / `uninstall` (the uninstall target invoking the channel binary's own `uninstall`, prompts intact) — and [M9](reset-keep-models.md)'s `reinstall` inherits `MODE` for free, since reset rides `apogee_home()`.
- `presentation/cli/check.cpp` + the version line: channel and resolved root named, with the rung that chose it.
- [DEVELOPER.md](../../assistant/DEVELOPER.md): the channels section — what each is for, the coexistence story, the one-chain precedence.
- Tests: `tests/data/contracts/` precedence tables (every rung × overrides, the disagree-refusal, the custom derivation and its refusal); `tests/presentation/cli/` flag goldens and check/version wording; the install-parity suite extended with a channel build.
- Consumes: the layout contract (shipped — all rows already flow through `apogee_home()`); uninstall's plan machinery (shipped); [M9](reset-keep-models.md)'s reset (interplay: per-root automatically, not a gate).

**Reference (Ommi).** No analog — Ommi has one root (`~/.ommi`), no channels, no root override beyond its own environment handling; its Makefile-shell uninstall is the drift lesson already recorded in Apogee's uninstall header. The in-house precedent is the `APOGEE_HOME` env override (`paths.cpp`), which every sandboxed probe and test already relies on — the chain extends it on both sides rather than replacing it.

**Decisions made** (dated):
- 2026-10-04 — Asked for by the user as the thorough fix for dev/test state sharing the real root; placed in **Maintenance** (their call), M10 by the ever-assigned rule.
- 2026-10-04 — The ask's `make --dev` lands as `MODE=dev` *(make-native; recorded as the translation, the targets keep their names and the mode is the argument fed after make — the ask's own framing)*.
- 2026-10-04 — Baked-at-build via a compile definition rather than an install-time marker file: the binary must know its root "immediately", with no file read before the root is known — a marker *in* a root begs the question of which root to read it from.
- 2026-10-04 — Precedence is explicit-beats-ambient-beats-built-in: `--custom`/channel flag, then `APOGEE_HOME`, then the baked channel. The env rung keeps every existing sandboxed test and probe working untouched.
- 2026-10-04 — `--custom` takes a **config file path** (the user's spelling) and derives the root per the layout rule *(recorded as the default, vetoable)*: `home_for_config()` exists and the one layout declaration already fixes where a config sits in a root; a file at a non-layout position is refused with the expected shape named, never half-rooted.

**Open calls:**
- [default: non-release binaries are suffixed (`apogee-dev`, `apogee-test`) and skip completion stubs — coexistence without litter; a shared-name install is the veto path if side-by-side is not wanted]

**Guardrail(s).**
- The precedence tables: every rung alone and every pairing, including flag-vs-env disagreement refused with both named; no-flag/no-env resolves to the baked channel's root (per channel, compile-tested).
- The re-root pin: with `--custom`, every layout row resolves under the derived root — asserted by enumerating `layout.h`'s rows, so a future row cannot half-escape (the no-second-list discipline).
- Channel uninstall: a dev-channel sandbox uninstall plans `.apogee-dev` and never the release root, runtime flags present or not.
- Install parity: the existing two-install diff extended with a `MODE=dev` install — identical tree, different root, suffixed binary.
- Check/version goldens per channel: the channel, the root, and the rung that chose it.

**Acceptance criteria:**
- [ ] `make install MODE=dev` installs `apogee-dev`, which with no environment and no flags reads and writes `~/.apogee-dev` — `check` says `channel: dev`, the root, and why; the release install is byte-identical to today's.
- [ ] `make uninstall MODE=dev` removes the dev binary and (confirmed) `~/.apogee-dev`, leaving `~/.apogee` and the release binary untouched; the respective behavior holds for each channel.
- [ ] `apogee --test models list` reads `~/.apogee-test`; `apogee --custom /elsewhere/config/config.yaml chat` runs fully rooted at `/elsewhere` (models, chats, secrets included); a `--custom` path off the layout shape is refused naming the expected one.
- [ ] `APOGEE_HOME` behaves exactly as today when no flag is given; a flag plus a disagreeing `APOGEE_HOME` is refused with both named.

**Scope note.** Maintenance item **M10**; gated on nothing ([M9](reset-keep-models.md) is interplay — reset is per-root through the one seam — not a gate). Out of scope: channel-aware curl/PowerShell installers (release-only by definition; a later deliberate item if ever); per-channel config semantics (a root is a root — nothing behaves differently inside one); migrating state between roots; suffixed completion stubs (noted default).
