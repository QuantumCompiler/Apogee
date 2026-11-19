import { renderToStaticMarkup } from 'react-dom/server'
import { describe, expect, test } from 'vitest'
import type { SessionFace } from '../../../source/presentation/session/ViewModels/SessionViewModel'
import { AttachmentChips, EmptySessionView } from '../../../source/presentation/session/Views/SessionExtras'
import { ActivityView, TranscriptRow } from '../../../source/presentation/session/Views/TranscriptRow'

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

describe("the rows 1v draws: what only the view decides", () => {
  test('a live reasoning ends in the old caret; a closed one does not', () => {
    const activity = { id: 0, kind: 'activity', summary: 'thinking · 1 step', live: true, waiting: false, reasoning: 'They want partial-failure tolerance.', lines: [] } as const
    expect(renderToStaticMarkup(<ActivityView activity={activity} />)).toContain('class="caret"')
    expect(renderToStaticMarkup(<ActivityView activity={{ ...activity, live: false, summary: 'thought · 1 step' }} />)).not.toContain('caret')
  })

  test('an activity whose turn waits on a card is drawn open, its reasoning and lines shown, and says it is paused, waiting for you', () => {
    const activity = { id: 0, kind: 'activity', summary: 'thinking · 2 steps', live: false, waiting: true, reasoning: 'Loop in _fetch().', lines: [{ mark: '', text: '[tool] read_file' }] } as const
    const markup = renderToStaticMarkup(<ActivityView activity={activity} />)
    expect(markup).toContain('Loop in _fetch().')
    expect(markup).toContain('[tool] read_file')
    expect(markup).toContain('· paused · waiting for you')
  })

  test("an answer billed per use carries the API's glyph in its footer, in place of the kind's dot", () => {
    const answer = { id: 0, kind: 'answer', text: 'x', finished: true, footer: { model: 'claude', kind: 'api', usage: [] }, sources: [], cited: 0 } as const
    const markup = renderToStaticMarkup(<TranscriptRow row={answer} />)
    expect(markup).toContain('title="billed per use"')
    expect(markup).not.toContain('border-radius:50%')
    const local = renderToStaticMarkup(<TranscriptRow row={{ ...answer, footer: { model: 'root', kind: 'local', usage: [] } }} />)
    expect(local).toContain('border-radius:50%')
    expect(local).not.toContain('billed per use')
  })
})
