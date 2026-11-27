import { describe, expect, test } from 'vitest'
import { playArguments, sampleImage, sampleInputs } from '../../../source/presentation/library/Previews/LibraryWorlds'
import { libraryPlace, settle } from './LibraryHarness'

async function symphonies(options: Parameters<typeof libraryPlace>[0] = {}) {
  const place = libraryPlace(options)
  await place.library.appeared()
  place.library.pick('symphonies')
  place.library.useSamples(sampleInputs, sampleImage)
  return place
}

describe('the symphonies (design 1n): the Mac app’s SymphoniesViewModel over symphonies-list.json', () => {
  test('the CLI’s nine in its order, each row picking; the first current, its page the old one’s words', async () => {
    const { library } = await symphonies()
    const face = library.face()
    expect(face.rows.map((row) => `${row.name} -- ${row.meta}`)).toEqual([
      'critique-revise -- 2 stages · shipped',
      'describe-answer -- 2 stages · shipped',
      'digest -- 2 stages · shipped',
      'extract-actions -- 1 stage · shipped',
      'extract-facts -- 1 stage · shipped',
      'redact-text -- 2 stages · shipped',
      'summarize-verify -- 2 stages · shipped',
      'quick-check -- 1 stage · config',
      'notes-digest -- 2 stages · file',
    ])
    expect(face.rows.every((row) => row.pickable)).toBe(true)
    const page = face.symphony
    expect([page.name, page.canEdit, page.description, page.stages, page.note, page.inputDescription, page.input]).toEqual([
      'critique-revise',
      false,
      'Critique a draft against what it is meant to do, then revise it to answer the critique.',
      ['1. critique · asks utility', '2. revise · asks chat'],
      'A shipped starter: apogee symphonies edit critique-revise makes it yours, at a prompt.',
      'The draft, opening with a line that says what it is for when that is not obvious.',
      'A draft README for the tool.',
    ])
    library.pickRow('digest')
    expect(library.face().symphony.stages, 'a stage that plays another says so').toEqual(['1. summary · plays summarize-verify', '2. card · asks extraction'])
    library.pickRow('notes-digest')
    expect([library.face().symphony.canEdit, library.face().symphony.note]).toEqual([true, ''])
  })

  test('Play runs the recorded play, supervised: the stage lines as it goes, then each stage’s answer and the output on its own page', async () => {
    const { library, child, clock } = await symphonies()
    library.symphonies.play()
    await settle()
    expect(child.calls.filter((call) => call.kind === 'jobs').map((call) => call.arguments)).toEqual([[...playArguments]])
    clock.advance(1)
    await settle()
    expect(library.face().symphony.playing).toEqual({ shown: true, line: 'stage 1/2 critique — asking utility (writer): Below is a draft. Work out what it is meant to do -- from… · 0.0 s' })
    expect(library.face().symphony.canPlay).toBe(false)
    clock.advance(40)
    await settle()
    const page = library.face().symphony
    expect(page.playing.shown).toBe(false)
    expect(page.played).toEqual({
      shown: true,
      stages: [
        { label: 'critique · utility (writer)', answer: '1. The draft never says who it is for.\n2. The second paragraph repeats the first.' },
        { label: 'revise · chat (writer)', answer: 'A tighter draft, for new contributors: what the tool does, then how to run it.' },
      ],
      output: 'A tighter draft, for new contributors: what the tool does, then how to run it.',
    })
    library.pickRow('critique-revise')
    expect(library.face().symphony.played.shown, 're-picking the symphony shown keeps its play').toBe(true)
    library.pickRow('digest')
    expect(library.face().symphony.played.shown, 'the play lands on its own page and no other').toBe(false)
    library.pickRow('critique-revise')
    expect(library.face().symphony.played.shown, 'another symphony clears the play, as the old pick does (main.html:6611)').toBe(false)
  })

  test('the image row’s two states: none chosen -- Play waits, and says why -- then chosen, with Remove and Choose another…; another symphony drops it', async () => {
    const { library } = await symphonies()
    library.pickRow('describe-answer')
    let page = library.face().symphony
    expect([page.image.takes, page.image.chosen, page.canPlay, page.needsImage]).toEqual([true, '', false, 'describe-answer takes an image with its input: choose one to play it.'])
    library.symphonies.chooseImage()
    page = library.face().symphony
    expect([page.image.chosen, page.canPlay, page.needsImage]).toEqual(['storefront.png', true, ''])
    expect(library.symphonies.image).toBe(sampleImage)
    library.symphonies.removeImage()
    expect(library.face().symphony.image.chosen).toBe('')
    library.symphonies.chooseImage()
    library.pickRow('critique-revise')
    library.pickRow('describe-answer')
    expect(library.face().symphony.image.chosen, 'chosen for the one before').toBe('')
    library.pickRow('critique-revise')
    expect(library.face().symphony.image.takes).toBe(false)
  })

  test('a play nobody recorded fails in the stand-in’s words, said in the toast', async () => {
    const { library, clock, said } = await symphonies()
    library.pickRow('describe-answer')
    library.symphonies.chooseImage()
    library.symphonies.play()
    clock.advance(2)
    await settle()
    expect(said).toEqual([`describe-answer: no recorded answer for apogee symphonies play describe-answer --input ${sampleInputs['describe-answer']} --image ${sampleImage} --output-format json`])
    expect(library.face().symphony.played.shown).toBe(false)
  })

  test('no apogee: no symphonies, and the page asks for one; a config typo: the recorded refusal', async () => {
    const missing = await symphonies({ apogee: 'missing' })
    expect([missing.library.face().note, missing.library.face().emptyDetail, missing.library.face().symphony.shown]).toEqual(["Apogee isn't installed, so there are no symphonies to list.", 'Choose a symphony.', false])
    const typo = await symphonies({ apogee: 'config-typo' })
    expect(typo.library.face().note).toBe("The symphonies could not be read: apogee symphonies: /Users/someone/.apogee/config/config.json: not valid JSON: line 4, column 5: expected ',' or '}' after a member (exited with status 1)")
  })
})
