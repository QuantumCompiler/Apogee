# Helper-model roles: vision, transcription, and a utility model

**What / why.** A harness that gets the most from small models uses several of them, each for what it is good at. The large model answers, while smaller ones do the chores and read what the large one cannot. Three roles join `chat`, `embedding` and `extraction` in the one resolver:

- **`vision`** describes an image, including the text in it, for a chat model with no projector. Used by [media attachments](attachments-media.md).
- **`transcription`** turns audio into timestamped text. That is an audio-capable model through mtmd (Gemma 4's `gemma4ua` projector, for example), with no separate speech dependency. Used by [media attachments](attachments-media.md).
- **`utility`** does the chores that today load or occupy the chat model: chat titles, compaction summaries, the rerank judge's default, rewriting a follow-up question into a standalone search query for retrieval, and summarising a tool result too large to hand the chat model whole.

Unset, each role falls back as `extraction` does today, to the chat backend, so nothing changes for a user who configures none.

**Core constraint(s).**
- **One role resolver.** Every role resolves through `harness::resolve_backend_key` and its ladder (flag > per-feature pin > role pointer > `models.default`); `models status` shows the rung that answered. No surface picks a helper by type.
- **Capabilities are asked, never cast.** A `vision` backend must answer yes to `accepts_images`. `transcription` needs a new capability probe (`accepts_audio`, from `mtmd_support_audio`) discovered by the Harness like the others. `check` warns when a role points at a backend that cannot do its job.
- **One config mutation path.** The pointers are written by three new verbs beside `config set-default-embedding` (`set-default-vision`, `set-default-transcription`, `set-default-utility`, each one `bind_set_role` call) and their admin twin, all through `set_models_role`.
- **Side requests stay side requests.** A helper's work never touches the chat session's cache (the `side_request` flag).
- **Memory is visible.** A second resident local model is reported by `models status`, and `idle_unload_seconds` applies per backend.

**Seam + files.**
- `harness/roles.h/.cpp`: `ModelRole::Vision`, `Transcription`, `Utility`, and `models.default_vision`, `default_transcription`, `default_utility` in `harness/config.h/.cpp` and the template.
- `harness/provider.h`: an `AudioCapable` probe beside `VisionCapable`; `backends/llamacpp.cpp` answers it from its projector.
- The chores move to the utility role:
  - `commands/chat.cpp` (`BackgroundTitle`);
  - `agentloop/` (`compact_history`, the rerank default);
  - a new `agentloop/query_rewrite.h/.cpp` for follow-up questions before retrieval;
  - `agentloop/loop.cpp` for tool results over a size threshold, summarised before they enter the chat model's context.
- `commands/models.cpp` (`models status`), `commands/check.cpp`, `commands/config_cmd.cpp`, and the role routes in `httpserver/`.

**Reference (Ommi).** Ommi's role pointers (`default`, `default_embedding`, `default_extraction`) are what Apogee's resolver ported, with its drift bug fixed (roles.h). Ommi had no helper roles. It advised routing tool-heavy sessions to a stronger backend by hand (MCP.md) rather than delegating chores downward.

**Decisions made:**
- 2026-09-25 — Asked for by the user ("Helper-model roles").
- 2026-09-25 — **A helper is used automatically** when the chat model cannot read a medium (the user's call, over asking first or refusing). This item provides the roles; [media attachments](attachments-media.md) is the consumer.
- 2026-09-25 — The capture clerk keeps using the chat model (Milestone Y's "the loaded model is the clerk, no second load") unless a `utility` role is set explicitly.

**Open calls:**
- [default: query rewriting runs only when the conversation has an earlier turn and retrieval is active] A first question is already standalone.
- [default: a tool result over 8 KiB is summarised by the utility model when one is set, and cut as now when not] The summary names what was dropped and how to get it (read the range, re-run narrower).
- [default: roles with no pointer fall back to the chat backend] The same rule `extraction` follows.

**Guardrail(s).**
- The resolver's ladder table-tested for the three new roles.
- A `vision` pointer at a projector-less backend warns in `check`, and so does a `transcription` pointer at a non-audio one.
- Titles, compaction, the rerank default and query rewriting go to the utility backend when set and the chat backend when not, asserted with scripted providers that record who was asked.
- The session cache is untouched by any helper.
- The role pointers round-trip byte-exact through the editor and the admin twin.

**Acceptance criteria:**
- [ ] With `models.default_utility` set to Qwen3-VL-8B and chat on Qwen3.8-27B, the chat's title, compaction and query rewriting run on the 8B (visible in `--verbose`), and the 27B's cache is never cleared by them.
- [ ] `apogee models status` lists all six roles and the rung each resolved on.
- [ ] `apogee check` names a role that points at a backend unable to do its job.

**Scope note.** Phase 4, item **26b**; gated on nothing, though running helpers beside a large model assumes [26a](context-fit-defaults.md)'s memory. Out of scope: automatic selection of which model plays which role, and cloud backends as helpers beyond what the resolver already allows (any backend may be pointed at).
