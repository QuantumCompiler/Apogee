# Settings: the five panes

**What / why.** The design's Settings window (`ScreenSettingsGeneral`, `…Appearance`, `…Backends`, `…Media`, `…Permissions`): **General** — the binary's path (the discovery override 1b shipped), startup behavior, the keyboard list; **Appearance** — the three themes (Graphite Dark, Light, High-contrast — `tokens.json`'s sets) over 1a's styles, type size; **Backends & keys** — backends and providers (`config add-backend | set-backend`, `providers scan`, all ready), keys through `auth add` with the key on stdin (**never an argument, never echoed** — the CLI's secrets law honored client-side), health from `check --output-format json` with each row's remedy, and `check --fix` and `bootstrap` as buttons (the design's startup section); **Media** — default pipeline (`config set-default-video`), output folders; **Permissions** — the gate's settings (`config set-permission`, `add/delete-allowed-host`, all ready). Two fallbacks: the web-search address and recall toggle read via `config get` but set nowhere until 52e's `config set` (G15 — the pane shows them read-only with "edit the config file" said, upgraded on landing); forgetting recall waits on 52e's verb (G16 — hidden until then).

**Core constraint(s).** ADR 0001/0002; **the key never touches the app's state**: `auth add` is driven with the secret written to the child's stdin and immediately discarded — no preference, no log, no view-model field that outlives the sheet (the secrets-unleakable law, client-side); every mutation is its verb with read-back; the Appearance setting re-opens the groomed "no theming yet" default deliberately — the three-theme setting is this item's scope, `styles` growing the theme switch (ADR 0002's module owns it).

**Seam + files.** `source/presentation/settings/` (the window, five panes; view-models), `source/presentation/styles/` (the theme switch over the tokens' three sets), `source/business/reads/` (config/auth/check/providers commands), `source/data/preferences/` (theme, folders, startup). Tests: pane view-models over scripted documents; the auth flow's stdin write and zero-retention pinned; check rows with remedies and the fix/bootstrap buttons' commands; the G15 read-only fallback; theme switching over the tokens; hermetic.

**Reference.** The five `ScreenSettings*` boards, `ColorTokens.dc.html`, `tokens.json`; the contract board's SETTINGS rows (G15/G16 fallbacks); [52e](../../cli/assistant/MILESTONES.md#milestone-ao--the-desktop-clients-cli-growths) (shipped 2026-10-20) cross-referenced, not gated; the CLI's secrets invariant — the stdin discipline mirrored.

**Decisions made** (dated):
- 2026-10-20 — From the design-completion spike (the user's call); the three-theme Appearance setting supersedes 1a's system-following default (the design's call, standing — `styles` owns the switch).

**Open calls:**
- [default: pane layouts per the prototype; the theme switch's persistence in the app's preferences; details at build.]

**Guardrail(s).** The secret-retention pin (the key appears in no state, log or view after the sheet); read-back pins per mutation; the fallback renders; theme-switch snapshots over the three token sets; hermetic.

**Acceptance criteria:**
- [ ] All five panes work over the real verbs with read-back; a key lands via stdin and is retained nowhere; check renders with remedies and working fix/bootstrap buttons.
- [ ] The three themes switch live from the tokens; G15's keys show read-only with the honest hint until `config set` exists; the full suite is green.

**Scope note.** Earmarked for v0.2.0; gated on [`app-shell.md`](app-shell.md) (1e). Out of scope: the CLI growths (52e), a keyboard-remapping UI (the list is shown, not edited).
