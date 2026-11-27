import { renderToStaticMarkup } from 'react-dom/server'
import { describe, expect, test } from 'vitest'
import { KeyboardList } from '../../../source/presentation/settings/Previews/Panes.preview'

describe('Settings › Keyboard, drawn (design 1s): the old prototype’s outline', () => {
  test('the keys are plain rows, as the old pane draws them: no list role and no label of its own', () => {
    const markup = renderToStaticMarkup(<KeyboardList />)
    expect(markup).not.toMatch(/role="list(item)?"/)
    expect(markup).not.toContain('aria-label="Keys"')
    expect(markup).toContain('data-keys')
    expect(markup).toContain('⌥⌘M')
  })
})
