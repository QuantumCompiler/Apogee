import { renderToStaticMarkup } from 'react-dom/server'
import { describe, expect, test } from 'vitest'
import { SheetHost } from '../../../source/presentation/shell/Views/Hosts'

describe('the sheet host (design 1e): its subtitle', () => {
  test('a file’s path in the code font where the sheet asks for it, as the old `subFont` draws the editor’s path (design 1n); the interface font otherwise', () => {
    const mono = renderToStaticMarkup(
      <SheetHost title="notes-digest" subtitle="/Users/someone/.apogee/symphonies/notes-digest.yaml" subtitleMono width={720} right={[]}>
        {null}
      </SheetHost>,
    )
    expect(mono).toContain('font-family:var(--font-mono)')
    const plain = renderToStaticMarkup(
      <SheetHost title="Run this session under" subtitle="Now: coding" width={560} right={[]}>
        {null}
      </SheetHost>,
    )
    expect(plain).not.toContain('var(--font-mono)')
  })
})
