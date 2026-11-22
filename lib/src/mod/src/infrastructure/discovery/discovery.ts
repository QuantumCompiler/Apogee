// Finds the apogee binary the mod drives: the macOS app's discovery ladder
// (lib/src/darwin/source/data/discovery/BinaryDiscovery.swift), mirrored.
//
// A hooks module has no PATH of its own to trust, so the binary is looked
// for where the CLI's installs put it, in order: the person's override (the
// plugin's `binary` option), then the channel's places in ~/.local/bin. The
// release channel looks at `apogee` (install.sh's and `make install`'s), then
// `apogee-dev` and `apogee-test` beside it; the dev channel looks at
// `apogee-dev` alone, so a development load never drives the production
// install (the dev channel law). Each candidate is verified by running it:
// `<candidate> version` must answer as Apogee does. One that fails is
// skipped and said. None found is a stated condition, never a crash.
import type { EngineInterface, PluginOptions } from 'claude-code'

import type { ApogeeBinary, ApogeeSkipped, ApogeeSource } from '../../../types'
import { lastLine, runBinary } from '../runner/runner'

/** Which places discovery looks at beside the override. */
export type Channel = 'release' | 'dev'

/** What discovery is asked to do: the plugin's `binary` and `channel` options, read. */
export type DiscoveryOptions = {
  /** The person's own binary, tried first; a leading `~/` is the home folder. */
  override?: string
  channel: Channel
}

/** A place the binary is looked for. */
export type Candidate = { path: string; source: ApogeeSource }

/** How to install the CLI, said where the release channel found no binary (the README's line). */
export const INSTALL_HINT =
  'Install Apogee with: curl -fsSL https://raw.githubusercontent.com/QuantumCompiler/Apogee/stable/lib/scripts/install.sh | bash'

/** How to install the dev channel's binary, said where the dev channel found none. */
export const DEV_INSTALL_HINT = 'Install the dev channel with: make -C lib/src/cli install MODE=dev'

/** How long `version` may take before the candidate is passed over: the macOS app's ten seconds. */
export const VERSION_TIMEOUT_MS = 10_000

const CHANNEL_PLACES: Readonly<Record<Channel, readonly Candidate[]>> = {
  release: [
    { path: '~/.local/bin/apogee', source: 'release' },
    { path: '~/.local/bin/apogee-dev', source: 'dev' },
    { path: '~/.local/bin/apogee-test', source: 'test' },
  ],
  dev: [{ path: '~/.local/bin/apogee-dev', source: 'dev' }],
}

/** The plugin's options as discovery takes them: an empty override is none, an unknown channel is release. */
export function discoveryOptions(options: PluginOptions): DiscoveryOptions {
  const binary = typeof options.binary === 'string' ? options.binary.trim() : ''
  const channel: Channel = options.channel === 'dev' ? 'dev' : 'release'

  return binary === '' ? { channel } : { override: binary, channel }
}

/** Where the binary is looked for, first to last, with `~/` read against `home`. */
export function candidates(home: string, options: DiscoveryOptions): Candidate[] {
  return placesOf(options).map(one => ({ ...one, path: expand(one.path, home) }))
}

/** The first candidate that answers as Apogee, with every one passed over; or every place, passed over. */
export async function locate($: EngineInterface, options: DiscoveryOptions): Promise<ApogeeBinary> {
  // An empty HOME is no home: read as one, ~/.local/bin would be /.local/bin.
  const home = (await $.env.get('HOME')) || undefined
  const skipped: ApogeeSkipped[] = []
  for (const { path, source } of home === undefined ? placesOf(options) : candidates(home, options)) {
    if (path.startsWith('~/')) {
      skipped.push({ path, source, reason: 'HOME is not set' })
      continue
    }
    const answered = await verify($, path)
    if (typeof answered === 'string') {
      skipped.push({ path: shown(path, home), source, reason: answered })
      continue
    }

    return { kind: 'found', path, source, ...answered, skipped }
  }

  return { kind: 'not-found', skipped }
}

/** The places as written, the override first. */
function placesOf(options: DiscoveryOptions): Candidate[] {
  const override = options.override === undefined ? [] : [{ path: options.override, source: 'override' as const }]

  return [...override, ...CHANNEL_PLACES[options.channel]]
}

/** `path` with a leading `~/` read as the home folder. */
function expand(path: string, home: string): string {
  return path.startsWith('~/') ? `${home}${path.slice(1)}` : path
}

/** `path` as the person reads it: under the home folder, written with `~/`. */
function shown(path: string, home: string | undefined): string {
  return home !== undefined && path.startsWith(`${home}/`) ? `~${path.slice(home.length)}` : path
}

type Verified = { version: string; versionLine: string; channel?: string }

/** What `version` says of the binary at `path`: who it is, or why it was passed over. */
async function verify($: EngineInterface, path: string): Promise<Verified | string> {
  // The look is guarded as the run is: `exists` rejects for a network
  // location, and an `fs.*` hook may refuse it. Either is a place passed over.
  let isThere
  try {
    isThere = await $.fs.exists(path)
  } catch (error) {
    return `could not be looked at: ${error instanceof Error ? error.message : String(error)}`
  }
  if (!isThere) {
    return 'not installed'
  }
  // Through the runner, as every run of the binary goes.
  const ran = await runBinary($, path, ['version'], { timeoutMs: VERSION_TIMEOUT_MS })
  if (ran.kind === 'refused') {
    return `could not run \`version\`: ${ran.reason}`
  }
  if (ran.exitCode !== 0) {
    const said = lastLine(ran.stderr)

    return `\`version\` exited with status ${ran.exitCode}${said === undefined ? '' : `: ${said}`}`
  }

  return versionOf(ran.stdout)
}

/**
 * The version report read tolerantly: its first line must be
 * `apogee <version> ...`; a `channel: <name>` line is taken when there is one,
 * and every other line is left alone.
 */
function versionOf(stdout: string): Verified | string {
  const lines = stdout.split('\n').map(line => line.trim())
  const first = lines[0] ?? ''
  const [name, version] = first.split(/\s+/)
  if (name !== 'apogee' || version === undefined) {
    return `\`version\` printed "${first}", not Apogee's version line`
  }
  const channel = lines.find(line => line.startsWith('channel: '))?.slice('channel: '.length)

  return channel === undefined || channel === '' ? { version, versionLine: first } : { version, versionLine: first, channel }
}
