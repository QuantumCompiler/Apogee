# ADR 0007 — Tab completion is answered by the binary, from live state

**Status:** Accepted · **Date:** 2026-10-03

## Context

Apogee has carried dynamic shell completion since v0.1.2: a hidden `apogee __complete` verb plus four thin stubs (bash, zsh, fish, PowerShell) that do nothing but call back into the binary, so `apogee complete -m <TAB>` offers the backends *this user actually has*. The M7 work (2026-10-03) extended it after a transcript showed the gap — a registered backend's own name refused by `models delete`, a store path hunted by hand — and settled the standing rules while doing so: candidates came from the live config and store, the completion lookups were the very ones the verbs use (`stored_gguf_at`, `stored_ggufs_named`), and, by the user's call, delivery stayed the installers' job — no `apogee completion <shell>` command, no second path. In-chat completion took the same shape earlier: `/` commands and flags complete through the one command table, `@` mentions through the real filesystem (Milestone H). And the M7 transcript showed what a lagging completion surface costs: the gap between what the product could do and what TAB offered is exactly where the user ended up hand-hunting store paths — completion that doesn't reflect the feature set misleads at the keystroke, before any help text gets a chance.

## Decision

**Completion is computed by the binary, from the live install, through the one resolver per surface — and what completion offers, the command accepts.**

- **Stubs know nothing.** A shell script may only call `__complete`; a candidate list hardcoded in a stub, an rc file, or a static completion spec is drift by construction and does not exist.
- **Candidates come from the registries the features themselves read** — config, the model store, the command table — at keystroke time. No cached candidate set that can go stale between a pull and a delete.
- **The per-keystroke budget is absolute:** completion reads cheap state only — directory records, config, the command table — never a GGUF header, a model load, or the network (the `models list` lesson: the cost of a listing path is system calls, and completion runs on every TAB).
- **One resolver per surface, shared with behavior:** shell candidates through `complete_sources.cpp`, whose name lookups are the same functions the verbs resolve with; in-chat `/` completion through the one command table. A surface growing its own candidate logic is the two-resolvers bug waiting to disagree.
- **The offer is a contract.** A name completion presents, the command accepts (`models delete <TAB>` offering a backend name is what obliged `models delete` to *take* backend names); a candidate the verb would refuse is a completion bug, not a user error.
- **Completion is part of every change's surface.** A new or modified feature ships with completion accurately reflecting what is now possible, **in the same change**: a new verb, flag, value set or name kind is offered the moment it exists, a renamed one completes under its new spelling, a removed one is never offered again. Both directions hold — everything completable is offered, nothing offered is impossible — and the cost of a feature includes its completion entry, exactly as [mode parity](mode-parity.md) prices in the machine event and the served read.
- **Delivery belongs to install:** `make install` and the installers place the stubs; nothing else writes shell configuration (consistent with [install-mode stability](install-mode-stability.md)).
- **`__complete` is a machine surface:** its output shape grows additively, like every surface a script calls across binary upgrades ([backwards compatibility](backwards-compatibility.md)).

## Consequences

- A new verb or flag ships with its completion, and the cost is one registry entry, not a script edit in four shells.
- New candidate kinds (suite names, provider names) ride the existing resolver — the queued items already assume this.
- Completion correctness is testable in-process: resolver tables against a scripted store and config, no live shell needed.

## Enforcement

The resolver's table tests and the shared-lookup construction (`complete_sources.cpp` calling the verbs' own resolution functions — one implementation, impossible to disagree); `cli.install_parity` covering the stubs' installation; the format → type map living beside the store's formats so completion, delete and registration stay one truth. The offer-is-a-contract rule is each verb's acceptance test: every candidate kind the resolver emits has a test that the verb accepts it. The ships-with-its-change rule is enforced where changes are judged: a feature's resolver/table entry and its completion tests land in the same change (the review bar), and the pre-MR docs pass audits the completion surface against the branch's new and changed verbs, flags and name kinds the way it audits parity — a verb reachable but not completable is a finding, not a footnote.
