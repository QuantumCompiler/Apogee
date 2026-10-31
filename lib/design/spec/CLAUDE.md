# CLAUDE.md — the Apogee Desktop design

Context for agents working on **the design** of Apogee's desktop applications. The design is the reference the apps under `lib/src/darwin/`, `linux/` and `windows/` are built from; the apps' own docs are [`lib/documentation/gui/`](../../documentation/gui/README.md). [README.md](README.md) has the file layout and how to open the design; this file says how to work on it.

## ⚠ The repo and the canvas move together

These files are also the **Apogee Desktop** design canvas, <https://claude.ai/artifact/HRGyHM1ZkRL3D1bWGXQGk7>, which lays them out side by side for review. Every design change lands in both, in the same piece of work, with the files kept identical:

| Canvas | Repo |
|---|---|
| `project/App.dc.html` | `html/main.html` |
| `project/<name>.dc.html` | `html/<name>.dc.html` |
| `project/canvas.json` | `json/canvas.json` |
| `project/handoff/tokens.json` | `json/tokens.json` |

- **Edit here, then publish.** Copy each changed file to its canvas path under a scratch folder and publish that folder's files to the canvas.
- **Read first if the canvas moved.** If someone edited on the canvas since its last publish, read those files down and copy them here before changing anything.
- **Never send `html/support.js` or these two `.md` files to the canvas.** They exist only in the repo; the canvas serves its own runtime.
- **Send `canvas.json` only when the layout changes:** boards added or removed, pages, headings or titles.
- **Don't commit** unless asked. Other sessions work in this checkout, and have swept untracked design files into their own commits before.

## What a change touches

| Changing | Update |
|---|---|
| A screen, a flow, copy, behavior | `html/main.html`. Its screen boards follow on their own. |
| A new screen or state worth its own board | A start preset in `applyStart()` and in `data-props`' `start` options in `main.html`; a new `html/Screen<Name>.dc.html` copied from a sibling, with its `start`; its entry in `json/canvas.json` (page `build`, next free slot in its row); a row in README.md. |
| What a part of the design needs from the CLI | `html/BuildContract.dc.html`: its row in the contract map, and the gap list if the CLI doesn't offer it. |
| A color | The theme table in `main.html`'s `themes()` (all three themes) and `colorGroups()`; the rows in `html/ColorTokens.dc.html`; `colors` in `json/tokens.json`. |
| A size, a region, a key | `html/BuildLayout.dc.html`, `json/tokens.json`'s layout values, and for a key, the Settings › Keyboard list in `main.html` (`set.keys`). |
| A component's look | The component boards (`html/Components*.dc.html`) when the piece appears there, so they match the prototype. |

`Main`, `Inspector`, `Trace`, `Chat` and `Appearance` are the exploration that led here. Leave them as they are.

## The design's rules

- **Every part maps to the CLI.** The app drives `apogee` over the machine-mode pipes and through the same commands a person types ([machine-mode.md](../../documentation/cli/reference/machine-mode.md)). Before drawing something, find the event or command it comes from in that document or the binary's `--help`. If none exists, draw it with its fallback and add the gap to the contract board; never design as if it existed.
- **Honest by default.** Say what the CLI says, in its words where it has them. Never invent a time estimate, a size or a count the CLI doesn't report; mark anything billed or sent off the machine.
- **Every color is a token.** Use `var(--surface-window)` and its 24 siblings, never a hex. The only fixed values are the modal scrim, the shadows and the window controls. Type sizes are `var(--t-…)` and fonts `var(--font-ui)` and `var(--font-mono)`, so Settings › Appearance changes everything.
- **Keyboard first, accessible as drawn.** Real `<button>`, `<input>` and `<label>` elements, an `aria-label` on icon-only buttons, a key for every destination, and text at 4.5:1 against what it sits on.
- **Copy is plain words:** short sentences, no jargon a person wouldn't use, no emoji.

## The prototype, `html/main.html`

One file holds the whole app.

- **The markup:** the sidebar (`<aside class="ap-side">`), then one block per screen, `<sc-if value="{{ui.isChat}}">` through `ui.isVoice`, then the overlays (`ui.monitorOpen`, `ui.settingsOpen`, the sheets, the toast).
- **The constructor** holds every bit of canned data in `this.state`: sessions, models, jobs, tasks, clips, compositions, speeches. Then `applyStart()` sets up the screen the `start` prop names.
- **`tick()`** runs every 400 ms, unless `still` is set. It drives anything that moves: streaming, downloads, tasks, clip runs, renders, speech.
- **`renderVals()`** is split into sections marked `/* ---------- name ---------- */`. Each builds one object the template reads, such as `chat`, `ins`, `mdl`, `vid`, `comp` or `vox`, and the method returns them all at the end.
- **To change a screen,** edit its markup block and the section that feeds it.

## The format, and the rules that bite

Each `.dc.html` file, and `main.html`, is a Design Component: HTML inside `<x-dc>` plus a `<script type="text/x-dc" data-dc-script>` holding `class Component extends DCLogic`. These rules fail silently when broken:

- **`{{hole}}` is a dotted lookup into `renderVals()`**, or a literal such as `{{ true }}`. Never an expression: compute it in `renderVals()` and expose the result by name.
- **An attribute that is one whole hole gets the raw value** (a function, a boolean, a number). Mixed text is interpolated.
- **Events are camelCase attributes holding a function:** `onClick="{{item.pick}}"`. A handler for each item in a loop is built in `renderVals()`.
- **`<sc-if value="{{x}}">` and `<sc-for list="{{xs}}" as="x">`** render no element of their own. A loop gives `{{ $index }}`.
- **`<dc-import name="App" start="video" still="{{ true }}">`** mounts another board, `App.dc.html` on the canvas and `main.html` here. Its other attributes become props.
- **State changes only through `this.setState({...})`** with new objects, never by mutating state in place.
- **Write it the canvas's way:**
  - Keep the `<script src="./support.js"></script>` line, quote every attribute, and close every element.
  - Plain JavaScript only, with no imports.
  - `data-props` is single-quoted JSON, so write `&amp;`, `&#39;` and `\"` inside it.
- **A `<button>` falls back to the browser's gray background** unless something sets one. `main.html`'s helmet resets it; other boards set a background per class.

## Checking a change

Open it in a browser, as README.md describes: serve `html/`, then open `main.html?start=<screen>`. Add `&still=true` for a frame that doesn't move, or `?source=dom` to see what opening the file from disk does. Before the page renders, the console logs a few errors about `{{…}}` attribute values; the browser parses the raw template first, and they are expected.

For a fast check without a browser, this runs the prototype's logic for every start screen, 30 ticks each, and reports anything that throws or reads `undefined` or `NaN`:

```bash
node -e '
const fs = require("fs");
const src = fs.readFileSync("lib/design/spec/html/main.html", "utf8");
const tag = /<script[^>]*data-dc-script[^>]*>/.exec(src)[0];
const code = /<script[^>]*data-dc-script[^>]*>([\s\S]*?)<\/script>/.exec(src)[1];
const starts = JSON.parse(/data-props=\x27([^\x27]*)\x27/.exec(tag)[1].replace(/&amp;/g, "&").replace(/&#39;/g, "\x27")).start.options;
function DCLogic(p) { this.props = p || {}; this.state = {}; }
DCLogic.prototype.setState = function (p) { Object.assign(this.state, typeof p === "function" ? p(this.state) : p); };
const Component = new Function("DCLogic", code + ";return Component;")(DCLogic);
const bad = [];
const walk = (o, at, d) => { if (d > 9 || o == null) return; if (typeof o === "number" && isNaN(o)) bad.push(at); else if (typeof o === "string" && /undefined|NaN/.test(o)) bad.push(at + " = " + o.slice(0, 60)); else if (typeof o === "object") for (const k in o) walk(o[k], at + "." + k, d + 1); };
for (const s of starts) { try { const c = new Component({ start: s, still: true }); for (let i = 0; i < 30; i++) c.tick(); walk(c.renderVals(), s, 0); } catch (e) { bad.push(s + ": " + e.message); } }
console.log(starts.length + " start screens, " + (bad.length ? bad.join("\n") : "no problems"));
'
```

The same setup, `new Component({ still: true })`, also reads the theme table: `themes()` and `colorGroups()` are what `json/tokens.json`'s `colors` must match.

## `html/support.js`

The repo's standalone runtime: it fills each file's template, wires its events and patches the page on `setState`, with no dependencies. It reads its own source when served over http, and the parsed page when opened from disk. Change it only when it renders something differently from the canvas, keep it dependency-free, and keep its `App` → `main.html` alias in step with the map above.
