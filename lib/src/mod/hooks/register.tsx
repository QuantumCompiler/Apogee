// The composition root (ADR 0001): wires the layers and holds nothing itself,
// as the CLI's main.cpp and the app's ApogeeMain do.
import type { Register } from 'claude-code'

import { discoveryOptions } from '../src/infrastructure/discovery/discovery'
import { answerApogee } from '../src/presentation/commands/apogee'
import { startSession } from '../src/presentation/session/start'
import { answerTool } from '../src/presentation/tools/tools'

export const register: Register = (on, options) => {
  const discovery = discoveryOptions(options)

  on('session.start', async ($, e, next) => {
    const started = await next(e)
    await startSession($, discovery)

    return started
  })

  on('command.run', { command: 'apogee' }, ($, e) => answerApogee($, e.args, discovery)).catch(() => ({
    text: '/apogee stopped before it answered; `claude --debug` says why. Nothing was run past what it said.',
  }))

  on('tool.run', { tool: /^mcp__apogee__/ }, async ($, e, next) => (await answerTool($, e)) ?? next(e)).catch((_, e, next) =>
    next.called ? next(e) : { deny: "Apogee's tool stopped before it answered; `claude --debug` says why." },
  )
}
