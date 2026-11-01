# Apogee Mod — Developer Reference

The architecture reference for the Claude plugin. The contributor entry point is [CLAUDE.md](CLAUDE.md); the contracts the mod consumes are the CLI's ([machine-mode.md](../../cli/reference/machine-mode.md), the 28h JSON documents, and `apogee mcp serve`, public since the CLI's 54a, 2026-11-01 -- its tools in [tools.md](../../cli/reference/tools.md#apogee-as-an-mcp-server-apogee-mcp-serve)).

---

## Layout

Nothing is built yet. The planned shape, per [ADR 0001](../adrs/layer-enforcement.md) (four layers, dependencies down):

```
lib/src/mod/
  .claude-plugin/plugin.json     the manifest
  hooks/hooks.json, register.tsx the entry point — the composition root
  types/index.d.ts               state typings, if atoms are kept
  src/infrastructure/            the process runner over $.process.run; discovery
  src/data/                      the documents as tolerant types; the mod's atoms
  src/business/                  the Apogee client: reads, supervised runs, handler logic
  src/presentation/              the pane, status, toasts, slash registration — the only $.ui layer
  tests/                         mirroring; the layering check (imports held to the layer map)
```

## The contracts the mod builds against

- The `--output-format json` documents (models, train status/versions, check, …) — measured live by the mod spike (2026-10-21).
- Machine mode, for anything session-grade later.
- `apogee mcp serve` (the hidden `__mcp-tools` its alias) — the MCP server: 22 read-only tools since the CLI's 54a, the spike's 17 plus the five status reads (`models_list`, `models_status`, `train_status`, `train_versions`, `check`), each answering with its command's JSON document byte for byte; the registration JSON printed by `apogee mcp presets`.

## Build & test

`tsc -p lib/src/mod`, `claude plugin validate lib/src/mod`, `claude plugin test lib/src/mod`; local development with `claude --plugin-dir lib/src/mod`. _TODO:_ the pipeline item (1d) records the CI shape — `tsc` on runners, validate where the `claude` CLI installs headlessly, validate/test always the dev-host gates — and the `changed.sh mod` entry.
