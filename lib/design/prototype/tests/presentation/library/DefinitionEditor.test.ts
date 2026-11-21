import { describe, expect, test } from 'vitest'
import { DefinitionEditor } from '../../../source/presentation/library/ViewModels/DefinitionEditor'

const path = '/Users/someone/.apogee/prompts/triage.txt'

describe('the definition editor (design 1n): the Mac app’s DefinitionEditor, its close reasons and its words (LibraryViewModels.swift:11-107)', () => {
  test('nothing typed loses nothing; typing is unsaved; a save that stood clears it; a save running closes at once', () => {
    const editor = new DefinitionEditor('triage · prompts/triage.txt', path, 'Sort the issue by area.')
    expect([editor.changed, editor.unsaved, editor.closeReason]).toEqual([false, false, undefined])
    editor.text = 'Sort the issue by area, then by urgency.'
    expect([editor.changed, editor.unsaved, editor.closeReason]).toEqual([true, true, 'unsaved'])
    editor.wrote(editor.text)
    editor.saving = true
    expect(editor.closeReason, 'a save running closes at once').toBeUndefined()
    editor.text = 'Sort the issue by area, then by urgency, then by owner.'
    expect(editor.closeReason, 'typing the save has not taken yet still closes at once while it runs (GUI 4h)').toBeUndefined()
    editor.text = 'Sort the issue by area, then by urgency.'
    editor.endSave(editor.text, 'Saved.', false)
    expect([editor.saving, editor.unsaved, editor.closeReason, editor.verdict]).toEqual([false, false, undefined, 'Saved.'])
  })

  test('a refused save leaves the refused text on disk: closing asks about the original first, and says typing since is lost either way', () => {
    const editor = new DefinitionEditor('notes-digest', '/Users/someone/.apogee/symphonies/notes-digest.yaml', 'a: 1\n')
    editor.text = 'a: 1\nb: 2\n'
    editor.wrote(editor.text)
    editor.saving = true
    editor.text = 'a: 1\nb: 2\nc: 3\n'
    editor.endSave('a: 1\nb: 2\n', 'refused', true)
    expect(editor.verdict, 'text that moved on during the save is said unsaved').toBe("refused\nWhat you typed since pressing Save isn't saved yet.")
    expect([editor.refused, editor.onDiskRefused, editor.rewritten, editor.closeReason]).toEqual([true, true, true, 'refusedOnDisk'])
    expect(editor.question('refusedOnDisk')).toBe('notes-digest.yaml holds the text apogee refused')
    expect(editor.explanation('refusedOnDisk')).toBe(
      "Put it back as it was writes the text it held when you opened it, and apogee reads it again. Leave it keeps the refused text in the file, and apogee can't play it until it is fixed. What you typed since the save is lost either way.",
    )
  })

  test('the unsaved prompt names the file, in plain words', () => {
    const editor = new DefinitionEditor('triage · prompts/triage.txt', path, 'x')
    expect(editor.fileName).toBe('triage.txt')
    expect(editor.question('unsaved')).toBe('Discard your changes to triage.txt?')
    expect(editor.explanation('unsaved')).toBe("What you typed isn't saved, and closing loses it.")
  })
})
