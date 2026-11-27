import { describe, expect, test } from 'vitest'
import { AsyncStream } from '../../../source/infrastructure/process/AsyncStream'

describe('a stream of what a child says', () => {
  test('values yielded before anyone reads are kept, in order, and finish ends the reading', async () => {
    const { stream, continuation } = AsyncStream.make<number>()
    continuation.yield(1)
    continuation.yield(2)
    continuation.finish()
    continuation.yield(3)
    const seen: number[] = []
    for await (const value of stream) seen.push(value)
    expect(seen).toEqual([1, 2])
  })

  test('a reader waiting is handed each value as it is yielded', async () => {
    const { stream, continuation } = AsyncStream.make<string>()
    const seen: string[] = []
    const reading = (async () => {
      for await (const value of stream) seen.push(value)
    })()
    await Promise.resolve()
    continuation.yield('a')
    await Promise.resolve()
    expect(seen).toEqual(['a'])
    continuation.yield('b')
    continuation.finish()
    await reading
    expect(seen).toEqual(['a', 'b'])
    expect(continuation.finished).toBe(true)
  })

  test('everything yielded so far can be read without waiting, read or not', async () => {
    const { stream, continuation } = AsyncStream.make<number>()
    continuation.yield(7)
    continuation.yield(8)
    expect(stream.yielded).toEqual([7, 8])
    continuation.finish()
    for await (const value of stream) expect(value).toBeGreaterThan(6)
    expect(stream.yielded).toEqual([7, 8])
  })
})
