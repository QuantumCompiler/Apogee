# Apogee design

The design of Apogee's GUI applications: an interactive prototype of the whole app, built from the CLI's own recorded output, with a gallery of every screen and state, the foundations (color tokens, type, components) and the build pages that map the design to the CLI's machine-mode contract.

It is a reference for building the apps under `lib/src/darwin/`, `linux/` and `windows/`, not an input to any build, and CI never reads it.

## Opening it

Double-click `main.html`. It opens from disk in a browser, with no server, at a chat.

- **A screen or state:** `main.html?start=models:store`, `?start=settings:appearance&still=true` (`still` stops the clock).
- **The gallery**, every screen and state side by side with the foundations: `main.html?gallery`, or ⌥⌘G in the app.

## Layout

| Path | What it is |
|---|---|
| `main.html` | **The design**, built: one self-contained file, written only by the build. |
| `prototype/` | Its source: a React and TypeScript app in four layers, mirroring the Mac app module for module, with its tests and its build. |
| `tokens.json` | The design's tokens: 25 colors in three themes, the type scale, fonts, radii, spacing, layout sizes and motion. The one place a token is written, and the one file other apps copy from. |
| `assets/` | The logo, `Logo.png`, the master the macOS app's icon is made from, and its 320-pixel copy. |

How to work on it, the design's rules and what each kind of change touches are in [the design's guide](../documentation/assistant/design/CLAUDE.md); how it is built and checked is its [DEVELOPER.md](../documentation/assistant/design/DEVELOPER.md).
