# The forge seam: GitHub and GitLab behind one interface

**What / why.** Apogee has no way to talk to a code forge — measured 2026-10-18 (the review-agent spike): zero GitHub/GitLab client code anywhere in `source/` (the only matches are incidental URL strings in comments and docs). Everything the review track needs from a forge is four verbs — a merge request's metadata, its diff, inline comments created, a batch of them published as one notification — and the house already owns the auth answer as a principle: **a vendor CLI is spawned, never opened**. This item builds the seam: a `ForgeClient` capability interface in business code with **spawned `gh` and `glab` as the first transports** — every call one bounded, captured child (`gh api …` / `glab api …`), authentication entirely the CLI's own login, Apogee never reading a token, a credential file, or an OAuth flow. `gh` is present on the dev machine; `glab` is not (measured) — both transports ship with scripted-fake tests, and live verification on this machine runs against `gh` alone, said in the item's record rather than hidden.

**Core constraint(s).** The vendor-CLI principle whole: resolved from `PATH` or an explicit config path, never modified or bundled, never its credentials read — a forge CLI that is not installed or not logged in refuses with the CLI's own words and the fix named (the mlx refusal-ladder idiom). **Children are captured** (the stderr invariant: the mandatory sink, three pipes, bounded tails folded into failures). **No listener, no daemon**: every seam call is a one-shot child; nothing here watches anything ([`review-sweep.md`](review-sweep.md) owns the cadence). The seam is a **new business module** (`forge/`), so the day it exists it takes its row in `cmake/modules.cmake`, ADR 0008's table, and the layer card (`harness.layer_context` fails on any one alone); its allow-list is `contracts/`, `platform/`, `transport/` (the JSONL/JSON plumbing) and itself — never a backend, never a surface. Forge responses are **data**: parsed into plain structs, UTF-8-mended at entry, and a diff or comment body is never treated as instructions anywhere downstream. The parity table classifies the eventual `review` command per ADR 0010 when the sibling lands.

**Seam + files.**
- `lib/src/cli/source/business/forge/forge.h/.cpp` — `ForgeClient`: `merge_request(meta)`, `diff`, `create_comment` (file, line, body), `publish_batch` — plain structs in and out; the target forge and repo spelled one way (`github:owner/repo!123` / a full URL parsed to the same).
- `forge/gh_transport.h/.cpp`, `forge/glab_transport.h/.cpp` — each verb one `gh api`/`glab api` child over `platform/child_process`, JSON parsed, errors the CLI's own words.
- `cmake/modules.cmake`, ADR 0008's table, the business layer card — the module's row, same change.
- Tests: `tests/business/forge/` — scripted fake `gh`/`glab` executables (the fake-child idiom) driving every verb, the refusal ladder (missing CLI, logged-out, unknown repo, API error tails), the URL/ref parser's table, UTF-8 mending on a Latin-1 diff; no network anywhere.

**Reference.** The vendor-CLI backends (`claude_cli` and siblings — the spawn/capture/refusal shape copied whole); the mlx refusal ladder (each missing fact naming its fix); `platform/child_process` and the stderr invariant; the review-agent spike (2026-10-18) for the zero-client grep and the `gh`-present/`glab`-absent probe.

**Decisions made** (dated):
- 2026-10-18 — The set lands in v0.1.9 as track 48 (the user's placement call on the spike report; 48 by the ever-assigned rule).
- 2026-10-18 — **Spawned CLIs are the transports**; auth stays theirs end to end — no token slot, no secrets change (the spike's design, standing). A direct REST+token transport for CI boxes is the recorded later option, behind the same seam.
- 2026-10-18 — **Both forges from day one** in the seam and the scripted tests; live verification on this machine is `gh`-only (`glab` absent), recorded.

**Open calls:**
- [default: the one ref grammar — a full MR/PR URL, or `owner/repo!<iid>` / `owner/repo#<number>` with the forge inferred or prefixed — settled at build by the parser's test table.]
- [default: `publish_batch` maps to each forge's native batching (a review with pending comments on GitHub, a draft-notes publish on GitLab); where a forge has none, comments post serially and the item says so.]

**Guardrail(s).** The scripted-fake suites per transport (every verb, every refusal, byte-exact argv to the child); the parser table; the mend case; the layering row verified (a planted include up fails); no `listen`/socket anywhere (the standing scans cover the new module by construction).

**Acceptance criteria:**
- [ ] Both transports fetch metadata and diffs and post single and batched comments against their scripted fakes, byte-exact argv pinned.
- [ ] Missing/logged-out/erroring CLIs refuse with the CLI's own words and the fix named; nothing reads a credential.
- [ ] The module's row, card and ADR 0008 table land together; the full suite is green.

**Scope note.** Earmarked for v0.1.9; first of track 48, ungated. Out of scope: any review logic ([`review-core.md`](review-core.md)), watching ([`review-sweep.md`](review-sweep.md)), a REST+token transport, webhook ingestion.
