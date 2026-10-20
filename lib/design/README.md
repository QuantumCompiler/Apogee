# Apogee Desktop design

The design of the desktop applications: an interactive prototype of the whole app, a board for every screen and state, the foundations (color tokens, type, components), and the build spec that maps the design to the CLI's machine-mode contract.

These files are the source files of the **Apogee Desktop** design canvas, <https://claude.ai/artifact/HRGyHM1ZkRL3D1bWGXQGk7>. The canvas renders them; this directory keeps them in the repo. They are a reference for building the apps under `lib/src/darwin/`, `linux/` and `windows/` ([GUI docs](../documentation/gui/README.md)), not an input to any build, and CI never reads them.

## What is here

| File | What it is |
|---|---|
| `canvas.json` | The canvas index: every board's place, size and title, the pages (Build, Foundations, Exploration) and their headings. |
| `App.dc.html` | The interactive prototype: every screen, overlay and flow in one file. Its `start` property picks the opening screen and `still` stops its clock. |
| `Screen*.dc.html` | One board per screen and state (29). Each mounts `App.dc.html` with a `start`, so they cannot drift from the prototype. |
| `BuildContract.dc.html` | How the app talks to Apogee: startup, each part of the design mapped to the event or command behind it, the CLI gaps (G1–G15) with their fallbacks, and the decisions the design makes. |
| `BuildLayout.dc.html` | Regions and sizes, navigation and keys, the states every screen draws, and each piece as SwiftUI, Qt Quick and WinUI. |
| `BuildProcesses.dc.html` | The process diagram: the app, its `apogee` children, and the files it reads and writes. |
| `handoff/tokens.json` | Machine-readable tokens: 25 colors in three themes, type scale, fonts, radii, spacing, layout sizes and motion. Generated from the prototype's theme table. |
| `ColorTokens.dc.html`, `TypeSpace.dc.html` | Foundations: the color tokens and their contrast; type, space and shape. |
| `Components*.dc.html` | Components: model, context and sources; the turn; asking and failing; making media. |
| `Main`, `Inspector`, `Trace`, `Chat`, `Appearance` `.dc.html` | Exploration: the three first directions and the first merged pass, superseded by the prototype and kept for reference. |

## The format

Each `.dc.html` file is a Design Component: HTML inside `<x-dc>`, with `{{holes}}` filled from the `renderVals()` of a `class Component extends DCLogic` script at the bottom, `<sc-if>` and `<sc-for>` for branches and repeats, and `<dc-import>` to mount another board. Styles are inline and in the `<helmet>`; the script is plain JavaScript. The `./support.js` each file names is the canvas runtime, supplied by the canvas when it serves the file, so the files render in the canvas rather than opened on their own.

## Keeping this in sync

This directory and the canvas hold the same files: canvas path `project/<file>` is `lib/design/<file>` here. A change to the design changes both.

- **Editing here:** publish the changed files to the canvas from this directory, mapping each `project/<file>` to `<file>`. Send `canvas.json` only when boards, pages, headings or titles change.
- **Editing on the canvas:** pull the changed files back down and copy them here before anything else changes them.
- **Adding a board:** add its file and its `canvas.json` entry together, and add a row to the table above.
