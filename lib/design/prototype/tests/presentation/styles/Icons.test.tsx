import { readdirSync, readFileSync, statSync } from 'node:fs'
import { dirname, join, relative, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { renderToStaticMarkup } from 'react-dom/server'
import { describe, expect, test } from 'vitest'
import { Icon } from '../../../source/presentation/styles/Components/Icon'
import { iconNames } from '../../../source/presentation/styles/Icons/IconNames'
import { iconSet } from '../../../source/presentation/styles/Icons/IconSet'

/** The prototype's source/presentation: this file is tests/presentation/styles/Icons.test.tsx. */
const presentation = resolve(dirname(fileURLToPath(import.meta.url)), '../../../source/presentation')

function files(folder: string): string[] {
  return readdirSync(folder).flatMap((name) => {
    const path = join(folder, name)
    return statSync(path).isDirectory() ? files(path) : [path]
  })
}

/** Where an <svg> is drawn that is no icon: the Icon component itself, the code graph's edges, a drawing of the graph, and the processes page's connectors, a drawing of the app's children (1r). */
const drawings = ['styles/Components/Icon.tsx', 'graph/Views/GraphScreen.tsx', 'gallery/Views/BuildProcessesPage.tsx']

describe("the app's icons: one set in styles (design 1q)", () => {
  test('every name has its glyph, each name once', () => {
    expect(new Set(iconNames).size).toBe(iconNames.length)
    expect(Object.keys(iconSet).sort()).toEqual([...iconNames].sort())
  })

  test('drawn on the 16 px grid at its size, in the set’s stroke, hidden from a screen reader unless it says something', () => {
    expect(renderToStaticMarkup(<Icon name="close" size={13} />)).toBe('<svg class="i" width="13" height="13" viewBox="0 0 16 16" aria-hidden="true"><path d="M4.5 4.5l7 7M11.5 4.5l-7 7"></path></svg>')
    expect(renderToStaticMarkup(<Icon name="check" size={14} label="done" />)).toContain('aria-label="done"')
    expect(renderToStaticMarkup(<Icon name="local" label="local" role="img" />)).toContain('aria-label="local" role="img"')
    // A filled glyph is drawn in its own fill, not the stroke.
    expect(renderToStaticMarkup(<Icon name="play" size={14} />)).toBe('<svg width="14" height="14" viewBox="0 0 16 16" aria-hidden="true"><path d="M5 3.2v9.6L13 8z" fill="currentColor"></path></svg>')
  })

  test('no view draws an icon of its own: every <svg> in the app is the set’s, but a drawing', () => {
    const own = files(presentation)
      .filter((file) => file.endsWith('.tsx') && !file.includes('/Previews/'))
      .map((file) => relative(presentation, file))
      .filter((file) => !drawings.includes(file) && readFileSync(join(presentation, file), 'utf8').includes('<svg'))
    expect(own).toEqual([])
  })
})
