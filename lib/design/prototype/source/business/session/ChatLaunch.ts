import type { RunSettings } from '../../data/protocol/DriverLine'
import { MachineProtocol } from '../../data/protocol/MachineProtocol'

/**
 * How a driven chat session's child is started: `apogee chat` with both
 * directions in stream-json -- or `apogee execute`, the same session's other
 * opening. The Mac app's `ChatLaunch` (`BusinessSession`, `ChatLaunch.swift`).
 */
export interface ChatLaunch {
  /** `--tools`, the CLI's interactive default: on unless said. */
  readonly tools?: boolean
  /** The saved conversation to carry on (`--resume <id>`). */
  readonly resume?: string
  /** The run settings to start with: `-m`, `-s`, `--think`, `--effort`, `-t`. */
  readonly settings?: RunSettings
  /** A chat that writes nothing (`--private`). */
  readonly privately?: boolean
  /** Recall no earlier chats (`--no-recall`). */
  readonly noRecall?: boolean
  /** `execute` rather than `chat`, under a suite or bare, and whether it orchestrates. */
  readonly execute?: { readonly suite?: string; readonly orchestrate?: boolean }
  /** A chat's suite (`--suite <name>`, or `ChatLaunch.noSuite` for none). */
  readonly suite?: string
  /** `--force`: run the suite even when the CLI prices it over this machine's memory. */
  readonly forceSuite?: boolean
  /** The folder the session works in. */
  readonly folder?: string
}

export const ChatLaunch = {
  /** The word `--suite` takes for none (`chat --suite off`). */
  noSuite: 'off',

  /** The arguments after `apogee`, as the Mac app builds them. */
  arguments(launch: ChatLaunch = {}): string[] {
    const args = [
      launch.execute ? 'execute' : 'chat',
      '--output-format',
      MachineProtocol.streamFormat,
      '--input-format',
      MachineProtocol.streamFormat,
    ]
    if (launch.tools ?? true) args.push('--tools')
    if (launch.resume !== undefined) args.push('--resume', launch.resume)
    const settings = launch.settings ?? {}
    if (settings.model !== undefined) args.push('-m', settings.model)
    if (settings.system !== undefined) args.push('-s', settings.system)
    if (settings.think !== undefined) args.push('--think', settings.think)
    if (settings.effort !== undefined && settings.effort !== 'auto') args.push('--effort', settings.effort)
    // As the `set` line writes it: 0.2, and 1 for 1.0.
    if (settings.temperature !== undefined) args.push('-t', String(settings.temperature))
    if (launch.privately) args.push('--private')
    if (launch.noRecall) args.push('--no-recall')
    if (launch.execute) {
      if (launch.execute.suite !== undefined) args.push('--suite', launch.execute.suite)
      if (launch.execute.orchestrate) args.push('--orchestrate')
    } else if (launch.suite !== undefined) {
      args.push('--suite', launch.suite)
    }
    if (launch.forceSuite) args.push('--force')
    return args
  },
}
