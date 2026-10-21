# Mod reference

Integrator-facing contracts the mod itself exposes. **Born empty** — the mod is a client: the contracts it consumes are the CLI's ([machine-mode](../../cli/reference/machine-mode.md), the JSON documents, the MCP server) and Claude's plugin API. A document lands here only when the mod exposes a contract of its own; until then, this README is deliberately the directory's only file.
