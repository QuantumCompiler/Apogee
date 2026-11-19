# The old prototype's baseline

**What / why.** The React app is checked against the old prototype, screen by screen ([parity-proof.md](parity-proof.md)), and then the old files are deleted ([retire-the-old-prototype.md](retire-the-old-prototype.md)). So the old prototype's look must be captured first, at one recorded revision, in a way that can be re-made after the files are gone. This item takes that picture and freezes the old prototype. Measured 2026-11-14:
- **The start presets.** The recon read 65 `start` options in `main.html`'s `data-props`: 59 with a Screen board and 6 without (`chat:compacting`, `library:unsaved`, `models:notran`, `settings:fixing`, `settings:fixfailed`, `settings:nosearch`). The count is moving:
  - at HEAD (`48f46831`): 57 presets (`data-props` at `main.html:2462`), 56 Screen boards, 1 without a board (`library:unsaved`);
  - in the working tree, re-checked while writing this: 70 presets (`main.html:2497`), 61 boards, 9 without (the 6 above plus `tasks:more`, `tasks:resuming`, `tasks:resumerefused`).
  - So the baseline takes whatever list the chosen revision's `data-props` holds, never a number written here.
- **Another session's design work is not committed.** The recon saw +391/−57 lines across `main.html`, `BuildContract.dc.html`, `README.md` and `canvas.json`, and three untracked boards (`ScreenSessionEnded`, `ScreenSessionNotSent`, `ScreenSessionRestarting`). It has grown since: +615/−82 across the same four files, and five untracked boards (adding `ScreenTasksDenied`, `ScreenTasksRefused`). A baseline taken before it lands would miss it.
- **The `apogee` prop** (installed, missing, config-typo; default installed) gives read screens a second state: "Apogee isn't installed…" notes, disabled buttons, refusals. It is read at `main.html:4287` (HEAD), and no board sets it (recon). Those states exist nowhere as a picture.
- **The 30-tick check passes today** (`spec/CLAUDE.md` "Checking a change", run read-only): HEAD prints "57 start screens, no problems" and the working tree "70 start screens, no problems", in all three `apogee` modes.
- **The canvas is behind the repo** (critic V2). Its `project/App.dc.html` is 754,066 bytes, exactly `main.html` at `d539f88b` (2026-10-26). It has 53 Screen boards. Three committed design changes from 2026-11-08 (`212311e3`, `d6f4af3c`, `231fb049`) never reached it. So the baseline is the repo, never the canvas.
- **The old data is invented** (critic V3: 0 `object:` keys, 0 `*_at` keys in the script). The new app shows the CLI's recorded documents, so names and counts will differ. The baseline is the reference for layout, states, controls and wording.

**Core constraint(s).**
- **The repo, not the canvas.** Every image comes from `lib/design/spec` at one recorded commit.
- **Re-made, never committed.** The images are not in git. A script re-makes them from the recorded revision, so the baseline outlives 1t's deletion.
- **Git is read, never written.** No index change, no checkout, no commit. Other sessions commit in this checkout mid-run.
- **Opened the README's way** ([spec/README.md](../../../../design/spec/README.md) "Opening it"): the archived `html/` folder served by `python3 -m http.server`, then `main.html?start=<preset>&still=true`. A page opened from disk cannot load the boards.
- **Still frames at the boards' size:** 1440 × 900, the `$preview` every Screen board declares.
- **The freeze** is 1a's rule ([the interim rule](../../../assistant/design/CLAUDE.md#the-interim-rule-while-the-port-runs)): from this revision on, the old prototype takes fixes only.
- **One download, with consent.** Playwright's Chromium was fetched once by 1b (2026-11-18, on the user's yes: Playwright 1.64.1's Chromium 156, about 286 MiB downloaded and 572 MB on disk in `~/Library/Caches/ms-playwright`). This item reuses it and downloads nothing. No `apogee` binary runs.

**Seam + files.**
- `lib/design/prototype/package.json` (from 1b): `playwright` as an exact-pinned dev dependency at 1b's version, 1.64.1 (the version `@playwright/test` already pins, so it finds the browser 1b downloaded), in the lockfile; a `baseline` script.
- `lib/design/prototype/scripts/baseline.ts`, new. Two modes:
  - `--record`: refuses while `git status --porcelain lib/design/spec` shows anything, naming the files. Otherwise takes the newest commit that touches `lib/design/spec`, runs the 30-tick check on that copy in all three `apogee` modes, and writes `baseline.json`.
  - default (capture): reads `baseline.json`, archives that revision, serves it, and captures:
    - every start preset at `apogee=installed`;
    - the same preset at `missing` and `config-typo`, kept only where the image differs from `installed`;
    - the 15 other boards at their `canvas.json` size, full page, once per value of their prop menus.
  - It writes the images, a hash list and an `index.html` contact sheet to `lib/design/prototype/.parity/baseline/<revision>/`.
- `lib/design/prototype/parity/baseline.json`, new, committed: the full revision hash, the date, the tree hash of `lib/design/spec` at it, the preset list, the modes kept per preset, the 30-tick result, the Playwright version, the viewport, and a short history of re-records with their reasons.
- `.gitignore`: `lib/design/prototype/.parity/`.
- `lib/design/spec/CLAUDE.md` and `README.md`: a short "Frozen at `<revision>` (design 1c): fixes only" banner at the top.
- [assistant/design/CLAUDE.md](../../../assistant/design/CLAUDE.md) "The interim rule": names the revision. MILESTONES.md and ROADMAP.md on ship.

**Reference.**
- The recon of 2026-11-14 (prototype map §5, boards §0–§1, critic V2, V3, G7, G8), re-checked against HEAD `48f46831` and the working tree the same day.
- `lib/design/spec/html/support.js`: `fromQuery` (:441) turns `?start=…&still=true&apogee=…` into props; `fail()` (:418) draws the inline error box a broken render shows.
- `main.html` reads the wall clock (`nowTime()`, :2929 at HEAD, :3074 in the working tree) and `Math.random()` (:4000 at HEAD, :4333 in the working tree), so the capture fixes both.
- Release branches merge into `stable` with merge commits (PR #11, `b54b25e9`), so a branch commit stays reachable after the merge.

**Decisions made** (dated):
- 2026-11-14: The user's call: rebuild the prototype in React, TypeScript and Vite, with the production apps' four layers and screen modules; the old one is the reference until then.
- 2026-11-14: The user's call: the canvas becomes view-only and is retired at the end; the review surface is the built file. So the baseline never comes from the canvas.
- 2026-11-14: The user's call: parity screenshots by Playwright, downloaded once as a dev dependency (about 150 MB), approved by the user when this item builds.
- 2026-11-14: The user's call: a design queue of its own; this item is design 1c, in v0.2.1.
- 2026-11-14: The recorded default: images are not committed; a script re-makes them from the recorded frozen revision.
- 2026-11-14: The recorded default: the old prototype is frozen, fixes only, from this item's revision; new design waits for 1e.
- 2026-11-14: The recorded default: parity is judged on layout, states, controls and wording, not pixels or data.
- 2026-11-14: The recorded default: checks run locally, not in CI; `changed.sh` and the workflows are untouched.

**Open calls:**
- [default: `git archive <revision> lib/design/spec` into a temp folder, not `git worktree add`. It leaves no worktree entry in the shared checkout.]
- [default: the revision is the newest commit touching `lib/design/spec` once it is clean. While the other session's work is uncommitted, this item waits and says so; it never commits that work itself.]
- [default: capture all three `apogee` modes and keep a `missing` or `config-typo` image only when its hash differs from `installed`. No hand-picked list of screens.]
- [default: the 15 non-screen boards are captured too (Build, Foundations, Components, Exploration), since 1q and 1r rebuild them and 1s judges them.]
- [default: the browser's clock is fixed to one instant and `Math.random` is seeded by an init script, so two runs give the same images.]
- [default: the capture waits for the fonts (`document.fonts.ready`). If the Google Fonts link fails, the run is refused, never recorded with fallback fonts.]
- [default: a fix to the frozen prototype re-records the baseline at the fix's commit, with the reason in `baseline.json`'s history.]
- [default: Chromium only; the browser lives in Playwright's own cache outside the repo.]
- (consumed decision: 1a) the freeze rule's wording; (consumed decision: 1b) `package.json`, the lockfile and the local check gate; (consumed decision: 1s) how each pair is compared; (consumed decision: 1t) the deletion.

**Guardrail(s).** Unit tests beside the script (1b's test runner), each on a planted case:
- the preset reader on a miniature `main.html` with `&amp;` and `&#39;` in `data-props`;
- `--record` on a planted dirty `lib/design/spec` in a temp git repo refuses and names the files;
- capture against an unreachable revision refuses and names it;
- a planted preset that draws `support.js`'s error box fails the capture, naming the preset;
- `baseline.json`'s preset list must equal the revision's `data-props` list, or the check fails.

A real run is captured twice; the two hash lists must match.

**Acceptance criteria:**
- [ ] `baseline.json` is committed, naming the revision, its preset count, and the 30-tick result in all three modes.
- [ ] `npm run baseline` re-makes every preset's image (plus the kept mode images and the 15 boards), and two runs match hash for hash.
- [ ] In a scratch clone with `lib/design/spec` deleted, the same command still re-makes the same images.
- [ ] The contact sheet is sent to the user to look through.
- [ ] `spec/CLAUDE.md`, `spec/README.md` and the design guide say the old prototype is frozen at that revision.

**Scope note.** Earmarked for v0.2.1 (design track 1); its gate, 1b, shipped 2026-11-18 ([Milestone A](../../../assistant/design/MILESTONES.md#milestone-a--the-design-prototype-in-react)), and in practice on the other session's design work being committed. Out of scope: comparing old and new (1s), republishing or resyncing the canvas (retired by 1t), new boards for board-less presets (1s), CI, and any change to the old prototype beyond the frozen banner.
