import { renderToStaticMarkup } from 'react-dom/server'
import { describe, expect, test } from 'vitest'
import type { SessionFace } from '../../../source/presentation/session/ViewModels/SessionViewModel'
import { AttachmentChips, EmptySessionView } from '../../../source/presentation/session/Views/SessionExtras'

/** The old chips' glyphs (`main.html:731-736` at `b375fac9`), by a path each draws and no other does. */
const glyphs = {
  folder: 'M2 4.5A1.5 1.5 0 0 1 3.5 3h2.8l1.5 1.5h4.7A1.5 1.5 0 0 1 14 6v5.5a1.5 1.5 0 0 1-1.5 1.5h-9A1.5 1.5 0 0 1 2 11.5z',
  file: '<path d="M9 2v4h4"></path>',
  pdf: 'M9 2v4h4M5.5 9h5M5.5 11.5h3',
  image: 'M13.5 10.5L10 7l-6 6',
  audio: 'M2.5 8h1M5 5.5v5M7.5 3.5v9M10 6v4M12.5 7v2',
  video: 'M11 7l3-2v6l-3-2',
} as const

describe("the session's smaller views, drawn: what only the view decides", () => {
  test('an attached image draws the old image glyph, not the file glyph; each sample draws its own kind', () => {
    const image = renderToStaticMarkup(<AttachmentChips paths={['crash-0912.png']} detach={undefined} />)
    expect(image).toContain('<rect x="2.5" y="3" width="11" height="10" rx="1.5"></rect>')
    expect(image).toContain('<circle cx="6" cy="6.5" r="1.2"></circle>')
    expect(image).toContain(glyphs.image)
    expect(image).not.toContain(glyphs.file)
    const drawn = (path: string) => {
      const markup = renderToStaticMarkup(<AttachmentChips paths={[path]} detach={undefined} />)
      return (Object.keys(glyphs) as Array<keyof typeof glyphs>).filter((kind) => markup.includes(glyphs[kind]))
    }
    expect(drawn('notes/wal-design.md')).toEqual(['file'])
    expect(drawn('~/code/ingest/')).toEqual(['folder'])
    expect(drawn('storage-engine-design.pdf')).toEqual(['pdf'])
    expect(drawn('crash-0912.png')).toEqual(['image'])
    expect(drawn('standup-2026-10-09.m4a')).toEqual(['audio'])
    expect(drawn('repro-crash.mov')).toEqual(['video'])
  })

  test('the empty state of a session nobody launched draws Attach a folder and Choose a model off', () => {
    const face: SessionFace['empty'] = { heading: 'What are we working on?', privateWords: '', notices: [], launched: false, privateInstead: false, chooseSuite: false }
    const model = { name: 'apogee', kind: 'unknown', tag: '', where: '' } as const
    const acts = { attachFolder: () => {}, chooseModel: () => {}, chooseSuite: () => {}, privateInstead: () => {} }
    const off = renderToStaticMarkup(<EmptySessionView face={face} model={model} acts={acts} />)
    for (const label of ['Attach a folder', 'Choose a model', 'Choose a suite']) expect(off, label).toMatch(new RegExp(`<button[^>]*disabled=""[^>]*>(?:(?!</button>).)*${label}`))
    expect(off).not.toContain('Private session instead')
    const on = renderToStaticMarkup(<EmptySessionView face={{ ...face, launched: true, privateInstead: true, chooseSuite: true }} model={model} acts={acts} />)
    expect(on).not.toContain('disabled')
    expect(on).toContain('Private session instead')
  })
})
