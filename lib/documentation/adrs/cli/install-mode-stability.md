# ADR 0005 — Changes never alter how Apogee installs or updates

**Status:** Accepted · **Date:** 2026-10-03

## Context

Apogee reaches machines three ways, and all three are load-bearing: **from source** (the CMake presets and `lib/scripts/cicd.sh`, `make install` to the user's bin), **by installer** (the README's copy-paste one-liners — `curl … lib/scripts/install.sh | bash`, `irm … install.ps1 | iex` — fetching the latest release's archive), and **by self-update** (`apogee update`, replacing the installed binary from the release the pipeline published). The pipeline's contract is equally fixed: releases are named by `lib/release/VERSION`, `changed.sh` decides whether the CLI rebuilt or the latest release's archives are copied, and `release-from-pr.sh` finds platform archives by their `apogee-<target>` names. Any of these drifting breaks users who never read a changelog — and the four-layer restructure (433 renames) shipped without touching one of them, proving the rule is holdable.

## Decision

**A change to the CLI — structural or functional — leaves every install and update path working unchanged.**

- The installer entry points (script URLs, flags, behavior), the archive names and layout, and the `update` path are a public contract; code changes never move them as a side effect.
- Restructures (renames, module splits, layer moves) are invisible at install time: same binary name, same archive per target, same `VERSION` discipline.
- A change that genuinely must alter the pipeline or install surface is its own deliberate act, following [DEVELOPER.md → Changing the pipeline](../../assistant/DEVELOPER.md#changing-the-pipeline) — including the required-checks step and the user's say-so for repository settings — never a rider on a feature.

## Consequences

- "Does this break install?" is answerable by inspection: did the change touch `lib/scripts/`, the workflows, archive names, or `VERSION` handling? If not, it cannot.
- Features and restructures ship without release-engineering review; pipeline changes get exactly that review, alone.

## Enforcement

CI's `what changed` gate (`changed.sh`) compares against the **latest release**, so a merge that silently altered what the CLI is built from rebuilds and reruns everything; the pipeline's names are read by `release-from-pr.sh` (renaming one requires changing the other — documented in DEVELOPER); the forgot-to-bump guard refuses a publish-nothing merge. Install scripts are exercised by their own documented checks on release.
