# The design's architecture decision records

The standing rules of the design prototype (`lib/design/`), dated and append-only, each with its reason and its enforcement. The format is the CLI's ([its index](../cli/README.md)); the rules every GUI app shares are [the GUI's](../gui/README.md), and the prototype mirrors them.

| # | Record | Date |
|---|---|---|
| [0001](react-design-prototype.md) | The design prototype is a layered React app built into one file: four layers mirroring the Mac app module for module, plain-TypeScript view-models, the binary's own recorded documents, one committed self-contained `lib/design/main.html` with a staleness check, local gates, the design's five rules, and the canvas retired | 2026-11-18 |
