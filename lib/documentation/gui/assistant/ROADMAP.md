# Apogee GUI — Roadmap

How we work: features get discussed in chat, written up here (and in [SPEC.md](SPEC.md)), given their own document in [`backlog/`](../backlog/README.md) when they're specced, then an agent takes the backlog item and builds it straight from that document (there is no separate TODO file — the backlog is the queue). When work ships it's recorded in detail in [MILESTONES.md](MILESTONES.md); this file stays high-level.

**Version scheme:** repo-wide — the release `lib/release/VERSION` names, published when its branch merges into `stable` (the CLI docs record the law). The GUI's themes ride those releases once the first application lands.

---

## Shipped releases

*(Nothing shipped yet — the first tagged release carrying GUI work starts this board.)*

---

## In progress

### v0.2.0 — The GUI effort opens

The documentation tree was restructured per application (2026-10-20, the user's call): CLI docs under `documentation/cli/`, this system scaffolded under `documentation/gui/`, the [router](../../CLAUDE.md) between them.

- [x] The docs home — this system, scaffolded 2026-10-20.
- [ ] **The first spike** — the questions the first application hangs on: which platform first, three native shells vs. one shared shell, the machine-mode client core (spawning the binary, the JSONL session, tolerant event handling), and what of it is shared across the three apps.
- See the [`backlog/`](../backlog/README.md) index for the work queue (empty until the spike's split lands).

---

## Fast follow

*(empty)*

## Unspecced ideas

*(none yet — ideas originate in chat)*

## Ideas / candidate features

*(none yet)*

## Explicit non-goals

Ruled out on purpose (see [SPEC.md](SPEC.md) → Non-goals for the rationale):

- **No capability of its own**
- **No HTTP path for local work**
