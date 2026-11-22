// `changed.sh mod` (lib/scripts/changed.sh), proven over throwaway commits: a
// clone of this repository made with `git clone --shared --no-checkout` in a
// temporary folder (under $TMPDIR), each commit built in the clone's own index
// on top of its HEAD. The working tree's changed.sh is the one run, so the
// declaration under test is the one on disk; nothing is written to this
// checkout or its history. Run by node (npm run test:node).
import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import { mkdtempSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { after, before, test } from 'node:test'

import { MOD_ROOT } from '../architecture/layering/tree.mjs'

/** The child's environment: no CI outputs to append to, no inherited git redirection. */
const ENV = Object.fromEntries(
  Object.entries(process.env).filter(([name]) => !name.startsWith('GIT_') && name !== 'GITHUB_OUTPUT' && name !== 'GITHUB_STEP_SUMMARY'),
)

function run(command, args, cwd, input) {
  const ran = spawnSync(command, args, { cwd, env: ENV, input, encoding: 'utf8' })
  if (ran.error) throw ran.error

  return ran
}

function git(args, cwd, input) {
  const ran = run('git', args, cwd, input)
  assert.equal(ran.status, 0, `git ${args.join(' ')}: ${ran.stderr}`)

  return ran.stdout.trim()
}

const REPO = git(['rev-parse', '--show-toplevel'], MOD_ROOT)
const CHANGED = join(REPO, 'lib', 'scripts', 'changed.sh')

let scratch
let clone
let base

/** A commit on `base` that adds one file, made in the clone's own index. */
function commitAdding(path, message) {
  git(['read-tree', base], clone)
  const blob = git(['hash-object', '-w', '--stdin'], clone, `${message}\n`)
  git(['update-index', '--add', '--cacheinfo', `100644,${blob},${path}`], clone)
  const tree = git(['write-tree'], clone)

  return git(['-c', 'user.name=changed.sh test', '-c', 'user.email=changed@test.invalid', 'commit-tree', tree, '-p', base, '-m', message], clone)
}

/** changed.sh's answer line for `deliverable` between `base` and `to`. */
function answer(deliverable, to) {
  const ran = run('bash', [CHANGED, deliverable, base, to], clone)
  assert.equal(ran.status, 0, ran.stderr)

  return ran.stdout.trim().split('\n').at(-1)
}

let modOnly
let docsOnly
let declarationOnly
let workflowOnly

before(() => {
  scratch = mkdtempSync(join(tmpdir(), 'apogee-changed-'))
  clone = join(scratch, 'repo')
  git(['clone', '--quiet', '--shared', '--no-checkout', REPO, clone], scratch)
  base = git(['rev-parse', 'HEAD'], clone)
  modOnly = commitAdding('lib/src/mod/src/data/state/probe.ts', 'a mod-only change')
  docsOnly = commitAdding('lib/documentation/assistant/mod/probe.md', 'a docs-only change')
  declarationOnly = commitAdding('lib/scripts/changed.sh', 'the declaration changed')
  workflowOnly = commitAdding('.github/workflows/mod.yml', "the mod's workflow changed")
})

after(() => {
  if (scratch !== undefined) rmSync(scratch, { recursive: true, force: true })
})

test('a mod-only change runs the mod pipeline, and neither other', () => {
  assert.equal(answer('mod', modOnly), 'mod=true')
  assert.equal(answer('cli', modOnly), 'cli=false')
  assert.equal(answer('gui-darwin', modOnly), 'gui-darwin=false')
})

test('a docs-only change runs no pipeline', () => {
  assert.equal(answer('mod', docsOnly), 'mod=false')
  assert.equal(answer('cli', docsOnly), 'cli=false')
  assert.equal(answer('gui-darwin', docsOnly), 'gui-darwin=false')
})

test('a change to the declaration runs the mod pipeline', () => {
  assert.equal(answer('mod', declarationOnly), 'mod=true')
})

test("a change to the mod's workflow alone runs the mod pipeline, and neither other (the mod's 1d)", () => {
  assert.equal(answer('mod', workflowOnly), 'mod=true')
  assert.equal(answer('cli', workflowOnly), 'cli=false')
  assert.equal(answer('gui-darwin', workflowOnly), 'gui-darwin=false')
})

test('--paths mod lists what the mod is built from', () => {
  const ran = run('bash', [CHANGED, '--paths', 'mod'], clone)
  assert.equal(ran.status, 0, ran.stderr)
  assert.deepEqual(ran.stdout.trim().split('\n'), ['lib/src/mod/', 'lib/scripts/changed.sh', '.github/workflows/mod.yml'])
})

test('an unknown deliverable is refused, naming the known ones', () => {
  const ran = run('bash', [CHANGED, 'nothing', base, base], clone)
  assert.equal(ran.status, 2)
  assert.equal(ran.stderr.trim(), 'changed.sh: no deliverable named nothing (known: cli, gui-darwin, mod)')
})
