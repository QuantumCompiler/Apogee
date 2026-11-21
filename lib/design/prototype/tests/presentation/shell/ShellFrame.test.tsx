import { renderToStaticMarkup } from 'react-dom/server'
import { describe, expect, test } from 'vitest'
import { Clock } from '../../../source/infrastructure/clock/Clock'
import { ThemeService } from '../../../source/presentation/styles/Appearance/ThemeService'
import { ShellViewModel } from '../../../source/presentation/shell/ViewModels/ShellViewModel'
import { ShellView, windowHeight } from '../../../source/presentation/shell/Views/ShellView'

const shell = () => new ShellViewModel(new Clock({ still: true }), new ThemeService())

describe("the window in a gallery frame (design 1q): the frame's size, not the page's", () => {
  test('as the window, the page’s height; in a frame, the frame’s', () => {
    expect(windowHeight(undefined)).toBe('100vh')
    expect(windowHeight({ width: 1440, height: 900, keys: false })).toBe(900)
  })

  test('drawn in a frame, it is the frame’s height, never the page’s', () => {
    const framed = renderToStaticMarkup(<ShellView model={shell()} screen={() => null} overlay={() => null} frame={{ width: 1440, height: 900, keys: false }} />)
    expect(framed).toContain('height:900px')
    expect(framed).not.toContain('100vh')
    expect(renderToStaticMarkup(<ShellView model={shell()} screen={() => null} overlay={() => null} />)).toContain('height:100vh')
  })
})
