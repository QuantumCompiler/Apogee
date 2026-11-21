import { renderToStaticMarkup } from 'react-dom/server'
import { describe, expect, test } from 'vitest'
import { PickColumn, PickRow } from '../../../source/presentation/styles/Components/PickList'

describe('the pick list: the old prototype’s 300 px list of definitions, promoted to styles on its second user (design 1n: the Library’s rows and Review’s skills)', () => {
  test('a named column on the sidebar’s surface, 300 px wide, its rows real buttons, the current one marked and drawn on the selection', () => {
    const markup = renderToStaticMarkup(
      <PickColumn label="Definitions">
        <PickRow name="triage" meta="yours · read-only" current pick={() => {}} />
        <PickRow name="code-review" meta="shipped · read-only" current={false} pick={() => {}} />
      </PickColumn>,
    )
    expect(markup).toContain('<nav aria-label="Definitions"')
    expect(markup).toContain('flex:0 0 300px')
    expect(markup).toContain('background:var(--surface-sidebar)')
    expect(markup.match(/<button type="button" class="hv"/g)?.length).toBe(2)
    expect(markup).toContain('aria-current="true"')
    expect(markup).toContain('background:var(--surface-selection)')
    expect(markup).toContain('<span>triage</span>')
  })

  test('a skill’s row: its name at the body size, what it applies to in mono, cut to one line', () => {
    const markup = renderToStaticMarkup(<PickRow name="ai-ml" meta="shipped · .ipynb" current={false} pick={() => {}} mono />)
    expect(markup).toContain('font-size:var(--t-body)')
    expect(markup).toContain('class="mono"')
    expect(markup).toContain('text-overflow:ellipsis')
  })
})
