import type { ApogeeMode } from '../../../infrastructure/process/StandInChild'

/**
 * What a page asks to open with (design 1e): the old prototype's three props,
 * `start`, `still` and `apogee` (`data-props`, `main.html:2512` at
 * `b375fac9`), as 1c's baseline opens it, `?start=<preset>&still=true&apogee=<mode>`.
 */
export interface StartRequest {
  /** The preset named; `undefined` when none is. */
  readonly start?: string
  /** A frame that does not move. */
  readonly still: boolean
  readonly apogee: ApogeeMode
}

const modes: readonly ApogeeMode[] = ['installed', 'missing', 'config-typo']

/**
 * The start a page at this location asks for, read from its query and from
 * its hash, both of which a page opened from disk keeps: `?start=monitor&still=true`
 * or `#start=monitor&still=true`. The hash speaks last, so what it names
 * overrides the query. `still` is true only as `true`; an `apogee` that is
 * not one of the three modes is `installed`. An unknown preset is passed on
 * as named, for the preset table to decide.
 */
export function startRequest(location: { readonly search: string; readonly hash: string }): StartRequest {
  const said = new Map<string, string>()
  for (const part of [location.search.replace(/^\?/, ''), location.hash.replace(/^#/, '')]) {
    for (const [name, value] of new URLSearchParams(part)) said.set(name, value)
  }
  const start = said.get('start')
  const apogee = said.get('apogee') as ApogeeMode | undefined
  return {
    ...(start ? { start } : {}),
    still: said.get('still') === 'true',
    apogee: apogee !== undefined && modes.includes(apogee) ? apogee : 'installed',
  }
}
