# The session's events say more: diffs, recalled names, sources, each chat's model

**What / why.** The desktop app draws what the protocol says, and four of its designed parts have nothing to draw from — the design's contract board (`lib/design/spec/html/BuildContract.dc.html`, checked against apogee 0.1.9, re-verified 2026-10-20 by the GUI spike): **(G1)** a `write_file`/`edit_file` permission question names the tool and target but never the change, so the card's "+12 −3" and "Show changes" have no source — while `fetch_url`'s question already carries its URL as `detail`, the precedent; **(G3)** the `memory` event carries counts, not which sessions were recalled, so "Recalled 1 past session: *asyncio timeouts in the scraper*" cannot link anywhere; **(G4)** an answer never says which retrieved excerpts it drew on, so the design's source chips `[1] [2]` with their pages have nothing behind them; **(G8)** `chats list` rows carry no backend or model, so the session list's model glyph cannot be drawn. This item grows the events, additively: the permission `question` for the writing fs tools gains `detail` (a unified diff of the proposed change, computed where the gate already holds the arguments); `memory` gains `sessions` (each recalled chat's `id`, `name`, `updated`); a **`sources` event** lands before the answer when retrieval injected excerpts (each one's file, page or lines, and its score on its retriever's scale — the honesty rules of 26s carried into the protocol); and `chats list`'s rows (and document) gain `backend` and `model`. The design marks G1 and G8 **before the app's first release** — this item is the track's head on that recommendation.

**Core constraint(s).** **Additive only** (ADR 0006): new fields and a new event older drivers ignore; every existing fixture stream still parses byte-identically; the `session` capabilities' `events` list gains `sources`. **One source of truth per fact**: the diff is computed from the very arguments the gate judges (never a second render), the recalled sessions are what `recall` actually injected, the sources are the excerpts the budget actually sent (not what retrieval found and trimmed), and a chat's model is what its session file already records. **Scores ride with their scale** (the one-retriever law's reporting rule). Machine-mode.md and the schema grow in the same change (`cli.machine_schema_conformance` holds them); mode parity per ADR 0002 — the terminal and TUI asks may render the diff's counts, said if skipped; no new verb, so ADR 0010's table is untouched.

**Seam + files.** `agent/` (the gate's ask carrying `detail` for the fs writers), `agentloop/recall` (+ the `memory` event's sessions), `agentloop/` retrieval → the `sources` event through the Reporter seam (all four adapters relay; the terminal may fold it into the thinking block), `machine/json_reporter` + `machine/protocol` (the event and fields declared), `cli/chat_history` (`chats list` fields), `lib/documentation/cli/reference/machine-mode.md`. Tests: fixture goldens per event; the additive guarantee (old streams parse); the diff's byte-equality with the gate's arguments; `sources` only when something was sent.

**Reference.** The design's contract board rows and gap ledger (G1, G3, G4, G8 with their fallbacks); `fetch_url`'s `detail` precedent; 26s — scores on their own scales; 28h — the documents.

**Decisions made** (dated):
- 2026-10-20 — Specced from the GUI design spike, the user's call; ordered first per the design's before-v1 recommendation (G1, G8 in it).
- 2026-10-20 — `sources` reports what was **sent**, never what was merely found; scores carry their retriever.

**Open calls:**
- [default: the diff's form in `detail` is a unified diff capped at a named size, the cap said in the field; exact caps at build.]
- [default: the `sources` event's shape (list of `{file, page?, lines?, score, retriever}`) finalized at build within 28h's family style.]

**Guardrail(s).** Schema conformance grown; per-event fixture goldens; the old-stream additive pin; the gate-argument byte-equality; no network, no models.

**Acceptance criteria:**
- [ ] A driven `write_file` ask carries the unified diff; `memory` names its sessions; an answer with injected excerpts is preceded by `sources`; `chats list` rows carry `backend` and `model`.
- [ ] Every pre-existing fixture stream parses unchanged; machine-mode.md and `__machine-schema` agree; the full suite is green.

**Scope note.** Earmarked for v0.2.0 (track 52, the desktop client's CLI growths); ungated. Out of scope: any GUI code (the app's items consume this), terminal/TUI rendering changes beyond relaying (their own calls at build), G5/G2/G6/G7 (the inbound lines, [`session-inbound-lines.md`](session-inbound-lines.md)).
