// `npm run validate:package` (scripts/validate-package.mjs): the zipped plugin
// validated, unpacked, by a `claude` at or above the mod's engine floor -- or
// the skip said in fixed words, never silent (the mod's 1d). These are the
// wording goldens: each skip's two lines exactly as the job prints them. The
// `claude` is a stand-in script under $TMPDIR that answers `--version` as told
// and records what `plugin validate` was handed; no engine runs. Run by node
// (npm run test:node).
import assert from 'node:assert/strict'
import { chmodSync, existsSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { after, before, test } from 'node:test'

import { MOD_ROOT, PACKAGE, VALIDATE_PACKAGE, childEnv, run, scratch } from './support.mjs'

let space
let zip
let raisedFloor

before(() => {
  space = scratch('apogee-validate-')
  zip = join(space.folder, 'apogee-mod.zip')
  assert.equal(run('node', [PACKAGE, '--out', zip]).status, 0)

  // A plugin whose manifest names a later floor: the floor is the zip's own.
  const root = join(space.folder, 'raised')
  const manifest = JSON.parse(readFileSync(join(MOD_ROOT, '.claude-plugin', 'plugin.json'), 'utf8'))
  manifest.engines['claude-code'] = '>=2.1.400'
  for (const [path, text] of Object.entries({
    '.claude-plugin/plugin.json': `${JSON.stringify(manifest, null, 2)}\n`,
    'hooks/hooks.json': '{ "modules": ["./register.tsx"] }\n',
    'hooks/register.tsx': '\n',
    'src/data/state/value.ts': '\n',
    'types/index.d.ts': '\n',
  })) {
    mkdirSync(join(root, path, '..'), { recursive: true })
    writeFileSync(join(root, path), text)
  }
  raisedFloor = join(space.folder, 'raised.zip')
  assert.equal(run('node', [PACKAGE, '--root', root, '--out', raisedFloor]).status, 0)
})

after(() => {
  space?.remove()
})

/**
 * A stand-in `claude` in its own folder: `--version` prints `version` and
 * exits `versionStatus`; anything else records its arguments and the folder
 * it was handed (its top, and its manifest), prints a line and exits
 * `validateStatus`.
 */
function standIn(name, { version = '2.1.326 (Claude Code)', versionStatus = 0, validateStatus = 0 } = {}) {
  const folder = join(space.folder, name)
  mkdirSync(folder, { recursive: true })
  const claude = join(folder, 'claude')
  const record = join(folder, 'record')
  writeFileSync(
    claude,
    [
      '#!/bin/sh',
      `if [ "$1" = "--version" ]; then printf '%s\\n' '${version}'; exit ${versionStatus}; fi`,
      `{ printf '%s\\n' "$@"; echo ---; ls -A "$4"; echo ---; cat "$4/.claude-plugin/plugin.json"; } > '${record}'`,
      `echo 'the stand-in validated it'`,
      `exit ${validateStatus}`,
      '',
    ].join('\n'),
  )
  chmodSync(claude, 0o755)

  return { claude, record }
}

/** validate:package with CLAUDE set to `claude`, over `zip`, with `extra` in its environment. */
function validate(claude, { over = zip, extra = {} } = {}) {
  return run('node', [VALIDATE_PACKAGE, '--zip', over], { env: childEnv({ CLAUDE: claude, ...extra }) })
}

/** The folders validate:package unpacks into, as $TMPDIR holds them now. */
function leftover() {
  return readdirSync(tmpdir()).filter(name => name.startsWith('apogee-mod-validate-')).sort()
}

/** The two lines of a skip, exactly. */
function skipped(reason, floor = '2.1.326') {
  return [
    `validate: skipped -- ${reason}.`,
    `validate: the zipped plugin is not validated on this host; claude plugin validate and claude plugin test stay dev-host gates (npm run check in lib/src/mod, with Claude Code ${floor} or newer).`,
    '',
  ].join('\n')
}

test('a claude that cannot be run is a skip, said in fixed words', () => {
  const claude = join(space.folder, 'never-installed', 'claude')
  const ran = validate(claude)
  assert.equal(ran.status, 0, ran.stderr)
  assert.equal(ran.stdout, skipped(`${claude} could not be run (ENOENT)`))
})

test('a claude older than the engine floor is a skip, and validate never runs', () => {
  const { claude, record } = standIn('older', { version: '2.1.289 (Claude Code)' })
  const ran = validate(claude)
  assert.equal(ran.status, 0, ran.stderr)
  assert.equal(ran.stdout, skipped(`${claude} is Claude Code 2.1.289, older than the mod's engine floor 2.1.326`))
  assert.equal(existsSync(record), false)
})

test('versions compare by number, never as text: 2.1.40 is older than 2.1.326', () => {
  const { claude, record } = standIn('textual', { version: '2.1.40 (Claude Code)' })
  const ran = validate(claude)
  assert.equal(ran.stdout, skipped(`${claude} is Claude Code 2.1.40, older than the mod's engine floor 2.1.326`))
  assert.equal(existsSync(record), false)
})

test('a --version that fails, or names no version, is a skip', () => {
  const failing = standIn('failing', { versionStatus: 3 })
  assert.equal(validate(failing.claude).stdout, skipped(`${failing.claude} --version exited with status 3`))
  const unnamed = standIn('unnamed', { version: 'Claude Code' })
  assert.equal(validate(unnamed.claude).stdout, skipped(`${unnamed.claude} --version printed no version`))
  assert.equal(existsSync(failing.record) || existsSync(unnamed.record), false)
})

test('the floor is the zipped manifest\'s own', () => {
  const { claude } = standIn('below-raised')
  const ran = validate(claude, { over: raisedFloor })
  assert.equal(ran.stdout, skipped(`${claude} is Claude Code 2.1.326, older than the mod's engine floor 2.1.400`, '2.1.400'))
})

test('at the floor, the zip is validated unpacked with --strict, and the folder is removed after', () => {
  const { claude, record } = standIn('at-floor')
  const ran = validate(claude)
  assert.equal(ran.status, 0, ran.stderr)
  assert.equal(
    ran.stdout,
    [
      `validate: ${claude} is Claude Code 2.1.326, at or above the mod's engine floor 2.1.326: validating ${zip}, unpacked`,
      'the stand-in validated it',
      `validate: ${zip} passed claude plugin validate --strict`,
      '',
    ].join('\n'),
  )
  const [args, top, manifest] = readFileSync(record, 'utf8').split('---\n')
  const [plugin, verb, strict, folder] = args.trim().split('\n')
  assert.deepEqual([plugin, verb, strict], ['plugin', 'validate', '--strict'])
  assert.deepEqual(top.trim().split('\n'), ['.claude-plugin', 'hooks', 'src', 'types'])
  assert.deepEqual(JSON.parse(manifest), JSON.parse(readFileSync(join(MOD_ROOT, '.claude-plugin', 'plugin.json'), 'utf8')))
  assert.equal(existsSync(folder), false)
})

test('a newer claude validates too', () => {
  const { claude, record } = standIn('newer', { version: '2.2.0 (Claude Code)' })
  assert.equal(validate(claude).status, 0)
  assert.equal(existsSync(record), true)
})

test("a validate that fails fails the step with validate's status", () => {
  const { claude } = standIn('refused', { validateStatus: 1 })
  const ran = validate(claude)
  assert.equal(ran.status, 1)
  assert.equal(ran.stdout.trim().split('\n').at(-1), `validate: ${zip} failed claude plugin validate --strict (exit 1)`)
})

test("on a runner, the skip is also a notice and a line of the run's summary", () => {
  const { claude } = standIn('runner-older', { version: '2.1.289 (Claude Code)' })
  const summary = join(space.folder, 'runner-older', 'summary.md')
  const ran = validate(claude, { extra: { GITHUB_ACTIONS: 'true', GITHUB_STEP_SUMMARY: summary } })
  const reason = `${claude} is Claude Code 2.1.289, older than the mod's engine floor 2.1.326`
  const [first, second] = skipped(reason).split('\n')
  assert.equal(ran.stdout, [first, `::notice title=claude plugin validate skipped::${reason}`, second, ''].join('\n'))
  assert.equal(readFileSync(summary, 'utf8'), `**claude plugin validate:** skipped -- ${reason}. The zipped plugin is not validated on this host; claude plugin validate and claude plugin test stay dev-host gates (npm run check in lib/src/mod, with Claude Code 2.1.326 or newer).\n\n`)
})

test("on a runner, a validate that ran says so in the run's summary", () => {
  const { claude } = standIn('runner-at-floor')
  const summary = join(space.folder, 'runner-at-floor', 'summary.md')
  assert.equal(validate(claude, { extra: { GITHUB_ACTIONS: 'true', GITHUB_STEP_SUMMARY: summary } }).status, 0)
  assert.equal(readFileSync(summary, 'utf8'), '**claude plugin validate:** the zipped plugin passed on Claude Code 2.1.326 (the floor is 2.1.326).\n\n')
})

test('a zipped manifest stating no floor fails the step, and the unpacked folder is still removed', () => {
  const root = join(space.folder, 'floorless')
  for (const [path, text] of Object.entries({
    '.claude-plugin/plugin.json': '{ "name": "floorless", "version": "0.0.1" }\n',
    'hooks/hooks.json': '{}\n',
    'types/index.d.ts': '\n',
  })) {
    mkdirSync(join(root, path, '..'), { recursive: true })
    writeFileSync(join(root, path), text)
  }
  const floorless = join(space.folder, 'floorless.zip')
  assert.equal(run('node', [PACKAGE, '--root', root, '--out', floorless]).status, 0)
  const { claude, record } = standIn('floorless-claude')
  const leftBefore = leftover()
  const ran = validate(claude, { over: floorless })
  assert.equal(ran.status, 1)
  assert.equal(ran.stderr.trim(), 'validate: the zipped .claude-plugin/plugin.json states no engine floor (engines["claude-code"] as ">=X.Y.Z")')
  assert.equal(existsSync(record), false)
  assert.deepEqual(leftover(), leftBefore)
})

test('with no zip, it fails, saying to package first', () => {
  const { claude, record } = standIn('no-zip')
  const missing = join(space.folder, 'no-zip', 'apogee-mod.zip')
  const ran = validate(claude, { over: missing })
  assert.equal(ran.status, 1)
  assert.equal(ran.stderr.trim(), `validate: ${missing} is missing: run npm run package first`)
  assert.equal(existsSync(record), false)
})
