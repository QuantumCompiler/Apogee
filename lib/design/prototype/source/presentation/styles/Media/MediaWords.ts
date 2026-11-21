const months = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec']

/** The home a path is written under, and how a person reads it: `~/`. */
const home = '/Users/someone/'

/**
 * When a record was made, against the clock's now, in UTC as the stand-in's
 * now is: `Today 12:58`, `Yesterday 18:20`, `Oct 16` (design 1k's, promoted
 * from Video on Compose's use, 1l).
 */
export function when(createdAt: string | undefined, now: Date): string {
  const at = createdAt === undefined ? undefined : new Date(createdAt)
  if (at === undefined || Number.isNaN(at.getTime())) return 'when it was made is not recorded'
  const day = (date: Date) => Date.UTC(date.getUTCFullYear(), date.getUTCMonth(), date.getUTCDate())
  const time = `${String(at.getUTCHours()).padStart(2, '0')}:${String(at.getUTCMinutes()).padStart(2, '0')}`
  const days = Math.round((day(now) - day(at)) / 86_400_000)
  if (days === 0) return `Today ${time}`
  if (days === 1) return `Yesterday ${time}`
  return `${months[at.getUTCMonth()]} ${at.getUTCDate()}`
}

/** Seconds as the old prototype says them: 5 is `5`, 2.25 is `2.25`. */
export function secs(seconds: number): string {
  return String(Math.round(seconds * 100) / 100)
}

/** `path` as a person reads it: `~/Movies/Apogee/x.mp4`. */
export function tilde(path: string): string {
  return path.startsWith(home) ? `~/${path.slice(home.length)}` : path
}
