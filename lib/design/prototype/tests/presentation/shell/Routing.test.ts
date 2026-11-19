import { describe, expect, test } from 'vitest'
import { startRequest } from '../../../source/presentation/shell/ViewModels/Routing'

/** The start a page at `href` asks for. */
const at = (href: string) => {
  const url = new URL(href)
  return startRequest({ search: url.search, hash: url.hash })
}

describe('routing: the start a page asks for, from the query and from the hash', () => {
  test("the query, as the old prototype's links write it", () => {
    expect(at('http://localhost:5173/main.html?start=monitor&still=true')).toEqual({ start: 'monitor', still: true, apogee: 'installed' })
  })

  test('the hash, the same words after #', () => {
    expect(at('http://localhost:5173/main.html#start=monitor&still=true')).toEqual({ start: 'monitor', still: true, apogee: 'installed' })
  })

  test('a file:// URL, as a double-click opens it, in either form', () => {
    expect(at('file:///Users/someone/Apogee/lib/design/main.html?start=monitor&still=true')).toEqual({ start: 'monitor', still: true, apogee: 'installed' })
    expect(at('file:///Users/someone/Apogee/lib/design/main.html#start=monitor&still=true')).toEqual({ start: 'monitor', still: true, apogee: 'installed' })
    expect(at('file:///Users/someone/Apogee/lib/design/main.html')).toEqual({ still: false, apogee: 'installed' })
  })

  test('still is true only when it says true, as the old boolean prop', () => {
    expect(at('file:///main.html?start=monitor').still).toBe(false)
    expect(at('file:///main.html?start=monitor&still=false').still).toBe(false)
    expect(at('file:///main.html?start=monitor&still=1').still).toBe(false)
    expect(at('file:///main.html?still=true').still).toBe(true)
  })

  test("the apogee mode, as 1c's baseline opens the old prototype: installed, missing or config-typo", () => {
    expect(at('file:///main.html?start=monitor&still=true&apogee=missing').apogee).toBe('missing')
    expect(at('file:///main.html#start=monitor&apogee=config-typo').apogee).toBe('config-typo')
    expect(at('file:///main.html?apogee=broken').apogee).toBe('installed')
  })

  test('the hash speaks last: what it names overrides the query', () => {
    expect(at('file:///main.html?start=models&still=true#start=monitor')).toEqual({ start: 'monitor', still: true, apogee: 'installed' })
    expect(at('file:///main.html?still=true#still=false').still).toBe(false)
  })

  test('an unknown preset is passed on as named: the preset table decides what it opens', () => {
    expect(at('file:///main.html?start=no-such-screen').start).toBe('no-such-screen')
    expect(at('file:///main.html?start=').start).toBeUndefined()
  })
})
