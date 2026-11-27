// `npm run validate:package`: the zipped plugin (`npm run package`'s
// build/apogee-mod.zip) validated as a person would load it -- unpacked, by
// `claude plugin validate --strict` -- wherever a `claude` at or above the
// mod's engine floor can run; otherwise the skip, said in fixed words and
// passed, never silent (the mod's 1d).
//
//   CLAUDE=<claude binary> node scripts/validate-package.mjs [--zip <zip>]
//
// CLAUDE names the binary, or `claude` on PATH when unset or empty, as for
// `npm run validate`. The floor is the zipped manifest's own
// (engines["claude-code"], ">=X.Y.Z"); an older engine refuses the layered
// module, so an older `claude` is a skip, never a pass. A skip exits 0: on a
// runner `tsc` is the gate that always runs, and `claude plugin validate` and
// `claude plugin test` stay dev-host gates (npm run check). On a runner
// (GITHUB_ACTIONS=true) the outcome is also a line of the run's summary, and a
// skip a notice.
import { spawnSync } from 'node:child_process'
import { appendFileSync, existsSync, mkdtempSync, readFileSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join, relative, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

/** lib/src/mod: this file is scripts/validate-package.mjs. */
const MOD_ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..')

const ON_A_RUNNER = process.env.GITHUB_ACTIONS === 'true'

function fail(message) {
  console.error(`validate: ${message}`)
  process.exit(1)
}

function summarize(line) {
  const summary = process.env.GITHUB_STEP_SUMMARY
  if (ON_A_RUNNER && summary) appendFileSync(summary, `**claude plugin validate:** ${line}\n\n`)
}

function zipOf(argv) {
  if (argv.length === 0) return join(MOD_ROOT, 'build', 'apogee-mod.zip')
  if (argv.length !== 2 || argv[0] !== '--zip') fail('usage: validate-package.mjs [--zip <zip>]')

  return resolve(argv[1])
}

/** `[major, minor, patch]` of the first X.Y.Z in `text`, or undefined. */
function versionIn(text) {
  const found = /\b(\d+)\.(\d+)\.(\d+)\b/.exec(text)

  return found === null ? undefined : found.slice(1, 4).map(Number)
}

function atOrAbove(version, floor) {
  for (let at = 0; at < 3; at += 1) {
    if (version[at] !== floor[at]) return version[at] > floor[at]
  }

  return true
}

/** Why `claude` cannot validate here, or its version when it can. */
function probe(claude, floor) {
  const ran = spawnSync(claude, ['--version'], { encoding: 'utf8', timeout: 60_000 })
  if (ran.error) return { skip: `${claude} could not be run (${ran.error.code ?? ran.error.message})` }
  if (ran.status !== 0) return { skip: ran.status === null ? `${claude} --version was ended by ${ran.signal}` : `${claude} --version exited with status ${ran.status}` }
  const version = versionIn(ran.stdout)
  if (version === undefined) return { skip: `${claude} --version printed no version` }
  if (!atOrAbove(version, floor)) return { skip: `${claude} is Claude Code ${version.join('.')}, older than the mod's engine floor ${floor.join('.')}` }

  return { version }
}

const zip = zipOf(process.argv.slice(2))
const shown = (() => {
  const path = relative(process.cwd(), zip)

  return path === '' || path.startsWith('..') ? zip : path
})()
if (!existsSync(zip)) fail(`${shown} is missing: run npm run package first`)

// Removed however this ends, a failure's process.exit included.
const unpacked = mkdtempSync(join(tmpdir(), 'apogee-mod-validate-'))
process.on('exit', () => rmSync(unpacked, { recursive: true, force: true }))
{
  const plugin = join(unpacked, 'apogee')
  const unzipped = spawnSync('unzip', ['-q', zip, '-d', plugin], { encoding: 'utf8' })
  if (unzipped.error || unzipped.status !== 0) {
    fail(`unzip could not unpack ${shown}: ${unzipped.error?.message ?? unzipped.stderr.trim()}`)
  }

  let manifest
  try {
    manifest = JSON.parse(readFileSync(join(plugin, '.claude-plugin', 'plugin.json'), 'utf8'))
  } catch (error) {
    fail(`the zipped .claude-plugin/plugin.json could not be read: ${error.message}`)
  }
  const stated = /^>=(\d+\.\d+\.\d+)$/.exec(String(manifest?.engines?.['claude-code'] ?? ''))
  if (stated === null) fail('the zipped .claude-plugin/plugin.json states no engine floor (engines["claude-code"] as ">=X.Y.Z")')
  const floor = versionIn(stated[1])

  const claude = process.env.CLAUDE || 'claude'
  const found = probe(claude, floor)
  if (found.skip !== undefined) {
    const gates = `claude plugin validate and claude plugin test stay dev-host gates (npm run check in lib/src/mod, with Claude Code ${floor.join('.')} or newer).`
    console.log(`validate: skipped -- ${found.skip}.`)
    if (ON_A_RUNNER) console.log(`::notice title=claude plugin validate skipped::${found.skip}`)
    console.log(`validate: the zipped plugin is not validated on this host; ${gates}`)
    summarize(`skipped -- ${found.skip}. The zipped plugin is not validated on this host; ${gates}`)
  } else {
    const version = found.version.join('.')
    console.log(`validate: ${claude} is Claude Code ${version}, at or above the mod's engine floor ${floor.join('.')}: validating ${shown}, unpacked`)
    const validated = spawnSync(claude, ['plugin', 'validate', '--strict', plugin], { stdio: 'inherit', timeout: 600_000 })
    const status = validated.status ?? 1
    process.exitCode = status
    if (status === 0) {
      console.log(`validate: ${shown} passed claude plugin validate --strict`)
      summarize(`the zipped plugin passed on Claude Code ${version} (the floor is ${floor.join('.')}).`)
    } else {
      console.log(`validate: ${shown} failed claude plugin validate --strict (exit ${status})`)
      summarize(`the zipped plugin failed on Claude Code ${version} (exit ${status}).`)
    }
  }
}
