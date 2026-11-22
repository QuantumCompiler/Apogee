// The composition root (ADR 0001): wires the layers and holds nothing itself,
// as the CLI's main.cpp and the app's ApogeeMain do.
import type { Register } from 'claude-code'

import { discoveryOptions } from '../src/infrastructure/discovery/discovery'
import { findAndSayAbsence } from '../src/presentation/notices/absence'

export const register: Register = (on, options) => {
  const discovery = discoveryOptions(options)

  on('session.start', async ($, e, next) => {
    const started = await next(e)
    await findAndSayAbsence($, discovery)

    return started
  })
}
