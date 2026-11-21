import { expect, test } from 'vitest'
import { secs, tilde, when } from '../../../source/presentation/styles/Media/MediaWords'

test("a media record's words, promoted from Video on Compose's use (design 1l): when it was made against the clock's now, seconds as the old prototype says them, and a path under the home as a person reads it", () => {
  const now = new Date('2026-11-14T12:00:00Z')
  expect(when('2026-11-14T09:40:00Z', now)).toBe('Today 09:40')
  expect(when('2026-11-13T21:30:00Z', now)).toBe('Yesterday 21:30')
  expect(when('2026-11-09T05:23:35Z', now)).toBe('Nov 9')
  expect(when(undefined, now)).toBe('when it was made is not recorded')
  expect([secs(5), secs(2.254), secs(33.5)]).toEqual(['5', '2.25', '33.5'])
  expect([tilde('/Users/someone/Movies/Apogee/heat-pump.mp4'), tilde('/elsewhere/a.mp4')]).toEqual(['~/Movies/Apogee/heat-pump.mp4', '/elsewhere/a.mp4'])
})
