# Reference

Integrator-facing contracts: the machine mode, the HTTP API, tools, training, ACP, one folder per platform. The [documentation router](../CLAUDE.md) says which application a change belongs to.

| Folder | Application |
|---|---|
| [`cli/`](cli/) | **The CLI**, the harness itself: commands, backends, the TUI, `serve`, machine mode |
| `gui/` | **Every GUI app**, what binds every graphical app alike, whatever its language |
| `darwin/` | **The Apple apps**, macOS, iPadOS and iOS, one Swift codebase; an item or note for one OS sits in its subfolder |
| `mod/` | **The mod**, the Claude plugin, driving the same binary |
| `unix/` | **The Linux app**, not built yet |
| `windows/` | **The Windows app**, not built yet |
