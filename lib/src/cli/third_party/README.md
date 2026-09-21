# third_party/

Vendored and pinned external code for the **CLI application**. **Nothing here is edited in-tree.**

That rule is a Core constraint carried over from Ommi (see [CLAUDE.md](../../../documentation/assistant/CLAUDE.md) → Invariants). A local edit to a pinned dependency is invisible to everyone reading the pin, survives no update, and turns every future version bump into an archaeology exercise. When a third-party library is wrong for us:

- **Fix it upstream** and move the pin forward, or
- **Wrap it** — put the adaptation in a first-party boundary class under `../source/`, where it is reviewed, tested, and owned.

## What lives here

| Dependency | Acquisition | Notes |
|---|---|---|
| llama.cpp | `FetchContent`, pinned to commit `549b9d84` | Local inference, linked **in-process** rather than spawned per turn. Off by default (`-DAPOGEE_ENABLE_LLAMA=ON` to build); compile-proofed by its own non-blocking CI job until the `llamacpp-backend` backlog item consumes its API. The pin is the revision Ommi's proven local-inference path builds against. |

Everything else the CLI depends on is fetched by [`../cmake/ApogeeDependencies.cmake`](../cmake/ApogeeDependencies.cmake) rather than living here — see that file for the dependency strategy and how to add one.

## Updating a pin

A pin bump is a deliberate, reviewable event, not a drift. Give it its own commit, and say in the message what changed upstream and why Apogee wants it.
