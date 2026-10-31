# GUI reference

Integrator-facing contracts the GUI itself exposes. **Born empty** — the GUI is a client, and the contract it *consumes* is the CLI's: the [stdio machine mode](../../cli/reference/machine-mode.md), pinned to the binary by the CLI's own conformance tests. A document lands here only when the GUI exposes a contract of its own (a plugin surface, a theming format, an automation interface — none planned); until then, this README is deliberately the directory's only file.
