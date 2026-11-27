# CLI-64a · `apogee update` brings the installed binary to the latest release

| Field | Value |
|---|---|
| **Type** | ✨ Feature |
| **Project** | CLI |
| **Status** | 🟢 Ready |
| **Fix version** | v0.2.2 |
| **Component** | `apogee update` (a new command), the install paths |
| **Labels** | `self-update` `install-paths` `release-channel` |
| **Epic** | Track 64 · Self-update |
| **Blocked by** | — |
| **Blocks** | — |
| **Reporter** | The user (2026-11-26), from the ROADMAP's self-update idea (deferred 2026-09-01) |
| **Created** | 2026-11-26 |

---

## Summary

Apogee has no way to update itself: the only upgrade is re-running the installer one-liner, and the published notes list "No self-update yet" among the known limitations. This item adds `apogee update`, the third install path beside `install.sh` and `install.ps1`: it fetches the release for this machine's target, checks it against the digest GitHub publishes, replaces the installed binary in place, and repairs the install through `apogee check --fix`, as both installers do. A once-a-day notice on an interactive terminal says when a newer release is out.

## Description

### User story

**As** a person running Apogee from a release install,
**I want** to type `apogee update` to move to the latest release, or `apogee update --check` to see whether one is out,
**so that** upgrading needs no installer URL, and the install it leaves is exactly the one a fresh install would.

### Background

The ROADMAP has carried the idea since the v0.1.0 cut, where it was deliberately deferred (2026-09-01) until a distribution host and a release pipeline existed. Both now exist. The ROADMAP's note fixes its shape: it is the **third parity path** alongside the two installers, so it repairs through `apogee check --fix` like they do rather than seeding anything itself, and a background update notifier (terminal only, a 24-hour cache, a skip-list of subcommands) rides with it.

The user asked on 2026-11-26 how to update Apogee from the command line, was told to re-run the installer, and asked for this item, placing it in v0.2.2 as its own track.

The shape, in brief:
- **`apogee update`** resolves the target release (the latest, or the one `--version` names), finds this machine's archive among its assets, downloads it to a staging folder, checks its sha256 against the release's published digest, unpacks the binary, replaces the running one in place, refreshes the completion stubs an installer put down, and hands off to the new binary's `check --fix` and then `bootstrap`, as the installers end.
- **`apogee update --check`** reports the installed and the latest version and changes nothing; with `--output-format json`, as a document.
- **The notifier**: after a command on an interactive terminal of the release channel, at most once a day, one line on stderr when a newer release is out, naming `apogee update`.

### Evidence

1. **No update command exists.** The v0.2.1 build tree's binary (`lib/src/cli/build/macos-arm64/source/apogee`, `33ea685d`, read 2026-11-26) lists no `update` in `--help`. The v0.2.1 release notes drafted the same day carry "**No self-update yet.** Reinstall through the installer to upgrade." among their known limitations, as every release's notes have.
2. **The user's own install is behind.** `~/.local/bin/apogee --version` printed `apogee 0.2.0 (fc380b4, 2026-10-31, macos-arm64)` on 2026-11-26, the day v0.2.1 was published.
3. **The deferred idea.** The CLI ROADMAP's ideas list (2026-11-26): "**Self-update (`apogee update`)** — deliberately deferred out of v0.1.0 *(decided 2026-09-01)*, and now unblocked: the distribution host is settled and the release pipeline exists. It becomes the **third parity path** alongside the two installers, so it must go through `apogee check --fix` like they do rather than seeding anything itself. Needs its own backlog item; the background update notifier (TTY-only, 24h cache, subcommand skip-list) rides with it."
4. **ADR 0005 already names the path.** [install-mode-stability.md](../../../adrs/cli/install-mode-stability.md): "Apogee reaches machines three ways … **by self-update** (`apogee update`, replacing the installed binary from the release the pipeline published)", and "the installer entry points …, the archive names and layout, and the `update` path are a public contract".
5. **What the installers do** (read 2026-11-26):
   - `lib/scripts/install.sh`: the release is `APOGEE_VERSION`, else `latest` (`:43`); the archive comes from `releases/latest/download/<archive>` or `releases/download/<tag>/<archive>` (`:112–114`); the binary is installed with mode 0755 to `$PREFIX/bin/apogee` (`:128–129`, `PREFIX` defaulting to `~/.local`, `:42`); the completions follow (`install_completions`, `:158`); then `apogee check --fix`, a failure fatal (`:136`), and `apogee bootstrap`, a failure a note (`:143`), skipped by `APOGEE_MINIMAL_INSTALL=1`.
   - `lib/scripts/install.ps1`: the same steps under `$env:LOCALAPPDATA\Programs\Apogee` (`:39`), the archive URLs at `:79–81`, `check --fix` at `:118`, `bootstrap` at `:125`.
   - **Neither checks a checksum.** No release ships one.
6. **GitHub publishes each asset's sha256.** The release API for v0.2.1 (`api.github.com/repos/QuantumCompiler/Apogee/releases/tags/v0.2.1`, read 2026-11-26) gives every asset a `digest` field, `sha256:<hex>`. The sizes, in bytes: `apogee-macos-arm64.tar.gz` 14,444,313; `apogee-windows-x64.zip` 18,253,241; `apogee-windows-arm64.zip` 13,657,021; `apogee-linux-x64.tar.gz` 308,966,596; `apogee-linux-arm64.tar.gz` 297,568,016; and `apogee-gui-darwin.zip` 6,142,430, the macOS app, which this item does not touch.
7. **The binary knows its channel, its root and its own path.**
   - `apogee version` on the build tree's binary (2026-11-26): `channel: release` and `root: /Users/taylor/.apogee (the release channel's own root, baked into this build)`. The channel is baked at build time from `APOGEE_CHANNEL`, one of `release`, `dev` or `test` (`source/main.cpp:17–29`), and `make install MODE=dev` builds `apogee-dev`.
   - `platform::executable_path()` (`source/infrastructure/platform/platform.h:99`) finds the running binary on every platform, and `platform::running_binary_deletable()` (`:158`) says whether it can remove itself while it runs.
8. **Replacing a running Windows binary has a precedent.** `apogee uninstall` renames its running `apogee.exe` aside when Windows will not delete a running program, and says when it goes (`source/presentation/cli/uninstall.cpp:175`, `:216–270`; the CLI's 57k).
9. **The download path exists.** The libcurl client is `source/data/transport/http_client.{h,cpp}`. `models pull` downloads through `business/models/acquire.h`'s `acquire_file` (`:114`), into staging that a stop removes.
10. **The Windows build starts no child process.** Since the CLI's 57k, a build that cannot start programs offers no `run_command` or git tool, and the vendor-CLI backends refuse there. So on Windows, `apogee update` cannot run the new binary's `check --fix` as the installers do.
11. **Every top-level command is classified for the full-screen shell.** `source/presentation/cli/tui_parity.cpp` places each command; `uninstall` and `reset` are carved out with a reason (`:101–102`) and an exec-line refusal (`:120–125`).
12. **The install-parity gate holds the paths to one tree.** `tests/scripts/sh/install_parity.sh` installs by two paths, and by a third for a channel (M10), into throwaway roots, and requires their sorted listings to be identical; each path goes through the binary, never a list written in shell.
13. **The GUI's parity table follows the binary's word list.** [The GUI's parity ADR](../../../adrs/gui/parity.md) holds the app's table to `apogee __complete ""`'s top-level words, recorded as a fixture and refreshed deliberately: "A command a new CLI adds fails the app's suite the day the fixture is refreshed against that CLI, until it is classified."

## Acceptance criteria

- [ ] **Given** a release-channel install of an older release, **when** the person runs `apogee update`, **then** it names the installed and the target version, downloads this target's archive, checks its sha256 against the release's published digest, replaces the binary at its own path, refreshes the completion stubs an installer put down, and runs the new binary's `check --fix` and then `bootstrap`; afterwards `apogee version` names the new release.
- [ ] **Given** the installed release is the latest, **when** `apogee update` runs, **then** it says so and changes nothing (exit 0).
- [ ] **Given** any install, **when** `apogee update --check` runs, **then** it prints the installed and the latest version and whether an update is available, changes nothing, and with `--output-format json` prints one document saying the same.
- [ ] **Given** `apogee update --version <tag>` naming a published release, **then** that release is installed; an older one than the installed one asks first at a terminal (`--yes` answers), and is refused when nobody can be asked.
- [ ] **Given** a download whose sha256 differs from the published digest, or a release that publishes none, **then** the update is refused naming the archive and both values, nothing is replaced and the staging is removed.
- [ ] **Given** no network, a release with no archive for this target, or a rate-limited GitHub API, **then** the update is refused in plain words (the rate limit's with when to try again), and nothing changes.
- [ ] **Given** a stop (Ctrl-C or SIGTERM) at any step before the replacement, **then** the staging is removed, the installed binary is untouched, and the run exits 130 (143 for SIGTERM), as the store's verbs do.
- [ ] **Given** the replacement step, **then** no moment leaves a half-written binary at the installed path: on macOS and Linux the new file is written beside it and renamed over it; on Windows the running `apogee.exe` is renamed aside first, as `uninstall` does, and the aside copy is removed on a later run, said when it is.
- [ ] **Given** Windows, where the binary starts no program, **when** the binary is replaced, **then** the update ends by naming the two commands to run, `apogee check --fix` and `apogee bootstrap`, rather than running them.
- [ ] **Given** a `dev` or `test` channel binary, **then** `apogee update` is refused, naming `make install MODE=<channel>` as that channel's way to update.
- [ ] **Given** a binary in a folder the person cannot write, **then** the update is refused naming the path and the installer line, and nothing changes.
- [ ] **Given** an interactive terminal of the release channel with the notifier on, **when** any command but those on the skip-list ends, **then** at most once in 24 hours it checks for a newer release, and when one is out prints one line on stderr naming it and `apogee update`. It never prints in machine mode, under `--output-format json`, in `serve`, `mcp serve` or `acp`, for `update` or `version`, or off a terminal, and the setting that turns it off is honoured. It is on by default (the Decision log, 2026-11-26).
- [ ] **Given** the full-screen shell, **then** `update` is carved out with its reason and refused by the exec line in its words; **and given** completion, **then** `update`'s flags complete as every command's do (ADR 0007), `--version` offering nothing rather than making a network read.
- [ ] **Given** the install-parity gate, **then** a tree updated from the previous release's archive by `apogee update` equals a fresh install's tree.

## Constraints

- **The update path is a public contract (ADR 0005).** Its target resolution, archive names and the steps it ends with mirror the installers'. A change to the installers' steps changes the update's in the same change, and the reverse.
- **One layout, through the binary.** The update never creates a folder or file of the install itself: the new binary's `check --fix` does (no silent install drift, the SPEC's non-goal; `cli.install_parity`). A file the notifier keeps goes in the one layout declaration (`source/data/contracts/layout.h`).
- **Production is the user's.** The release `apogee` over `~/.apogee` changes only by what a release ships (the router's "Development runs on the dev channel"). No test or agent run ever runs `apogee update` against `~/.local/bin`, `~/.apogee` or the user's real `PATH`; tests use scratch prefixes and roots and fake hosts, and the first real update is the user's.
- **Verified before replaced.** The archive's sha256 must equal the digest the release publishes before a byte of the installed binary changes; there is no fallback to an unchecked download. HTTPS only.
- **Atomic in place.** Never a half-written binary at the installed path; never a replacement on a refusal or a stop.
- **No secret is read.** The release lookup is GitHub's unauthenticated API; no token, `gh` login or credential file is read for it (the SPEC's secrets principle).
- **Local by default.** The notifier is the one automatic network read Apogee would make; it is a request for the release document, sends nothing of the person's, and is said in the docs with the way to turn it off.
- **Mode parity.** The full-screen shell carves it out (ADR 0010); completion offers what it takes, and nothing that would need the network (ADR 0007); `--check`'s JSON document is additive (ADR 0006).
- **Code style and layers.** The four-layer law and the module map hold for any new module (ADR 0001); clang-format through the Makefile's fallback; warning-free on every CI compiler.

## Technical approach

Paths under `lib/src/cli/`.

| File | Change |
|---|---|
| `source/presentation/cli/update_cmd.h/.cpp` (new) | `UpdateCommand`: `--check`, `--version <tag>`, `--yes`, `--minimal`, `--output-format`; the plan said before anything changes, the run, the report and the refusals, in the house voice. |
| `source/presentation/cli/registry.cpp` | Registers `UpdateCommand` beside `UninstallCommand`. |
| `source/presentation/cli/tui_parity.cpp` | `update` carved out ("replaces the running binary") with its exec-line refusal, as `uninstall` is. |
| A Business module for releases (new; its name and place are the build's, declared in the module map) | The release lookup (the release document, the asset for this target, its digest), the version comparison, the download into staging through `acquire_file`, the digest check, and the unpacking of the one binary from `.tar.gz` or `.zip`. |
| `source/infrastructure/platform/platform.h/.cpp` | Replacing the running binary: write beside and rename over on POSIX; on Windows, rename aside and place, sharing `uninstall`'s aside naming; removing a leftover aside copy. |
| `source/data/contracts/layout.h` | The notifier's last-check record, if it needs a path the layout does not already give. |
| The notifier's hook, where a command ends | The once-a-day check on a terminal of the release channel, the skip-list, the setting that turns it off. |
| `source/presentation/cli/` completion | `update`'s flags. |
| `tests/presentation/cli/update_cmd_test.cpp`, the new module's tests (new) | Below. |
| `tests/scripts/sh/install_parity.sh` | The update path joins the parity gate. |
| `tests/fixtures/` (new files) | The v0.2.1 release document recorded from the API, and planted archives built by the test from a stub binary. |

**The steps, in order.** Refuse first: the channel, a binary that cannot be replaced, `--version` naming no release, an older release nobody confirmed. Then resolve, download, verify and unpack into staging under the root's temporary folder, said as each runs. Replace. Refresh the completion stubs at the paths an installer writes, the list `uninstall` already knows, writing only where a stub is already present. Then run the new binary's `check --fix` and `bootstrap`, or on Windows name them. The target names follow the pipeline's `ALL_TARGETS` (`lib/scripts/cicd.sh`): `apogee-<target>.tar.gz`, `apogee-<target>.zip` for Windows.

**Reference.**
- The installers, `lib/scripts/install.sh` and `install.ps1`: the steps mirrored.
- `apogee uninstall`, the CLI's 57k: a running Windows binary renamed aside, and the completion paths.
- `models pull`'s `acquire_file`: staging a stop removes.
- M10's install channels and `cli.install_parity`.
- External prior art: `gh` and Homebrew check for a newer release once a day on an interactive terminal and print one line; `rustup self update` replaces its own binary in place. Divergence: Apogee checks the digest GitHub publishes and refuses without one.

**Docs when it ships:**
- `README.md`'s install section gains the upgrade line.
- ADR 0005's "by self-update" stops being a promise.
- The CLI's DEVELOPER.md: the install channels' and the release's sections.
- The next release's notes drop "No self-update yet".

## Testing

Hermetic, as every CLI test: a fake release host through an injected client, planted archives, scratch prefixes and roots. Never the network, never the user's install.

- **Unit, the release logic.** Each target's asset name. The version comparison: latest, same, older, a named tag with and without its `v`. The digest check: match, mismatch, none published. A rate-limit answer read as one. A release with no archive for this target.
- **Unit, the command.** The plan's words. Each refusal: dev and test channels, an unwritable folder, an older release unconfirmed and when nobody can be asked, a mismatched digest. `--check` as text and JSON. A stop at each step before the replacement, leaving the staging gone and the binary untouched.
- **Unit, the platform seam.** POSIX's write-beside-and-rename. Windows' aside and its later removal, on CI's Windows rows.
- **Unit, the notifier.** Its cadence over a fake clock, terminal only, the skip-list, the setting off, and the channel gate.
- **The executable tier.** `cli.install_parity` gains the update path: a scratch prefix installed from a planted older archive, updated by the binary from a planted newer one through the fake host, its tree equal to a fresh install's. A planted mismatched digest leaves the installed binary byte for byte.
- **Gate:** `lib/scripts/cicd.sh --app cli --test`.
- **The live proof is the user's:** the first real `apogee update`, from the release this ships in to the one after it.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Does an update run the bootstrap? | `[default]` | Yes, after `check --fix`, as both installers end, skipped by `--minimal` and by `APOGEE_MINIMAL_INSTALL=1`, the installers' own switch. A bootstrap failure is a note, as the installers make it. |
| May `--version` name an older release? | `[default]` | Yes. At a terminal it asks first; `--yes` answers; with nobody to ask it is refused. The installers already install any named release. |
| How is the archive unpacked? | `[default]` | In the binary, with no new dependency where the build can avoid one: the `.tar.gz` through a zlib the build already links (libcurl's), and the `.zip`'s stored or deflated entry the same way. If no linked inflater is usable on every CI target, the build raises the dependency as a `[user]` call before adding one. |
| Which completion stubs are refreshed? | `[default]` | Only those already at the paths an installer writes, the list `uninstall` removes; none is created for a shell that has none. |
| What does the notifier keep, and where? | `[default]` | One small record of the last check and the release it saw, under the root through the layout declaration, so `reset` and `uninstall` take it as they take the rest. |
| Is the item one session's work? | `[default]` | One item. If the build finds the notifier makes it too big, it splits the notifier into 64b at the build's start, recorded here and in the index. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-09-01 | Self-update deferred out of v0.1.0 until a distribution host and a release pipeline exist. | the user (the CLI ROADMAP) |
| 2026-11-26 | Specified as its own item: "spec apogee update into the CLI backlog". | the user |
| 2026-11-26 | Placed in v0.2.2 as its own track, 64, ungated. | the user (asked, of v0.2.2, Maintenance or v0.2.3) |
| 2026-11-26 | It repairs through the new binary's `check --fix` and seeds nothing itself, and the notifier rides with it, as the ROADMAP's note recorded. | the spec, from the user's recorded note |
| 2026-11-26 | The download is checked against the sha256 GitHub publishes for each asset before anything is replaced: no release ships a checksum, and the installers check none. | the spec |
| 2026-11-26 | **The update notifier is on by default**: only on an interactive terminal of the release channel, at most once a day, one line on stderr, naming `apogee update`; off with `apogee config set update.check false` or `APOGEE_NO_UPDATE_CHECK=1`; said in the README. It is the one automatic network read Apogee makes, a request for the release document that sends nothing of the person's. | the user (asked at the start of the v0.2.2 maintenance run) |

## Out of scope

- Updating the macOS app (`Apogee.app`) or the Claude plugin; each is its own application's concern.
- Installing an update unasked. The notifier only says one is out.
- Signing or notarizing the binary.
- Changing what `install.sh` or `install.ps1` do. A change there is ADR 0005's deliberate act, and this item mirrors them as they are.
- Delta or partial downloads; resuming an interrupted download.
- Updating a `dev` or `test` channel binary, which is built from source.

## Links

- **Blocked by:** — · **Blocks:** —
- **Relates to:** [ADR 0005](../../../adrs/cli/install-mode-stability.md) (the update path as public contract), [ADR 0002](../../../adrs/cli/mode-parity.md), [ADR 0007](../../../adrs/cli/tab-completion.md), [ADR 0010](../../../adrs/cli/the-shell-is-a-mode.md); the CLI's 57k (`uninstall`'s aside rename) and 57l (the install scripts finish clean); M10 (install channels).
- **Follow-on:** the Apple apps' parity table classifies `update` the day its word-list fixture is refreshed against a CLI that has it ([the GUI's parity ADR](../../../adrs/gui/parity.md)); a place in the app for it is the app's own item, if wanted.
