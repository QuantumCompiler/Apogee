import { afterEach, beforeEach, describe, expect, test, vi } from 'vitest'
import { Clock } from '../../../source/infrastructure/clock/Clock'

beforeEach(() => {
  vi.useFakeTimers()
})
afterEach(() => {
  vi.useRealTimers()
})

describe('the clock', () => {
  test('it ticks every 400 ms once started, as the old prototype\'s tick() does', () => {
    const clock = new Clock()
    const seen: number[] = []
    clock.onTick((tick) => seen.push(tick))
    clock.start()
    vi.advanceTimersByTime(399)
    expect(seen).toEqual([])
    vi.advanceTimersByTime(1)
    expect(seen).toEqual([1])
    vi.advanceTimersByTime(800)
    expect(seen).toEqual([1, 2, 3])
    expect(clock.ticks).toBe(3)
    expect(Clock.tickMilliseconds).toBe(400)
  })

  test('nothing ticks before start, and stop stops it', () => {
    const clock = new Clock()
    vi.advanceTimersByTime(4000)
    expect(clock.ticks).toBe(0)
    clock.start()
    vi.advanceTimersByTime(400)
    clock.stop()
    vi.advanceTimersByTime(4000)
    expect(clock.ticks).toBe(1)
  })

  test('a preset\'s start ticks are applied at once, then it runs on', () => {
    const clock = new Clock({ start: 5 })
    const seen: number[] = []
    clock.onTick((tick) => seen.push(tick))
    clock.start()
    expect(seen).toEqual([1, 2, 3, 4, 5])
    vi.advanceTimersByTime(400)
    expect(seen).toEqual([1, 2, 3, 4, 5, 6])
  })

  test('still holds the state after its start ticks: nothing moves after it loads', () => {
    const clock = new Clock({ start: 3, still: true })
    const seen: number[] = []
    clock.onTick((tick) => seen.push(tick))
    clock.start()
    expect(seen).toEqual([1, 2, 3])
    vi.advanceTimersByTime(60_000)
    expect(seen).toEqual([1, 2, 3])
    expect(clock.ticks).toBe(3)
    expect(clock.still).toBe(true)
  })

  test('a still frame is its ticks in for whatever opens after its start: missedTicks; a live or unstarted clock owes none', () => {
    const still = new Clock({ start: 3, still: true })
    expect(still.missedTicks).toBe(0)
    still.start()
    expect(still.missedTicks).toBe(3)
    vi.advanceTimersByTime(60_000)
    expect(still.missedTicks).toBe(3)

    const live = new Clock({ start: 3 })
    live.start()
    expect(live.missedTicks).toBe(0)
    live.stop()
  })

  test('a second start applies no start ticks again', () => {
    const clock = new Clock({ start: 2, still: true })
    clock.start()
    clock.start()
    expect(clock.ticks).toBe(2)
  })

  test('tests drive it by hand: advance(n) runs n ticks, each listener in order', () => {
    const clock = new Clock()
    const seen: string[] = []
    clock.onTick((tick) => seen.push(`a${tick}`))
    const leave = clock.onTick((tick) => seen.push(`b${tick}`))
    clock.advance(2)
    leave()
    clock.advance()
    expect(seen).toEqual(['a1', 'b1', 'a2', 'b2', 'a3'])
    expect(clock.ticks).toBe(3)
  })

  test('"now" is fixed per preset: 2026-11-14 12:00 UTC unless the preset names another, and it never drifts', () => {
    const clock = new Clock()
    expect(clock.now.toISOString()).toBe('2026-11-14T12:00:00.000Z')
    clock.start()
    vi.advanceTimersByTime(10_000)
    clock.advance(30)
    expect(clock.now.toISOString()).toBe('2026-11-14T12:00:00.000Z')
    expect(new Clock({ now: '2026-10-02T08:30:00Z' }).now.toISOString()).toBe('2026-10-02T08:30:00.000Z')
    expect(() => new Clock({ now: 'yesterday' })).toThrow('a preset\'s "now" is an ISO 8601 time, not yesterday')
    // A time with no zone would read in the viewer's own zone, so "now" would differ by machine.
    expect(() => new Clock({ now: '2026-11-14T12:03:00' })).toThrow('a preset\'s "now" is an ISO 8601 time, not 2026-11-14T12:03:00')
    expect(() => new Clock({ now: '2026-11-14' })).toThrow('a preset\'s "now" is an ISO 8601 time, not 2026-11-14')
    expect(new Clock({ now: '2026-11-14T05:03:00-07:00' }).now.toISOString()).toBe('2026-11-14T12:03:00.000Z')
    expect(new Clock({ now: '2026-11-14T12:03:00.250Z' }).now.toISOString()).toBe('2026-11-14T12:03:00.250Z')
  })

  test('a job or chat replays one line a tick, unless the preset sets a faster pace', () => {
    expect(new Clock().linesPerTick).toBe(1)
    expect(new Clock({ pace: 4 }).linesPerTick).toBe(4)
    expect(() => new Clock({ pace: 0 })).toThrow('a preset\'s pace is a whole number of lines a tick, 1 or more, not 0')
    expect(() => new Clock({ start: -1 })).toThrow('a preset\'s start is a whole number of ticks, 0 or more, not -1')
  })
})
