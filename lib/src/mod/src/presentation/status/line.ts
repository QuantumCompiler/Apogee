// The status line under the prompt: the active backend, its promoted version
// and the run state, from the reads Business keeps; staleness said with the
// reading's age and why, never hidden (the item's acceptance: the line tracks
// them and says staleness).
import type { EngineInterface } from 'claude-code'

import type { FoundBinary } from '../../business/client/client'
import { STALE_AFTER_MS, refreshStatus, startStatusPoll } from '../../business/status/status'
import type { StatusShown } from '../../business/status/status'

/** Starts the line for the session over the found binary. */
export function startStatusLine($: EngineInterface, binary: FoundBinary): void {
  startStatusPoll($, binary, shown => $.ui.status(lineOf(binary, shown)))
}

/** Reads the line again now and shows it: the read-back after a command that changed the store. */
export async function refreshStatusLine($: EngineInterface, binary: FoundBinary): Promise<void> {
  $.ui.status(lineOf(binary, await refreshStatus($, binary)))
}

/**
 * The read-back from work that outlived its call: not awaited, and a read-back
 * that cannot finish (the module unloading under it) has nobody to tell.
 */
export function refreshStatusLineLater($: EngineInterface, binary: FoundBinary): void {
  refreshStatusLine($, binary).catch(() => undefined)
}

/** The line's words. */
export function lineOf(binary: FoundBinary, { state, at }: StatusShown): string {
  const who = binary.channel === 'dev' ? 'Apogee (dev)' : 'Apogee'
  const reading = state.reading
  if (reading === undefined) {
    return `${who}: status unread${state.failure === undefined ? '' : `: ${state.failure}`}`
  }
  const backend =
    reading.backend === undefined
      ? 'chat backend unread'
      : reading.backend.kind === 'unset'
        ? 'no chat backend'
        : `${reading.backend.name} ${reading.version === undefined ? '(no promoted version)' : `v${reading.version}`}`
  const parts = [backend, reading.run ?? 'run state unread']
  if (reading.missing.length > 0) {
    parts.push(`lacked: ${lacked(reading.missing)}`)
  }
  const age = at - (state.readAt ?? at)
  const isFailed = state.failure !== undefined && (state.attemptAt ?? 0) > (state.readAt ?? 0)
  if (isFailed || age >= STALE_AFTER_MS) {
    parts.push(`stale: read ${ageText(age)} ago${isFailed ? `; ${state.failure}` : ''}`)
  }

  return `${who}: ${parts.join(' · ')}`
}

/** The fields lacked, each document named once: `models status chat, train status running, cycle_active`. */
function lacked(missing: readonly string[]): string {
  let document = ''

  return missing
    .map(one => {
      const [name, ...rest] = one.startsWith('models status ') ? ['models status', one.slice(14)] : one.startsWith('train status ') ? ['train status', one.slice(13)] : ['', one]
      const field = rest.join(' ')
      const said = name === document ? field : `${name} ${field}`.trim()
      document = name

      return said
    })
    .join(', ')
}

function ageText(ms: number): string {
  const seconds = Math.round(ms / 1000)
  if (seconds < 60) return `${seconds} s`
  const minutes = Math.round(seconds / 60)

  return minutes < 60 ? `${minutes} min` : `${Math.round(minutes / 60)} h`
}
