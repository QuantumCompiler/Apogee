# Apogee Desktop design

The design of the desktop applications: an interactive prototype of the whole app, a board for every screen and state, the foundations (color tokens, type, components), and the build spec that maps the design to the CLI's machine-mode contract.

It is a reference for building the apps under `lib/src/darwin/`, `linux/` and `windows/` ([GUI docs](../../documentation/gui/README.md)), not an input to any build, and CI never reads it. The same files also live in the **Apogee Desktop** design canvas, <https://claude.ai/artifact/HRGyHM1ZkRL3D1bWGXQGk7>, where they are laid out side by side.

## Layout

| Path | What it is |
|---|---|
| `CLAUDE.md` | How to work on the design: keeping it in step with the canvas, what each kind of change touches, the format's rules, and how to check a change. Repo-only. |
| `html/main.html` | **The entry point**: the interactive prototype, with every screen, overlay and flow in one file. |
| `html/support.js` | The standalone runtime that lets the design files open in a browser (below). Repo-only: the canvas serves its own. |
| `html/Screen*.dc.html` | One board per screen and state (48), each mounting the prototype at a starting point, so they cannot drift from it. |
| `html/BuildContract.dc.html` | How the app talks to Apogee: startup, each part of the design mapped to the event or command behind it, the CLI gaps (G1–G24, two of them closed) with their fallbacks, and the decisions the design makes. |
| `html/BuildLayout.dc.html` | Regions and sizes, navigation and keys, the states every screen draws, and each piece as SwiftUI, Qt Quick and WinUI. |
| `html/BuildProcesses.dc.html` | The process diagram: the app, its `apogee` children, and the files it reads and writes. |
| `html/ColorTokens.dc.html`, `html/TypeSpace.dc.html` | Foundations: the color tokens and their contrast; type, space and shape. |
| `html/Components*.dc.html` | Components: model, context and sources; the turn; asking and failing; making media. |
| `html/Main`, `Inspector`, `Trace`, `Chat`, `Appearance` `.dc.html` | Exploration: the first three directions and the first merged pass, superseded by the prototype and kept for reference. |
| `json/tokens.json` | Machine-readable tokens: 25 colors in three themes, the type scale, fonts, radii, spacing, layout sizes and motion. Generated from the prototype's theme table. |
| `json/canvas.json` | The canvas index: every board's place, size and title, and the pages (Build, Foundations, Exploration). Its paths are the canvas's. |

## Opening it

Serve the `html/` folder and open the prototype:

```bash
python3 -m http.server 8000 --directory lib/design/spec/html
```

Then go to <http://localhost:8000/main.html>. Any other board opens the same way, by its file name.

- **A starting screen** comes from the URL: `main.html?start=video`, `?start=compose:edit`, `?start=settings:appearance`, `?start=chat:retry`. The full list is the `start` options in `main.html`'s `data-props`. `&still=true` stops the clock.
- **Opened straight from disk** (`file://`), a file renders from the page its browser parsed. That works in current browsers, but the screen boards can't load `main.html` that way, so serve the folder to see them. `?source=dom` shows the disk behavior over http.

## The format

Each `.dc.html` file, and `main.html`, is a Design Component. The page is HTML inside `<x-dc>`, with `{{holes}}` filled from the `renderVals()` of the `class Component extends DCLogic` script at the bottom. `<sc-if>` and `<sc-for>` handle branches and repeats, and `<dc-import>` mounts another board. Styles are inline and in each file's `<helmet>`; the script is plain JavaScript. Each file loads `./support.js` for its runtime.

## Keeping it in sync with the canvas

The repo and the canvas hold the same files, so a change to the design changes both. The canvas's paths map to these:

| Canvas | Repo |
|---|---|
| `project/App.dc.html` | `html/main.html` |
| `project/<name>.dc.html` | `html/<name>.dc.html` |
| `project/canvas.json` | `json/canvas.json` |
| `project/handoff/tokens.json` | `json/tokens.json` |

- **Editing here:** publish each changed file to its canvas path. Send `canvas.json` only when boards, pages, headings or titles change. Never send `support.js`; the canvas has its own.
- **Editing on the canvas:** pull the changed files down and copy them to their paths here before anything else changes them.
- **Adding a board:** add its file and its `canvas.json` entry together, and a row to the layout table above.
