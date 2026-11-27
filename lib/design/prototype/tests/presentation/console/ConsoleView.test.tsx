import { renderToStaticMarkup } from 'react-dom/server'
import { describe, expect, test } from 'vitest'
import { NothingRun } from '../../../source/presentation/console/Previews/ConsoleView.preview'

describe('the console, drawn (design 1s): the old prototype’s outline', () => {
  test('its output is plain lines, as the old draws it: no log role and no label of its own', () => {
    const markup = renderToStaticMarkup(<NothingRun />)
    expect(markup).not.toContain('role="log"')
    expect(markup).not.toContain('Console output')
    expect(markup).toContain('data-console-output')
  })
})
