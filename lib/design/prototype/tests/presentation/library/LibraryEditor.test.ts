import { describe, expect, test } from 'vitest'
import { editorFiles, libraryFolders, refusedSpec, typedLine } from '../../../source/presentation/library/Previews/LibraryWorlds'
import { libraryPlace, settle } from './LibraryHarness'

const refusal = "apogee symphonies: /Users/someone/.apogee/symphonies/notes-digest.yaml: stages[1]: unknown key 'temprature' (accepted: name, role, play, prompt, input, schema, image, brief_tokens, answer_tokens) (exited with status 1)"

/** The agents' editor open on the triage prompt. */
async function agentEditor() {
  const place = libraryPlace()
  await place.library.appeared()
  place.library.agents.edit('prompts/triage.txt')
  return place
}

/** The symphonies' editor open on notes-digest, on the `library:refused` world. */
async function symphonyEditor() {
  const place = libraryPlace({ preset: 'library:refused' })
  await place.library.appeared()
  place.library.pick('symphonies')
  place.library.pickRow('notes-digest')
  place.library.symphonies.edit()
  return place
}

const fileOn = (place: ReturnType<typeof libraryPlace>, folder: string, name: string) => place.child.folders.files(folder).find((file) => file.name === name)?.text

describe('the editor flows (design 1n): the Mac app’s LibraryEditorCloseTests, on the old prototype’s 4-tick save', () => {
  test('typed text then Close asks first, the editor left open; Keep editing keeps every character; Discard closes and writes nothing', async () => {
    const place = await agentEditor()
    const { library, asked, sheets } = place
    library.setEditorText(editorFiles.triage + typedLine)
    library.requestClose()
    expect(asked).toEqual(['editorClose'])
    expect(library.confirmWords('editorClose')).toEqual({ title: 'Discard your changes to triage.txt?', body: "What you typed isn't saved, and closing loses it.", cancel: 'Keep editing', alternative: '', confirm: 'Discard', destructive: true })
    expect(library.editorFace().text).toBe(editorFiles.triage + typedLine)
    library.dismissPending('editorClose')
    expect([library.editorFace().shown, library.editorFace().text, library.confirmWords('editorClose')]).toEqual([true, editorFiles.triage + typedLine, undefined])
    library.requestClose()
    await library.confirm('editorClose', false)
    expect(library.editorFace().shown).toBe(false)
    expect(sheets.at(-1)).toBe('dismissed')
    expect(fileOn(place, libraryFolders.prompts, 'triage.txt'), 'discarding writes nothing').toBe(editorFiles.triage)
  })

  test('the save is read-only until its verdict, 4 ticks later; then it says the CLI checks an agent when it runs, and Close loses nothing', async () => {
    const place = await agentEditor()
    const { library, clock, asked } = place
    library.setEditorText(editorFiles.triage + typedLine)
    void library.save()
    expect(fileOn(place, libraryFolders.prompts, 'triage.txt'), 'written at once').toBe(editorFiles.triage + typedLine)
    for (let tick = 1; tick <= 3; tick += 1) {
      clock.advance(1)
      await settle()
      const editor = library.editorFace()
      expect([editor.saving, editor.canSave, editor.busy], `tick ${tick}`).toEqual([true, false, "Saving: the editor waits for apogee's word on the file, then takes your typing again."])
    }
    clock.advance(1)
    await settle()
    const editor = library.editorFace()
    expect([editor.saving, editor.busy, editor.verdict]).toEqual([false, '', { text: "Saved. Apogee checks an agent's prompt and schema when it runs -- its first run says what is wrong, if anything.", refused: false }])
    expect(editor.restore, 'an agent’s file can be put back after any save').toEqual({ shown: true, enabled: true })
    library.requestClose()
    expect([library.editorFace().shown, asked]).toEqual([false, []])
  })

  test('closing during a save, or with nothing to lose, closes at once and asks nothing; the save’s end does not bring it back', async () => {
    const place = await agentEditor()
    const { library, clock, asked } = place
    library.requestClose()
    expect([library.editorFace().shown, asked]).toEqual([false, []])
    library.agents.edit('prompts/triage.txt')
    library.setEditorText(editorFiles.triage + typedLine)
    void library.save()
    library.requestClose()
    expect([library.editorFace().shown, asked]).toEqual([false, []])
    clock.advance(4)
    await settle()
    expect(library.editorFace().shown).toBe(false)
  })

  test('a refused save: its verdict the CLI’s refusal; Close offers put it back, leave it, or keep editing', async () => {
    const place = await symphonyEditor()
    const { library, clock, asked } = place
    expect(library.editorFace().text).toBe(editorFiles.notesDigest)
    library.setEditorText(refusedSpec(editorFiles.notesDigest))
    void library.save()
    clock.advance(4)
    await settle()
    const editor = library.editorFace()
    expect(editor.verdict).toEqual({ text: refusal, refused: true })
    expect(editor.restore).toEqual({ shown: true, enabled: true })
    expect(fileOn(place, libraryFolders.symphonies, 'notes-digest.yaml'), 'the refused text is on disk').toBe(refusedSpec(editorFiles.notesDigest))
    library.requestClose()
    expect(asked).toEqual(['editorClose'])
    expect(library.confirmWords('editorClose')).toEqual({
      title: 'notes-digest.yaml holds the text apogee refused',
      body: "Put it back as it was writes the text it held when you opened it, and apogee reads it again. Leave it keeps the refused text in the file, and apogee can't play it until it is fixed.",
      cancel: 'Keep editing',
      alternative: 'Leave it',
      confirm: 'Put it back as it was',
      destructive: false,
    })
    library.dismissPending('editorClose')
    expect(library.editorFace().shown, 'Keep editing: the editor as it was').toBe(true)
  })

  test('the refused text saved again is refused again, and Close asks again: apogee judges what the file holds, as the old save does', async () => {
    const place = await symphonyEditor()
    const { library, clock, asked } = place
    library.setEditorText(refusedSpec(editorFiles.notesDigest))
    void library.save()
    clock.advance(4)
    await settle()
    library.requestClose()
    library.dismissPending('editorClose')
    const saved = library.save()
    clock.advance(4)
    await saved
    await settle()
    expect(library.editorFace().verdict).toEqual({ text: refusal, refused: true })
    library.requestClose()
    expect([library.editorFace().shown, asked]).toEqual([true, ['editorClose', 'editorClose']])
  })

  test('on any preset, a spec saved with the unknown key is refused: the file’s text decides, not the preset', async () => {
    const place = libraryPlace()
    const { library, clock, asked } = place
    await library.appeared()
    library.pick('symphonies')
    library.pickRow('notes-digest')
    library.symphonies.edit()
    library.setEditorText(refusedSpec(editorFiles.notesDigest))
    const saved = library.save()
    clock.advance(4)
    await saved
    expect(library.editorFace().verdict).toEqual({ text: refusal, refused: true })
    library.requestClose()
    expect(asked).toEqual(['editorClose'])
  })

  test('Put it back as it was writes the original, asks show again, and closes once apogee has answered', async () => {
    const place = await symphonyEditor()
    const { library, clock, child } = place
    library.setEditorText(refusedSpec(editorFiles.notesDigest))
    void library.save()
    clock.advance(4)
    await settle()
    library.requestClose()
    const putBack = library.confirm('editorClose', false)
    await settle()
    expect(fileOn(place, libraryFolders.symphonies, 'notes-digest.yaml'), 'the original is written back first').toBe(editorFiles.notesDigest)
    expect([library.editorFace().shown, library.editorFace().saving], 'open until apogee has answered').toEqual([true, true])
    clock.advance(4)
    await putBack
    await settle()
    expect(library.editorFace().shown).toBe(false)
    expect(child.calls.filter((call) => call.arguments.join(' ') === 'symphonies show notes-digest').length).toBe(2)
  })

  test('Leave it closes with the refused text on disk, and asks nothing more of the binary', async () => {
    const place = await symphonyEditor()
    const { library, clock, child } = place
    library.setEditorText(refusedSpec(editorFiles.notesDigest))
    void library.save()
    clock.advance(4)
    await settle()
    library.requestClose()
    const calls = child.calls.length
    await library.confirm('editorClose', true)
    expect(library.editorFace().shown).toBe(false)
    expect(fileOn(place, libraryFolders.symphonies, 'notes-digest.yaml')).toBe(refusedSpec(editorFiles.notesDigest))
    expect(child.calls.length).toBe(calls)
  })

  test('a symphony save that stands says apogee reads it, and closes with no prompt', async () => {
    const place = libraryPlace()
    const { library, clock, asked } = place
    await library.appeared()
    library.pick('symphonies')
    library.pickRow('notes-digest')
    library.symphonies.edit()
    library.setEditorText(editorFiles.notesDigest.replace('Summarize then check.', 'Summarize, then check.'))
    void library.save()
    clock.advance(4)
    await settle()
    expect(library.editorFace().verdict).toEqual({ text: 'Saved, and apogee reads it.', refused: false })
    expect(library.editorFace().restore.shown, 'a symphony is put back only after a refused save').toBe(false)
    library.requestClose()
    expect([library.editorFace().shown, asked]).toEqual([false, []])
  })
})
