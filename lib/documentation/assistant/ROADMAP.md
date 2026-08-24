# Apogee — Roadmap

How we work: features get discussed in chat, written up here (and in [SPEC.md](SPEC.md)), given their own document in [`backlog/`](../backlog/README.md) when they're specced, then an agent takes the backlog item and builds it straight from that document (there is no separate TODO file — the backlog is the queue). When work ships it's recorded in detail in [MILESTONES.md](MILESTONES.md); this file stays high-level — the running board of themes per release, plus what's next.

**Version scheme:** feature releases are `v0.x.0`, patch releases are `v0.x.y`; development happens on a branch named for the upcoming release (currently `v0.1.0`) and merges into `stable`. Checked boxes are shipped; the detailed write-up of each is in MILESTONES.md.

---

## Shipped releases

*(Nothing shipped yet — the first tagged release starts this board.)*

---

## In progress

### v0.1.0 — Initial harness

The first working version of Apogee: the stated backend set behind one harness. The product surfaces (library / CLI / server) are still an open call — see [SPEC.md](SPEC.md) → Core surfaces — and none of these items is specced into the backlog yet.

- [ ] Anthropic cloud backend
- [ ] OpenAI cloud backend
- [ ] Google cloud backend
- [ ] Local model inference via llama.cpp
- See the [`backlog/`](../backlog/README.md) index for the work queue (priority-ordered; topmost claimable item = next to build).

---

## Fast follow

*(empty)*

## Unspecced ideas

*(none yet)*

## Ideas / candidate features

*(none yet)*

## Explicit non-goals

Ruled out on purpose (see [SPEC.md](SPEC.md) → Non-goals for the rationale):

*(none recorded yet — see SPEC.md)*
