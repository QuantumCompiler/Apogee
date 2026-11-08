# Architecture Decision Records: the Apple apps

The records that bind the Apple apps (macOS, iPadOS and iOS, one Swift codebase) and no other. They belong to the GUI's one numbered series: what binds every app, **0001** (four layers, whatever the language) and **0003** (parity), is in [`adrs/gui/`](../gui/README.md), with the series' format. A record for one OS alone sits in `macos/`, `ipados/` or `ios/`.

| ADR | Rule |
|---|---|
| [0002](presentation-mvvm.md) | Presentation modules are MVVM (`Views/`, `ViewModels/`, module-specific extras), sharing one `styles` module |
| [0004](module-schemes-and-previews.md) | The macOS app: every Presentation module has its own shared scheme, and every view it draws has a hermetic `#Preview` over the binary's recorded documents |
