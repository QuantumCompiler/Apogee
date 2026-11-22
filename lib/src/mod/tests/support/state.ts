// Watches what the mod keeps in $.state. The test's engine has no state noun
// to read with, so a hook beneath the plugin sees each write on its way to
// the kit's own store, and passes it on.
import type { On } from 'claude-code'

import type { ApogeeBinaryState } from '../../types'

/** The binary state as last written. */
export type Kept = { binary: () => ApogeeBinaryState | undefined; writes: () => number }

/** Records the mod's writes of `apogee.binary` from here on. */
export function keptState(on: On): Kept {
  let last: ApogeeBinaryState | undefined
  let count = 0
  on('state.set', (_, e, next) => {
    if (e.plugin === 'apogee' && e.key === 'binary') {
      const written = e.value as { shape?: string; value?: ApogeeBinaryState }
      last = written.shape === 'binary-1' ? written.value : undefined
      count += 1
    }

    return next(e)
  })

  return { binary: () => last, writes: () => count }
}
