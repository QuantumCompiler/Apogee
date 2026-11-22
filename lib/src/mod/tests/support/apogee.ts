// Drives the mod as the person and the model do: `/apogee <words>` typed at
// the prompt, a tool called. Scripted answers for the found binary's commands
// are written here once, each from a recorded document or hand-made in the
// CLI's exact shape, naming where the shape comes from.
import type { CommandRunResult, ToolCallResult } from 'claude-code'
import type { Engine } from 'claude-code/testing'

import type { Answer, Script } from './machine'
import { MODELS_LIST_ADAPTERS, MODELS_STATUS_CHAT, SERVED_LIST } from './recorded'

/** `/apogee <words>` as the person types it at the prompt. */
export async function apogee($: Engine, args = ''): Promise<CommandRunResult> {
  return $.command.run({ command: 'apogee', args, origin: { kind: 'composer' }, presentation: { isFullscreen: false, columns: 100 } })
}

/** The model's call of one of the mod's tools, `mcp__apogee__<name>`, with its arguments. */
export async function callTool($: Engine, name: string, args: Readonly<Record<string, unknown>> = {}): Promise<ToolCallResult> {
  return $.tool.run({ tool: `mcp__apogee__${name}`, tool_use_id: `toolu_${name}`, ...args } as Parameters<Engine['tool']['run']>[0])
}

/** A script answering each command by its words (`check --output-format json`), the rest unscripted. */
export function scripted(answers: Readonly<Record<string, Answer>>, rest?: Script): Script {
  return (args, init) => (Object.hasOwn(answers, args.join(' ')) ? answers[args.join(' ')] : rest?.(args, init))
}

/** The words of `apogee mcp serve`, and what the server does with what the mod writes. */
export const SERVE = 'mcp serve'

/**
 * The server as recorded: its listing for any listing written to it; a call
 * answered by `calls`, by tool name, with the document text (hand-made frames
 * in serve_stdio.cpp's shape around it); any other call left to the next script.
 */
export function served(calls: Readonly<Record<string, { text: string; isError?: boolean }>> = {}, listing = SERVED_LIST): Script {
  return (args, init) => {
    if (args.join(' ') !== SERVE) return undefined
    const stdin = init?.stdin ?? ''
    if (stdin.includes('"method":"tools/list"')) return { stdout: listing }
    const call = /"method":"tools\/call","params":\{"name":"([^"]+)"/.exec(stdin)
    const answer = call === null ? undefined : calls[call[1] ?? '']
    if (answer === undefined) return undefined

    return { stdout: INITIALIZED + servedReply(answer.text, answer.isError ?? false) }
  }
}

/** The recorded listing with one more tool added (a served complete), as a config listing it lists it. */
export function servedListWith(tool: unknown): string {
  const [handshake, list] = SERVED_LIST.split('\n')
  const reply = JSON.parse(list ?? '{}') as { result: { tools: unknown[] } }
  reply.result.tools.push(tool)

  return `${handshake}\n${JSON.stringify(reply)}\n`
}

/** The handshake's reply, as recorded (SERVED_LIST's first line). */
export const INITIALIZED = `${SERVED_LIST.split('\n')[0] ?? ''}\n`

/**
 * A `tools/call` reply in the server's exact shape
 * (lib/src/cli/source/business/mcp/serve_stdio.cpp: one text block, isError),
 * hand-made around a document.
 */
export function servedReply(text: string, isError = false): string {
  return `${JSON.stringify({ id: 2, jsonrpc: '2.0', result: { content: [{ text, type: 'text' }], isError } })}\n`
}

/** Scripts tried in order: the first that answers is the answer. */
export function either(...scripts: readonly Script[]): Script {
  return async (args, init) => {
    for (const script of scripts) {
      const answer = await script(args, init)
      if (answer !== undefined) return answer
    }

    return undefined
  }
}

/**
 * The machine apogee_complete's CLI path meets: the server listing no
 * complete, `models list` as given (recorded: llama.cpp backends based, q4,
 * tuned), the utility model unset (`models status`, recorded), then `script`,
 * then every backend entry's max_tokens unset (`config get` printing an empty
 * line, config_cmd.cpp).
 */
export function cliPath(script: Script = () => undefined, models = MODELS_LIST_ADAPTERS): Script {
  return either(
    served(),
    scripted({ 'models list --output-format json': { stdout: models }, 'models status --output-format json': { stdout: MODELS_STATUS_CHAT } }),
    script,
    args => (args[0] === 'config' && args[1] === 'get' ? { stdout: '\n' } : undefined),
  )
}
