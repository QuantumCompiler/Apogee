# ACP host fixtures

`schema_examples.jsonl` -- the requests a host sends, one per line, taken from
the Agent Client Protocol's own examples (agentclientprotocol.com, protocol v1:
Initialization, Session Setup, Prompt Turn; read 2026-11-01). They stand in for
a captured Xcode transcript until one is recorded (56a): `cli.acp_conformance`
replays them against the real binary, `{{cwd}}` and `{{sessionId}}` filled in
with its temporary project and the session `session/new` minted.

`xcode-27.1.jsonl`, once captured, is what Xcode 27.1 sent the logging stub
(`tests/scripts/py/acp_capture_stub.py`) registered in Settings > Intelligence
> Agents > Add an Agent: `initialize`, `session/new` and a first
`session/prompt`, env and header values and home-folder paths redacted by the
stub. The capture changes Xcode's settings, so it is the user's to run; the
suite replays it whenever the file is here.

`saved_chat.json` -- a saved chat as the session store writes it (56c): a
compaction, an answered turn, a turn left unanswered (cancelled) and another
answered, begun by `acp-test` in `{{cwd}}`. `cli.acp_conformance` copies it
into its home's `sessions/` and loads it with `session/load`: the compaction
and the unanswered turn share turn numbers with their neighbours, and every
replayed `messageId` must still be distinct.
