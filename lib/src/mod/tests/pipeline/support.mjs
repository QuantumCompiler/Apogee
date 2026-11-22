// What the pipeline's node tests share: a child's environment cut loose from
// the run that hosts it, a run that never throws on a status, scratch folders
// under $TMPDIR, and the mod's scripts by path. Run by node, never under
// `claude plugin test`.
import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import { mkdtempSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'

import { MOD_ROOT } from '../architecture/layering/tree.mjs'

export { MOD_ROOT }

/** The mod's pipeline scripts (scripts/), as the npm scripts run them. */
export const PACKAGE = join(MOD_ROOT, 'scripts', 'package.mjs')
export const VALIDATE_PACKAGE = join(MOD_ROOT, 'scripts', 'validate-package.mjs')

/**
 * The environment a child runs in: this one, less what a CI run or a shell
 * would hand it unasked -- the runner's outputs and summary, any git
 * redirection, and the engine a developer pointed CLAUDE at. A test sets what
 * it means to.
 */
export function childEnv(extra = {}) {
  const kept = Object.entries(process.env).filter(
    ([name]) => !name.startsWith('GIT_') && !name.startsWith('GITHUB_') && !name.startsWith('RUNNER_') && name !== 'CLAUDE',
  )

  return { ...Object.fromEntries(kept), ...extra }
}

/** `{ status, stdout, stderr }` of a run; only a run that cannot start throws. */
export function run(command, args, { cwd, env = childEnv(), input } = {}) {
  const ran = spawnSync(command, args, { cwd, env, input, encoding: 'utf8' })
  if (ran.error) throw ran.error

  return ran
}

/** git's trimmed stdout, failing the test on any status but 0. */
export function git(args, cwd, input) {
  const ran = run('git', args, { cwd, input })
  assert.equal(ran.status, 0, `git ${args.join(' ')}: ${ran.stderr}`)

  return ran.stdout.trim()
}

/** A new folder under $TMPDIR, and its removal for `after`. */
export function scratch(prefix) {
  const folder = mkdtempSync(join(tmpdir(), prefix))

  return { folder, remove: () => rmSync(folder, { recursive: true, force: true }) }
}

/** The entries of a zip, as `unzip -Z1` lists them: an independent reader, never the writer's own. */
export function entriesOf(zip) {
  const ran = run('unzip', ['-Z1', zip])
  assert.equal(ran.status, 0, `unzip -Z1 ${zip}: ${ran.stderr}`)

  return ran.stdout.split('\n').filter(line => line !== '')
}
