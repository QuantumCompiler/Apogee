# The Library editor keeps edits, and Play gives an image to a symphony that takes one

**What / why.** The Library's editor (an agent's prompt or schema, a symphony's own spec file) is where a person changes the files the CLI reads. Today it drops their work without asking. Close and Esc throw away unsaved typing. After a save the binary refused, Close leaves the broken file on disk and loses the only copy of the working original. Separately, Play can never run a symphony whose input takes an image, though Apogee ships one. Found by the second bug sweep, 2026-10-31, and re-read at HEAD (`b26550bb`) the same day:

1. **Close and Esc discard edits without asking, and after a refused save the original is gone too.** Both editors share `EditorSheet`, whose `Button("Close", action: close).keyboardShortcut(.cancelAction)` (`source/presentation/library/Views/LibraryScreen.swift:390`) makes Esc the same act. `close` is `closeEditor()`, which is `editor = nil` and nothing else (`source/presentation/library/ViewModels/LibraryViewModels.swift:201-203` for agents, `:382-384` for symphonies). The sheets' dismiss bindings also go straight to `closeEditor()` (`LibraryScreen.swift:55`, `:58`). Neither path checks `DefinitionEditor.changed` (`LibraryViewModels.swift:23`) or `refused` (`:18`).
   - **Unsaved typing.** A person edits a spec for several minutes, then presses Close, or Esc reaches the cancel action. Everything typed is gone, and nothing asked first.
   - **A refused symphony save.** `SymphoniesViewModel.save()` writes the spec to its file first (`saved.write(toFile: editor.path, atomically: true, …)`, `:352`). Only then does it ask `symphonies show` (`:361`). If the binary refuses the file (say `stages[1]: unknown key`), the refused text is already on disk and the working original exists only in `DefinitionEditor.original` (`:13`). "Put it back as it was" (`LibraryScreen.swift:388`, `restore()` at `LibraryViewModels.swift:375`) is the only way back. If the person presses Close instead, the broken spec stays on disk, the symphony no longer reads, and the app has no way left to recover it.
   - **The agents editor** writes the same way (`:185`) and is given `restore: nil` (`LibraryScreen.swift:56`), so it never offers the way back. Nothing checks an agent's files before they run (the save's own verdict says so, `:197`). Its loss is unsaved typing and the pre-save text.
   - **What the record covers.** GUI MILESTONES:2057 ("No save loses typing", 4h) is about typing *during* a save. The one recorded decision about closing (MILESTONES:2072, "Closing the editor mid-save leaves it closed") is about closing *mid-save*. Neither covers discarding unsaved edits, so this is a gap, not a reversal.
   - **A second defect under the first.** `changed` compares the text with `original`, and `original` is a `let` that a save never moves (`endSave`, `:29-33`). After a successful save, `changed` stays true while nothing is unsaved. The prompt cannot key on `changed` as it stands.
2. **Play always fails for a symphony that takes an image.** `playIt()` always runs `["symphonies", "play", symphony.name, "--input", input, "--output-format", "json"]` (`LibraryViewModels.swift:309`), with no way to attach an image. `Symphony.takesImage` is decoded (`source/business/reads/Library.swift:113`, `:128`), and grep finds no other use of it in `source/` or `tests/`. Play is disabled only while a play runs or the input is empty (`LibraryScreen.swift:211`). The shipped `describe-answer` (CLI 27q, `lib/src/cli/source/data/contracts/assets_symphonies.cpp:53-64`) declares `input: {description: "A question about the image, which is given with --image.", image: true}`. The binary refuses any play of it without an image (`lib/src/cli/source/business/symphony/runner.cpp:304-306`). It also refuses one *with* an image for a symphony that takes none (`:307-309`).
   - **Probe (sandbox, 2026-10-31).** `symphonies list --output-format json` gives describe-answer `"input":{"description":"A question about the image, which is given with --image.","image":true}`. `symphonies play --help` lists `--image PATH` ("An image for a symphony whose input takes one (png, jpg, gif, webp, bmp)", `symphonies_cmd.cpp:711-713`). `apogee symphonies play describe-answer --input "what is it" --output-format json` → `apogee symphonies: 'describe-answer' takes an image with its input, and none was given`, exit 1.
   - **The failure.** Library › Symphonies › describe-answer: type a question, press Play, and it fails the same way every time. The page does show the input description (`LibraryScreen.swift:208`), but that text names a CLI flag the app gives no way to set.

What already works: a save never loses typing *during* the save (4h's `saving`), a refused symphony save is said in the binary's words with "Put it back as it was" beside it, and every other shipped symphony plays (2e).

**Core constraint(s).**
- **The GUI is a machine-mode client.** The binary still judges a spec (`symphonies show`) and still refuses an image where none belongs. The app checks nothing of its own beyond "does this symphony's own document say it takes an image".
- **Every view can be seen on its own** (ADR 0004). The close prompt's states and the image row each get a named `#Preview` over the module's `Previews/` fakes and recorded documents.
- **Presentation keeps the MVVM shape** (ADR 0002). Whether closing needs a prompt, and the play's argv, are view-model state, tested without a window. The view only draws them.
- **No save loses typing** (4h) and **closing mid-save leaves it closed** (4h's recorded decision) both stay as built.
- **Development runs on the dev channel.** Any live pass drives `apogee-dev` over `~/.apogee-dev`, never the release `apogee` or `~/.apogee`. Until M18 ([`app-dev-channel.md`](../maintenance/app-dev-channel.md)) lands, a live pass of a development build is asked of the user first.
- **Hermetic tests**: scripted fakes and recorded documents, no binary, no network, no window.

**Seam + files.**
- `source/presentation/library/ViewModels/LibraryViewModels.swift`:
  - `DefinitionEditor` records the text on disk (`onDisk`, set at open and moved by every write, including a restore). `unsaved` becomes `text != onDisk`. `changed` stays "differs from the original" for Save's and restore's own uses.
  - A close request, `requestClose()`, on both view-models. It closes at once when there is nothing to lose. Otherwise it raises a prompt, `closing`, with one of two reasons:
    - `.unsaved`: typing not yet saved.
    - `.refusedOnDisk`: the symphony's last save was refused, and the file now holds that text rather than the original.
  - Each prompt choice is its own method: discard and close; put it back as it was and close (restore, then close once the binary has answered); leave the file as it is and close; keep editing.
  - `SymphoniesViewModel` gains `image: String?`. `playIt()` adds `--image <path>` when, and only when, `current.takesImage` and an image is chosen. `canPlay` also requires an image when the symphony takes one. Choosing another symphony clears the image.
  - The agents editor gains its restore (see Open calls).
- `source/presentation/library/Views/LibraryScreen.swift`:
  - `EditorSheet`'s Close, and Esc through `.cancelAction`, call `requestClose`. The sheets' dismiss bindings (`:55`, `:58`) route through it, with `.interactiveDismissDisabled` while there is something to lose. A `confirmationDialog` draws the prompt, as the Library's deletes already do (`:153`, `:237`, `:315`).
  - The symphony page shows an image row, "Choose an image…", with the file's name and Remove, only where `takesImage`. A `fileImporter` limits the choice to the CLI's types. Play is disabled with a line saying the symphony takes an image until one is chosen.
- `source/presentation/library/Previews/`: the unsaved prompt, the refused-on-disk prompt, and describe-answer with and without an image chosen (its preview document is already there, `PreviewDocuments.swift:216-239`).
- Tests: `tests/presentation/library/` (a new `LibraryEditorCloseTests.swift` beside `LibraryCorrectnessTests.swift`, over `Scripted.swift`). The play's argv is pinned there over the recorded `tests/business/reads/Fixtures/symphonies-list.json`, whose describe-answer has `"image": true`.
- Docs: GUI `DEVELOPER.md` (the library module's editor and Play), `MILESTONES.md` on ship. The design canvas (`lib/design/spec`) gets the new states (see Open calls).

**Reference.** 2e (`agents-mcp-symphonies`, GUI MILESTONES:990) built the editor, Put it back as it was, and Play. 4h (`library-tasks-review-correctness`, MILESTONES:2057, :2072) made a save keep typing and settled closing mid-save. The Video place's clip sheet is the in-house precedent for an image passed to the CLI: `ClipForm.image` becomes `--image` (`source/business/media/ClipForm.swift:27`, `:66`), chosen by a `fileImporter` (`source/presentation/video/Views/VideoScreen.swift:347`). The Sessions place's private-switch question (4a) is the precedent for asking before an act loses something.

**Decisions made** (dated):
- 2026-10-31: Specced from the second CLI/GUI bug sweep (the user's call: every finding becomes a v0.2.1 item in its queue, CLI track 57, GUI track 6).
- 2026-10-31: One item for both findings: both are the Library's own pages (one module, one `LibraryViewModels.swift`, one `LibraryScreen.swift`). Each is a place where the page loses or blocks what the person brought, their text or their image, and one pass over the module fixes both.
- 2026-10-31: This is a gap, not a reversal. 4h's two editor rules stand: typing during a save is kept, and an editor closed mid-save stays closed. Closing mid-save is not prompted, because 4h already keeps that text and says it is unsaved.
- 2026-10-31: The fix is the app's alone. `symphonies show` and `symphonies play --image` already do what is needed, so there is no CLI sibling.

**Open calls:**
- [default: the prompt keys on what is *on disk*, not on `changed`. Opening and then closing without typing, or closing after a save that stood, closes at once with no prompt.]
- [default: the prompt's words. Unsaved: "Discard your changes to <file>?" with Discard and Keep editing. Refused on disk: "<file> holds the text apogee refused" with Put it back as it was, Leave it, and Keep editing. Esc on the prompt is Keep editing. Wording settled at build, in the CLI's plain voice.]
- [default: the agents editor gains "Put it back as it was" after any save, not only a refused one. Nothing checks an agent's files before it runs, so a save is the only point to undo from. Closing after a save that stood does not prompt (the person's text is saved, not lost).]
- [default: keep write-then-check, 2e's and 4h's shape, and make Close safe. Checking a copy before writing is not taken. A copy inside the symphonies folder is read by the folder scan (`lib/src/cli/source/business/symphony/definition.cpp:93`) while it exists, and a copy outside it is not the file the CLI resolves.]
- [default: the image chooser accepts the CLI's list (png, jpg, gif, webp, bmp). The binary judges the file. A refusal is shown in its words, as any play's failure is (`LibraryViewModels.swift:325`).]
- [default: an image is offered only where the symphony's own document says `image: true`. One that takes none shows no image row, so the CLI's "takes no image" refusal cannot be reached from the page.]
- [default: the design canvas's Library board draws the two prompts and the image row in the same change, as M12 drew the states track 4 added.]
- [default: an image in an execute session's `/play` is out of scope. That is the session window's attach path, not this page.]

**Guardrail(s).** Pinned in `tests/presentation/library/`, over scripted fakes:
- **Unsaved close.** Typed text then `requestClose()` raises `.unsaved` and leaves the editor open. Discard closes. Keep editing keeps the text.
- **No prompt when nothing would be lost.** Opening then closing, or closing after a save that stood, closes with no prompt (the `onDisk` fix: `changed` alone would prompt here).
- **Refused on disk.** A scripted `symphonies show` refusal (`stages[1]: unknown key`), then `requestClose()`, raises `.refusedOnDisk`. "Put it back and close" writes the original, re-runs `show`, and closes only after it answers. Leave it closes with the refused text on disk.
- **Agents restore.** After a save, the agents editor's restore writes the original back.
- **Closing mid-save** still closes at once (4h's test stays green).
- **Play argv.** describe-answer from the recorded `symphonies-list.json`: no image means Play is unavailable and no job is started. A chosen image gives exactly `symphonies play describe-answer --input <text> --image <path> --output-format json`. A symphony with `image: false` never carries `--image`. Switching symphonies clears the image.
- **Previews.** Each new state has its named preview.

**Acceptance criteria:**
- [ ] Close or Esc on an editor with unsaved typing asks before discarding, and Keep editing keeps every character.
- [ ] After a refused symphony save, closing offers to put the file back as it was; the working original can no longer be lost silently.
- [ ] An agent's prompt or schema can be put back as it was after a save.
- [ ] An editor with nothing unsaved still closes at once; 4h's mid-save behaviour is unchanged.
- [ ] Library › Symphonies › describe-answer, given a question and an image, plays; without an image, Play is not offered and the page says an image is needed.
- [ ] The new states have previews; the full suite is green (`lib/scripts/cicd.sh --app darwin --test`).

**Scope note.** Earmarked for v0.2.1 (track 6); ungated. Out of scope:
- editing a shipped starter or a config entry in the app (still `symphonies edit` at a prompt, 2e's decision);
- any GUI-side check of an agent's or symphony's files;
- an image in the session window's `/play`;
- every other place's sheets (each would need its own audit).
